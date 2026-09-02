#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <oopsie_wal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define VAULT_PATH (const char*)"/tmp/oopsie/vault_"
#define WAL_PATH (const char*)"/tmp/oopsie/vault.wal"
#define BUFFER_SZ (size_t)512

OopWalContext GlobalWal;

__attribute__((constructor)) void oopsie_init() {
   WalFile_Open(&GlobalWal, WAL_PATH, WF_CREATE | WF_TRUNCATE);
}
__attribute__((destructor)) void oopsie_cleanup() {
   WalFile_Close(&GlobalWal);
}

static inline void make_vault_path(char* buffer, unsigned long ino) {
    char temp[32];
    char* p = temp + 31;
    *p = '\0';
    do {
        *--p = '0' + (ino % 10);
        ino /= 10;
    } while (ino > 0);
    
    memcpy(buffer, "/tmp/oopsie/vault_", 18);
    size_t len = (temp + 31) - p + 1;
    memcpy(buffer + 18, p, len);
}

typedef int(gnu_unlink_t)(const char* path);
typedef int(gnu_unlinkat_t)(int dirfd, const char* path, int flags);
typedef int(gnu_open_t)(const char* pathname, int flags, ...);
typedef int(gnu_openat_t)(int dirfd, const char* pathname, int flags, ...);
typedef int(gnu_rename_t)(const char* oldpath, const char* newpath);
typedef int(gnu_renameat_t)(int olddirfd, const char* oldpath, int newdirfd, const char* newpath);
typedef int(gnu_renameat2_t)(int olddirfd, const char* oldpath, int newdirfd, const char* newpath, unsigned int flags);

gnu_rename_t* gnu_rename = NULL;
gnu_renameat_t* gnu_renameat = NULL;
gnu_renameat2_t* gnu_renameat2 = NULL;
gnu_unlink_t* gnu_unlink = NULL;
gnu_unlinkat_t* gnu_unlinkat = NULL;
gnu_open_t* gnu_open = NULL;
gnu_openat_t* gnu_openat = NULL;

int unlink(const char* path) {
   if (gnu_unlink == NULL) {
      gnu_unlink = (gnu_unlink_t*)dlsym(RTLD_NEXT, "unlink");
   }
   if (gnu_open == NULL) {
      gnu_open = (gnu_open_t*)dlsym(RTLD_NEXT, "open");
   }
   if (gnu_openat == NULL) {
      gnu_openat = (gnu_openat_t*)dlsym(RTLD_NEXT, "openat");
   }
   OopWalRecord rec;
   struct stat stat_buf;
   if (lstat(path, &stat_buf) == 0 && S_ISREG(stat_buf.st_mode)) {
      rec.magic = RECORD_MAGIC;
      rec.filesize = (uint64_t)stat_buf.st_size;
      rec.timestamp = (uint64_t)time(NULL);
      rec.action = OopAction_DELETE;
      rec.pathlen = (uint32_t)strlen(path);
      WalFile_Append(&GlobalWal, path, &rec);
      int cloneRes = -1;
      int srcFd = gnu_open(path, O_RDONLY | O_CLOEXEC, 0600);
      if (srcFd < 0) {
         return gnu_unlink(path);
      }
      char buffer[64];
      make_vault_path(buffer, stat_buf.st_ino);
      int destFd = gnu_open(buffer, O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
      if (destFd < 0) {
         close(srcFd);
         return gnu_unlink(path);
      }
#ifdef FICLONE
      cloneRes = ioctl(destFd, FICLONE, srcFd);
#endif
      if (cloneRes != 0) {
         sendfile(destFd, srcFd, NULL, stat_buf.st_size);
      }
      close(srcFd);
      close(destFd);
   }
   return gnu_unlink(path);
}

int unlinkat(int dirfd, const char* path, int flags) {
   if (gnu_unlinkat == NULL) {
      gnu_unlinkat = (gnu_unlinkat_t*)dlsym(RTLD_NEXT, "unlinkat");
   }
   if (gnu_open == NULL) {
      gnu_open = (gnu_open_t*)dlsym(RTLD_NEXT, "open");
   }
   if (gnu_openat == NULL) {
      gnu_openat = (gnu_openat_t*)dlsym(RTLD_NEXT, "openat");
   }
   OopWalRecord rec;
   struct stat stat_buf;
   if (fstatat(dirfd, path, &stat_buf, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(stat_buf.st_mode)) {
      rec.magic = RECORD_MAGIC;
      rec.filesize = (uint64_t)stat_buf.st_size;
      rec.timestamp = (uint64_t)time(NULL);
      rec.action = OopAction_DELETE;
      rec.pathlen = (uint32_t)strlen(path);
      WalFile_Append(&GlobalWal, path, &rec);
      int cloneRes = (int)-1;
      int srcFd = gnu_openat(dirfd, path, O_RDONLY | O_CLOEXEC, 0600);
      if (srcFd < 0) {
         return gnu_unlink(path);
      }
      char buffer[64];
      make_vault_path(buffer, stat_buf.st_ino);
      int destFd = gnu_open(buffer, O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
      if (destFd < (int)0) {
         close(srcFd);
         return gnu_unlink(path);
      }
#ifdef FICLONE
      cloneRes = ioctl(destFd, FICLONE, srcFd);
#endif
      if (cloneRes != (int)0) {
         sendfile(destFd, srcFd, NULL, stat_buf.st_size);
      }
      close(srcFd);
      close(destFd);
   }
   return gnu_unlinkat(dirfd, path, flags);
}

int open(const char* pathname, int flags, ...) {
   if (gnu_open == NULL) {
      gnu_open = (gnu_open_t*)dlsym(RTLD_NEXT, "open");
   }
   mode_t mode = (mode_t)0;
   if (flags & O_CREAT) {
      va_list args;
      va_start(args, flags);
      mode = va_arg(args, mode_t);
      va_end(args);
   }
   if (strncmp(pathname, "/tmp/oopsie/", 12) == (int)0) {
      return gnu_open(pathname, flags, mode);
   }
   struct stat stat_;
   int exists = (lstat(pathname, &stat_) == 0 && S_ISREG(stat_.st_mode));
   if (exists && ((flags & O_WRONLY) || (flags & O_RDWR) || (flags & O_TRUNC))) {
      OopWalRecord rec;
      rec.magic = RECORD_MAGIC;
      rec.action = OopAction_MODIFY;
      rec.filesize = (uint64_t)stat_.st_size;
      rec.timestamp = (uint64_t)time(NULL);
      rec.pathlen = (uint32_t)strlen(pathname);
      WalFile_Append(&GlobalWal, pathname, &rec);
      int srcFd = gnu_open(pathname, O_RDONLY | O_CLOEXEC, 0600);
      if (srcFd < (int)0) {
         return gnu_open(pathname, flags, mode);
      }
      char buffer[BUFFER_SZ];
      int filled = snprintf(buffer, BUFFER_SZ, "%s%lu", VAULT_PATH, stat_.st_ino);
      if (filled >= BUFFER_SZ) {
         close(srcFd);
         return gnu_open(pathname, flags, mode);
      }
      buffer[filled] = '\0';
      int destFd = gnu_open(buffer, O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
      if (destFd < (int)0) {
         close(srcFd);
         return gnu_open(pathname, flags, mode);
      }
      int cloneRes = (int)-1;
#ifdef FICLONE
      cloneRes = ioctl(destFd, FICLONE, srcFd);
#endif
      if (cloneRes != (int)0) {
         sendfile(destFd, srcFd, NULL, stat_.st_size);
      }
      close(srcFd);
      close(destFd);
   } else if (!exists && (flags & O_CREAT)) {
      OopWalRecord rec;
      rec.magic = RECORD_MAGIC;
      rec.action = OopAction_CREATE;
      rec.filesize = (uint64_t)0;
      rec.timestamp = (uint64_t)time(NULL);
      rec.pathlen = (uint32_t)strlen(pathname);
      WalFile_Append(&GlobalWal, pathname, &rec);
   }

   return gnu_open(pathname, flags, mode);
}

int openat(int dirfd, const char* pathname, int flags, ...) {
   if (gnu_openat == NULL) {
      gnu_openat = (gnu_openat_t*)dlsym(RTLD_NEXT, "openat");
   }
   mode_t mode = (mode_t)0;
   if (flags & O_CREAT) {
      va_list args;
      va_start(args, flags);
      mode = va_arg(args, mode_t);
      va_end(args);
   }
   if (strncmp(pathname, "/tmp/oopsie/", 12) == (int)0) {
      return gnu_openat(dirfd, pathname, flags, mode);
   }
   struct stat stat_;
   int exists = (fstatat(dirfd, pathname, &stat_, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(stat_.st_mode));
   if (exists && ((flags & O_WRONLY) || (flags & O_RDWR) || (flags & O_TRUNC))) {
      OopWalRecord rec;
      rec.magic = RECORD_MAGIC;
      rec.action = OopAction_MODIFY;
      rec.filesize = (uint64_t)stat_.st_size;
      rec.timestamp = (uint64_t)time(NULL);
      rec.pathlen = (uint32_t)strlen(pathname);
      WalFile_Append(&GlobalWal, pathname, &rec);
      int srcFd = gnu_openat(dirfd, pathname, O_RDONLY | O_CLOEXEC, 0600);
      if (srcFd < (int)0) {
         return gnu_openat(dirfd, pathname, flags, mode);
      }
      char buffer[BUFFER_SZ];
      int filled = snprintf(buffer, BUFFER_SZ, "%s%lu", VAULT_PATH, stat_.st_ino);
      if (filled >= BUFFER_SZ) {
         close(srcFd);
         return gnu_openat(dirfd, pathname, flags, mode);
      }
      buffer[filled] = '\0';
      int destFd = gnu_open(buffer, O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
      if (destFd < (int)0) {
         close(srcFd);
         return gnu_openat(dirfd, pathname, flags, mode);
      }
      int cloneRes = (int)-1;
#ifdef FICLONE
      cloneRes = ioctl(destFd, FICLONE, srcFd);
#endif
      if (cloneRes != (int)0) {
         sendfile(destFd, srcFd, NULL, stat_.st_size);
      }
      close(srcFd);
      close(destFd);
   } else if (!exists && (flags & O_CREAT)) {
      OopWalRecord rec;
      rec.magic = RECORD_MAGIC;
      rec.action = OopAction_CREATE;
      rec.filesize = (uint64_t)0;
      rec.timestamp = (uint64_t)time(NULL);
      rec.pathlen = (uint32_t)strlen(pathname);
      WalFile_Append(&GlobalWal, pathname, &rec);
   }
   return gnu_openat(dirfd, pathname, flags, mode);
}
int rename(const char* oldpath, const char* newpath) {
   if (gnu_rename == NULL) {
      gnu_rename = (gnu_rename_t*)dlsym(RTLD_NEXT, "rename");
   }
   if (gnu_open == NULL) {
      gnu_open = (gnu_open_t*)dlsym(RTLD_NEXT, "open");
   }
   if (strncmp(oldpath, "/tmp/oopsie/", 12) == 0 || strncmp(newpath, "/tmp/oopsie/", 12) == 0) {
      return gnu_rename(oldpath, newpath);
   }
   struct stat stat_buf;
   if (lstat(newpath, &stat_buf) == 0 && S_ISREG(stat_buf.st_mode)) {
      OopWalRecord rec_new;
      rec_new.magic = RECORD_MAGIC;
      rec_new.filesize = (uint64_t)stat_buf.st_size;
      rec_new.timestamp = (uint64_t)time(NULL);
      rec_new.action = OopAction_DELETE;
      rec_new.pathlen = (uint32_t)strlen(newpath);
      WalFile_Append(&GlobalWal, newpath, &rec_new);
      int srcFd = gnu_open(newpath, O_RDONLY | O_CLOEXEC, 0600);
      if (srcFd >= 0) {
         char buffer[64];
         make_vault_path(buffer, stat_buf.st_ino);
         if (1) {
            int destFd = gnu_open(buffer, O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
            if (destFd >= 0) {
               int cloneRes = -1;
#ifdef FICLONE
               cloneRes = ioctl(destFd, FICLONE, srcFd);
#endif
               if (cloneRes != 0) {
                  sendfile(destFd, srcFd, NULL, stat_buf.st_size);
               }
               close(destFd);
            }
         }
         close(srcFd);
      }
   }
   OopWalRecord rec_rename;
   rec_rename.magic = RECORD_MAGIC;
   rec_rename.action = OopAction_RENAME;
   rec_rename.filesize = 0;
   rec_rename.timestamp = (uint64_t)time(NULL);
   size_t old_len = strlen(oldpath);
   size_t new_len = strlen(newpath);
   rec_rename.pathlen = (uint32_t)(old_len + 1 + new_len + 1);
   char packed[4096];
   if (rec_rename.pathlen <= 4096) {
      memcpy(packed, oldpath, old_len + 1);
      memcpy(packed + old_len + 1, newpath, new_len + 1);
      WalFile_Append(&GlobalWal, packed, &rec_rename);
   }
   return gnu_rename(oldpath, newpath);
}

int renameat(int olddirfd, const char* oldpath, int newdirfd, const char* newpath) {
   if (gnu_renameat == NULL) {
      gnu_renameat = (gnu_renameat_t*)dlsym(RTLD_NEXT, "renameat");
   }
   if (gnu_openat == NULL) {
      gnu_openat = (gnu_openat_t*)dlsym(RTLD_NEXT, "openat");
   }
   if (gnu_open == NULL) {
      gnu_open = (gnu_open_t*)dlsym(RTLD_NEXT, "open");
   }
   if (strncmp(oldpath, "/tmp/oopsie/", 12) == 0 || strncmp(newpath, "/tmp/oopsie/", 12) == 0) {
      return gnu_renameat(olddirfd, oldpath, newdirfd, newpath);
   }
   struct stat stat_buf;
   if (fstatat(newdirfd, newpath, &stat_buf, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(stat_buf.st_mode)) {
      OopWalRecord rec_new;
      rec_new.magic = RECORD_MAGIC;
      rec_new.filesize = (uint64_t)stat_buf.st_size;
      rec_new.timestamp = (uint64_t)time(NULL);
      rec_new.action = OopAction_DELETE;
      rec_new.pathlen = (uint32_t)strlen(newpath);
      WalFile_Append(&GlobalWal, newpath, &rec_new);
      int srcFd = gnu_openat(newdirfd, newpath, O_RDONLY | O_CLOEXEC, 0600);
      if (srcFd >= 0) {
         char buffer[64];
         make_vault_path(buffer, stat_buf.st_ino);
         if (1) {
            int destFd = gnu_open(buffer, O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
            if (destFd >= 0) {
               int cloneRes = -1;
#ifdef FICLONE
               cloneRes = ioctl(destFd, FICLONE, srcFd);
#endif
               if (cloneRes != 0) {
                  sendfile(destFd, srcFd, NULL, stat_buf.st_size);
               }
               close(destFd);
            }
         }
         close(srcFd);
      }
   }
   OopWalRecord rec_rename;
   rec_rename.magic = RECORD_MAGIC;
   rec_rename.action = OopAction_RENAME;
   rec_rename.filesize = 0;
   rec_rename.timestamp = (uint64_t)time(NULL);
   size_t old_len = strlen(oldpath);
   size_t new_len = strlen(newpath);
   rec_rename.pathlen = (uint32_t)(old_len + 1 + new_len + 1);
   char packed[4096];
   if (rec_rename.pathlen <= 4096) {
      memcpy(packed, oldpath, old_len + 1);
      memcpy(packed + old_len + 1, newpath, new_len + 1);
      WalFile_Append(&GlobalWal, packed, &rec_rename);
   }
   return gnu_renameat(olddirfd, oldpath, newdirfd, newpath);
}

int renameat2(int olddirfd, const char* oldpath, int newdirfd, const char* newpath, unsigned int flags) {
   if (gnu_renameat2 == NULL) {
      gnu_renameat2 = (gnu_renameat2_t*)dlsym(RTLD_NEXT, "renameat2");
   }
   if (gnu_openat == NULL) {
      gnu_openat = (gnu_openat_t*)dlsym(RTLD_NEXT, "openat");
   }
   if (gnu_open == NULL) {
      gnu_open = (gnu_open_t*)dlsym(RTLD_NEXT, "open");
   }
   if (strncmp(oldpath, "/tmp/oopsie/", 12) == 0 || strncmp(newpath, "/tmp/oopsie/", 12) == 0) {
      return gnu_renameat2(olddirfd, oldpath, newdirfd, newpath, flags);
   }
   struct stat stat_buf;
   if (fstatat(newdirfd, newpath, &stat_buf, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(stat_buf.st_mode)) {
      OopWalRecord rec_new;
      rec_new.magic = RECORD_MAGIC;
      rec_new.filesize = (uint64_t)stat_buf.st_size;
      rec_new.timestamp = (uint64_t)time(NULL);
      rec_new.action = OopAction_DELETE;
      rec_new.pathlen = (uint32_t)strlen(newpath);
      WalFile_Append(&GlobalWal, newpath, &rec_new);
      int srcFd = gnu_openat(newdirfd, newpath, O_RDONLY | O_CLOEXEC, 0600);
      if (srcFd >= 0) {
         char buffer[64];
         make_vault_path(buffer, stat_buf.st_ino);
         if (1) {
            int destFd = gnu_open(buffer, O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
            if (destFd >= 0) {
               int cloneRes = -1;
#ifdef FICLONE
               cloneRes = ioctl(destFd, FICLONE, srcFd);
#endif
               if (cloneRes != 0) {
                  sendfile(destFd, srcFd, NULL, stat_buf.st_size);
               }
               close(destFd);
            }
         }
         close(srcFd);
      }
   }
   OopWalRecord rec_rename;
   rec_rename.magic = RECORD_MAGIC;
   rec_rename.action = OopAction_RENAME;
   rec_rename.filesize = 0;
   rec_rename.timestamp = (uint64_t)time(NULL);
   size_t old_len = strlen(oldpath);
   size_t new_len = strlen(newpath);
   rec_rename.pathlen = (uint32_t)(old_len + 1 + new_len + 1);
   char packed[4096];
   if (rec_rename.pathlen <= 4096) {
      memcpy(packed, oldpath, old_len + 1);
      memcpy(packed + old_len + 1, newpath, new_len + 1);
      WalFile_Append(&GlobalWal, packed, &rec_rename);
   }
   return gnu_renameat2(olddirfd, oldpath, newdirfd, newpath, flags);
}
