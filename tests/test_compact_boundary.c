#include "../include/oopsie_wal.h"
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

int main(void) {
   OopWalContext reset_wal;
   if (WalFile_Open(&reset_wal, "/tmp/oopsie/vault.wal")) {
       WalHeader* hdr = (WalHeader*)reset_wal.map;
       hdr->current_offset = sizeof(WalHeader);
       hdr->toombstone = 0;
       WalFile_Close(&reset_wal);
   }
   printf("[TEST] Starting Compactor Boundary Test...\n");
   
   
   int fd = open("/tmp/compact_bound.txt", O_CREAT | O_WRONLY, 0644);
   if (fd >= 0) close(fd);
   
   OopWalContext wal;
   if (!WalFile_Open(&wal, "/tmp/oopsie/vault.wal")) return 1;
   
   OopWalRecordView views[1];
   size_t parsed = WalFile_Parse(&wal, views, 1);
   
   // Compact with 0 tombstones
   WalFile_Compact(&wal);
   
   parsed = WalFile_Parse(&wal, views, 1);
   if (parsed != 1) return 1;
   
   // Purge the only record (ALL TOMBSTONES)
   WalFile_Purge(&wal, &views[0]);
   WalFile_Compact(&wal);
   
   parsed = WalFile_Parse(&wal, views, 1);
   if (parsed != 0) {
       printf("[TEST] FAILED: Expected 0, got %zu\n", parsed);
       return 1;
   }
   
   WalFile_Close(&wal);
   printf("[TEST] PASSED!\n");
   return 0;
}
