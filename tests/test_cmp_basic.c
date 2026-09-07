#include "../include/oopsie_wal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
   printf("[TEST] Starting Sorter Basic Test...\n");
   
   OopWalRecord r1 = { .pathlen = 5, .timestamp = 200 };
   OopWalRecord r2 = { .pathlen = 6, .timestamp = 300 };
   OopWalRecord r3 = { .pathlen = 5, .timestamp = 100 };
   
   OopWalRecordView views[3] = {
       { &r1, "zebra" },
       { &r2, "banana" },
       { &r3, "apple" }
   };
   
   qsort(views, 3, sizeof(OopWalRecordView), cmp_wal_name_asc);
   
   if (strncmp(views[0].path, "apple", 5) != 0 ||
       strncmp(views[1].path, "banana", 6) != 0 ||
       strncmp(views[2].path, "zebra", 5) != 0) {
       printf("[TEST] FAILED: Name sort failed.\n");
       return 1;
   }
   
   qsort(views, 3, sizeof(OopWalRecordView), cmp_wal_time);
   
   if (views[0].rec->timestamp != 100 ||
       views[1].rec->timestamp != 200 ||
       views[2].rec->timestamp != 300) {
       printf("[TEST] FAILED: Time sort failed.\n");
       return 1;
   }
   
   printf("[TEST] PASSED!\n");
   return 0;
}
