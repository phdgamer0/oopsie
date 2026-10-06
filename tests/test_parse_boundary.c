#include <sys/stat.h>
#include "../include/oopsie_wal.h"
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/syscall.h>
#include <unistd.h>

int main(void) {
   mkdir("/tmp/oopsie", 0700);
   OopWalContext reset_wal;
   if (WalFile_Open(&reset_wal, "/tmp/oopsie/vault.wal")) {
       WalHeader* hdr = (WalHeader*)reset_wal.map;
       hdr->current_offset = sizeof(WalHeader);
       hdr->toombstone = 0;
       WalFile_Close(&reset_wal);
   }
   printf("[TEST] Starting Boundary Parser Test...\n");
   
   
   
   for (int i = 0; i < 5; i++) {
      char pbuf[128];
      sprintf(pbuf, "/tmp/parse_bound_%d.txt", i);
      int fd = open(pbuf, O_CREAT | O_WRONLY, 0644);
      if (fd >= 0) close(fd);
   }
   
   OopWalContext wal;
   if (!WalFile_Open(&wal, "/tmp/oopsie/vault.wal")) {
       printf("[TEST] FAILED: Could not open WAL.\n");
       return 1;
   }
   
   OopWalRecordView views[5];
   size_t parsed = WalFile_Parse(&wal, views, 5);
   if (parsed != 5) {
       printf("[TEST] FAILED: Expected 5 views, got %zu\n", parsed);
       return 1;
   }
   
   WalFile_Close(&wal);
   printf("[TEST] PASSED!\n");
   return 0;
}
