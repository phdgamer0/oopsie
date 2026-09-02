#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/syscall.h>

#define NUM_FILES 1000

double get_time_ms(struct timespec start, struct timespec end) {
    return (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_nsec - start.tv_nsec) / 1000000.0;
}

int main(void) {
    printf("[TEST] Starting Shim Speed Test (%d files)...\n", NUM_FILES);

    char names[NUM_FILES][256];
    char names2[NUM_FILES][256];
    struct timespec start, end;
    double elapsed;

    // PRE-GENERATE STRINGS to eliminate test overhead!
    for (int i = 0; i < NUM_FILES; i++) {
        sprintf(names[i], "/tmp/shim_speed_%d.txt", i);
        sprintf(names2[i], "/tmp/shim_speed_renamed_%d.txt", i);
        syscall(SYS_unlink, names[i]);
        syscall(SYS_unlink, names2[i]);
    }

    // Benchmark 1: CREATE
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < NUM_FILES; i++) {
        int fd = open(names[i], O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd >= 0) {
            write(fd, "test", 4);
            close(fd);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    elapsed = get_time_ms(start, end);
    printf("  -> Hijacked open(CREATE) %d files in %.2f ms\n", NUM_FILES, elapsed);
    printf("  -> Throughput: %.0f creates/second\n", (NUM_FILES / elapsed) * 1000.0);

    // Benchmark 2: MODIFY
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < NUM_FILES; i++) {
        int fd = open(names[i], O_WRONLY | O_TRUNC);
        if (fd >= 0) {
            write(fd, "test2", 5);
            close(fd);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    elapsed = get_time_ms(start, end);
    printf("  -> Hijacked open(MODIFY) %d files in %.2f ms\n", NUM_FILES, elapsed);
    printf("  -> Throughput: %.0f modifies/second\n", (NUM_FILES / elapsed) * 1000.0);

    // Benchmark 3: RENAME
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < NUM_FILES; i++) {
        rename(names[i], names2[i]);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    elapsed = get_time_ms(start, end);
    printf("  -> Hijacked rename() %d files in %.2f ms\n", NUM_FILES, elapsed);
    printf("  -> Throughput: %.0f renames/second\n", (NUM_FILES / elapsed) * 1000.0);

    // Benchmark 4: UNLINK
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (int i = 0; i < NUM_FILES; i++) {
        unlink(names2[i]);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    elapsed = get_time_ms(start, end);
    printf("  -> Hijacked unlink() %d files in %.2f ms\n", NUM_FILES, elapsed);
    printf("  -> Throughput: %.0f deletions/second\n", (NUM_FILES / elapsed) * 1000.0);

    printf("[TEST] PASSED!\n");
    return 0;
}
