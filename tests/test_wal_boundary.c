#include "../include/oopsie_wal.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    printf("[TEST] Starting Boundary Tests...\n");

    OopWalContext wal;
    remove("test_wal_boundary.wal");

    bool opened = WalFile_Open(&wal, "test_wal_boundary.wal", WF_CREATE | WF_TRUNCATE);
    if (!opened) {
        printf("[TEST] FAILED: Could not open test_wal_boundary.wal\n");
        return 1;
    }

    // 1. Test Empty Path (pathlen = 0)
    const char* empty_path = "";
    OopWalRecord rec_empty = {
        .magic = RECORD_MAGIC,
        .action = OopAction_CREATE,
        .timestamp = 1000,
        .filesize = 0,
        .pathlen = 0
    };
    WalFile_Append(&wal, empty_path, &rec_empty);

    // 2. Test Max Size Path
    char large_path[4000];
    memset(large_path, 'A', 3999);
    large_path[3999] = '\0';
    OopWalRecord rec_large = {
        .magic = RECORD_MAGIC,
        .action = OopAction_MODIFY,
        .timestamp = 2000,
        .filesize = 9999999,
        .pathlen = 3999
    };
    WalFile_Append(&wal, large_path, &rec_large);

    size_t cursor = 0;
    OopWalRecord outRec;
    char outPath[4096];

    // Read backward (large path)
    cursor = WalFile_GetPrevious(&wal, cursor, &outRec, outPath);
    if (cursor == (size_t)-1) {
        printf("[TEST] FAILED: Reading large path returned error.\n");
        return 1;
    }
    if (outRec.pathlen != 3999 || strlen(outPath) != 3999 || outPath[0] != 'A' || outPath[3998] != 'A') {
        printf("[TEST] FAILED: Large path data corrupted.\n");
        return 1;
    }

    // Read backward (empty path)
    cursor = WalFile_GetPrevious(&wal, cursor, &outRec, outPath);
    if (cursor == (size_t)-1) {
        printf("[TEST] FAILED: Reading empty path returned error.\n");
        return 1;
    }
    if (outRec.pathlen != 0 || strlen(outPath) != 0) {
        printf("[TEST] FAILED: Empty path data corrupted.\n");
        return 1;
    }

    // Read backward (should return 0)
    if (cursor != 0) {
        printf("[TEST] FAILED: Cursor is not 0 at the start of the file.\n");
        return 1;
    }

    WalFile_Close(&wal);
    printf("[TEST] PASSED: Boundary Tests successful!\n");
    return 0;
}
