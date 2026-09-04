#pragma once

#ifndef OOPSIE_WAL_H
#define OOPSIE_WAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RECORD_MAGIC (uint32_t)0xDEADFACE
#define WAL_HEADER_MAGIC (uint32_t)0x00BADDAD
#define DEFAULT_MAP_SIZE (size_t)0x01000000 // 16MB

typedef enum : uint8_t {
   OopAction_CREATE = 0x01,
   OopAction_MODIFY = 0x02,
   OopAction_DELETE = 0x03,
   OopAction_RENAME = 0x04,
} OopAction_t;

#pragma pack(push, 1)
typedef struct {
   uint32_t magic;
   uint8_t action;
   uint64_t timestamp;
   uint64_t filesize;
   uint64_t inode;
   uint32_t pathlen;
} OopWalRecord;

typedef struct {
   uint32_t magic;
   uint64_t current_offset;
} WalHeader;
#pragma pack(pop)

typedef struct {
   int fd;
   void* map;
   size_t map_size;
   char path[256];
} OopWalContext;

bool WalFile_IsOpen(const OopWalContext* WalFile);
bool WalFile_Open(OopWalContext* WalFile, const char* Path);
size_t WalFile_Append(OopWalContext* WalFile, const char* Path, const OopWalRecord* RecP);
bool WalFile_Close(OopWalContext* WalFile);
size_t WalFile_GetPrevious(const OopWalContext* WalFile, size_t current_offset, OopWalRecord* OutRec, char* OutPath);

#endif