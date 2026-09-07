#include <sys/stat.h>
#include "../include/oopsie_wal.h"
#include <sys/syscall.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define NUM_RECORDS 10000

static double get_time_ms(struct timespec start, struct timespec end) {
   return (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
}

int main(void) {
   mkdir("/tmp/oopsie", 0700);
   OopWalContext reset_wal;
   if (WalFile_Open(&reset_wal, "/tmp/oopsie/vault.wal")) {
       WalHeader* hdr = (WalHeader*)reset_wal.map;
       hdr->current_offset = sizeof(WalHeader);
       hdr->toombstone = 0;
       WalFile_Close(&reset_wal);
   }
   printf("[TEST] Starting Compact Speed Test...\n");
   
   
   for(int i=0; i<NUM_RECORDS; i++) {
       int fd = open("/tmp/compact_speed.txt", O_CREAT | O_WRONLY, 0644);
       if (fd >= 0) close(fd);
   }
   
   OopWalContext wal;
   if (!WalFile_Open(&wal, "/tmp/oopsie/vault.wal")) return 1;
   
   OopWalRecordView* views = malloc(sizeof(OopWalRecordView) * NUM_RECORDS);
   size_t parsed = WalFile_Parse(&wal, views, NUM_RECORDS);
   
   for(size_t i=0; i<parsed; i+=2) { // purge half
       WalFile_Purge(&wal, &views[i]);
   }
   
   struct timespec start, end;
   clock_gettime(CLOCK_MONOTONIC, &start);
   
   WalFile_Compact(&wal);
   
   clock_gettime(CLOCK_MONOTONIC, &end);
   double elapsed = get_time_ms(start, end);
   
   printf("  -> Compacted %d records in %.2f ms\n", NUM_RECORDS, elapsed);
   
   WalFile_Close(&wal);
   free(views);
   printf("[TEST] PASSED!\n");
   return 0;
}
