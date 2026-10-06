#include "oopsie_wal.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

void true_unlink(const char* path) {
#ifdef SYS_unlink
   syscall(SYS_unlink, path);
#else
   syscall(SYS_unlinkat, AT_FDCWD, path, 0);
#endif
}

int true_open(const char* path, int flags, mode_t mode) {
#ifdef SYS_open
   return (int)syscall(SYS_open, path, flags, mode);
#else
   return (int)syscall(SYS_openat, AT_FDCWD, path, flags, mode);
#endif
}

void reset_wal(OopWalContext* wal) {
   WalFile_Close(wal);
   mkdir("/tmp/oopsie", 0700);
   if (WalFile_Open(wal, WAL_PATH)) {
      WalHeader* header = (WalHeader*)wal->map;
      __atomic_store_n(&header->current_offset, sizeof(WalHeader), __ATOMIC_RELAXED);
      __atomic_store_n(&header->toombstone, (uint32_t)0, __ATOMIC_RELAXED);
   }
}

int has_record(OopWalContext* wal, const char* path, uint8_t action) {
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

void seed_file(const char* path, const char* content) {
   true_unlink(path);
   int fd = true_open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
   if (fd >= 0) {
      size_t len = strlen(content);
      if (write(fd, content, len) != (ssize_t)len) {}
      close(fd);
   }
}

int meta_blob_ok(OopWalContext* wal, const char* path, uint32_t want_mode) {
   WalHeader* header = (WalHeader*)wal->map;
   size_t want = strlen(path);
   size_t cursor = sizeof(WalHeader);
   int ino = 0;
   while (cursor < header->current_offset) {
      OopWalRecord* rec_ptr = (OopWalRecord*)((char*)wal->map + cursor);
      if (rec_ptr->magic != RECORD_MAGIC)
         break;
      char* path_ptr = (char*)wal->map + cursor + sizeof(OopWalRecord);
      if (rec_ptr->pathlen == want && memcmp(path_ptr, path, want) == 0)
         ino = (int)rec_ptr->inode;
      size_t rs = sizeof(OopWalRecord) + rec_ptr->pathlen + sizeof(uint32_t);
      cursor += (rs + 7) & ~(size_t)7;
   }
   if (ino == 0)
      return 0;
   char meta_path[BUFFER_SZ];
   make_vault_meta_path(meta_path, (unsigned long)ino);
   int fd = true_open(meta_path, O_RDONLY, 0);
   if (fd < 0)
      return 0;
   OopMetaBlob blob;
   int ok = 0;
   if (read(fd, &blob, sizeof(blob)) == (ssize_t)sizeof(blob) && blob.magic == METABLOB_MAGIC) {
      ok = ((uint32_t)(blob.mode & 07777) == want_mode);
   }
   close(fd);
   return ok;
}

int main() {
   printf("[TEST] Starting Shim Hooks Test...\n");
   OopWalContext wal;
   memset(&wal, 0, sizeof(OopWalContext));
   int failures = 0;

   const char* fopen_path = "/tmp/test_shim_hooks_fopen.txt";
   seed_file(fopen_path, "ORIGINAL-CONTENT");
   reset_wal(&wal);
   FILE* fp = fopen(fopen_path, "w");
   if (fp == NULL) {
      printf("[TEST] FAILED: fopen returned NULL\n");
      return 1;
   }
   if (fputs("SHORT", fp) < 0) {}
   fclose(fp);
   if (!has_record(&wal, fopen_path, OopAction_MODIFY)) {
      printf("[TEST] FAILED: fopen('w') produced no MODIFY record\n");
      failures++;
   }

   const char* remove_path = "/tmp/test_shim_hooks_remove.txt";
   seed_file(remove_path, "GONE");
   reset_wal(&wal);
   if (remove(remove_path) != 0) {
      printf("[TEST] FAILED: remove() did not unlink\n");
      failures++;
   }
   if (!has_record(&wal, remove_path, OopAction_DELETE)) {
      printf("[TEST] FAILED: remove() produced no DELETE record\n");
      failures++;
   }

   const char* link_src = "/tmp/test_shim_hooks_link_src.txt";
   const char* link_dst = "/tmp/test_shim_hooks_link_dst.txt";
   seed_file(link_src, "LINKED");
   true_unlink(link_dst);
   reset_wal(&wal);
   if (link(link_src, link_dst) != 0) {
      printf("[TEST] FAILED: link() failed\n");
      failures++;
   }
   if (!has_record(&wal, link_dst, OopAction_CREATE)) {
      printf("[TEST] FAILED: link() produced no CREATE record for dest\n");
      failures++;
   }

   const char* sym_dst = "/tmp/test_shim_hooks_sym_dst";
   const char* sym_src = "/tmp/test_shim_hooks_sym_src.txt";
   seed_file(sym_src, "TARGET");
   true_unlink(sym_dst);
   reset_wal(&wal);
   if (symlink(sym_src, sym_dst) != 0) {
      printf("[TEST] FAILED: symlink() failed\n");
      failures++;
   }
   if (!has_record(&wal, sym_dst, OopAction_CREATE)) {
      printf("[TEST] FAILED: symlink() produced no CREATE record for dest\n");
      failures++;
   }

   const char* meta_path = "/tmp/test_shim_hooks_meta.txt";
   seed_file(meta_path, "METADATA");
#ifdef SYS_chmod
   syscall(SYS_chmod, meta_path, (mode_t)0640);
#else
   syscall(SYS_fchmodat, AT_FDCWD, meta_path, (mode_t)0640, 0);
#endif
   reset_wal(&wal);
   if (chmod(meta_path, (mode_t)0777) != 0) {
      printf("[TEST] FAILED: chmod() failed\n");
      failures++;
   }
   if (!has_record(&wal, meta_path, OopAction_MODIFY)) {
      printf("[TEST] FAILED: chmod() produced no record\n");
      failures++;
   }
   if (!meta_blob_ok(&wal, meta_path, (uint32_t)0640)) {
      printf("[TEST] FAILED: metadata sidecar did not capture original mode 0640\n");
      failures++;
   }

   const char* mkstemp_path = "/tmp/test_shim_hooks_tmpXXXXXX";
   reset_wal(&wal);
   char template[128];
   snprintf(template, sizeof(template), "%s", mkstemp_path);
   int mfd = mkstemp(template);
   if (mfd < 0) {
      printf("[TEST] FAILED: mkstemp() failed\n");
      failures++;
   }
   else {
      if (write(mfd, "T", 1) != 1) {}
      close(mfd);
   }
   if (!has_record(&wal, template, OopAction_CREATE)) {
      printf("[TEST] FAILED: mkstemp() produced no CREATE record\n");
      failures++;
   }

   if (failures == 0)
      printf("[TEST] PASSED!\n");
   else
      printf("[TEST] %d assertion(s) failed.\n", failures);
   WalFile_Close(&wal);
   return failures == 0 ? 0 : 1;
}