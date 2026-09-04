#define _GNU_SOURCE
#include "oopsie_wal.h"
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
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
               header->current_offset = sizeof(WalHeader);
               msync(tmap, WalFile->map_size, MS_SYNC);
               munmap(tmap, WalFile->map_size);
            }
         }
         close(tfd);
         link(tmp_path, Path);
         unlink(tmp_path);
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
      } else {
         return 0;
      }
   }
   void* dest = (void*)((uintptr_t)WalFile->map + old_off);
   memcpy(dest, RecP, sizeof(OopWalRecord));
   dest = (void*)((uintptr_t)dest + sizeof(OopWalRecord));
   memcpy(dest, Path, RecP->pathlen);
   dest = (void*)((uintptr_t)dest + RecP->pathlen);
   memcpy(dest, &RecP->pathlen, sizeof(uint32_t));

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
   if (current_offset < total_record_size + sizeof(WalHeader)) {
      return (size_t)-1;
   }
   void* PathStart = (void*)((uintptr_t)PathEnd - PathLen);
   memcpy(OutPath, PathStart, PathLen);
   OutPath[PathLen] = '\0';
   void* RecStart = (void*)((uintptr_t)PathStart - sizeof(OopWalRecord));
   if (*(uint32_t*)RecStart != RECORD_MAGIC) {
      return (size_t)-1;
   }
   memcpy(OutRec, RecStart, sizeof(OopWalRecord));
   size_t prev_off = current_offset - total_record_size;
   if (prev_off == sizeof(WalHeader)) {
      return 0;
   }
   return prev_off;
}