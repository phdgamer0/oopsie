#ifndef VAULT_PURGE_H
#define VAULT_PURGE_H

#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>

// Speed tests create one vault_<inode>/.meta pair per file they touch and the
// shim never unlinks them, so a few runs fill /tmp and WalFile_Open stops
// working. Purge the vault entries around each test run instead.
static inline void vault_purge(void) {
   char dirpath[32];
   char fpath[96];
   for (int d = 0; d < 256; d++) {
      snprintf(dirpath, sizeof(dirpath), "/tmp/oopsie/%03d", d);
      DIR* dir = opendir(dirpath);
      if (!dir)
         continue;
      struct dirent* ent;
      while ((ent = readdir(dir)) != NULL) {
         if (strncmp(ent->d_name, "vault_", 6) != 0)
            continue;
         snprintf(fpath, sizeof(fpath), "/tmp/oopsie/%03d/%s", d, ent->d_name);
#ifdef SYS_unlink
         syscall(SYS_unlink, fpath);
#else
         syscall(SYS_unlinkat, AT_FDCWD, fpath, 0);
#endif
      }
      closedir(dir);
   }
}

#endif
