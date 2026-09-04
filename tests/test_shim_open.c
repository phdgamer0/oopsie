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
   printf("[TEST] Starting Shim Open Test...\n");
   const char* target = "/tmp/test_shim_open.txt";

   // Clean up first
   true_unlink(target);

   // 1. Test CREATE
   int fd = open(target, O_CREAT | O_WRONLY, 0644);
   if (fd < 0) {
      printf("[TEST] Failed to create file\n");
      return 1;
   }
   write(fd, "hello", 5);
   close(fd);

   // Verify WAL has CREATE
   OopWalContext wal;
   if (!WalFile_Open(&wal, "/tmp/oopsie/vault.wal")) {
      printf("[TEST] Failed to open WAL\n");
      return 1;
   }

   int found_create = 0;
   WalHeader* header = (WalHeader*)wal.map;
   size_t cursor = sizeof(WalHeader);
   while (cursor < header->current_offset) {
      OopWalRecord* rec_ptr = (OopWalRecord*)((char*)wal.map + cursor);
      if (rec_ptr->magic != RECORD_MAGIC)
         break;

      char* path_ptr = (char*)wal.map + cursor + sizeof(OopWalRecord);
      if (rec_ptr->action == OopAction_CREATE && strncmp(path_ptr, target, rec_ptr->pathlen) == 0) {
         found_create = 1;
      }
      cursor += sizeof(OopWalRecord) + rec_ptr->pathlen + sizeof(uint32_t);
   }

   if (!found_create) {
      printf("[TEST] FAILED: CREATE record not found!\n");
      WalFile_Close(&wal);
      return 1;
   }

   // 2. Test MODIFY
   fd = open(target, O_TRUNC | O_WRONLY);
   if (fd < 0) {
      printf("[TEST] Failed to modify file\n");
      return 1;
   }
   write(fd, "world", 5);
   close(fd);

   // Verify WAL has MODIFY
   int found_modify = 0;
   cursor = sizeof(WalHeader);
   while (cursor < header->current_offset) {
      OopWalRecord* rec_ptr = (OopWalRecord*)((char*)wal.map + cursor);
      if (rec_ptr->magic != RECORD_MAGIC)
         break;

      char* path_ptr = (char*)wal.map + cursor + sizeof(OopWalRecord);
      if (rec_ptr->action == OopAction_MODIFY && strncmp(path_ptr, target, rec_ptr->pathlen) == 0) {
         found_modify = 1;
      }
      cursor += sizeof(OopWalRecord) + rec_ptr->pathlen + sizeof(uint32_t);
   }
   WalFile_Close(&wal);

   if (!found_modify) {
      printf("[TEST] FAILED: MODIFY record not found!\n");
      return 1;
   }

   // True clean up
   true_unlink(target);
   printf("[TEST] PASSED!\n");
   return 0;
}
