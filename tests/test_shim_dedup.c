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

void reset_wal(OopWalContext* wal) {
   WalFile_Close(wal);
   mkdir("/tmp/oopsie", 0700);
   if (WalFile_Open(wal, WAL_PATH)) {
      WalHeader* header = (WalHeader*)wal->map;
      __atomic_store_n(&header->current_offset, sizeof(WalHeader), __ATOMIC_RELAXED);
      __atomic_store_n(&header->toombstone, (uint32_t)0, __ATOMIC_RELAXED);
   }
}

size_t count_records(OopWalContext* wal, const char* path) {
   size_t want = strlen(path);
   WalHeader* header = (WalHeader*)wal->map;
   size_t cursor = sizeof(WalHeader);
   size_t found = 0;
   while (cursor < header->current_offset) {
      OopWalRecord* rec_ptr = (OopWalRecord*)((char*)wal->map + cursor);
      if (rec_ptr->magic != RECORD_MAGIC)
         break;
      char* path_ptr = (char*)wal->map + cursor + sizeof(OopWalRecord);
      if (rec_ptr->pathlen == want && memcmp(path_ptr, path, want) == 0)
         found++;
      size_t rs = sizeof(OopWalRecord) + rec_ptr->pathlen + sizeof(uint32_t);
      cursor += (rs + 7) & ~(size_t)7;
   }
   return found;
}

int last_inode(OopWalContext* wal, const char* path) {
   size_t want = strlen(path);
   WalHeader* header = (WalHeader*)wal->map;
   size_t cursor = sizeof(WalHeader);
   int inode = 0;
   while (cursor < header->current_offset) {
      OopWalRecord* rec_ptr = (OopWalRecord*)((char*)wal->map + cursor);
      if (rec_ptr->magic != RECORD_MAGIC)
         break;
      char* path_ptr = (char*)wal->map + cursor + sizeof(OopWalRecord);
      if (rec_ptr->pathlen == want && memcmp(path_ptr, path, want) == 0)
         inode = (int)rec_ptr->inode;
      size_t rs = sizeof(OopWalRecord) + rec_ptr->pathlen + sizeof(uint32_t);
      cursor += (rs + 7) & ~(size_t)7;
   }
   return inode;
}

int vault_content(OopWalContext* wal, const char* path, char* out, size_t max) {
   int ino = last_inode(wal, path);
   if (ino == 0)
      return -1;
   char vault_path[BUFFER_SZ];
   make_vault_path(vault_path, (unsigned long)ino);
   int fd = syscall(SYS_open, vault_path, O_RDONLY);
   if (fd < 0)
      return -1;
   ssize_t got = read(fd, out, max - 1);
   close(fd);
   if (got < 0)
      return -1;
   out[got] = '\0';
   return (int)got;
}

int check_path(OopWalContext* wal, const char* path, const char* want_content, size_t want_len, size_t want_records) {
   char buff[256];
   int failures = 0;
   int got = vault_content(wal, path, buff, sizeof(buff));
   if (got != (int)want_len || memcmp(buff, want_content, want_len) != 0) {
      printf("[TEST] FAILED: %s vault = '%s' (want '%s')\n", path, buff, want_content);
      failures++;
   }
   size_t n = count_records(wal, path);
   if (n != want_records) {
      printf("[TEST] FAILED: %s produced %zu records (want %zu)\n", path, n, want_records);
      failures++;
   }
   return failures;
}

int main() {
   printf("[TEST] Starting Shim Dedup Test...\n");
   OopWalContext wal;
   memset(&wal, 0, sizeof(OopWalContext));
   int failures = 0;

   const char* reopen_path = "/tmp/test_shim_dedup_reopen.txt";
   true_unlink(reopen_path);
   int fd = syscall(SYS_open, reopen_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
   if (fd < 0) {
      printf("[TEST] FAILED: cannot seed reopen file\n");
      return 1;
   }
   if (write(fd, "ORIGINAL", 8) != 8) {
      printf("[TEST] FAILED: cannot seed reopen file\n");
      return 1;
   }
   close(fd);
   reset_wal(&wal);
   fd = open(reopen_path, O_RDWR);
   if (fd < 0) {
      printf("[TEST] FAILED: reopen open failed\n");
      return 1;
   }
   if (write(fd, "XXXX", 4) != 4) {
      printf("[TEST] FAILED: reopen write failed\n");
      return 1;
   }
   close(fd);
   for (int i = 0; i < 8; i++) {
      fd = open(reopen_path, O_RDWR);
      if (fd < 0) {
         printf("[TEST] FAILED: extra open failed\n");
         return 1;
      }
      close(fd);
   }
   failures += check_path(&wal, reopen_path, "ORIGINAL", 8, 1);
   reset_wal(&wal);

   const char* trunc_path = "/tmp/test_shim_dedup_trunc.txt";
   true_unlink(trunc_path);
   fd = syscall(SYS_open, trunc_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
   if (fd < 0) {
      printf("[TEST] FAILED: cannot seed trunc file\n");
      return 1;
   }
   if (write(fd, "FIRST", 5) != 5) {
      printf("[TEST] FAILED: cannot seed trunc file\n");
      return 1;
   }
   close(fd);
   reset_wal(&wal);
   for (int i = 0; i < 5; i++) {
      fd = open(trunc_path, O_WRONLY | O_TRUNC);
      if (fd < 0) {
         printf("[TEST] FAILED: trunc open failed\n");
         return 1;
      }
      char payload[8];
      for (int j = 0; j < 8; j++)
         payload[j] = (char)('A' + i);
      if (write(fd, payload, 8) != 8) {
         printf("[TEST] FAILED: trunc write failed\n");
         return 1;
      }
      close(fd);
   }
   failures += check_path(&wal, trunc_path, "FIRST", 5, 1);

   if (failures == 0)
      printf("[TEST] PASSED!\n");
   else
      printf("[TEST] %d assertion(s) failed.\n", failures);
   WalFile_Close(&wal);
   return failures == 0 ? 0 : 1;
}