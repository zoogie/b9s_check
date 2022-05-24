#pragma once

#include "common.h"

#define KEYDB_NAME "aeskeydb.bin"

#define KEYS_UNKNOWN 0
#define KEYS_DEVKIT  1
#define KEYS_RETAIL  2

typedef struct {
    u8   slot; // keyslot, 0x00...0x3F 
    char type; // type 'X' / 'Y' / 'N' for normalKey / 'I' for IV
    char id[10]; // key ID for special keys, all zero for standard keys
    u8   reserved[2]; // reserved space
    u8   keyUnitType; // 0 for ALL units / 1 for retail exclusive / 2 for devkit exclusive
    u8   isEncrypted; // 0 if not / anything else if it is
    u8   key[16];
} __attribute__((packed)) AesKeyInfo;

u32 GetUnitKeysType(void);
void CryptAesKeyInfo(AesKeyInfo* info);
u32 LoadKeyFromFile(u8* key, u32 keyslot, char type, char* id);
