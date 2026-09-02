#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>

int main(void) {
    printf("[TEST] Starting Shim Boundary Test...\n");

    // 1. Try to delete a directory (shim should pass it through, and it should fail)
    const char* test_dir = "/tmp/test_shim_dir";
    mkdir(test_dir, 0755);
    
    int res = unlink(test_dir);
    if (res == 0) {
        printf("[TEST] FAILED: unlink() succeeded on a directory!\n");
        return 1;
    }
    
    rmdir(test_dir); // Cleanup
    
    // 2. Try to delete a non-existent file
    res = unlink("/tmp/test_shim_does_not_exist_999.txt");
    if (res == 0) {
        printf("[TEST] FAILED: unlink() succeeded on non-existent file!\n");
        return 1;
    }

    printf("[TEST] PASSED! Shim handled boundary conditions perfectly.\n");
    return 0;
}
