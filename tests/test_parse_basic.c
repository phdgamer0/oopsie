#include "../include/oopsie_wal.h"
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
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
   printf("[TEST] Starting Basic Parser Test...\n");
   
   
   
   int fd1 = open("/tmp/parse_basic_1.txt", O_CREAT | O_WRONLY, 0644);
   if (fd1 >= 0) close(fd1);
   int fd2 = open("/tmp/parse_basic_2.txt", O_CREAT | O_WRONLY, 0644);
   if (fd2 >= 0) close(fd2);
   int fd3 = open("/tmp/parse_basic_3.txt", O_CREAT | O_WRONLY, 0644);
   if (fd3 >= 0) close(fd3);
   
   OopWalContext wal;
   if (!WalFile_Open(&wal, "/tmp/oopsie/vault.wal")) {
       printf("[TEST] FAILED: Could not open WAL.\n");
       return 1;
   }
   
   OopWalRecordView views[10];
   size_t parsed = WalFile_Parse(&wal, views, 10);
   if (parsed != 3) {
       printf("[TEST] FAILED: Expected 3 views, got %zu\n", parsed);
       return 1;
   }
   
   if (strncmp(views[0].path, "/tmp/parse_basic_1.txt", views[0].rec->pathlen) != 0) {
       printf("[TEST] FAILED: Path mismatch 0\n");
       return 1;
   }
   
   WalFile_Close(&wal);
   printf("[TEST] PASSED!\n");
   return 0;
}
