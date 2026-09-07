#include <sys/stat.h>
#include "../include/oopsie_wal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
   mkdir("/tmp/oopsie", 0700);
   printf("[TEST] Starting Sorter Boundary Test...\n");
   
   OopWalRecord r1 = { .pathlen = 2, .timestamp = 100 };
   OopWalRecord r2 = { .pathlen = 1, .timestamp = 100 };
   OopWalRecord r3 = { .pathlen = 2, .timestamp = 50 };
   
   OopWalRecordView views[3] = {
       { &r1, "ab" }, // longer
       { &r2, "a" },  // shorter, should be first
       { &r3, "ab" }  // same length, older timestamp, should beat r1
   };
   
   qsort(views, 3, sizeof(OopWalRecordView), cmp_wal_name_asc);
   
   if (views[0].rec->pathlen != 1) { // "a"
       printf("[TEST] FAILED: Expected shortest string first\n");
       return 1;
   }
   
   if (views[1].rec->timestamp != 50) { // older "ab"
       printf("[TEST] FAILED: Expected older timestamp to tie-break\n");
       return 1;
   }
   
   printf("[TEST] PASSED!\n");
   return 0;
}
