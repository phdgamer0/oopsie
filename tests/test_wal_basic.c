#include "../include/oopsie_wal.h"
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    printf("[TEST] Starting Basic Append and Read...\n");

    OopWalContext wal;
    remove("test_wal_basic.wal");

    bool opened = WalFile_Open(&wal, "test_wal_basic.wal", WF_CREATE | WF_TRUNCATE);
    if (!opened) {
        printf("[TEST] FAILED: Could not open test_wal_basic.wal\n");
        return 1;
    }

    const char* path1 = "/home/user/test1.c";
    OopWalRecord rec1 = {
        .magic = RECORD_MAGIC,
        .action = OopAction_DELETE,
        .timestamp = 1693652130,
        .filesize = 1024,
        .pathlen = (uint32_t)strlen(path1)
    };
    WalFile_Append(&wal, path1, &rec1);

    const char* path2 = "/home/user/important.h";
    OopWalRecord rec2 = {
        .magic = RECORD_MAGIC,
        .action = OopAction_MODIFY,
        .timestamp = 1693652135,
        .filesize = 4096,
        .pathlen = (uint32_t)strlen(path2)
    };
    WalFile_Append(&wal, path2, &rec2);

    size_t cursor = wal.offset;
    OopWalRecord outRec;
    char outPath[4096];

    cursor = WalFile_GetPrevious(&wal, cursor, &outRec, outPath);
    if (cursor == (size_t)-1 || strcmp(outPath, path2) != 0) {
        printf("[TEST] FAILED: First read backward returned error offset.\n");
        WalFile_Close(&wal);
        return 1;
    }

    cursor = WalFile_GetPrevious(&wal, cursor, &outRec, outPath);
    if (cursor == (size_t)-1 || strcmp(outPath, path1) != 0) {
        printf("[TEST] FAILED: Second read backward returned error offset.\n");
        WalFile_Close(&wal);
        return 1;
    }

    if (cursor != 0) {
        printf("[TEST] FAILED: Expected cursor to be 0 after reading all records.\n");
        WalFile_Close(&wal);
        return 1;
    }

    WalFile_Close(&wal);
    printf("[TEST] PASSED: All WAL operations successful!\n");
    return 0;
}
