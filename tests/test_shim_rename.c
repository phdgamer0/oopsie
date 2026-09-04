#include "oopsie_wal.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

extern OopWalContext GlobalWal;

void true_unlink(const char* path) {
#ifdef SYS_unlink
   syscall(SYS_unlink, path);
#else
   syscall(SYS_unlinkat, AT_FDCWD, path, 0);
#endif
}

int main() {
   printf("[TEST] Starting Shim Rename Test...\n");
   const char* old_target = "/tmp/test_shim_rename_old.txt";
   const char* new_target = "/tmp/test_shim_rename_new.txt";

   // Clean up first
   true_unlink(old_target);
   true_unlink(new_target);

// Setup: create old file (bypassing shim to avoid noise, though noise is fine)
#ifdef SYS_open
   int fd = syscall(SYS_open, old_target, O_CREAT | O_WRONLY, 0644);
#else
   int fd = syscall(SYS_openat, AT_FDCWD, old_target, O_CREAT | O_WRONLY, 0644);
#endif
   if (fd < 0) {
      printf("[TEST] Failed to setup file\n");
      return 1;
   }
   write(fd, "rename_me", 9);
   close(fd);

   // 1. Test RENAME
   if (rename(old_target, new_target) != 0) {
      printf("[TEST] Rename failed\n");
      return 1;
   }

   // Verify WAL has RENAME
   OopWalContext wal;
   if (!WalFile_Open(&wal, "/tmp/oopsie/vault.wal")) {
      printf("[TEST] Failed to open WAL\n");
      return 1;
   }

   int found_rename = 0;
   WalHeader* header = (WalHeader*)wal.map;
   size_t cursor = sizeof(WalHeader);
   while (cursor < header->current_offset) {
      OopWalRecord* rec_ptr = (OopWalRecord*)((char*)wal.map + cursor);
      if (rec_ptr->magic != RECORD_MAGIC)
         break;

      char* path_ptr = (char*)wal.map + cursor + sizeof(OopWalRecord);
      if (rec_ptr->action == OopAction_RENAME) {
         // path_ptr contains old_target \0 new_target \0
         if (strcmp(path_ptr, old_target) == 0) {
            char* second_str = path_ptr + strlen(path_ptr) + 1;
            if (strcmp(second_str, new_target) == 0) {
               found_rename = 1;
            }
         }
      }
      cursor += sizeof(OopWalRecord) + rec_ptr->pathlen + sizeof(uint32_t);
   }
   WalFile_Close(&wal);

   if (!found_rename) {
      printf("[TEST] FAILED: RENAME record not found!\n");
      return 1;
   }

   // True clean up
   true_unlink(old_target);
   true_unlink(new_target);
   printf("[TEST] PASSED!\n");
   return 0;
}
