#pragma once

#ifndef OOPSIE_WAL_H
#define OOPSIE_WAL_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RECORD_MAGIC (uint32_t)0xDEADFACE
#define METABLOB_MAGIC (uint32_t)0x4F4F504D
#define WAL_HEADER_MAGIC (uint32_t)0x00BADDAD
#define DEFAULT_MAP_SIZE (size_t)0x01000000 // 16MB
#define TOMBSTONE_MAGIC (uint32_t)0xDEADF12E
#define VAULT_PATH (const char*)"/tmp/oopsie/000"
#define VAULT_PATH_LEN (size_t)16
#define WAL_PATH (const char*)"/tmp/oopsie/vault.wal"
#define BUFFER_SZ (size_t)64
#define TOMBSTONE_LIMIT (uint32_t)1000
#define COMPACT_FLAG_SLOTS (uint32_t)0x00100000
#define COMPACT_FLAG_CREATE (uint32_t)1
#define COMPACT_FLAG_DELETE (uint32_t)2
#define COMPACT_FLAG_OTHER (uint32_t)4

typedef enum : uint8_t {
   OopAction_CREATE = 0x01,
   OopAction_MODIFY = 0x02,
   OopAction_DELETE = 0x03,
   OopAction_RENAME = 0x04,
} OopAction_t;

#pragma pack(push, 1)
typedef struct {
   uint64_t timestamp;
   uint64_t filesize;
   uint64_t inode;
   uint32_t magic;
   uint32_t pathlen;
   uint8_t action;
} OopWalRecord;

typedef struct {
   uint32_t magic;
   uint32_t is_monitoring;
   uint64_t current_offset;
   uint32_t toombstone;
   uint64_t start_time;
} WalHeader;
#pragma pack(pop)

typedef struct {
   int fd;
   void* map;
   size_t map_size;
   char path[256];
} OopWalContext;

#pragma pack(push, 1)
typedef struct {
   uint32_t magic;
   uint32_t xattr_len;
   uint64_t mode;
   uint64_t uid;
   uint64_t gid;
   uint64_t mtime_sec;
   uint64_t mtime_nsec;
   uint64_t atime_sec;
   uint64_t atime_nsec;
} OopMetaBlob;
#pragma pack(pop)

typedef struct {
   OopWalRecord* rec;
   const char* path; // not null terminated
} OopWalRecordView;

bool WalFile_IsOpen(const OopWalContext* WalFile);
bool WalFile_IsCurrent(const OopWalContext* WalFile);
bool WalFile_Open(OopWalContext* WalFile, const char* Path);
size_t WalFile_Append(OopWalContext* WalFile, const char* Path, const OopWalRecord* RecP);
bool WalFile_Close(OopWalContext* WalFile);
size_t WalFile_GetPrevious(const OopWalContext* WalFile, size_t current_offset, OopWalRecord* OutRec, char* OutPath);
bool WalFile_HasPath(const OopWalContext* WalFile, const char* path, size_t pathlen);
size_t WalFile_Parse(const OopWalContext* WalFile, OopWalRecordView* views, size_t max_views);
void WalFile_Purge(OopWalContext* WalFile, OopWalRecordView* view);
bool WalFile_Compact(OopWalContext* WalFile);
int cmp_wal_time(const void* a, const void* b);
int cmp_wal_name_asc(const void* a, const void* b);
void make_vault_path(char* buffer, unsigned long ino);
void make_vault_meta_path(char* buffer, unsigned long ino);
size_t compact_next_record(const OopWalContext* WalFile, size_t offset, size_t limit, OopWalRecord** out_rec, size_t* out_size);

#endif