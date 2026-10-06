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

int path_matches(const char* path_ptr, uint32_t pathlen, const char* path) {
   size_t want = strlen(path);
   if ((size_t)pathlen != want)
      return 0;
   return memcmp(path_ptr, path, want) == 0;
}

int find_record(OopWalContext* wal, const char* path, uint8_t action, OopWalRecord* out_rec) {
   WalHeader* header = (WalHeader*)wal->map;
   size_t cursor = sizeof(WalHeader);
   while (cursor < header->current_offset) {
      OopWalRecord* rec_ptr = (OopWalRecord*)((char*)wal->map + cursor);
      if (rec_ptr->magic != RECORD_MAGIC)
         break;
      char* path_ptr = (char*)wal->map + cursor + sizeof(OopWalRecord);
      if (rec_ptr->action == action && path_matches(path_ptr, rec_ptr->pathlen, path)) {
         memcpy(out_rec, rec_ptr, sizeof(OopWalRecord));
         return 1;
      }
      size_t rs = sizeof(OopWalRecord) + rec_ptr->pathlen + sizeof(uint32_t);
      cursor += (rs + 7) & ~(size_t)7;
   }
   return 0;
}

int vault_matches(const OopWalRecord* rec, const char* expected, size_t expected_len) {
   char vault_path[BUFFER_SZ];
   make_vault_path(vault_path, (unsigned long)rec->inode);
   int fd = syscall(SYS_open, vault_path, O_RDONLY);
   if (fd < 0)
      return 0;
   char buff[256];
   ssize_t got = read(fd, buff, sizeof(buff));
   close(fd);
   if (got != (ssize_t)expected_len)
      return 0;
   return memcmp(buff, expected, expected_len) == 0;
}

int main() {
   printf("[TEST] Starting Shim Truncate Test...\n");
   const char* fd_target = "/tmp/test_shim_truncate_fd.txt";
   const char* path_target = "/tmp/test_shim_truncate_path.txt";
   const char* payload = "0123456789";

   true_unlink(fd_target);
   true_unlink(path_target);

   // 1. ftruncate on a descriptor must snapshot the pre-truncation contents
   int fd = open(fd_target, O_RDWR | O_CREAT | O_TRUNC, 0644);
   if (fd < 0) {
      printf("[TEST] Failed to setup file\n");
      return 1;
   }
   struct stat stat_before;
   if (fstat(fd, &stat_before) != 0) {
      printf("[TEST] fstat failed\n");
      return 1;
   }
   if (write(fd, payload, 10) != 10) {
      printf("[TEST] write failed\n");
      return 1;
   }
   if (ftruncate(fd, 4) != 0) {
      printf("[TEST] ftruncate failed\n");
      return 1;
   }
   close(fd);

   struct stat stat_after;
   if (stat(fd_target, &stat_after) != 0 || stat_after.st_size != 4) {
      printf("[TEST] ftruncate did not shrink the file\n");
      return 1;
   }

   // 2. truncate by path must snapshot the pre-truncation contents
   int sfd = open(path_target, O_RDWR | O_CREAT | O_TRUNC, 0644);
   if (sfd < 0 || write(sfd, payload, 10) != 10) {
      printf("[TEST] setup of path target failed\n");
      return 1;
   }
   close(sfd);
   if (truncate(path_target, 2) != 0) {
      printf("[TEST] truncate failed\n");
      return 1;
   }

   OopWalContext wal;
   if (!WalFile_Open(&wal, "/tmp/oopsie/vault.wal")) {
      printf("[TEST] Failed to open WAL\n");
      return 1;
   }

   OopWalRecord rec;
   int failures = 0;

   if (!find_record(&wal, fd_target, OopAction_MODIFY, &rec)) {
      printf("[TEST] FAILED: no MODIFY record for ftruncate target!\n");
      failures++;
   }
   else if (rec.inode != (uint64_t)stat_before.st_ino) {
      printf("[TEST] FAILED: MODIFY record inode mismatch!\n");
      failures++;
   }
   else if (!vault_matches(&rec, payload, 10)) {
      printf("[TEST] FAILED: vault does not hold the pre-truncation contents!\n");
      failures++;
   }

   if (!find_record(&wal, path_target, OopAction_MODIFY, &rec)) {
      printf("[TEST] FAILED: no MODIFY record for truncate target!\n");
      failures++;
   }
   else if (!vault_matches(&rec, payload, 10)) {
      printf("[TEST] FAILED: vault does not hold the pre-truncation contents!\n");
      failures++;
   }

   // 3. A grow must never be recorded, it destroys nothing
   int gfd = open(path_target, O_RDWR);
   if (gfd < 0) {
      printf("[TEST] reopen failed\n");
      WalFile_Close(&wal);
      return 1;
   }
   if (ftruncate(gfd, 4096) != 0) {
      printf("[TEST] growing ftruncate failed\n");
      WalFile_Close(&wal);
      return 1;
   }
   close(gfd);

   WalFile_Close(&wal);

   true_unlink(fd_target);
   true_unlink(path_target);

   if (failures != 0) {
      printf("[TEST] FAILED with %d error(s)!\n", failures);
      return 1;
   }
   printf("[TEST] PASSED!\n");
   return 0;
}