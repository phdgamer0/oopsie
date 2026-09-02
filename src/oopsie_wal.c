#define _GNU_SOURCE
#include "oopsie_wal.h"
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
bool WalFile_IsOpen(const OopWalContext* __restrict WalFile) {
   return (bool)(WalFile->fd != (int)0);
}
bool WalFile_Open(OopWalContext* __restrict WalFile, const char* __restrict Path, WalFileFlags_t Flags) {
   WalFile->map_size = DEFAULT_MAP_SIZE;
   int fd = open(Path, (Flags & WF_CREATE ? O_CREAT : (Flags & WF_TRUNCATE ? O_TRUNC : 0)) | O_RDWR | O_CLOEXEC, 0600);
   if (fd < 0) {
      return (bool)false;
   }
   WalFile->fd = fd;
   off_t size = lseek(fd, 0, SEEK_END);
   if (size < 0) {
      close(fd);
      return (bool)false;
   }
   size_t actual_map_size = ((size_t)size < WalFile->map_size) ? WalFile->map_size : (size_t)size;
   if ((size_t)size < actual_map_size) {
      ftruncate(fd, actual_map_size);
   }
   WalFile->map = mmap(NULL, actual_map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
   if (WalFile->map == MAP_FAILED) {
      close(fd);
      return (bool)false;
   }
   WalFile->map_size = actual_map_size;
   WalFile->offset = (size_t)size;
   return (bool)true;
}
size_t WalFile_Append(OopWalContext* __restrict WalFile, const char* __restrict Path, const OopWalRecord* __restrict RecP) {
   size_t PrevOffset = WalFile->offset;
   memcpy((void*)((uintptr_t)WalFile->map + WalFile->offset), (void*)RecP, sizeof(OopWalRecord));
   WalFile->offset += sizeof(OopWalRecord);
   memcpy((void*)((uintptr_t)WalFile->map + WalFile->offset), (void*)Path, RecP->pathlen);
   WalFile->offset += RecP->pathlen;
   memcpy((void*)((uintptr_t)WalFile->map + WalFile->offset), (void*)&RecP->pathlen, sizeof(uint32_t));
   WalFile->offset += sizeof(uint32_t);
   return (size_t)(WalFile->offset - PrevOffset);
}
bool WalFile_Close(OopWalContext* __restrict WalFile) {
   if (WalFile->map) {
      msync(WalFile->map, WalFile->map_size, MS_SYNC);
      munmap(WalFile->map, WalFile->map_size);
      WalFile->map = NULL;
      WalFile->map_size = (size_t)0;
      WalFile->offset = (size_t)0;
   }
   if (WalFile->fd) {
      close(WalFile->fd);
      WalFile->fd = (int)0;
   }
   return (bool)true;
}
size_t WalFile_GetPrevious(const OopWalContext* WalFile, size_t current_offset, OopWalRecord* OutRec, char* OutPath) {
   if (current_offset < (size_t)(sizeof(OopWalRecord) + sizeof(uint32_t)) || current_offset > (size_t)WalFile->map_size) {
      return (size_t)-1; // Corrupted WAL or bad offset
   }
   void* PathEnd = (void*)((uintptr_t)WalFile->map + current_offset - sizeof(uint32_t));
   if (!PathEnd) {
      return (size_t)-1;
   }
   uint32_t PathLen = *(uint32_t*)PathEnd;
   size_t total_record_size = sizeof(OopWalRecord) + PathLen + sizeof(uint32_t);
   if (current_offset < total_record_size) {
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
   return (size_t)(current_offset - total_record_size);
}