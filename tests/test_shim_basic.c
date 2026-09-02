#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "../include/oopsie_wal.h"

int main(void) {
    printf("[TEST] Starting Shim Basic Test...\n");

    const char* target = "/tmp/test_shim_basic.txt";
    
    // Create dummy file
    int fd = open(target, O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) {
        printf("[TEST] Failed to create dummy file\n");
        return 1;
    }
    write(fd, "hello shim", 10);
    close(fd);
    
    // Get its inode to verify the vault clone later
    struct stat target_st;
    stat(target, &target_st);

    // Delete it! Our shim should catch this via LD_PRELOAD.
    int res = unlink(target);
    if (res != 0) {
        printf("[TEST] Failed to unlink\n");
        return 1;
    }
    
    // Check if it's actually gone from /tmp
    if (stat(target, &target_st) == 0) {
        printf("[TEST] FAILED: File wasn't actually deleted from original path!\n");
        return 1;
    }
    
    // Verify WAL has the record
    OopWalContext wal;
    if (!WalFile_Open(&wal, "/tmp/oopsie/vault.wal", 0)) {
        printf("[TEST] Failed to open WAL\n");
        return 1;
    }
    
    int found = 0;
    size_t cursor = 0;
    while (cursor < wal.offset) {
        OopWalRecord* rec_ptr = (OopWalRecord*)((char*)wal.map + cursor);
        if (rec_ptr->magic != RECORD_MAGIC) {
            break; // Reached end of valid records
        }
        
        char* path_ptr = (char*)wal.map + cursor + sizeof(OopWalRecord);
        if (strncmp(path_ptr, target, rec_ptr->pathlen) == 0) {
            found = 1;
            break;
        }
        
        cursor += sizeof(OopWalRecord) + rec_ptr->pathlen + sizeof(uint32_t);
    }
    
    WalFile_Close(&wal);
    
    if (!found) {
        printf("[TEST] FAILED: WAL record not found for %s!\n", target);
        return 1;
    }
    
    printf("[TEST] PASSED! Shim successfully intercepted the delete.\n");
    return 0;
}
