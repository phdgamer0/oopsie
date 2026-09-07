#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <liburing.h>
#include <liburing/io_uring.h>
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

OopWalContext GlobalWal;
struct io_uring ring;

__attribute__((constructor)) void oopsie_init() {
   WalFile_Open(&GlobalWal, WAL_PATH);
   struct io_uring_params params;
   memset((void*)&params, (int)0, sizeof(params));
   params.flags = IORING_SETUP_SQPOLL;
   params.sq_thread_idle = (unsigned int)2000;
   io_uring_queue_init_params((unsigned int)1024, &ring, &params);
   char paths[VAULT_PATH_LEN];
   memcpy(paths, VAULT_PATH, VAULT_PATH_LEN);
   uint16_t i = 0;
   while (i < (uint16_t)256) { // hoping the almighty compiler parallelize this loop
      paths[VAULT_PATH_LEN - 2] = (char)((i % (uint16_t)10) + '0');
      paths[VAULT_PATH_LEN - 3] = (char)(((i / (uint16_t)10) % (uint16_t)10) + '0');
      paths[VAULT_PATH_LEN - 4] = (char)(i / (uint16_t)100 + '0');
      (void)mkdir(paths, 0700);
      i++;
   }
}
__attribute__((destructor)) void oopsie_cleanup() {
   io_uring_queue_exit(&ring);
   WalFile_Close(&GlobalWal);
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
   if (strncmp(path, "/tmp/oopsie/", 12) == 0) {
      return gnu_unlink(path);
   }
   if (!WalFile_IsOpen(&GlobalWal)) {
      return gnu_unlink(path);
   }
   OopWalRecord rec;
   struct stat stat_buf;
   if (lstat(path, &stat_buf) == 0 && S_ISREG(stat_buf.st_mode)) {
      rec.magic = RECORD_MAGIC;
      rec.filesize = (uint64_t)stat_buf.st_size;
      rec.timestamp = (uint64_t)time(NULL);
      rec.action = OopAction_DELETE;
      rec.pathlen = (uint32_t)strlen(path);
      rec.inode = (uint64_t)stat_buf.st_ino;
      WalFile_Append(&GlobalWal, path, &rec);
      char buffer[BUFFER_SZ];
      make_vault_path(buffer, stat_buf.st_ino);
      struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
      io_uring_prep_link(sqe, path, buffer, 0);
      sqe->flags |= IOSQE_IO_LINK;
      struct io_uring_sqe* sqe2 = io_uring_get_sqe(&ring);
      io_uring_prep_unlink(sqe2, path, 0);
      io_uring_submit(&ring);
      struct io_uring_cqe* cqe = NULL;
      struct io_uring_cqe* cqe2 = NULL;
      while (io_uring_peek_cqe(&ring, &cqe) != (int)0) {
         __builtin_ia32_pause();
      }
      int link_res = cqe->res;
      io_uring_cqe_seen(&ring, cqe);
      while (io_uring_peek_cqe(&ring, &cqe2) != (int)0) {
         __builtin_ia32_pause();
      }
      int unlink_res = cqe2->res;
      io_uring_cqe_seen(&ring, cqe2);
      if (link_res >= (int)0) {
         return (unlink_res >= (int)0) ? (int)0 : (int)-1;
      }
      int srcFd = gnu_open(path, O_RDONLY | O_CLOEXEC, 0600);
      if (srcFd < 0)
         return gnu_unlink(path);
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
      close(srcFd);
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
   if (strncmp(path, "/tmp/oopsie/", 12) == 0) {
      return gnu_unlinkat(dirfd, path, flags);
   }
   if (!WalFile_IsOpen(&GlobalWal)) {
      return gnu_unlinkat(dirfd, path, flags);
   }
   OopWalRecord rec;
   struct stat stat_buf;
   if (fstatat(dirfd, path, &stat_buf, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(stat_buf.st_mode)) {
      rec.magic = RECORD_MAGIC;
      rec.filesize = (uint64_t)stat_buf.st_size;
      rec.timestamp = (uint64_t)time(NULL);
      rec.action = OopAction_DELETE;
      rec.pathlen = (uint32_t)strlen(path);
      rec.inode = (uint64_t)stat_buf.st_ino;
      WalFile_Append(&GlobalWal, path, &rec);
      char buffer[BUFFER_SZ];
      make_vault_path(buffer, stat_buf.st_ino);
      struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
      io_uring_prep_linkat(sqe, dirfd, path, AT_FDCWD, buffer, (int)0);
      sqe->flags |= IOSQE_IO_LINK;
      struct io_uring_sqe* sqe2 = io_uring_get_sqe(&ring);
      io_uring_prep_unlinkat(sqe2, dirfd, path, flags);
      io_uring_submit(&ring);
      struct io_uring_cqe* cqe = NULL;
      struct io_uring_cqe* cqe2 = NULL;
      while (io_uring_peek_cqe(&ring, &cqe) != 0) {
         __builtin_ia32_pause();
      }
      int link_res = cqe->res;
      io_uring_cqe_seen(&ring, cqe);
      while (io_uring_peek_cqe(&ring, &cqe2) != 0) {
         __builtin_ia32_pause();
      }
      int unlink_res = cqe2->res;
      io_uring_cqe_seen(&ring, cqe2);
      if (link_res >= (int)0) {
         return (unlink_res >= (int)0) ? (int)0 : (int)-1;
      }
      int cloneRes = (int)-1;
      int srcFd = gnu_openat(dirfd, path, O_RDONLY | O_CLOEXEC, 0600);
      if (srcFd < 0) {
         return gnu_unlinkat(dirfd, path, flags);
      }
      int destFd = gnu_open(buffer, O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
      if (destFd < (int)0) {
         close(srcFd);
         return gnu_unlinkat(dirfd, path, flags);
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
   if (!WalFile_IsOpen(&GlobalWal)) {
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
      rec.inode = (uint64_t)stat_.st_ino;
      WalFile_Append(&GlobalWal, pathname, &rec);
      char buffer[BUFFER_SZ];
      make_vault_path(buffer, stat_.st_ino);
      if (flags & O_TRUNC) {
         if (gnu_rename == NULL) gnu_rename = (gnu_rename_t*)dlsym(RTLD_NEXT, "rename");
         if (gnu_rename(pathname, buffer) == 0) {
            return gnu_open(pathname, flags | O_CREAT, stat_.st_mode);
         }
      }
      int cloneRes = (int)-1;
      int srcFd = gnu_open(pathname, O_RDONLY | O_CLOEXEC, 0600);
      if (srcFd < 0) {
         return gnu_open(pathname, flags, mode);
      }
      int destFd = gnu_open(buffer, O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
      if (destFd < 0) {
         close(srcFd);
         return gnu_open(pathname, flags, mode);
      }
#ifdef FICLONE
      cloneRes = ioctl(destFd, FICLONE, srcFd);
#endif
      if (cloneRes != (int)0) {
         sendfile(destFd, srcFd, NULL, stat_.st_size);
      }
      close(srcFd);
      close(destFd);
   }
   else if (!exists && (flags & O_CREAT)) {
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
   if (!WalFile_IsOpen(&GlobalWal)) {
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
      rec.inode = (uint64_t)stat_.st_ino;
      WalFile_Append(&GlobalWal, pathname, &rec);
      char buffer[BUFFER_SZ];
      make_vault_path(buffer, stat_.st_ino);
      if (flags & O_TRUNC) {
         if (gnu_renameat == NULL) gnu_renameat = (gnu_renameat_t*)dlsym(RTLD_NEXT, "renameat");
         if (gnu_renameat(dirfd, pathname, AT_FDCWD, buffer) == 0) {
            return gnu_openat(dirfd, pathname, flags | O_CREAT, stat_.st_mode);
         }
      }
      int cloneRes = (int)-1;
      int srcFd = gnu_openat(dirfd, pathname, O_RDONLY | O_CLOEXEC, 0600);
      if (srcFd < 0) {
         return gnu_openat(dirfd, pathname, flags, mode);
      }
      int destFd = gnu_open(buffer, O_WRONLY | O_CREAT | O_CLOEXEC, 0600);
      if (destFd < 0) {
         close(srcFd);
         return gnu_openat(dirfd, pathname, flags, mode);
      }
#ifdef FICLONE
      cloneRes = ioctl(destFd, FICLONE, srcFd);
#endif
      if (cloneRes != (int)0) {
         sendfile(destFd, srcFd, NULL, stat_.st_size);
      }
      close(srcFd);
      close(destFd);
   }
   else if (!exists && (flags & O_CREAT)) {
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
   if (!WalFile_IsOpen(&GlobalWal)) {
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
      rec_new.inode = (uint64_t)stat_buf.st_ino;
      WalFile_Append(&GlobalWal, newpath, &rec_new);
      char buffer[BUFFER_SZ];
      make_vault_path(buffer, stat_buf.st_ino);
      struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
      io_uring_prep_link(sqe, newpath, buffer, 0);
      sqe->flags |= IOSQE_IO_LINK;
      struct io_uring_sqe* sqe2 = io_uring_get_sqe(&ring);
      io_uring_prep_rename(sqe2, oldpath, newpath);
      io_uring_submit(&ring);
      struct io_uring_cqe* cqe = NULL;
      struct io_uring_cqe* cqe2 = NULL;
      while (io_uring_peek_cqe(&ring, &cqe) != 0) {
         __builtin_ia32_pause();
      }
      int link_res = cqe->res;
      io_uring_cqe_seen(&ring, cqe);
      while (io_uring_peek_cqe(&ring, &cqe2) != 0) {
         __builtin_ia32_pause();
      }
      int rename_res = cqe2->res;
      io_uring_cqe_seen(&ring, cqe2);
      if (link_res >= 0) {
         return (rename_res >= 0) ? 0 : -1;
      }
      int srcFd = gnu_open(newpath, O_RDONLY | O_CLOEXEC, 0600);
      if (srcFd >= 0) {
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
         close(srcFd);
      }
   }
   OopWalRecord rec_rename;
   rec_rename.magic = RECORD_MAGIC;
   rec_rename.action = OopAction_RENAME;
   rec_rename.filesize = 0;
   rec_rename.timestamp = (uint64_t)time(NULL);
   rec_rename.inode = (uint64_t)stat_buf.st_ino;
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
   if (!WalFile_IsOpen(&GlobalWal)) {
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
      rec_new.inode = (uint64_t)stat_buf.st_ino;
      WalFile_Append(&GlobalWal, newpath, &rec_new);
      char buffer[BUFFER_SZ];
      make_vault_path(buffer, stat_buf.st_ino);
      struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
      io_uring_prep_linkat(sqe, newdirfd, newpath, AT_FDCWD, buffer, (int)0);
      sqe->flags |= IOSQE_IO_LINK;
      struct io_uring_sqe* sqe2 = io_uring_get_sqe(&ring);
      io_uring_prep_renameat(sqe2, olddirfd, oldpath, newdirfd, newpath, 0);
      io_uring_submit(&ring);
      struct io_uring_cqe* cqe = NULL;
      struct io_uring_cqe* cqe2 = NULL;
      while (io_uring_peek_cqe(&ring, &cqe) != 0) {
         __builtin_ia32_pause();
      }
      int link_res = cqe->res;
      io_uring_cqe_seen(&ring, cqe);
      while (io_uring_peek_cqe(&ring, &cqe2) != 0) {
         __builtin_ia32_pause();
      }
      int rename_res = cqe2->res;
      io_uring_cqe_seen(&ring, cqe2);
      if (link_res >= 0) {
         return (rename_res >= 0) ? 0 : -1;
      }
      int srcFd = gnu_openat(newdirfd, newpath, O_RDONLY | O_CLOEXEC, 0600);
      if (srcFd >= 0) {
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
   rec_rename.inode = (uint64_t)stat_buf.st_ino;
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
   if (!WalFile_IsOpen(&GlobalWal)) {
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
      char buffer[BUFFER_SZ];
      make_vault_path(buffer, stat_buf.st_ino);
      struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
      io_uring_prep_linkat(sqe, newdirfd, newpath, AT_FDCWD, buffer, (int)0);
      sqe->flags |= IOSQE_IO_LINK;
      struct io_uring_sqe* sqe2 = io_uring_get_sqe(&ring);
      io_uring_prep_renameat(sqe2, olddirfd, oldpath, newdirfd, newpath, flags);
      io_uring_submit(&ring);
      struct io_uring_cqe* cqe = NULL;
      struct io_uring_cqe* cqe2 = NULL;
      while (io_uring_peek_cqe(&ring, &cqe) != 0) {
         __builtin_ia32_pause();
      }
      int link_res = cqe->res;
      io_uring_cqe_seen(&ring, cqe);
      while (io_uring_peek_cqe(&ring, &cqe2) != 0) {
         __builtin_ia32_pause();
      }
      int rename_res = cqe2->res;
      io_uring_cqe_seen(&ring, cqe2);
      if (link_res >= 0) {
         return (rename_res >= 0) ? 0 : -1;
      }
      int srcFd = gnu_openat(newdirfd, newpath, O_RDONLY | O_CLOEXEC, 0600);
      if (srcFd >= 0) {
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
