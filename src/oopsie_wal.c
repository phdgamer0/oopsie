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

bool WalFile_IsCurrent(const OopWalContext* __restrict WalFile) {
   struct stat mapped;
   struct stat on_disk;
   if (fstat(WalFile->fd, &mapped) != 0)
      return false;
   if (stat(WalFile->path, &on_disk) != 0)
      return false;
   return mapped.st_dev == on_disk.st_dev && mapped.st_ino == on_disk.st_ino;
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
   OopWalRecord* rec_ptr = (OopWalRecord*)RecStart;
   if (rec_ptr->magic != RECORD_MAGIC) {
      printf("Failed 2: magic %x != %x\n", rec_ptr->magic, RECORD_MAGIC);
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

bool WalFile_HasPath(const OopWalContext* WalFile, const char* path, size_t pathlen) {
   if (!WalFile->map || path == NULL || pathlen == (size_t)0) {
      return false;
   }
   WalHeader* header = (WalHeader*)(WalFile->map);
   uint64_t limit = __atomic_load_n(&header->current_offset, __ATOMIC_ACQUIRE);
   uintptr_t offset = (uintptr_t)sizeof(WalHeader);
   while (offset < (uintptr_t)limit) {
      OopWalRecord* curr_rec = (OopWalRecord*)((uintptr_t)WalFile->map + offset);
      if (curr_rec->magic == RECORD_MAGIC) {
         if (curr_rec->pathlen == pathlen &&
             memcmp((const char*)((uintptr_t)curr_rec + (uintptr_t)sizeof(*curr_rec)), path, pathlen) == 0) {
            return true;
         }
      }
      else if (curr_rec->magic != TOMBSTONE_MAGIC) {
         break;
      }
      size_t rec_size = (sizeof(OopWalRecord) + curr_rec->pathlen + sizeof(uint32_t) + (size_t)7) & ~(size_t)7;
      if (rec_size < sizeof(OopWalRecord)) {
         break;
      }
      offset += rec_size;
   }
   return false;
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
}

size_t compact_next_record(const OopWalContext* WalFile, size_t offset, size_t limit, OopWalRecord** out_rec, size_t* out_size) {
   *out_rec = NULL;
   *out_size = (size_t)0;
   if (offset < sizeof(WalHeader) || offset >= limit) {
      return (size_t)-1;
   }
   OopWalRecord* rec = (OopWalRecord*)((uintptr_t)WalFile->map + (uintptr_t)offset);
   size_t rec_size = sizeof(OopWalRecord) + rec->pathlen + sizeof(uint32_t);
   rec_size = (rec_size + 7) & ~(size_t)7;
   if (offset + rec_size > limit) {
      return (size_t)-1;
   }
   *out_rec = rec;
   *out_size = rec_size;
   return offset + rec_size;
}

static inline uint32_t compact_hash_path(const char* p, uint32_t len) {
   uint32_t h = 2166136261u;
   for (uint32_t i = 0; i < len; i++) {
      h ^= (uint32_t)(unsigned char)p[i];
      h *= 16777619u;
   }
   return h;
}

static int compact_find_slot(const uint64_t* path_flags, const char* path, uint32_t len) {
   uint32_t key = compact_hash_path(path, len) | 1u;
   uint32_t slot = key & (COMPACT_FLAG_SLOTS - 1);
   for (uint32_t probe = 0; probe < COMPACT_FLAG_SLOTS; probe++) {
      uint32_t idx = (slot + probe) & (COMPACT_FLAG_SLOTS - 1);
      uint64_t entry = path_flags[idx];
      if (entry == 0) {
         return -1;
      }
      if ((uint32_t)(entry >> 32) == key) {
         return (int)idx;
      }
   }
   return -1;
}

bool WalFile_Compact(OopWalContext* WalFile) {
   if (!WalFile)
      return false;

   const WalHeader* oldheader = (const WalHeader*)WalFile->map;

   const char* path = WalFile->path;
   size_t len = strlen(path);
   char reopen[256];
   memcpy(reopen, path, len + 1);
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
   
   uint64_t* path_flags = (uint64_t*)calloc(COMPACT_FLAG_SLOTS, sizeof(uint64_t));
   uint8_t* emitted = (uint8_t*)calloc(COMPACT_FLAG_SLOTS, sizeof(uint8_t));
   bool flags_ok = (path_flags != NULL && emitted != NULL);
   if (flags_ok) {
      uintptr_t scan = sizeof(WalHeader);
      while (scan < _max_offset) {
         OopWalRecord* rec = NULL;
         size_t scan_size = (size_t)0;
         uintptr_t scan_next = compact_next_record(WalFile, scan, _max_offset, &rec, &scan_size);
         if (scan_next == (uintptr_t)-1) {
            break;
         }
         if (rec->magic == RECORD_MAGIC && rec->pathlen > 0) {
            const char* rec_path = (const char*)((uintptr_t)rec + (uintptr_t)sizeof(*rec));
            size_t hash_len = rec->pathlen;
            if (rec->action == OopAction_RENAME) {
               size_t first_len = strlen(rec_path);
               hash_len = (first_len < rec->pathlen) ? first_len : rec->pathlen;
            }
            uint32_t key = compact_hash_path(rec_path, (uint32_t)hash_len) | 1u;
            uint32_t bit = (rec->action == OopAction_CREATE) ? COMPACT_FLAG_CREATE : ((rec->action == OopAction_DELETE) ? COMPACT_FLAG_DELETE : COMPACT_FLAG_OTHER);
            uint32_t slot = key & (COMPACT_FLAG_SLOTS - 1);
            for (uint32_t probe = 0; probe < COMPACT_FLAG_SLOTS; probe++) {
               uint32_t idx = (slot + probe) & (COMPACT_FLAG_SLOTS - 1);
               uint64_t entry = path_flags[idx];
               if (entry == 0) {
                  path_flags[idx] = ((uint64_t)key << 32) | (uint64_t)bit;
                  break;
               }
               if ((uint32_t)(entry >> 32) == key) {
                  path_flags[idx] = entry | (uint64_t)bit;
                  break;
               }
            }
         }
         scan = scan_next;
      }
   }

   struct io_uring ring;
   bool ring_ok = (io_uring_queue_init(TOMBSTONE_LIMIT, &ring, 0) == 0);
   char vault_paths[TOMBSTONE_LIMIT][BUFFER_SZ];
   uint32_t chunk_count = 0;

   while (_old_offset < _max_offset) {
      OopWalRecord* rec = NULL;
      size_t rec_size = (size_t)0;
      uintptr_t next_offset = compact_next_record(WalFile, _old_offset, _max_offset, &rec, &rec_size);
      if (next_offset == (uintptr_t)-1) {
         break;
      }
      if (rec->magic == RECORD_MAGIC) {
         bool drop = false;
         if (flags_ok && rec->pathlen > 0) {
            const char* rec_path = (const char*)((uintptr_t)rec + (uintptr_t)sizeof(*rec));
            uint32_t scan_len = rec->pathlen;
            if (rec->action == OopAction_RENAME) {
               size_t first_len = strlen(rec_path);
               scan_len = (first_len < rec->pathlen) ? (uint32_t)first_len : rec->pathlen;
            }
            int idx = compact_find_slot(path_flags, rec_path, scan_len);
            if (idx >= 0) {
               uint32_t bits = (uint32_t)(path_flags[idx] & (uint64_t)0xFFFFFFFFu);
               if (rec->action == OopAction_CREATE) {
                  if ((bits & COMPACT_FLAG_DELETE) != 0 && (bits & COMPACT_FLAG_OTHER) == 0) {
                     drop = true;
                  }
               }
               if (!drop && rec->action != OopAction_RENAME) {
                  uint32_t seen_bit = (rec->action == OopAction_CREATE) ? 1u : ((rec->action == OopAction_DELETE) ? 2u : 4u);
                  if ((emitted[idx] & seen_bit) != 0) {
                     drop = true;
                  }
                  else {
                     emitted[idx] = (uint8_t)(emitted[idx] | seen_bit);
                  }
               }
            }
         }
         if (!drop) {
            memcpy((void*)((uintptr_t)file + _new_offset), rec, rec_size);
            _new_offset += rec_size;
         }
      } else if (rec->magic == TOMBSTONE_MAGIC && ring_ok && rec->inode != 0) {
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
      _old_offset = next_offset;
   }
   free(path_flags);
   free(emitted);

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
   memset(&header, 0, sizeof(WalHeader));
   header.magic = WAL_HEADER_MAGIC;
   header.is_monitoring = oldheader->is_monitoring;
   header.current_offset = _new_offset;
   header.toombstone = (uint32_t)0;
   header.start_time = oldheader->start_time;
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
   WalFile_Open(WalFile, reopen);
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

void make_vault_meta_path(char* buffer, unsigned long ino) {
   make_vault_path(buffer, ino);
   size_t len = strlen(buffer);
   if (len + sizeof(".meta") <= BUFFER_SZ) {
      memcpy(buffer + len, ".meta", sizeof(".meta"));
   }
}