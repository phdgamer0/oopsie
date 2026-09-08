#define _GNU_SOURCE
#include "oopsie_wal.h"
#include <fcntl.h>
#include <liburing.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

bool WalFile_IsOpen(const OopWalContext* __restrict WalFile) {
   return (bool)(WalFile->fd != (int)0);
}

bool WalFile_Open(OopWalContext* __restrict WalFile, const char* __restrict Path) {
   strncpy(WalFile->path, Path, sizeof(WalFile->path) - 1);
   WalFile->path[sizeof(WalFile->path) - 1] = '\0';
   WalFile->map_size = DEFAULT_MAP_SIZE; // 16MB
   int fd = open(Path, O_RDWR | O_CLOEXEC, 0600);
   if (fd < 0) {
      char tmp_path[300];
      snprintf(tmp_path, sizeof(tmp_path), "%s.%d.tmp", Path, getpid());
      int tfd = open(tmp_path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
      if (tfd >= 0) {
         if (ftruncate(tfd, WalFile->map_size) == 0) {
            void* tmap = mmap(NULL, WalFile->map_size, PROT_READ | PROT_WRITE, MAP_SHARED, tfd, 0);
            if (tmap != MAP_FAILED) {
               WalHeader* header = (WalHeader*)tmap;
               header->magic = WAL_HEADER_MAGIC;
               header->is_monitoring = 1;
               header->current_offset = sizeof(WalHeader);
               msync(tmap, WalFile->map_size, MS_SYNC);
               munmap(tmap, WalFile->map_size);
            }
         }
         close(tfd);
         (void)link(tmp_path, Path);
         (void)unlink(tmp_path);
      }
      fd = open(Path, O_RDWR | O_CLOEXEC, 0600);
      if (fd < 0)
         return false;
   }
   WalFile->fd = fd;
   WalFile->map = mmap(NULL, WalFile->map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   if (WalFile->map == MAP_FAILED) {
      close(fd);
      WalFile->fd = 0;
      return false;
   }
   return true;
}

size_t WalFile_Append(OopWalContext* __restrict WalFile, const char* __restrict Path, const OopWalRecord* __restrict RecP) {
   size_t required = sizeof(OopWalRecord) + RecP->pathlen + sizeof(uint32_t);
   required = (required + 7) & ~(size_t)7;
   WalHeader* header = (WalHeader*)WalFile->map;
   uint64_t old_off = __atomic_fetch_add(&header->current_offset, required, __ATOMIC_SEQ_CST);
   if (old_off + required > WalFile->map_size) { // The segment is full
      if (old_off <= WalFile->map_size) {
         char new_name[300];
         snprintf(new_name, sizeof(new_name), "%s.%lu", WalFile->path, (unsigned long)time(NULL));
         rename(WalFile->path, new_name);
         WalFile_Close(WalFile);
         if (WalFile_Open(WalFile, WalFile->path)) {
            return WalFile_Append(WalFile, Path, RecP);
         }
         return 0;
      }
      else {
         return 0;
      }
   }
   void* dest = (void*)((uintptr_t)WalFile->map + old_off);
   memcpy(dest, RecP, sizeof(OopWalRecord));
   dest = (void*)((uintptr_t)dest + sizeof(OopWalRecord));
   memcpy(dest, Path, RecP->pathlen);
   void* suffix_dest = (void*)((uintptr_t)WalFile->map + old_off + required - sizeof(uint32_t));
   memcpy(suffix_dest, &RecP->pathlen, sizeof(uint32_t));

   return required;
}

bool WalFile_Close(OopWalContext* __restrict WalFile) {
   if (WalFile->map) {
      msync(WalFile->map, WalFile->map_size, MS_SYNC);
      munmap(WalFile->map, WalFile->map_size);
      WalFile->map = NULL;
   }
   if (WalFile->fd) {
      close(WalFile->fd);
      WalFile->fd = (int)0;
   }
   return true;
}

size_t WalFile_GetPrevious(const OopWalContext* WalFile, size_t current_offset, OopWalRecord* OutRec, char* OutPath) {
   if (current_offset == 0) {
      WalHeader* header = (WalHeader*)WalFile->map;
      current_offset = header->current_offset;
      if (current_offset > WalFile->map_size) {
         current_offset = WalFile->map_size;
      }
   }
   if (current_offset == sizeof(WalHeader)) {
      return 0;
   }
   if (current_offset < sizeof(WalHeader) || current_offset > WalFile->map_size) {
      return (size_t)-1;
   }
   void* PathEnd = (void*)((uintptr_t)WalFile->map + current_offset - sizeof(uint32_t));
   uint32_t PathLen = *(uint32_t*)PathEnd;
   size_t total_record_size = sizeof(OopWalRecord) + PathLen + sizeof(uint32_t);
   total_record_size = (total_record_size + 7) & ~(size_t)7;
   if (current_offset < total_record_size + sizeof(WalHeader)) {
      printf("Failed 1: curr_off %zu, total_rec %zu\n", current_offset, total_record_size);
      return (size_t)-1;
   }
   void* RecStart = (void*)((uintptr_t)WalFile->map + current_offset - total_record_size);
   if (*(uint32_t*)RecStart != RECORD_MAGIC) {
      printf("Failed 2: magic %x != %x\n", *(uint32_t*)RecStart, RECORD_MAGIC);
      return (size_t)-1;
   }
   memcpy(OutRec, RecStart, sizeof(OopWalRecord));
   void* PathStart = (void*)((uintptr_t)RecStart + sizeof(OopWalRecord));
   memcpy(OutPath, PathStart, PathLen);
   OutPath[PathLen] = '\0';
   size_t prev_off = current_offset - total_record_size;
   if (prev_off == sizeof(WalHeader)) {
      return 0;
   }
   return prev_off;
}

size_t WalFile_Parse(const OopWalContext* WalFile, OopWalRecordView* views, size_t max_views) {
   if (!WalFile->map)
      return (size_t)0;
   size_t _views = (size_t)0;
   WalHeader* header = (WalHeader*)(WalFile->map);
   uintptr_t _offset = (uintptr_t)sizeof(WalHeader);
   while (_offset < header->current_offset && _views < max_views) {
      OopWalRecord* curr_rec = (OopWalRecord*)((uintptr_t)WalFile->map + _offset);
      size_t rec_size = sizeof(OopWalRecord) + curr_rec->pathlen + sizeof(uint32_t);
      rec_size = (rec_size + 7) & ~(size_t)7;
      if (curr_rec->magic == RECORD_MAGIC) {
         views[_views].rec = curr_rec;
         views[_views++].path = (const char*)((uintptr_t)curr_rec + (uintptr_t)sizeof(*curr_rec)); // we can later replace the size of with a const
      }
      else if (curr_rec->magic != TOMBSTONE_MAGIC) {
         break;
      }
      _offset += rec_size;
   }
   return _views;
}

void WalFile_Purge(OopWalContext* WalFile, OopWalRecordView* view) {
   if (!WalFile->map)
      return;
   __atomic_store_n(&view->rec->magic, TOMBSTONE_MAGIC, __ATOMIC_SEQ_CST);
   __atomic_add_fetch(&((WalHeader*)WalFile->map)->toombstone, 1, __ATOMIC_SEQ_CST);
   if (((WalHeader*)(WalFile->map))->toombstone >= TOMBSTONE_LIMIT) {
      WalFile_Compact(WalFile);
   }
}

bool WalFile_Compact(OopWalContext* WalFile) {
   if (!WalFile)
      return false;

   const WalHeader* oldheader = (const WalHeader*)WalFile->map;

   const char* path = WalFile->path;
   size_t len = strlen(path);
   char newpath[256];
   memcpy(newpath, path, len);
   newpath[len] = '\0';
   memmove((void*)((uintptr_t)newpath + (uintptr_t)len - (uintptr_t)3), "tmp", 3);
   int fd = open(newpath, O_RDWR | O_CREAT | O_TRUNC, 0666);
   if (fd == -1) {
      return false;
   }
   if (ftruncate(fd, WalFile->map_size) != 0) {
      close(fd);
      return false;
   }
   void* file = mmap(NULL, WalFile->map_size, PROT_WRITE | PROT_READ, MAP_SHARED, fd, 0);
   if (file == MAP_FAILED) {
      close(fd);
      return false;
   }
   
   uintptr_t _new_offset = sizeof(WalHeader);
   uintptr_t _old_offset = sizeof(WalHeader);
   const uintptr_t _max_offset = (uintptr_t)oldheader->current_offset;
   
   struct io_uring ring;
   bool ring_ok = (io_uring_queue_init(TOMBSTONE_LIMIT, &ring, 0) == 0);
   char vault_paths[TOMBSTONE_LIMIT][BUFFER_SZ];
   uint32_t chunk_count = 0;

   while (_old_offset < _max_offset) {
      const OopWalRecord* rec = (const OopWalRecord*)((uintptr_t)WalFile->map + _old_offset);
      size_t rec_size = sizeof(OopWalRecord) + rec->pathlen + sizeof(uint32_t);
      rec_size = (rec_size + 7) & ~(size_t)7;
      if (rec->magic == RECORD_MAGIC) {
         memcpy((void*)((uintptr_t)file + _new_offset), rec, rec_size);
         _new_offset += rec_size;
      } else if (rec->magic == TOMBSTONE_MAGIC && ring_ok) {
         make_vault_path(vault_paths[chunk_count], rec->inode);
         struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
         if (sqe) {
            io_uring_prep_unlinkat(sqe, AT_FDCWD, vault_paths[chunk_count], 0);
         }
         chunk_count++;
         if (chunk_count == TOMBSTONE_LIMIT) {
            io_uring_submit(&ring);
            for (uint32_t i = 0; i < TOMBSTONE_LIMIT; i++) {
               struct io_uring_cqe* cqe;
               if (io_uring_wait_cqe(&ring, &cqe) == 0) io_uring_cqe_seen(&ring, cqe);
            }
            chunk_count = 0;
         }
      }
      _old_offset += rec_size;
   }

   if (ring_ok) {
      if (chunk_count > 0) {
         io_uring_submit(&ring);
         for (uint32_t i = 0; i < chunk_count; i++) {
            struct io_uring_cqe* cqe;
            if (io_uring_wait_cqe(&ring, &cqe) == 0) io_uring_cqe_seen(&ring, cqe);
         }
      }
      io_uring_queue_exit(&ring);
   }

   WalHeader header;
   header.magic = WAL_HEADER_MAGIC;
   header.is_monitoring = 1;
   header.current_offset = _new_offset;
   header.toombstone = (uint32_t)0;
   memcpy(file, &header, sizeof(WalHeader));
   msync(file, WalFile->map_size, MS_SYNC);
   munmap(file, WalFile->map_size);
   close(fd);
#ifdef SYS_rename
   syscall(SYS_rename, newpath, path);
#else
   syscall(SYS_renameat, AT_FDCWD, newpath, AT_FDCWD, path);
#endif
   WalFile_Close(WalFile);
   WalFile_Open(WalFile, path);
   return true;
}

int cmp_wal_time(const void* a, const void* b) {
   if (!a || !b)
      return -2;
   const OopWalRecordView* _a = (const OopWalRecordView*)a;
   const OopWalRecordView* _b = (const OopWalRecordView*)b;
   const uint64_t __a = _a->rec->timestamp;
   const uint64_t __b = _b->rec->timestamp;
   return (int)((__a > __b) - (__a < __b));
}

int cmp_wal_name_asc(const void* a, const void* b) {
   if (!a || !b)
      return -2;
   const OopWalRecordView* _a = (const OopWalRecordView*)a;
   const OopWalRecordView* _b = (const OopWalRecordView*)b;
   size_t __a = _a->rec->pathlen;
   size_t __b = _b->rec->pathlen;
   size_t min = (__a < __b) ? __a : __b;
   int res = strncmp(_a->path, _b->path, min);
   if (res != 0)
      return res;
   if (__a != __b)
      return (__a > __b) - (__a < __b);
   return cmp_wal_time(a, b);
}

void make_vault_path(char* buffer, unsigned long ino) {
   char temp[48];
   memcpy(buffer, "/tmp/oopsie/000/vault_", 22);
   uint8_t i = (uint8_t)(ino & UINT8_MAX);
   buffer[VAULT_PATH_LEN - 2] = (char)((i % (uint8_t)10) + (char)'0');
   buffer[VAULT_PATH_LEN - 3] = (char)(((i / (uint8_t)10) % (uint8_t)10) + (char)'0');
   buffer[VAULT_PATH_LEN - 4] = (char)(i / (uint8_t)100 + (char)'0');
   char* p = temp + 47;
   *p = '\0';
   do {
      *--p = '0' + (ino % 10);
      ino /= 10;
   } while (ino > 0);
   size_t len = (temp + 47) - p + 1;
   memcpy(buffer + 22, p, len);
}