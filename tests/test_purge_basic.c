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
   printf("[TEST] Starting Purger Basic Test...\n");
   
   
   for(int i=0; i<3; i++) {
       char buf[64];
       sprintf(buf, "/tmp/purge_basic_%d.txt", i);
       int fd = open(buf, O_CREAT | O_WRONLY, 0644);
       if (fd >= 0) close(fd);
   }
   
   OopWalContext wal;
   if (!WalFile_Open(&wal, "/tmp/oopsie/vault.wal")) return 1;
   
   OopWalRecordView views[3];
   size_t parsed = WalFile_Parse(&wal, views, 3);
   if (parsed != 3) return 1;
   
   WalFile_Purge(&wal, &views[1]); // purge the middle one
   
   // re-parse
   parsed = WalFile_Parse(&wal, views, 3);
   if (parsed != 2) {
       printf("[TEST] FAILED: Expected 2, got %zu\n", parsed);
       return 1;
   }
   
   WalHeader* hdr = (WalHeader*)wal.map;
   if (hdr->toombstone != 1) {
       printf("[TEST] FAILED: Expected 1 tombstone, got %u\n", hdr->toombstone);
       return 1;
   }
   
   WalFile_Close(&wal);
   printf("[TEST] PASSED!\n");
   return 0;
}
