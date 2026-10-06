#define _GNU_SOURCE
#include "oopsie_wal.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

static void true_unlink(const char* path) {
#ifdef SYS_unlink
   syscall(SYS_unlink, path);
#else
   syscall(SYS_unlinkat, AT_FDCWD, path, 0);
#endif
}

static int true_open(const char* path, int flags, mode_t mode) {
#ifdef SYS_open
   return (int)syscall(SYS_open, path, flags, mode);
#else
   return (int)syscall(SYS_openat, AT_FDCWD, path, flags, mode);
#endif
}

static void reset_wal(OopWalContext* wal) {
   WalFile_Close(wal);
   mkdir("/tmp/oopsie", 0700);
   if (WalFile_Open(wal, WAL_PATH)) {
      WalHeader* header = (WalHeader*)wal->map;
      __atomic_store_n(&header->current_offset, sizeof(WalHeader), __ATOMIC_RELAXED);
      __atomic_store_n(&header->toombstone, (uint32_t)0, __ATOMIC_RELAXED);
   }
}

static int has_record(OopWalContext* wal, const char* path, uint8_t action) {
   size_t want = strlen(path);
   WalHeader* header = (WalHeader*)wal->map;
   size_t cursor = sizeof(WalHeader);
   int found = 0;
   while (cursor < header->current_offset) {
      OopWalRecord* rec_ptr = (OopWalRecord*)((char*)wal->map + cursor);
      if (rec_ptr->magic != RECORD_MAGIC)
         break;
      char* path_ptr = (char*)wal->map + cursor + sizeof(OopWalRecord);
      if (rec_ptr->action == action && rec_ptr->pathlen == want && memcmp(path_ptr, path, want) == 0)
         found = 1;
      size_t rs = sizeof(OopWalRecord) + rec_ptr->pathlen + sizeof(uint32_t);
      cursor += (rs + 7) & ~(size_t)7;
   }
   return found;
}

static void seed_file(const char* path, const char* content) {
   true_unlink(path);
   int fd = true_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
   if (fd >= 0) {
      size_t len = strlen(content);
      if (write(fd, content, len) != (ssize_t)len) {}
      close(fd);
   }
}

static int record_inode(OopWalContext* wal, const char* path) {
   size_t want = strlen(path);
   WalHeader* header = (WalHeader*)wal->map;
   size_t cursor = sizeof(WalHeader);
   while (cursor < header->current_offset) {
      OopWalRecord* rec_ptr = (OopWalRecord*)((char*)wal->map + cursor);
      if (rec_ptr->magic != RECORD_MAGIC)
         break;
      char* path_ptr = (char*)wal->map + cursor + sizeof(OopWalRecord);
      if (rec_ptr->pathlen == want && memcmp(path_ptr, path, want) == 0)
         return (int)rec_ptr->inode;
      size_t rs = sizeof(OopWalRecord) + rec_ptr->pathlen + sizeof(uint32_t);
      cursor += (rs + 7) & ~(size_t)7;
   }
   return 0;
}

static int read_sidecar(int ino, OopMetaBlob* meta, char** blob) {
   char meta_path[BUFFER_SZ];
   make_vault_meta_path(meta_path, (unsigned long)ino);
   int fd = true_open(meta_path, O_RDONLY, 0);
   if (fd < 0)
      return 0;
   int ok = 0;
   if (read(fd, meta, sizeof(*meta)) == (ssize_t)sizeof(*meta) && meta->magic == METABLOB_MAGIC) {
      *blob = NULL;
      if (meta->xattr_len > 0) {
         *blob = (char*)malloc(meta->xattr_len);
         if (*blob != NULL && read(fd, *blob, meta->xattr_len) != (ssize_t)meta->xattr_len) {
            free(*blob);
            *blob = NULL;
         }
      }
      ok = (*blob != NULL);
   }
   close(fd);
   return ok;
}

static int blob_entry_matches(const char* blob, size_t blob_len, const char* name, const void* value, size_t value_len) {
   size_t name_len = strlen(name);
   size_t off = 0;
   while (off + 6 <= blob_len) {
      uint16_t rec_name_len = 0;
      uint32_t rec_value_len = 0;
      memcpy(&rec_name_len, blob + off, sizeof(rec_name_len));
      off += sizeof(rec_name_len);
      memcpy(&rec_value_len, blob + off, sizeof(rec_value_len));
      off += sizeof(rec_value_len);
      if (rec_name_len == 0 || off + (size_t)rec_name_len + (size_t)rec_value_len > blob_len)
         break;
      if (rec_name_len == name_len && memcmp(blob + off, name, name_len) == 0) {
         if (rec_value_len != value_len)
            return 0;
         return (value_len == 0) || (memcmp(blob + off + name_len, value, value_len) == 0);
      }
      off += (size_t)rec_name_len + (size_t)rec_value_len;
   }
   return 0;
}

static int blob_entry_count(const char* blob, size_t blob_len) {
   size_t off = 0;
   int count = 0;
   while (off + 6 <= blob_len) {
      uint16_t rec_name_len = 0;
      uint32_t rec_value_len = 0;
      memcpy(&rec_name_len, blob + off, sizeof(rec_name_len));
      off += sizeof(rec_name_len);
      memcpy(&rec_value_len, blob + off, sizeof(rec_value_len));
      off += sizeof(rec_value_len);
      if (rec_name_len == 0 || off + (size_t)rec_name_len + (size_t)rec_value_len > blob_len)
         break;
      count++;
      off += (size_t)rec_name_len + (size_t)rec_value_len;
   }
   return count;
}

static void test_xattr_sidecar(OopWalContext* wal, int* failures) {
   const char* path = "/tmp/test_shim_extra_xattr.txt";
   unsigned char binary[31];
   for (int i = 0; i < 31; i++) {
      binary[i] = (unsigned char)(i + 1);
   }
   seed_file(path, "ORIGINAL");
   syscall(SYS_setxattr, path, "user.one", "Vone", 4, 0);
   syscall(SYS_setxattr, path, "user.two", "Vtwo", 4, 0);
   syscall(SYS_setxattr, path, "user.three", "Vthree", 6, 0);
   syscall(SYS_setxattr, path, "user.empty", "", 0, 0);
   syscall(SYS_setxattr, path, "user.bin", binary, sizeof(binary), 0);
   reset_wal(wal);

   int fd = open(path, O_WRONLY | O_TRUNC);
   if (fd < 0) {
      printf("[TEST] FAILED: could not open xattr seed file for writing\n");
      (*failures)++;
      return;
   }
   if (write(fd, "CHANGED", 7) != 7) {}
   close(fd);

   int ino = record_inode(wal, path);
   if (ino == 0) {
      printf("[TEST] FAILED: no record for xattr seed file\n");
      (*failures)++;
      return;
   }
   OopMetaBlob meta;
   char* blob = NULL;
   if (!read_sidecar(ino, &meta, &blob)) {
      printf("[TEST] FAILED: could not read metadata sidecar\n");
      (*failures)++;
      return;
   }
   if (blob_entry_count(blob, meta.xattr_len) != 5) {
      printf("[TEST] FAILED: sidecar blob held %d xattrs, expected 5\n", blob_entry_count(blob, meta.xattr_len));
      (*failures)++;
   }
   if (!blob_entry_matches(blob, meta.xattr_len, "user.one", "Vone", 4) ||
       !blob_entry_matches(blob, meta.xattr_len, "user.two", "Vtwo", 4) ||
       !blob_entry_matches(blob, meta.xattr_len, "user.three", "Vthree", 6)) {
      printf("[TEST] FAILED: sidecar blob lost a text xattr\n");
      (*failures)++;
   }
   if (!blob_entry_matches(blob, meta.xattr_len, "user.empty", NULL, 0)) {
      printf("[TEST] FAILED: sidecar blob lost the empty xattr\n");
      (*failures)++;
   }
   if (!blob_entry_matches(blob, meta.xattr_len, "user.bin", binary, sizeof(binary))) {
      printf("[TEST] FAILED: sidecar blob corrupted the binary xattr\n");
      (*failures)++;
   }
   free(blob);
}

int main(void) {
   printf("[TEST] Starting Shim Extra Hooks Test...\n");
   OopWalContext wal;
   memset(&wal, 0, sizeof(wal));
   int failures = 0;

   const char* f64_path = "/tmp/test_shim_extra_fopen64.txt";
   seed_file(f64_path, "ORIGINAL-F64");
   reset_wal(&wal);
   FILE* fp = fopen64(f64_path, "w");
   if (fp == NULL) {
      printf("[TEST] FAILED: fopen64() returned NULL\n");
      failures++;
   }
   else {
      if (fputs("SHORT", fp) < 0) {}
      fclose(fp);
   }
   if (!has_record(&wal, f64_path, OopAction_MODIFY)) {
      printf("[TEST] FAILED: fopen64('w') produced no MODIFY record\n");
      failures++;
   }

   reset_wal(&wal);
   char ts_template[128];
   snprintf(ts_template, sizeof(ts_template), "/tmp/test_shim_extra_tsXXXXXX.suf");
   int ts_fd = mkstemps(ts_template, 4);
   if (ts_fd < 0) {
      printf("[TEST] FAILED: mkstemps() failed\n");
      failures++;
   }
   else {
      if (write(ts_fd, "T", 1) != 1) {}
      close(ts_fd);
   }
   if (!has_record(&wal, ts_template, OopAction_CREATE)) {
      printf("[TEST] FAILED: mkstemps() produced no CREATE record\n");
      failures++;
   }

   reset_wal(&wal);
   char dt_template[128];
   snprintf(dt_template, sizeof(dt_template), "/tmp/test_shim_extra_dtXXXXXX");
   char* dt_result = mkdtemp(dt_template);
   if (dt_result == NULL) {
      printf("[TEST] FAILED: mkdtemp() failed\n");
      failures++;
   }
   if (!has_record(&wal, dt_result, OopAction_CREATE)) {
      printf("[TEST] FAILED: mkdtemp() produced no CREATE record\n");
      failures++;
   }

   const char* dir_path = "/tmp/test_shim_extra_dir";
   reset_wal(&wal);
   true_unlink(dir_path);
   if (mkdir(dir_path, 0755) != 0) {
      printf("[TEST] FAILED: mkdir() failed\n");
      failures++;
   }
   if (!has_record(&wal, dir_path, OopAction_CREATE)) {
      printf("[TEST] FAILED: mkdir() produced no CREATE record\n");
      failures++;
   }
   if (rmdir(dir_path) != 0) {
      printf("[TEST] FAILED: cleanup rmdir failed\n");
      failures++;
   }

   test_xattr_sidecar(&wal, &failures);

   if (failures == 0)
      printf("[TEST] PASSED!\n");
   else
      printf("[TEST] %d assertion(s) failed.\n", failures);
   WalFile_Close(&wal);
   return failures == 0 ? 0 : 1;
}