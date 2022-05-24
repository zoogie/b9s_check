#pragma once

#include "common.h"

// hardcoded start sectors
#define SECTOR_D0K3     0x000001
#define SECTOR_SECRET   0x000096

// filenames for sector 0x96
#define SECTOR_NAME     "sector0x96.bin"
#define SECRET_NAME     "secret_sector.bin"

// 0x110...0x118 in the NAND NCSD header
// see: https://www.3dbrew.org/wiki/NCSD#NCSD_header
#define NP_TYPE_NONE        0
#define NP_TYPE_STD         1
#define NP_TYPE_FAT         2 // this is of our own making
#define NP_TYPE_FIRM        3
#define NP_TYPE_AGB         4
#define NP_TYPE_NCSD        5 // this is of our own making
#define NP_TYPE_D0K3        6 // my own partition ^_^
#define NP_TYPE_SECRET      7 // this is of our own making
#define NP_TYPE_BONUS       8 // this is of our own making

// 0x118...0x120 in the NAND NCSD header
// see: https://www.3dbrew.org/wiki/NCSD#NCSD_header
#define NP_SUBTYPE_NONE     0
#define NP_SUBTYPE_TWL      1
#define NP_SUBTYPE_CTR      2
#define NP_SUBTYPE_CTR_N    3


typedef struct {
    u32 sector;
    u32 count;
    u32 keyslot;
} __attribute__((packed)) NandPartitionInfo;

typedef struct {
	u32 offset;
	u32 size;
} __attribute__((packed)) NandNcsdPartition;

// see: https://www.3dbrew.org/wiki/NCSD#NCSD_header
typedef struct {
	u8  signature[0x100];
	u8  magic[4];
	u32 size;
	u64 mediaId; // this is zero
	u8  partitions_fs_type[8];
	u8  partitions_crypto_type[8];
	NandNcsdPartition partitions[8];
	u8  unknown[0x5E];
    u8  twl_mbr[0x42];
} __attribute__((packed)) NandNcsdHeader;


bool InitNandCrypto(void);
bool CheckSlot0x05Crypto(void);
bool CheckSector0x96Crypto(void);
bool CheckFirmCrypto(void);

void CryptNand(void* buffer, u32 sector, u32 count, u32 keyslot);
void CryptSector0x96(void* buffer, bool encrypt);
int ReadNandBytes(void* buffer, u64 offset, u64 count, u32 keyslot);
int WriteNandBytes(const void* buffer, u64 offset, u64 count, u32 keyslot);
int ReadNandSectors(void* buffer, u32 sector, u32 count, u32 keyslot);
int WriteNandSectors(const void* buffer, u32 sector, u32 count, u32 keyslot);

u32 ValidateNandNcsdHeader(NandNcsdHeader* header);
u32 GetNandNcsdMinSizeSectors(NandNcsdHeader* ncsd);
u32 GetNandMinSizeSectors(void);
u32 GetNandSizeSectors(void);
u32 GetNandNcsdPartitionInfo(NandPartitionInfo* info, u32 type, u32 subtype, u32 index, NandNcsdHeader* ncsd);
u32 GetNandPartitionInfo(NandPartitionInfo* info, u32 type, u32 subtype, u32 index);
