#define _GNU_SOURCE
#include <dlfcn.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <oopsie_wal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define SHIM_SHOULD_MONITOR() (WalFile_IsOpen(&GlobalWal) && __atomic_load_n(&((WalHeader*)GlobalWal.map)->is_monitoring, __ATOMIC_RELAXED) == 1)
#define PATH_BUFF_SIZE (size_t)4096

static inline bool is_ignored_path(const char* p) {
   if (!p) return true;
   if (p[0] == '\0') return true;
   if (strncmp(p, "/tmp/oopsie/", 12) == 0) return true;
   if (strncmp(p, "/dev/", 5) == 0) return true;
   if (strncmp(p, "/sys/", 5) == 0) return true;
   if (strncmp(p, "/proc/", 6) == 0) return true;
   return false;
}

static void resolve_absolute_path(int dirfd, const char* __restrict rel_path, char* __restrict abs_path, size_t max_len) {
   size_t rel_len = strlen(rel_path);
   if (rel_len + 1 >= max_len) {
      abs_path[0] = '\0';
      return;
   }
   if (rel_path[0] == '/') {
      memcpy(abs_path, rel_path, rel_len + 1);
      return;
   }
   size_t dir_len = 0;
   if (dirfd == AT_FDCWD) {
      if (getcwd(abs_path, max_len - rel_len - 1) == NULL) {
         abs_path[0] = '\0';
         return;
      }
      dir_len = strlen(abs_path);
   }
   else {
      char t_buff[32];
      snprintf(t_buff, sizeof(t_buff), "/proc/self/fd/%d", dirfd);
      ssize_t link_len = readlink(t_buff, abs_path, max_len - rel_len - 1);
      if (link_len < 0) {
         abs_path[0] = '\0';
         return;
      }
      dir_len = (size_t)link_len;
   }
   if (dir_len + rel_len + 2 > max_len) {
      abs_path[0] = '\0';
      return;
   }
   abs_path[dir_len] = '/';
   memcpy((void*)((uintptr_t)abs_path + (uintptr_t)dir_len + (uintptr_t)1), rel_path, rel_len + 1);
}

static bool resolve_fd_path(int fd, char* __restrict abs_path, size_t max_len) {
   if (max_len == 0) {
      return false;
   }
   char t_buff[32];
   snprintf(t_buff, sizeof(t_buff), "/proc/self/fd/%d", fd);
   ssize_t link_len = readlink(t_buff, abs_path, max_len - 1);
   if (link_len < 0) {
      abs_path[0] = '\0';
      return false;
   }
   abs_path[link_len] = '\0';
   size_t len = (size_t)link_len;
   const char* deleted_suffix = " (deleted)";
   size_t suffix_len = strlen(deleted_suffix);
   if (len > suffix_len && strcmp((const char*)((uintptr_t)abs_path + (uintptr_t)len - suffix_len), deleted_suffix) == 0) {
      abs_path[len - suffix_len] = '\0';
   }
   return true;
}

OopWalContext GlobalWal;

__attribute__((constructor)) void oopsie_init() {
   mkdir("/tmp/oopsie", 0700);
   WalFile_Open(&GlobalWal, WAL_PATH);
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
   WalFile_Close(&GlobalWal);
}

typedef int(gnu_unlink_t)(const char* path);
typedef int(gnu_unlinkat_t)(int dirfd, const char* path, int flags);
typedef int(gnu_open_t)(const char* pathname, int flags, ...);
typedef int(gnu_openat_t)(int dirfd, const char* pathname, int flags, ...);
typedef int(gnu_rename_t)(const char* oldpath, const char* newpath);
typedef int(gnu_renameat_t)(int olddirfd, const char* oldpath, int newdirfd, const char* newpath);
typedef int(gnu_renameat2_t)(int olddirfd, const char* oldpath, int newdirfd, const char* newpath, unsigned int flags);
typedef int(gnu_ftruncate_t)(int fd, off_t length);
typedef int(gnu_truncate_t)(const char* path, off_t length);
typedef FILE*(gnu_fopen_t)(const char* path, const char* mode);
typedef FILE*(gnu_freopen_t)(const char* path, const char* mode, FILE* stream);
typedef int(gnu_mkstemp_t)(char* template);
typedef int(gnu_mkstemps_t)(char* template, int suffixlen);
typedef char*(gnu_mkdtemp_t)(char* template);
typedef int(gnu_mkdir_t)(const char* path, mode_t mode);
typedef int(gnu_mkostemp_t)(char* template, int flags);
typedef int(gnu_remove_t)(const char* path);
typedef int(gnu_link_t)(const char* oldpath, const char* newpath);
typedef int(gnu_linkat_t)(int olddirfd, const char* oldpath, int newdirfd, const char* newpath, int flags);
typedef int(gnu_symlink_t)(const char* target, const char* linkpath);
typedef int(gnu_symlinkat_t)(const char* target, int newdirfd, const char* linkpath);
typedef int(gnu_chmod_t)(const char* path, mode_t mode);
typedef int(gnu_fchmod_t)(int fd, mode_t mode);
typedef int(gnu_fchmodat_t)(int dirfd, const char* path, mode_t mode, int flags);
typedef int(gnu_chown_t)(const char* path, uid_t owner, gid_t group);
typedef int(gnu_lchown_t)(const char* path, uid_t owner, gid_t group);
typedef int(gnu_fchown_t)(int fd, uid_t owner, gid_t group);
typedef int(gnu_fchownat_t)(int dirfd, const char* path, uid_t owner, gid_t group, int flags);
typedef int(gnu_utimensat_t)(int dirfd, const char* path, const struct timespec times[2], int flags);
typedef int(gnu_utimes_t)(const char* path, const struct timeval times[2]);
typedef int(gnu_utimens_t)(const char* path, const struct timespec times[2]);
typedef int(gnu_futimens_t)(int fd, const struct timespec times[2]);
typedef int(gnu_setxattr_t)(const char* path, const char* name, const void* value, size_t size, int flags);
typedef int(gnu_lsetxattr_t)(const char* path, const char* name, const void* value, size_t size, int flags);
typedef int(gnu_fsetxattr_t)(int fd, const char* name, const void* value, size_t size, int flags);
typedef int(gnu_removexattr_t)(const char* path, const char* name);
typedef int(gnu_lremovexattr_t)(const char* path, const char* name);
typedef int(gnu_fremovexattr_t)(int fd, const char* name);
typedef ssize_t(gnu_flistxattr_t)(int fd, char* list, size_t size);
typedef ssize_t(gnu_fgetxattr_t)(int fd, const char* name, void* value, size_t size);

gnu_rename_t* gnu_rename = NULL;
gnu_renameat_t* gnu_renameat = NULL;
gnu_renameat2_t* gnu_renameat2 = NULL;
gnu_unlink_t* gnu_unlink = NULL;
gnu_unlinkat_t* gnu_unlinkat = NULL;
gnu_open_t* gnu_open = NULL;
gnu_openat_t* gnu_openat = NULL;
gnu_ftruncate_t* gnu_ftruncate = NULL;
gnu_truncate_t* gnu_truncate = NULL;
gnu_flistxattr_t* gnu_flistxattr = NULL;
gnu_fgetxattr_t* gnu_fgetxattr = NULL;

static inline bool vault_copy(int dirfd, const char* __restrict rel_path, const struct stat* __restrict stat_buf) {
   char buffer[BUFFER_SZ];
   make_vault_path(buffer, (unsigned long)stat_buf->st_ino);
   int srcFd = (dirfd == AT_FDCWD) ? gnu_open(rel_path, O_RDONLY | O_CLOEXEC, 0600) : gnu_openat(dirfd, rel_path, O_RDONLY | O_CLOEXEC, 0600);
   if (srcFd < 0)
      return false;
   int destFd = gnu_open(buffer, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
   if (destFd < 0) {
      close(srcFd);
      return false;
   }
   int cloneRes = -1;
#ifdef FICLONE
   cloneRes = ioctl(destFd, FICLONE, srcFd);
#endif
   if (cloneRes != 0) {
      sendfile(destFd, srcFd, NULL, stat_buf->st_size);
   }
   close(srcFd);
   close(destFd);
   return true;
}

static inline void record_modify(const char* __restrict abs_path, const struct stat* __restrict stat_buf) {
   OopWalRecord rec;
   rec.magic = RECORD_MAGIC;
   rec.action = OopAction_MODIFY;
   rec.filesize = (uint64_t)stat_buf->st_size;
   rec.timestamp = (uint64_t)time(NULL);
   rec.pathlen = (uint32_t)strlen(abs_path);
   rec.inode = (uint64_t)stat_buf->st_ino;
   WalFile_Append(&GlobalWal, abs_path, &rec);
}

static inline void record_delete(const char* __restrict abs_path, const struct stat* __restrict stat_buf) {
   OopWalRecord rec;
   rec.magic = RECORD_MAGIC;
   rec.action = OopAction_DELETE;
   rec.filesize = (uint64_t)stat_buf->st_size;
   rec.timestamp = (uint64_t)time(NULL);
   rec.pathlen = (uint32_t)strlen(abs_path);
   rec.inode = (uint64_t)stat_buf->st_ino;
   WalFile_Append(&GlobalWal, abs_path, &rec);
}

static inline void record_create(const char* __restrict abs_path) {
   OopWalRecord rec;
   rec.magic = RECORD_MAGIC;
   rec.action = OopAction_CREATE;
   rec.filesize = (uint64_t)0;
   rec.timestamp = (uint64_t)time(NULL);
   rec.pathlen = (uint32_t)strlen(abs_path);
   rec.inode = (uint64_t)0;
   WalFile_Append(&GlobalWal, abs_path, &rec);
}

#define GUARD_SLOTS (size_t)65536
#define GUARD_EMPTY ((unsigned char)0)
#define GUARD_BUSY ((unsigned char)1)
#define GUARD_DONE ((unsigned char)2)
#define GUARD_PROBES (size_t)16

static unsigned long Guard_Keys[GUARD_SLOTS];
static unsigned long Guard_Inodes[GUARD_SLOTS];
static unsigned char Guard_States[GUARD_SLOTS];

typedef enum {
   GUARD_CLAIMED,
   GUARD_ALREADY,
   GUARD_FAILED,
} GuardResult_t;

typedef enum {
   CAPTURE_FAILED,
   CAPTURE_SKIPPED,
   CAPTURE_COPIED,
   CAPTURE_MOVED,
} CaptureResult_t;

static inline size_t guard_slot_of(unsigned long hash) {
   return (size_t)((hash * 2654435761UL) & (GUARD_SLOTS - 1));
}

static inline unsigned long guard_path_hash(const char* p) {
   unsigned long h = 1469598103934665603UL;
   for (size_t i = 0; p != NULL && p[i] != '\0'; i++) {
      h ^= (unsigned long)(unsigned char)p[i];
      h *= 1099511628211UL;
   }
   return h;
}

static size_t guard_lookup(unsigned long key) {
   size_t base = guard_slot_of(key);
   for (size_t i = 0; i < GUARD_PROBES; i++) {
      size_t s = (base + i) & (GUARD_SLOTS - 1);
      if (__atomic_load_n(&Guard_States[s], __ATOMIC_ACQUIRE) != GUARD_EMPTY && __atomic_load_n(&Guard_Keys[s], __ATOMIC_ACQUIRE) == key) {
         return s;
      }
   }
   return (size_t)-1;
}

static GuardResult_t guard_claim(const char* abs_path, unsigned long ino) {
   unsigned long key = guard_path_hash(abs_path);
   size_t found = guard_lookup(key);
   if (found == (size_t)-1) {
      size_t base = guard_slot_of(key);
      for (size_t i = 0; i < GUARD_PROBES; i++) {
         size_t p = (base + i) & (GUARD_SLOTS - 1);
         if (__atomic_load_n(&Guard_States[p], __ATOMIC_ACQUIRE) != GUARD_EMPTY && __atomic_load_n(&Guard_Inodes[p], __ATOMIC_ACQUIRE) == ino) {
            found = p;
            break;
         }
      }
   }
   if (found != (size_t)-1) {
      return (__atomic_load_n(&Guard_States[found], __ATOMIC_ACQUIRE) == GUARD_DONE) ? GUARD_ALREADY : GUARD_FAILED;
   }
   size_t base = guard_slot_of(key);
   for (size_t i = 0; i < GUARD_PROBES; i++) {
      size_t p = (base + i) & (GUARD_SLOTS - 1);
      if (__atomic_load_n(&Guard_States[p], __ATOMIC_ACQUIRE) == GUARD_EMPTY) {
         __atomic_store_n(&Guard_Keys[p], key, __ATOMIC_RELAXED);
         __atomic_store_n(&Guard_Inodes[p], ino, __ATOMIC_RELAXED);
         unsigned char expect = GUARD_EMPTY;
         if (__atomic_compare_exchange_n(&Guard_States[p], &expect, GUARD_BUSY, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            return GUARD_CLAIMED;
         }
      }
   }
   return GUARD_FAILED;
}

static void guard_finish(const char* abs_path, bool commit) {
   size_t s = guard_lookup(guard_path_hash(abs_path));
   if (s == (size_t)-1) {
      return;
   }
   __atomic_store_n(&Guard_States[s], (unsigned char)(commit ? GUARD_DONE : GUARD_EMPTY), __ATOMIC_RELEASE);
}

static bool collect_xattrs(int fd, char** out_buf, size_t* out_len) {
   *out_buf = NULL;
   *out_len = (size_t)0;
   if (gnu_flistxattr == NULL) {
      gnu_flistxattr = (gnu_flistxattr_t*)dlsym(RTLD_NEXT, "flistxattr");
   }
   if (gnu_fgetxattr == NULL) {
      gnu_fgetxattr = (gnu_fgetxattr_t*)dlsym(RTLD_NEXT, "fgetxattr");
   }
   if (gnu_flistxattr == NULL || gnu_fgetxattr == NULL) {
      return true;
   }
   ssize_t need = gnu_flistxattr(fd, NULL, 0);
   if (need <= 0) {
      return true;
   }
   char* names = (char*)malloc((size_t)need);
   if (names == NULL) {
      return false;
   }
   ssize_t got = gnu_flistxattr(fd, names, (size_t)need);
   if (got <= 0) {
      free(names);
      return true;
   }
   size_t total = (size_t)0;
   size_t offset = 0;
   while (offset < (size_t)got) {
      const char* name = names + offset;
      size_t name_len = strlen(name);
      offset += name_len + 1;
      ssize_t vneed = gnu_fgetxattr(fd, name, NULL, 0);
      if (vneed < 0) {
         continue;
      }
      total += 6 + name_len + (size_t)vneed;
   }
   char* blob = (char*)malloc(total > 0 ? total : 1);
   if (blob == NULL) {
      free(names);
      return false;
   }
   size_t w = 0;
   offset = 0;
   while (offset < (size_t)got) {
      const char* name = names + offset;
      size_t name_len = strlen(name);
      offset += name_len + 1;
      ssize_t vneed = gnu_fgetxattr(fd, name, NULL, 0);
      if (vneed < 0) {
         continue;
      }
      uint16_t rec_name_len = (uint16_t)name_len;
      uint32_t rec_value_len = (uint32_t)vneed;
      memcpy(blob + w, &rec_name_len, sizeof(rec_name_len));
      w += sizeof(rec_name_len);
      memcpy(blob + w, &rec_value_len, sizeof(rec_value_len));
      w += sizeof(rec_value_len);
      memcpy(blob + w, name, name_len);
      w += name_len;
      if (vneed > 0) {
         ssize_t vgot = gnu_fgetxattr(fd, name, blob + w, (size_t)vneed);
         if (vgot > 0) {
            w += (size_t)vgot;
         }
         else {
            rec_value_len = 0;
            memcpy(blob + w - sizeof(rec_value_len), &rec_value_len, sizeof(rec_value_len));
         }
      }
   }
   free(names);
   *out_buf = blob;
   *out_len = w;
   return true;
}

static bool write_meta_blob(int dirfd, const char* __restrict rel_path, const struct stat* __restrict stat_buf) {
   char meta_path[BUFFER_SZ];
   make_vault_meta_path(meta_path, (unsigned long)stat_buf->st_ino);
   int srcFd = (dirfd == AT_FDCWD) ? gnu_open(rel_path, O_RDONLY | O_CLOEXEC, 0600) : gnu_openat(dirfd, rel_path, O_RDONLY | O_CLOEXEC, 0600);
   if (srcFd < 0) {
      return false;
   }
   char* blob = NULL;
   size_t blob_len = (size_t)0;
   if (!collect_xattrs(srcFd, &blob, &blob_len)) {
      close(srcFd);
      return false;
   }
   OopMetaBlob meta;
   meta.magic = METABLOB_MAGIC;
   meta.xattr_len = (uint32_t)blob_len;
   meta.mode = (uint64_t)stat_buf->st_mode;
   meta.uid = (uint64_t)stat_buf->st_uid;
   meta.gid = (uint64_t)stat_buf->st_gid;
   meta.mtime_sec = (uint64_t)stat_buf->st_mtim.tv_sec;
   meta.mtime_nsec = (uint64_t)stat_buf->st_mtim.tv_nsec;
   meta.atime_sec = (uint64_t)stat_buf->st_atim.tv_sec;
   meta.atime_nsec = (uint64_t)stat_buf->st_atim.tv_nsec;
   int destFd = gnu_open(meta_path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
   if (destFd >= 0) {
      if (write(destFd, &meta, sizeof(meta)) != (ssize_t)sizeof(meta)) {}
      if (blob_len > 0) {
         if (write(destFd, blob, blob_len) != (ssize_t)blob_len) {}
      }
      close(destFd);
   }
   free(blob);
   close(srcFd);
   return destFd >= 0;
}

static CaptureResult_t vault_ensure(int dirfd, const char* __restrict rel_path, const char* __restrict abs_path, const struct stat* __restrict stat_buf, bool trunc_move) {
   GuardResult_t claim = guard_claim(abs_path, (unsigned long)stat_buf->st_ino);
   if (claim == GUARD_ALREADY) {
      return CAPTURE_SKIPPED;
   }
   bool ok = write_meta_blob(dirfd, rel_path, stat_buf);
   char buffer[BUFFER_SZ];
   make_vault_path(buffer, (unsigned long)stat_buf->st_ino);
   if (trunc_move) {
      if (dirfd == AT_FDCWD) {
         if (gnu_rename(rel_path, buffer) == 0) {
            guard_finish(abs_path, true);
            return CAPTURE_MOVED;
         }
      }
      else {
         if (gnu_renameat(dirfd, rel_path, AT_FDCWD, buffer) == 0) {
            guard_finish(abs_path, true);
            return CAPTURE_MOVED;
         }
      }
   }
   if (vault_copy(dirfd, rel_path, stat_buf)) {
      guard_finish(abs_path, true);
      return CAPTURE_COPIED;
   }
   guard_finish(abs_path, ok);
   return CAPTURE_FAILED;
}

static bool capture_modify_intent(int dirfd, const char* __restrict rel_path, const char* __restrict abs_path, const struct stat* __restrict stat_buf, bool trunc_move) {
   CaptureResult_t cap = vault_ensure(dirfd, rel_path, abs_path, stat_buf, trunc_move);
   if (cap == CAPTURE_MOVED || cap == CAPTURE_COPIED) {
      record_modify(abs_path, stat_buf);
      return true;
   }
   return false;
}

static bool note_metadata(int dirfd, const char* __restrict rel_path, const char* __restrict abs_path, const struct stat* __restrict stat_buf) {
   GuardResult_t claim = guard_claim(abs_path, (unsigned long)stat_buf->st_ino);
   if (claim == GUARD_ALREADY) {
      return false;
   }
   if (WalFile_HasPath(&GlobalWal, abs_path, strlen(abs_path))) {
      guard_finish(abs_path, false);
      return false;
   }
   if (!write_meta_blob(dirfd, rel_path, stat_buf)) {
      guard_finish(abs_path, false);
      return false;
   }
   record_modify(abs_path, stat_buf);
   guard_finish(abs_path, true);
   return true;
}

static inline void ensure_std_resolved(void) {
   if (gnu_open == NULL) {
      gnu_open = (gnu_open_t*)dlsym(RTLD_NEXT, "open");
   }
   if (gnu_openat == NULL) {
      gnu_openat = (gnu_openat_t*)dlsym(RTLD_NEXT, "openat");
   }
   if (gnu_rename == NULL) {
      gnu_rename = (gnu_rename_t*)dlsym(RTLD_NEXT, "rename");
   }
   if (gnu_renameat == NULL) {
      gnu_renameat = (gnu_renameat_t*)dlsym(RTLD_NEXT, "renameat");
   }
}

static inline void record_intent_open(int dirfd, const char* __restrict rel_path, const char* __restrict abs_path, int flags) {
   ensure_std_resolved();
   struct stat stat_;
   bool exists = (dirfd == AT_FDCWD) ? (lstat(rel_path, &stat_) == 0 && S_ISREG(stat_.st_mode)) : (fstatat(dirfd, rel_path, &stat_, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(stat_.st_mode));
   if (exists && ((flags & O_WRONLY) || (flags & O_RDWR) || (flags & O_TRUNC))) {
      CaptureResult_t cap = vault_ensure(dirfd, rel_path, abs_path, &stat_, (flags & O_TRUNC) != 0);
      if (cap == CAPTURE_MOVED || cap == CAPTURE_COPIED) {
         record_modify(abs_path, &stat_);
      }
      return;
   }
   if (!exists && (flags & O_CREAT)) {
      record_create(abs_path);
   }
}

static inline int stdio_mode_flags(const char* __restrict mode) {
   if (mode == NULL || mode[0] == '\0') {
      return -1;
   }
   int flags = O_RDONLY;
   switch (mode[0]) {
   case 'r':
      flags = O_RDONLY;
      break;
   case 'w':
      flags = O_WRONLY | O_CREAT | O_TRUNC;
      break;
   case 'a':
      flags = O_WRONLY | O_CREAT | O_APPEND;
      break;
   default:
      return -1;
   }
   for (const char* m = mode + 1; *m != '\0'; m++) {
      if (*m == '+') {
         flags = (flags & ~O_ACCMODE) | O_RDWR;
      }
   }
   if (strchr(mode, 'x') != NULL) {
      flags |= O_EXCL;
   }
   return flags;
}

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
   if (!SHIM_SHOULD_MONITOR()) {
      return gnu_unlink(path);
   }
   char abs_path[PATH_BUFF_SIZE];
   resolve_absolute_path(AT_FDCWD, path, abs_path, sizeof(abs_path));
   if (is_ignored_path(abs_path)) {
      return gnu_unlink(path);
   }
   struct stat stat_buf;
   if (lstat(path, &stat_buf) == 0 && S_ISREG(stat_buf.st_mode)) {
      ensure_std_resolved();
      (void)vault_ensure(AT_FDCWD, path, abs_path, &stat_buf, false);
      record_delete(abs_path, &stat_buf);
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
   if (!SHIM_SHOULD_MONITOR()) {
      return gnu_unlinkat(dirfd, path, flags);
   }
   char abs_path[PATH_BUFF_SIZE];
   resolve_absolute_path(dirfd, path, abs_path, sizeof(abs_path));
   if (is_ignored_path(abs_path)) {
      return gnu_unlinkat(dirfd, path, flags);
   }
   struct stat stat_buf;
   if (fstatat(dirfd, path, &stat_buf, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(stat_buf.st_mode)) {
      ensure_std_resolved();
      (void)vault_ensure(dirfd, path, abs_path, &stat_buf, false);
      record_delete(abs_path, &stat_buf);
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
   if (!SHIM_SHOULD_MONITOR()) {
      return gnu_open(pathname, flags, mode);
   }
   char abs_path[PATH_BUFF_SIZE];
   resolve_absolute_path(AT_FDCWD, pathname, abs_path, sizeof(abs_path));
   if (is_ignored_path(abs_path)) {
      return gnu_open(pathname, flags, mode);
   }
   struct stat stat_;
   int exists = (lstat(pathname, &stat_) == 0 && S_ISREG(stat_.st_mode));
   if (exists && ((flags & O_WRONLY) || (flags & O_RDWR) || (flags & O_TRUNC))) {
      ensure_std_resolved();
      CaptureResult_t cap = vault_ensure(AT_FDCWD, pathname, abs_path, &stat_, (flags & O_TRUNC) != 0);
      if (cap == CAPTURE_MOVED || cap == CAPTURE_COPIED) {
         record_modify(abs_path, &stat_);
      }
      if (cap == CAPTURE_MOVED) {
         return gnu_open(pathname, flags | O_CREAT, stat_.st_mode);
      }
   }
   else if (!exists && (flags & O_CREAT)) {
      record_create(abs_path);
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
   if (!SHIM_SHOULD_MONITOR()) {
      return gnu_openat(dirfd, pathname, flags, mode);
   }
   char abs_path[PATH_BUFF_SIZE];
   resolve_absolute_path(dirfd, pathname, abs_path, sizeof(abs_path));
   if (is_ignored_path(abs_path)) {
      return gnu_openat(dirfd, pathname, flags, mode);
   }
   struct stat stat_;
   int exists = (fstatat(dirfd, pathname, &stat_, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(stat_.st_mode));
   if (exists && ((flags & O_WRONLY) || (flags & O_RDWR) || (flags & O_TRUNC))) {
      ensure_std_resolved();
      CaptureResult_t cap = vault_ensure(dirfd, pathname, abs_path, &stat_, (flags & O_TRUNC) != 0);
      if (cap == CAPTURE_MOVED || cap == CAPTURE_COPIED) {
         record_modify(abs_path, &stat_);
      }
      if (cap == CAPTURE_MOVED) {
         return gnu_openat(dirfd, pathname, flags | O_CREAT, stat_.st_mode);
      }
   }
   else if (!exists && (flags & O_CREAT)) {
      record_create(abs_path);
   }
   return gnu_openat(dirfd, pathname, flags, mode);
}

int open64(const char* pathname, int flags, ...) {
   mode_t mode = (mode_t)0;
   if (flags & O_CREAT) {
      va_list args;
      va_start(args, flags);
      mode = va_arg(args, mode_t);
      va_end(args);
   }
   return open(pathname, flags, mode);
}

int openat64(int dirfd, const char* pathname, int flags, ...) {
   mode_t mode = (mode_t)0;
   if (flags & O_CREAT) {
      va_list args;
      va_start(args, flags);
      mode = va_arg(args, mode_t);
      va_end(args);
   }
   return openat(dirfd, pathname, flags, mode);
}

int creat(const char* pathname, mode_t mode) {
   return open(pathname, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

int creat64(const char* pathname, mode_t mode) {
   return creat(pathname, mode);
}

int rename(const char* oldpath, const char* newpath) {
   if (gnu_rename == NULL) {
      gnu_rename = (gnu_rename_t*)dlsym(RTLD_NEXT, "rename");
   }
   if (gnu_open == NULL) {
      gnu_open = (gnu_open_t*)dlsym(RTLD_NEXT, "open");
   }
   if (!SHIM_SHOULD_MONITOR()) {
      return gnu_rename(oldpath, newpath);
   }
   char abs_old[PATH_BUFF_SIZE];
   char abs_new[PATH_BUFF_SIZE];
   resolve_absolute_path(AT_FDCWD, oldpath, abs_old, sizeof(abs_old));
   resolve_absolute_path(AT_FDCWD, newpath, abs_new, sizeof(abs_new));
   if (is_ignored_path(abs_old) || is_ignored_path(abs_new)) {
      return gnu_rename(oldpath, newpath);
   }
   struct stat stat_buf;
   bool new_exists = (lstat(newpath, &stat_buf) == 0 && S_ISREG(stat_buf.st_mode));
   if (new_exists) {
      ensure_std_resolved();
      (void)vault_ensure(AT_FDCWD, newpath, abs_new, &stat_buf, false);
      record_delete(abs_new, &stat_buf);
   }
   OopWalRecord rec_rename;
   rec_rename.magic = RECORD_MAGIC;
   rec_rename.action = OopAction_RENAME;
   rec_rename.filesize = 0;
   rec_rename.timestamp = (uint64_t)time(NULL);
   rec_rename.inode = (uint64_t)(new_exists ? stat_buf.st_ino : 0);
   size_t old_len = strlen(abs_old);
   size_t new_len = strlen(abs_new);
   rec_rename.pathlen = (uint32_t)(old_len + 1 + new_len + 1);
   char packed[PATH_BUFF_SIZE * 2];
   if (rec_rename.pathlen <= sizeof(packed)) {
      memcpy(packed, abs_old, old_len + 1);
      memcpy((void*)((uintptr_t)packed + (uintptr_t)old_len + 1), abs_new, new_len + 1);
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
   if (!SHIM_SHOULD_MONITOR()) {
      return gnu_renameat(olddirfd, oldpath, newdirfd, newpath);
   }
   char abs_old[PATH_BUFF_SIZE];
   char abs_new[PATH_BUFF_SIZE];
   resolve_absolute_path(olddirfd, oldpath, abs_old, sizeof(abs_old));
   resolve_absolute_path(newdirfd, newpath, abs_new, sizeof(abs_new));
   if (is_ignored_path(abs_old) || is_ignored_path(abs_new)) {
      return gnu_renameat(olddirfd, oldpath, newdirfd, newpath);
   }
   struct stat stat_buf;
   bool new_exists = (fstatat(newdirfd, newpath, &stat_buf, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(stat_buf.st_mode));
   if (new_exists) {
      ensure_std_resolved();
      (void)vault_ensure(newdirfd, newpath, abs_new, &stat_buf, false);
      record_delete(abs_new, &stat_buf);
   }
   OopWalRecord rec_rename;
   rec_rename.magic = RECORD_MAGIC;
   rec_rename.action = OopAction_RENAME;
   rec_rename.filesize = 0;
   rec_rename.timestamp = (uint64_t)time(NULL);
   size_t old_len = strlen(abs_old);
   size_t new_len = strlen(abs_new);
   rec_rename.pathlen = (uint32_t)(old_len + 1 + new_len + 1);
   rec_rename.inode = (uint64_t)(new_exists ? stat_buf.st_ino : 0);
   char packed[PATH_BUFF_SIZE * 2];
   if (rec_rename.pathlen <= sizeof(packed)) {
      memcpy(packed, abs_old, old_len + 1);
      memcpy((void*)((uintptr_t)packed + (uintptr_t)old_len + 1), abs_new, new_len + 1);
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
   if (!SHIM_SHOULD_MONITOR()) {
      return gnu_renameat2(olddirfd, oldpath, newdirfd, newpath, flags);
   }
   char abs_old[PATH_BUFF_SIZE];
   char abs_new[PATH_BUFF_SIZE];
   resolve_absolute_path(olddirfd, oldpath, abs_old, sizeof(abs_old));
   resolve_absolute_path(newdirfd, newpath, abs_new, sizeof(abs_new));
   if (is_ignored_path(abs_old) || is_ignored_path(abs_new)) {
      return gnu_renameat2(olddirfd, oldpath, newdirfd, newpath, flags);
   }
   struct stat stat_buf;
   bool new_exists = (fstatat(newdirfd, newpath, &stat_buf, AT_SYMLINK_NOFOLLOW) == 0 && S_ISREG(stat_buf.st_mode));
   if (new_exists) {
      ensure_std_resolved();
      (void)vault_ensure(newdirfd, newpath, abs_new, &stat_buf, false);
      record_delete(abs_new, &stat_buf);
   }
   OopWalRecord rec_rename;
   rec_rename.magic = RECORD_MAGIC;
   rec_rename.action = OopAction_RENAME;
   rec_rename.filesize = 0;
   rec_rename.timestamp = (uint64_t)time(NULL);
   size_t old_len = strlen(abs_old);
   size_t new_len = strlen(abs_new);
   rec_rename.pathlen = (uint32_t)(old_len + 1 + new_len + 1);
   rec_rename.inode = (uint64_t)(new_exists ? stat_buf.st_ino : 0);
   char packed[PATH_BUFF_SIZE * 2];
   if (rec_rename.pathlen <= sizeof(packed)) {
      memcpy(packed, abs_old, old_len + 1);
      memcpy((void*)((uintptr_t)packed + (uintptr_t)old_len + 1), abs_new, new_len + 1);
      WalFile_Append(&GlobalWal, packed, &rec_rename);
   }
   return gnu_renameat2(olddirfd, oldpath, newdirfd, newpath, flags);
}

int ftruncate(int fd, off_t length) {
   if (gnu_ftruncate == NULL) {
      gnu_ftruncate = (gnu_ftruncate_t*)dlsym(RTLD_NEXT, "ftruncate");
   }
   if (!SHIM_SHOULD_MONITOR()) {
      return gnu_ftruncate(fd, length);
   }
   struct stat stat_buf;
   if (fstat(fd, &stat_buf) == 0 && S_ISREG(stat_buf.st_mode) && length < stat_buf.st_size) {
      char abs_path[PATH_BUFF_SIZE];
      if (resolve_fd_path(fd, abs_path, sizeof(abs_path)) && !is_ignored_path(abs_path)) {
         ensure_std_resolved();
         (void)capture_modify_intent(AT_FDCWD, abs_path, abs_path, &stat_buf, false);
      }
   }
   return gnu_ftruncate(fd, length);
}

int ftruncate64(int fd, off_t length) {
   return ftruncate(fd, length);
}

int truncate(const char* path, off_t length) {
   if (gnu_truncate == NULL) {
      gnu_truncate = (gnu_truncate_t*)dlsym(RTLD_NEXT, "truncate");
   }
   if (!SHIM_SHOULD_MONITOR()) {
      return gnu_truncate(path, length);
   }
   char abs_path[PATH_BUFF_SIZE];
   resolve_absolute_path(AT_FDCWD, path, abs_path, sizeof(abs_path));
   if (is_ignored_path(abs_path)) {
      return gnu_truncate(path, length);
   }
   struct stat stat_buf;
   if (lstat(path, &stat_buf) == 0 && S_ISREG(stat_buf.st_mode) && length < stat_buf.st_size) {
      ensure_std_resolved();
      (void)capture_modify_intent(AT_FDCWD, path, abs_path, &stat_buf, false);
   }
   return gnu_truncate(path, length);
}

int truncate64(const char* path, off_t length) {
   return truncate(path, length);
}

static inline void note_metadata_fd(int fd) {
   struct stat stat_buf;
   if (fstat(fd, &stat_buf) != 0 || !S_ISREG(stat_buf.st_mode)) {
      return;
   }
   char abs_path[PATH_BUFF_SIZE];
   if (!resolve_fd_path(fd, abs_path, sizeof(abs_path)) || is_ignored_path(abs_path)) {
      return;
   }
   ensure_std_resolved();
   (void)note_metadata(AT_FDCWD, abs_path, abs_path, &stat_buf);
}

static inline void note_metadata_path(int dirfd, const char* path) {
   struct stat stat_buf;
   int ok_stat = (dirfd == AT_FDCWD) ? (lstat(path, &stat_buf) == 0) : (fstatat(dirfd, path, &stat_buf, AT_SYMLINK_NOFOLLOW) == 0);
   if (!ok_stat || !S_ISREG(stat_buf.st_mode)) {
      return;
   }
   char abs_path[PATH_BUFF_SIZE];
   resolve_absolute_path(dirfd, path, abs_path, sizeof(abs_path));
   if (is_ignored_path(abs_path)) {
      return;
   }
   ensure_std_resolved();
   (void)note_metadata(dirfd, path, abs_path, &stat_buf);
}

FILE* fopen(const char* __restrict path, const char* __restrict mode) {
   static gnu_fopen_t* real = NULL;
   if (real == NULL) {
      real = (gnu_fopen_t*)dlsym(RTLD_NEXT, "fopen");
   }
   if (SHIM_SHOULD_MONITOR() && path != NULL) {
      int flags = stdio_mode_flags(mode);
      if (flags >= 0 && (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC))) {
         char abs_path[PATH_BUFF_SIZE];
         resolve_absolute_path(AT_FDCWD, path, abs_path, sizeof(abs_path));
         if (!is_ignored_path(abs_path)) {
            record_intent_open(AT_FDCWD, path, abs_path, flags);
         }
      }
   }
   return real(path, mode);
}

FILE* fopen64(const char* __restrict path, const char* __restrict mode) {
   static gnu_fopen_t* real = NULL;
   if (real == NULL) {
      real = (gnu_fopen_t*)dlsym(RTLD_NEXT, "fopen64");
   }
   if (SHIM_SHOULD_MONITOR() && path != NULL) {
      int flags = stdio_mode_flags(mode);
      if (flags >= 0 && (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC))) {
         char abs_path[PATH_BUFF_SIZE];
         resolve_absolute_path(AT_FDCWD, path, abs_path, sizeof(abs_path));
         if (!is_ignored_path(abs_path)) {
            record_intent_open(AT_FDCWD, path, abs_path, flags);
         }
      }
   }
   return real(path, mode);
}

FILE* freopen(const char* __restrict path, const char* __restrict mode, FILE* stream) {
   static gnu_freopen_t* real = NULL;
   if (real == NULL) {
      real = (gnu_freopen_t*)dlsym(RTLD_NEXT, "freopen");
   }
   if (SHIM_SHOULD_MONITOR() && path != NULL) {
      int flags = stdio_mode_flags(mode);
      if (flags >= 0 && (flags & (O_WRONLY | O_RDWR | O_CREAT | O_TRUNC))) {
         char abs_path[PATH_BUFF_SIZE];
         resolve_absolute_path(AT_FDCWD, path, abs_path, sizeof(abs_path));
         if (!is_ignored_path(abs_path)) {
            record_intent_open(AT_FDCWD, path, abs_path, flags);
         }
      }
   }
   return real(path, mode, stream);
}

int mkstemp(char* template) {
   static gnu_mkstemp_t* real = NULL;
   if (real == NULL) {
      real = (gnu_mkstemp_t*)dlsym(RTLD_NEXT, "mkstemp");
   }
   int fd = real(template);
   if (fd >= 0 && SHIM_SHOULD_MONITOR()) {
      char abs_path[PATH_BUFF_SIZE];
      resolve_absolute_path(AT_FDCWD, template, abs_path, sizeof(abs_path));
      if (!is_ignored_path(abs_path)) {
         record_create(abs_path);
      }
   }
   return fd;
}

int mkostemp(char* template, int flags) {
   static gnu_mkostemp_t* real = NULL;
   if (real == NULL) {
      real = (gnu_mkostemp_t*)dlsym(RTLD_NEXT, "mkostemp");
   }
   int fd = real(template, flags);
   if (fd >= 0 && SHIM_SHOULD_MONITOR()) {
      char abs_path[PATH_BUFF_SIZE];
      resolve_absolute_path(AT_FDCWD, template, abs_path, sizeof(abs_path));
      if (!is_ignored_path(abs_path)) {
         record_create(abs_path);
      }
   }
   return fd;
}

int mkdir(const char* path, mode_t mode) {
   static gnu_mkdir_t* real = NULL;
   if (real == NULL) {
      real = (gnu_mkdir_t*)dlsym(RTLD_NEXT, "mkdir");
   }
   int rc = real(path, mode);
   if (rc == 0 && SHIM_SHOULD_MONITOR()) {
      char abs_path[PATH_BUFF_SIZE];
      resolve_absolute_path(AT_FDCWD, path, abs_path, sizeof(abs_path));
      if (!is_ignored_path(abs_path)) {
         record_create(abs_path);
      }
   }
   return rc;
}

int mkstemps(char* template, int suffixlen) {
   static gnu_mkstemps_t* real = NULL;
   if (real == NULL) {
      real = (gnu_mkstemps_t*)dlsym(RTLD_NEXT, "mkstemps");
   }
   int fd = real(template, suffixlen);
   if (fd >= 0 && SHIM_SHOULD_MONITOR()) {
      char abs_path[PATH_BUFF_SIZE];
      resolve_absolute_path(AT_FDCWD, template, abs_path, sizeof(abs_path));
      if (!is_ignored_path(abs_path)) {
         record_create(abs_path);
      }
   }
   return fd;
}

char* mkdtemp(char* template) {
   static gnu_mkdtemp_t* real = NULL;
   if (real == NULL) {
      real = (gnu_mkdtemp_t*)dlsym(RTLD_NEXT, "mkdtemp");
   }
   char* result = real(template);
   if (result != NULL && SHIM_SHOULD_MONITOR()) {
      char abs_path[PATH_BUFF_SIZE];
      resolve_absolute_path(AT_FDCWD, result, abs_path, sizeof(abs_path));
      if (!is_ignored_path(abs_path)) {
         record_create(abs_path);
      }
   }
   return result;
}

int remove(const char* path) {
   static gnu_remove_t* real = NULL;
   if (real == NULL) {
      real = (gnu_remove_t*)dlsym(RTLD_NEXT, "remove");
   }
   if (!SHIM_SHOULD_MONITOR()) {
      return real(path);
   }
   char abs_path[PATH_BUFF_SIZE];
   resolve_absolute_path(AT_FDCWD, path, abs_path, sizeof(abs_path));
   if (is_ignored_path(abs_path)) {
      return real(path);
   }
   struct stat stat_buf;
   if (lstat(path, &stat_buf) == 0 && !S_ISREG(stat_buf.st_mode)) {
      return real(path);
   }
   return unlink(path);
}

int link(const char* oldpath, const char* newpath) {
   static gnu_link_t* real = NULL;
   if (real == NULL) {
      real = (gnu_link_t*)dlsym(RTLD_NEXT, "link");
   }
   int res = real(oldpath, newpath);
   if (res == 0 && SHIM_SHOULD_MONITOR()) {
      char abs_new[PATH_BUFF_SIZE];
      resolve_absolute_path(AT_FDCWD, newpath, abs_new, sizeof(abs_new));
      if (!is_ignored_path(abs_new)) {
         record_create(abs_new);
      }
   }
   return res;
}

int linkat(int olddirfd, const char* oldpath, int newdirfd, const char* newpath, int flags) {
   static gnu_linkat_t* real = NULL;
   if (real == NULL) {
      real = (gnu_linkat_t*)dlsym(RTLD_NEXT, "linkat");
   }
   int res = real(olddirfd, oldpath, newdirfd, newpath, flags);
   if (res == 0 && SHIM_SHOULD_MONITOR()) {
      char abs_new[PATH_BUFF_SIZE];
      resolve_absolute_path(newdirfd, newpath, abs_new, sizeof(abs_new));
      if (!is_ignored_path(abs_new)) {
         record_create(abs_new);
      }
   }
   return res;
}

int symlink(const char* target, const char* linkpath) {
   static gnu_symlink_t* real = NULL;
   if (real == NULL) {
      real = (gnu_symlink_t*)dlsym(RTLD_NEXT, "symlink");
   }
   int res = real(target, linkpath);
   if (res == 0 && SHIM_SHOULD_MONITOR()) {
      char abs_new[PATH_BUFF_SIZE];
      resolve_absolute_path(AT_FDCWD, linkpath, abs_new, sizeof(abs_new));
      if (!is_ignored_path(abs_new)) {
         record_create(abs_new);
      }
   }
   return res;
}

int symlinkat(const char* target, int newdirfd, const char* linkpath) {
   static gnu_symlinkat_t* real = NULL;
   if (real == NULL) {
      real = (gnu_symlinkat_t*)dlsym(RTLD_NEXT, "symlinkat");
   }
   int res = real(target, newdirfd, linkpath);
   if (res == 0 && SHIM_SHOULD_MONITOR()) {
      char abs_new[PATH_BUFF_SIZE];
      resolve_absolute_path(newdirfd, linkpath, abs_new, sizeof(abs_new));
      if (!is_ignored_path(abs_new)) {
         record_create(abs_new);
      }
   }
   return res;
}

int chmod(const char* path, mode_t mode) {
   static gnu_chmod_t* real = NULL;
   if (real == NULL) {
      real = (gnu_chmod_t*)dlsym(RTLD_NEXT, "chmod");
   }
   if (SHIM_SHOULD_MONITOR()) {
      note_metadata_path(AT_FDCWD, path);
   }
   return real(path, mode);
}

int fchmod(int fd, mode_t mode) {
   static gnu_fchmod_t* real = NULL;
   if (real == NULL) {
      real = (gnu_fchmod_t*)dlsym(RTLD_NEXT, "fchmod");
   }
   if (SHIM_SHOULD_MONITOR()) {
      note_metadata_fd(fd);
   }
   return real(fd, mode);
}

int fchmodat(int dirfd, const char* path, mode_t mode, int flags) {
   static gnu_fchmodat_t* real = NULL;
   if (real == NULL) {
      real = (gnu_fchmodat_t*)dlsym(RTLD_NEXT, "fchmodat");
   }
   if (SHIM_SHOULD_MONITOR()) {
      note_metadata_path(dirfd, path);
   }
   return real(dirfd, path, mode, flags);
}

int chown(const char* path, uid_t owner, gid_t group) {
   static gnu_chown_t* real = NULL;
   if (real == NULL) {
      real = (gnu_chown_t*)dlsym(RTLD_NEXT, "chown");
   }
   if (SHIM_SHOULD_MONITOR()) {
      note_metadata_path(AT_FDCWD, path);
   }
   return real(path, owner, group);
}

int lchown(const char* path, uid_t owner, gid_t group) {
   static gnu_lchown_t* real = NULL;
   if (real == NULL) {
      real = (gnu_lchown_t*)dlsym(RTLD_NEXT, "lchown");
   }
   if (SHIM_SHOULD_MONITOR()) {
      note_metadata_path(AT_FDCWD, path);
   }
   return real(path, owner, group);
}

int fchown(int fd, uid_t owner, gid_t group) {
   static gnu_fchown_t* real = NULL;
   if (real == NULL) {
      real = (gnu_fchown_t*)dlsym(RTLD_NEXT, "fchown");
   }
   if (SHIM_SHOULD_MONITOR()) {
      note_metadata_fd(fd);
   }
   return real(fd, owner, group);
}

int fchownat(int dirfd, const char* path, uid_t owner, gid_t group, int flags) {
   static gnu_fchownat_t* real = NULL;
   if (real == NULL) {
      real = (gnu_fchownat_t*)dlsym(RTLD_NEXT, "fchownat");
   }
   if (SHIM_SHOULD_MONITOR()) {
      note_metadata_path(dirfd, path);
   }
   return real(dirfd, path, owner, group, flags);
}

int utimensat(int dirfd, const char* path, const struct timespec times[2], int flags) {
   static gnu_utimensat_t* real = NULL;
   if (real == NULL) {
      real = (gnu_utimensat_t*)dlsym(RTLD_NEXT, "utimensat");
   }
   if (SHIM_SHOULD_MONITOR()) {
      note_metadata_path(dirfd, path);
   }
   return real(dirfd, path, times, flags);
}

int utimes(const char* path, const struct timeval times[2]) {
   static gnu_utimes_t* real = NULL;
   if (real == NULL) {
      real = (gnu_utimes_t*)dlsym(RTLD_NEXT, "utimes");
   }
   if (SHIM_SHOULD_MONITOR()) {
      note_metadata_path(AT_FDCWD, path);
   }
   return real(path, times);
}

int utimens(const char* path, const struct timespec times[2]) {
   static gnu_utimens_t* real = NULL;
   if (real == NULL) {
      real = (gnu_utimens_t*)dlsym(RTLD_NEXT, "utimens");
   }
   if (SHIM_SHOULD_MONITOR()) {
      note_metadata_path(AT_FDCWD, path);
   }
   return real(path, times);
}

int futimens(int fd, const struct timespec times[2]) {
   static gnu_futimens_t* real = NULL;
   if (real == NULL) {
      real = (gnu_futimens_t*)dlsym(RTLD_NEXT, "futimens");
   }
   if (SHIM_SHOULD_MONITOR()) {
      note_metadata_fd(fd);
   }
   return real(fd, times);
}

int setxattr(const char* path, const char* name, const void* value, size_t size, int flags) {
   static gnu_setxattr_t* real = NULL;
   if (real == NULL) {
      real = (gnu_setxattr_t*)dlsym(RTLD_NEXT, "setxattr");
   }
   if (SHIM_SHOULD_MONITOR()) {
      note_metadata_path(AT_FDCWD, path);
   }
   return real(path, name, value, size, flags);
}

int lsetxattr(const char* path, const char* name, const void* value, size_t size, int flags) {
   static gnu_lsetxattr_t* real = NULL;
   if (real == NULL) {
      real = (gnu_lsetxattr_t*)dlsym(RTLD_NEXT, "lsetxattr");
   }
   if (SHIM_SHOULD_MONITOR()) {
      note_metadata_path(AT_FDCWD, path);
   }
   return real(path, name, value, size, flags);
}

int fsetxattr(int fd, const char* name, const void* value, size_t size, int flags) {
   static gnu_fsetxattr_t* real = NULL;
   if (real == NULL) {
      real = (gnu_fsetxattr_t*)dlsym(RTLD_NEXT, "fsetxattr");
   }
   if (SHIM_SHOULD_MONITOR()) {
      note_metadata_fd(fd);
   }
   return real(fd, name, value, size, flags);
}

int removexattr(const char* path, const char* name) {
   static gnu_removexattr_t* real = NULL;
   if (real == NULL) {
      real = (gnu_removexattr_t*)dlsym(RTLD_NEXT, "removexattr");
   }
   if (SHIM_SHOULD_MONITOR()) {
      note_metadata_path(AT_FDCWD, path);
   }
   return real(path, name);
}

int lremovexattr(const char* path, const char* name) {
   static gnu_lremovexattr_t* real = NULL;
   if (real == NULL) {
      real = (gnu_lremovexattr_t*)dlsym(RTLD_NEXT, "lremovexattr");
   }
   if (SHIM_SHOULD_MONITOR()) {
      note_metadata_path(AT_FDCWD, path);
   }
   return real(path, name);
}

int fremovexattr(int fd, const char* name) {
   static gnu_fremovexattr_t* real = NULL;
   if (real == NULL) {
      real = (gnu_fremovexattr_t*)dlsym(RTLD_NEXT, "fremovexattr");
   }
   if (SHIM_SHOULD_MONITOR()) {
      note_metadata_fd(fd);
   }
   return real(fd, name);
}
