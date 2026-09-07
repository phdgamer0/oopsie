#include "../include/oopsie_wal.h"
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sys/stat.h>

int main(void) {
   OopWalContext reset_wal;
   if (WalFile_Open(&reset_wal, "/tmp/oopsie/vault.wal")) {
       WalHeader* hdr = (WalHeader*)reset_wal.map;
       hdr->current_offset = sizeof(WalHeader);
       hdr->toombstone = 0;
       WalFile_Close(&reset_wal);
   }
   printf("[TEST] Starting Compactor Basic Test...\n");
   
   
   for(int i=0; i<10; i++) {
       char buf[64];
       sprintf(buf, "/tmp/compact_basic_%d.txt", i);
       int fd = open(buf, O_CREAT | O_WRONLY, 0644);
       if (fd >= 0) close(fd);
   }
   
   OopWalContext wal;
   if (!WalFile_Open(&wal, "/tmp/oopsie/vault.wal")) return 1;
   
   OopWalRecordView views[10];
   size_t parsed = WalFile_Parse(&wal, views, 10);
   
   // Purge 5
   for(int i=0; i<5; i++) {
       WalFile_Purge(&wal, &views[i]);
   }
   
   bool res = WalFile_Compact(&wal);
   if (!res) return 1;
   
   parsed = WalFile_Parse(&wal, views, 10);
   if (parsed != 5) {
       printf("[TEST] FAILED: Expected 5 views after compaction, got %zu\n", parsed);
       return 1;
   }
   
   WalHeader* hdr = (WalHeader*)wal.map;
   if (hdr->toombstone != 0) {
       printf("[TEST] FAILED: Expected 0 tombstones after compaction, got %u\n", hdr->toombstone);
       return 1;
   }
   
   WalFile_Close(&wal);
   printf("[TEST] PASSED!\n");
   return 0;
}
