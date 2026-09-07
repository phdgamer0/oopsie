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
   printf("[TEST] Starting Purger Boundary Test...\n");
   
   
   for(int i=0; i<5; i++) {
       int fd = open("/tmp/purge_bound.txt", O_CREAT | O_WRONLY, 0644);
       if (fd >= 0) close(fd);
   }
   
   OopWalContext wal;
   if (!WalFile_Open(&wal, "/tmp/oopsie/vault.wal")) return 1;
   
   OopWalRecordView views[5];
   size_t parsed = WalFile_Parse(&wal, views, 5);
   if (parsed != 5) return 1;
   
   WalFile_Purge(&wal, &views[0]); // Purge head
   WalFile_Purge(&wal, &views[4]); // Purge tail
   
   parsed = WalFile_Parse(&wal, views, 5);
   if (parsed != 3) {
       printf("[TEST] FAILED: Expected 3, got %zu\n", parsed);
       return 1;
   }
   
   WalFile_Close(&wal);
   printf("[TEST] PASSED!\n");
   return 0;
}
