#include <sys/stat.h>
#include "../include/oopsie_wal.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define NUM_RECORDS 100000

static double get_time_ms(struct timespec start, struct timespec end) {
   return (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
}

int main(void) {
   mkdir("/tmp/oopsie", 0700);
   printf("[TEST] Starting Sorter Speed Test (%d items)...\n", NUM_RECORDS);
   
   OopWalRecordView* views = malloc(sizeof(OopWalRecordView) * NUM_RECORDS);
   OopWalRecord* recs = malloc(sizeof(OopWalRecord) * NUM_RECORDS);
   
   for(int i=0; i<NUM_RECORDS; i++) {
       recs[i].timestamp = (uint64_t)rand();
       recs[i].pathlen = 4;
       views[i].rec = &recs[i];
       views[i].path = "test";
   }
   
   struct timespec start, end;
   clock_gettime(CLOCK_MONOTONIC, &start);
   
   qsort(views, NUM_RECORDS, sizeof(OopWalRecordView), cmp_wal_time);
   
   clock_gettime(CLOCK_MONOTONIC, &end);
   double elapsed = get_time_ms(start, end);
   
   printf("  -> Sorted %d records in %.2f ms\n", NUM_RECORDS, elapsed);
   
   free(views);
   free(recs);
   printf("[TEST] PASSED!\n");
   return 0;
}
