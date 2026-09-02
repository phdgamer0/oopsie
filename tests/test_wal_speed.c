#define _GNU_SOURCE
#include "../include/oopsie_wal.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#define NUM_RECORDS 15000

double get_time_in_ms(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
}

int main(void) {
    printf("[BENCHMARK] Starting Speed Test (%d records)...\n", NUM_RECORDS);

    OopWalContext wal;
    remove("test_wal_speed.wal");

    bool opened = WalFile_Open(&wal, "test_wal_speed.wal", WF_CREATE | WF_TRUNCATE);
    if (!opened) {
        printf("[BENCHMARK] FAILED: Could not open test_wal_speed.wal\n");
        return 1;
    }

    const char* path = "/var/log/syslog";
    OopWalRecord rec = {
        .magic = RECORD_MAGIC,
        .action = OopAction_MODIFY,
        .timestamp = 1693652130,
        .filesize = 1024,
        .pathlen = (uint32_t)strlen(path)
    };

    struct timespec start, end;
    
    // --- BENCHMARK WRITING ---
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < NUM_RECORDS; i++) {
        WalFile_Append(&wal, path, &rec);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    double write_time = get_time_in_ms(start, end);
    printf("  -> Wrote %d records in %.3f ms\n", NUM_RECORDS, write_time);
    printf("  -> Throughput: %.0f records/second\n", (NUM_RECORDS / write_time) * 1000.0);

    // --- BENCHMARK READING ---
    size_t cursor = wal.offset;
    OopWalRecord outRec;
    char outPath[4096];

    int read_count = 0;
    clock_gettime(CLOCK_MONOTONIC, &start);
    while (cursor > 0) {
        cursor = WalFile_GetPrevious(&wal, cursor, &outRec, outPath);
        if (cursor == (size_t)-1) {
            printf("[BENCHMARK] FAILED: Corrupted read at record %d\n", read_count);
            return 1;
        }
        read_count++;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    double read_time = get_time_in_ms(start, end);
    printf("  <- Read %d records backwards in %.3f ms\n", read_count, read_time);
    printf("  <- Throughput: %.0f records/second\n", (read_count / read_time) * 1000.0);

    WalFile_Close(&wal);
    printf("[BENCHMARK] PASSED!\n");
    return 0;
}
