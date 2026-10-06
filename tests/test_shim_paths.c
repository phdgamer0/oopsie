#define _GNU_SOURCE
#include "oopsie_wal.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

void true_unlink(const char* path) {
#ifdef SYS_unlink
   syscall(SYS_unlink, path);
#else
   syscall(SYS_unlinkat, AT_FDCWD, path, 0);
#endif
}

void true_rmdir(const char* path) {
#ifdef SYS_rmdir
   syscall(SYS_rmdir, path);
#else
   syscall(SYS_unlinkat, AT_FDCWD, path, AT_REMOVEDIR);
#endif
}

int path_matches(const char* path_ptr, uint32_t pathlen, const char* path) {
   size_t want = strlen(path);
   if ((size_t)pathlen != want)
      return 0;
   return memcmp(path_ptr, path, want) == 0;
}

int has_record(const char* path, uint8_t action) {
   OopWalContext wal;
   if (!WalFile_Open(&wal, "/tmp/oopsie/vault.wal"))
      return 0;
   WalHeader* header = (WalHeader*)wal.map;
   size_t cursor = sizeof(WalHeader);
   int found = 0;
   while (cursor < header->current_offset) {
      OopWalRecord* rec_ptr = (OopWalRecord*)((char*)wal.map + cursor);
      if (rec_ptr->magic != RECORD_MAGIC)
         break;
      char* path_ptr = (char*)wal.map + cursor + sizeof(OopWalRecord);
      if (rec_ptr->action == action && path_matches(path_ptr, rec_ptr->pathlen, path)) {
         found = 1;
         break;
      }
      size_t rs = sizeof(OopWalRecord) + rec_ptr->pathlen + sizeof(uint32_t);
      cursor += (rs + 7) & ~(size_t)7;
   }
   WalFile_Close(&wal);
   return found;
}

int main() {
   printf("[TEST] Starting Shim Paths Test...\n");
   const char* dir_target = "/tmp/test_shim_paths_dir";
   const char* at_target = "/tmp/test_shim_paths_dir/rel_at.txt";
   const char* cwd_target = "/tmp/test_shim_paths_dir/rel_cwd.txt";
   const char* creat_target = "/tmp/test_shim_paths_creat.txt";
   const char* lfs_target = "/tmp/test_shim_paths_lfs.txt";

   (void)mkdir(dir_target, 0700);
   true_unlink(at_target);
   true_unlink(cwd_target);
   true_unlink(creat_target);
   true_unlink(lfs_target);

   int failures = 0;
   char abs_cwd[4096];

   // 1. A relative path handed to openat must be recorded fully qualified
   int dirfd = open(dir_target, O_RDONLY | O_DIRECTORY);
   if (dirfd < 0) {
      printf("[TEST] Failed to open test directory\n");
      return 1;
   }
   int afd = openat(dirfd, "rel_at.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
   if (afd < 0 || write(afd, "via_dirfd", 9) != 9) {
      printf("[TEST] Failed to setup openat target\n");
      return 1;
   }
   close(afd);
   close(dirfd);
   if (!has_record(at_target, OopAction_CREATE)) {
      printf("[TEST] FAILED: openat recorded no fully qualified CREATE for %s!\n", at_target);
      failures++;
   }

   // 2. A relative path resolved against the cwd must be recorded fully qualified
   if (getcwd(abs_cwd, sizeof(abs_cwd)) == NULL) {
      printf("[TEST] getcwd failed\n");
      return 1;
   }
   if (chdir(dir_target) != 0) {
      printf("[TEST] chdir failed\n");
      return 1;
   }
   int cfd = open("rel_cwd.txt", O_RDWR | O_CREAT | O_TRUNC, 0644);
   if (cfd >= 0) {
      write(cfd, "via_cwd", 7);
      close(cfd);
   }
   if (chdir(abs_cwd) != 0) {
      printf("[TEST] chdir back failed\n");
      return 1;
   }
   if (cfd < 0) {
      printf("[TEST] Failed to setup cwd target\n");
      return 1;
   }
   if (!has_record(cwd_target, OopAction_CREATE)) {
      printf("[TEST] FAILED: relative open recorded no fully qualified CREATE for %s!\n", cwd_target);
      failures++;
   }

   // 3. creat must be intercepted, tar creates its archive through it
   int crfd = creat(creat_target, 0644);
   if (crfd < 0) {
      printf("[TEST] creat failed\n");
      return 1;
   }
   write(crfd, "archive", 7);
   close(crfd);
   if (!has_record(creat_target, OopAction_CREATE)) {
      printf("[TEST] FAILED: creat was not intercepted!\n");
      failures++;
   }

   // 4. The large file variants are what modern toolchains actually call
   int lfd = open64(lfs_target, O_RDWR | O_CREAT | O_TRUNC, 0644);
   if (lfd < 0) {
      printf("[TEST] open64 failed\n");
      return 1;
   }
   write(lfd, "large", 5);
   close(lfd);
   if (!has_record(lfs_target, OopAction_CREATE)) {
      printf("[TEST] FAILED: open64 was not intercepted!\n");
      failures++;
   }
   int lfd2 = creat64(lfs_target, 0644);
   if (lfd2 < 0) {
      printf("[TEST] creat64 failed\n");
      return 1;
   }
   close(lfd2);
   if (!has_record(lfs_target, OopAction_MODIFY)) {
      printf("[TEST] FAILED: creat64 was not intercepted!\n");
      failures++;
   }

   true_unlink(at_target);
   true_unlink(cwd_target);
   true_unlink(creat_target);
   true_unlink(lfs_target);
   true_rmdir(dir_target);

   if (failures != 0) {
      printf("[TEST] FAILED with %d error(s)!\n", failures);
      return 1;
   }
   printf("[TEST] PASSED!\n");
   return 0;
}