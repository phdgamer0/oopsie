#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define RECORD_MAGIC (uint32_t)0xDEADFACE
#define DEFAULT_MAP_SIZE (size_t)0x00100000
typedef enum : uint8_t {
   OopAction_CREATE = 0x01,
   OopAction_MODIFY = 0x02,
   OopAction_DELETE = 0x03,
   OopAction_RENAME = 0x04,
} OopAction_t;

typedef enum : uint8_t {
   WF_CREATE = 0x01,
   WF_OPEN = 0x02,
   WF_TRUNCATE = 0x04,
   WF_CLOEXEC = 0x08,
} WalFileFlags_t;

#pragma pack(push, 1)
typedef struct {
   uint32_t magic;
   uint8_t action;
   uint64_t timestamp;
   uint64_t filesize;
   uint32_t pathlen;
} OopWalRecord;
#pragma pack(pop)

typedef struct {
   void* map;
   int fd;
   size_t map_size;
   size_t offset;
} OopWalContext;

bool WalFile_IsOpen(const OopWalContext* __restrict WalFile);
bool WalFile_Open(OopWalContext* __restrict WalFile, const char* __restrict Path, WalFileFlags_t Flags);
size_t WalFile_Append(OopWalContext* __restrict WalFile, const char* __restrict Path, const OopWalRecord* __restrict RecP);
bool WalFile_Close(OopWalContext* __restrict WalFile);
size_t WalFile_GetPrevious(const OopWalContext* WalFile, size_t current_offset, OopWalRecord* OutRec, char* OutPath);