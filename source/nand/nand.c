#include "nand.h"
#include "keydb.h"
#include "aes.h"
#include "sha.h"
#include "fatmbr.h"
#include "unittype.h"
#include "sdmmc.h"


#define KEY95_SHA256    ((IS_DEVKIT) ? slot0x11Key95dev_sha256 : slot0x11Key95_sha256)

// see: https://www.3dbrew.org/wiki/NCSD#NCSD_header
static const u32 np_keyslots[9][4] = { // [NP_TYPE][NP_SUBTYPE]
    { 0xFF, 0xFF, 0xFF, 0xFF }, // none
    { 0xFF, 0x03, 0x04, 0x05 }, // standard
    { 0xFF, 0x03, 0x04, 0x05 }, // FAT (custom, not in NCSD)
    { 0xFF, 0xFF, 0x06, 0xFF }, // FIRM
    { 0xFF, 0xFF, 0x07, 0xFF }, // AGBSAVE
    { 0xFF, 0xFF, 0xFF, 0xFF }, // NCSD (custom)
    { 0xFF, 0xFF, 0xFF, 0xFF }, // D0K3 (custom)
    { 0xFF, 0xFF, 0xFF, 0x11 }, // SECRET (custom)
    { 0xFF, 0xFF, 0xFF, 0xFF }  // BONUS (custom)
};

static u8 slot0x05KeyY[0x10] = { 0x00 }; // need to load this from FIRM0 / external file
static const u8 slot0x05KeyY_sha256[0x20] = { // hash for slot0x05KeyY (16 byte)
    0x98, 0x24, 0x27, 0x14, 0x22, 0xB0, 0x6B, 0xF2, 0x10, 0x96, 0x9C, 0x36, 0x42, 0x53, 0x7C, 0x86,
    0x62, 0x22, 0x5C, 0xFD, 0x6F, 0xAE, 0x9B, 0x0A, 0x85, 0xA5, 0xCE, 0x21, 0xAA, 0xB6, 0xC8, 0x4D
};
static const u8 slot0x24KeyY_sha256[0x20] = { // hash for slot0x24KeyY (16 byte) 
    0x5F, 0x04, 0x01, 0x22, 0x95, 0xB2, 0x23, 0x70, 0x12, 0x40, 0x53, 0x30, 0xC0, 0xA7, 0xBF, 0x7C, 
    0xD4, 0x40, 0x92, 0x25, 0xD1, 0x9D, 0xA2, 0xDE, 0xCD, 0xC7, 0x12, 0x97, 0x08, 0x46, 0x54, 0xB7
};

static const u8 slot0x11Key95_sha256[0x20] = { // slot0x11Key95 hash (first 16 byte of sector0x96)
    0xBA, 0xC1, 0x40, 0x9C, 0x6E, 0xE4, 0x1F, 0x04, 0xAA, 0xC4, 0xE2, 0x09, 0x5C, 0xE9, 0x4F, 0x78, 
    0x6C, 0x78, 0x5F, 0xAC, 0xEC, 0x7E, 0xC0, 0x11, 0x26, 0x9D, 0x4E, 0x47, 0xB3, 0x64, 0xC4, 0xA5
};

static const u8 slot0x11Key95dev_sha256[0x20] = { // slot0x11Key95 hash (first 16 byte of sector0x96)
    0x97, 0x0E, 0x52, 0x29, 0x63, 0x19, 0x47, 0x51, 0x15, 0xD8, 0x02, 0x7A, 0x22, 0x0F, 0x58, 0x15,
    0xD7, 0x6C, 0xE9, 0xAD, 0xE7, 0xFE, 0x9A, 0x25, 0x4E, 0x4A, 0x0C, 0x82, 0x67, 0xB5, 0x4A, 0x7B
};

static u8 CtrNandCtr[16];
static u8 TwlNandCtr[16];
static u8 OtpSha256[32] = { 0 };


u32 LoadKeyYFromP9(u8* key, const u8* keyhash, u32 offset, u32 keyslot)
{
    static const u32 offsetA9l = 0x066A00; // fixed offset, this only has to work for FIRM90 / FIRM81
    static const u32 sector_firm0 = 0x058980; // standard firm0 sector (this only has to work in A9LH anyways)
    u8 ctr0x15[16] __attribute__((aligned(32)));
    u8 keyY0x15[16] __attribute__((aligned(32)));
    u8 keyY[16] __attribute__((aligned(32)));
    u8 header[0x200];
    
    // check arm9loaderhax
    if (!IS_A9LH || IS_SIGHAX || (offset < (offsetA9l + 0x0800))) return 1;
    
    // section 2 (arm9loader) header of FIRM
    // this is @0x066A00 in FIRM90 & FIRM81
    ReadNandBytes(header, (sector_firm0 * 0x200) + offsetA9l, 0x200, 0x06);
    memcpy(keyY0x15, header + 0x10, 0x10); // 0x15 keyY
    memcpy(ctr0x15, header + 0x20, 0x10); // 0x15 counter
    
    // read and decrypt the encrypted keyY
    ReadNandBytes(keyY, (sector_firm0 * 0x200) + offset, 0x10, 0x06);
    setup_aeskeyY(0x15, keyY0x15);
    use_aeskey(0x15);
    ctr_decrypt_byte(keyY, keyY, 0x10, offset - (offsetA9l + 0x800), AES_CNT_CTRNAND_MODE, ctr0x15);
    if (key) memcpy(key, keyY, 0x10);
    
    // check the key
    u8 shasum[0x32];
    sha_quick(shasum, keyY, 16, SHA256_MODE);
    if (memcmp(shasum, keyhash, 32) == 0) {
        setup_aeskeyY(keyslot, keyY);
        use_aeskey(keyslot);
        return 0;
    }
    
    return 1;
}

bool InitNandCrypto(void)
{   
    // part #0: KeyX / KeyY for secret sector 0x96
    // on a9lh this MUST be run before accessing the SHA register in any other way
    if (IS_UNLOCKED) { // if OTP is unlocked
        // see: https://www.3dbrew.org/wiki/OTP_Registers
        sha_quick(OtpSha256, (u8*) 0x10012000, 0x90, SHA256_MODE);
    } else if (IS_A9LH) { // for a9lh
        // store the current SHA256 from register
        memcpy(OtpSha256, (void*) REG_SHAHASH, 32);
    }
    if (!CheckSector0x96Crypto()) { // if all else fails...
        u8 __attribute__((aligned(32))) otp0x90[0x90];
        u8 __attribute__((aligned(32))) otp_key[0x10];
        u8 __attribute__((aligned(32))) otp_iv[0x10];
        memcpy(otp0x90, (u8*) 0x01FFB800, 0x90);
        if ((LoadKeyFromFile(otp_key, 0x11, 'N', "OTP") == 0) &&
            ((LoadKeyFromFile(otp_iv, 0x11, 'I', "IVOTP") == 0) ||
             (LoadKeyFromFile(otp_iv, 0x11, 'I', "OTP") == 0))) {
            setup_aeskey(0x11, otp_key);
            use_aeskey(0x11);
            cbc_encrypt(otp0x90, otp0x90, 0x90 / 0x10, AES_CNT_TITLEKEY_ENCRYPT_MODE, otp_iv);
            sha_quick(OtpSha256, otp0x90, 0x90, SHA256_MODE);
        }
    }
    
    // part #1: Get NAND CID, set up TWL/CTR counter
    u32 NandCid[4];
    u8 shasum[32];
    
    sdmmc_sdcard_init();
    sdmmc_get_cid(1, NandCid);
    sha_quick(shasum, (u8*) NandCid, 16, SHA256_MODE);
    memcpy(CtrNandCtr, shasum, 16);
    sha_quick(shasum, (u8*) NandCid, 16, SHA1_MODE);
    for(u32 i = 0; i < 16; i++) // little endian and reversed order
        TwlNandCtr[i] = shasum[15-i];
    
    // part #2: TWL KEY
    // see: https://www.3dbrew.org/wiki/Memory_layout#ARM9_ITCM
    if (IS_A9LH) { // only for a9lh (and sighax, for now)
        u32* TwlCustId = (u32*) (0x01FFB808);
        u8 TwlKeyX[16] __attribute__((aligned(32)));
        u8 TwlKeyY[16] __attribute__((aligned(32)));
        
        // thanks b1l1s & Normmatt
        // see source from https://gbatemp.net/threads/release-twltool-dsi-downgrading-save-injection-etc-multitool.393488/
        const char* nintendo = "NINTENDO";
        u32 TwlKeyXW0 = (TwlCustId[0] ^ 0xB358A6AF) | 0x80000000;
        u32 TwlKeyXW3 = TwlCustId[1] ^ 0x08C267B7;
        memcpy(TwlKeyX +  4, nintendo, 8);
        memcpy(TwlKeyX +  0, &TwlKeyXW0, 4);
        memcpy(TwlKeyX + 12, &TwlKeyXW3, 4);
        
        // see: https://www.3dbrew.org/wiki/Memory_layout#ARM9_ITCM
        u32 TwlKeyYW3 = 0xE1A00005;
        memcpy(TwlKeyY, (u8*) 0x01FFD3C8, 12);
        memcpy(TwlKeyY + 12, &TwlKeyYW3, 4);
        
        setup_aeskeyX(0x03, TwlKeyX);
        setup_aeskeyY(0x03, TwlKeyY);
        use_aeskey(0x03);
    }
    
    // part #3: CTRNAND N3DS KEY / AGBSAVE CMAC KEY
    // thanks AuroraWright and Gelex for advice on this
    // see: https://github.com/AuroraWright/Luma3DS/blob/master/source/crypto.c#L347
    if (IS_A9LH && !IS_SIGHAX) { // only on A9LH, not required on sighax
        // keyY 0x05 is encrypted @0x0EB014 in the FIRM90
        // keyY 0x05 is encrypted @0x0EB24C in the FIRM81
        if ((LoadKeyYFromP9(slot0x05KeyY, slot0x05KeyY_sha256, 0x0EB014, 0x05) != 0) &&
            (LoadKeyYFromP9(slot0x05KeyY, slot0x05KeyY_sha256, 0x0EB24C, 0x05) != 0))
            LoadKeyFromFile(slot0x05KeyY, 0x05, 'Y', NULL);
        
        // keyY 0x24 is encrypted @0x0E62DC in the FIRM90
        // keyY 0x24 is encrypted @0x0E6514 in the FIRM81
        if ((LoadKeyYFromP9(NULL, slot0x24KeyY_sha256, 0x0E62DC, 0x24) != 0) &&
            (LoadKeyYFromP9(NULL, slot0x24KeyY_sha256, 0x0E6514, 0x24) != 0))
            LoadKeyFromFile(NULL, 0x24, 'Y', NULL);
    }
    
    return true;
}

bool CheckSlot0x05Crypto(void)
{
    // step #1 - check the slot0x05KeyY SHA-256
    if (sha_cmp(slot0x05KeyY_sha256, slot0x05KeyY, 16, SHA256_MODE) == 0)
        return true;
    
    // step #2 - check actual presence of CTRNAND FAT
    if (GetNandPartitionInfo(NULL, NP_TYPE_STD, NP_SUBTYPE_CTR_N, 0) == 0)
        return true;
    
    // failed if we arrive here
    return false;
}

bool CheckSector0x96Crypto(void)
{
    u8 buffer[0x200];
    ReadNandSectors(buffer, SECTOR_SECRET, 1, 0x11);
    return (sha_cmp(KEY95_SHA256, buffer, 16, SHA256_MODE) == 0);
}

bool CheckFirmCrypto(void)
{
    // check the FIRM magic
    const u8 magic[8] = {'F', 'I', 'R', 'M'};
    for (u32 i = 0; i < 8; i++) {
        NandPartitionInfo np_info;
        u8 buffer[0x200];
        if ((GetNandPartitionInfo(&np_info, NP_TYPE_FIRM, NP_SUBTYPE_CTR, i) != 0) ||
            (ReadNandSectors(buffer, np_info.sector, 1, np_info.keyslot) != 0)) break;
        if (memcmp(buffer, magic, sizeof(magic)) == 0) return true; // success
    }
    
    // failed if we arrive here
    return false;
}

void CryptNand(void* buffer, u32 sector, u32 count, u32 keyslot)
{
    u32 mode = (keyslot != 0x03) ? AES_CNT_CTRNAND_MODE : AES_CNT_TWLNAND_MODE; // somewhat hacky
    u8 ctr[16] __attribute__((aligned(32)));
    u32 blocks = count * (0x200 / 0x10);
    
    // copy NAND CTR and increment it
    memcpy(ctr, (keyslot != 0x03) ? CtrNandCtr : TwlNandCtr, 16); // hacky again
    add_ctr(ctr, sector * (0x200 / 0x10));
    
    // decrypt the data
    use_aeskey(keyslot);
    ctr_decrypt((void*) buffer, (void*) buffer, blocks, mode, ctr);
}

void CryptSector0x96(void* buffer, bool encrypt)
{
    u32 mode = encrypt ? AES_CNT_ECB_ENCRYPT_MODE : AES_CNT_ECB_DECRYPT_MODE;
    
    // setup the key
    setup_aeskeyX(0x11, OtpSha256);
    setup_aeskeyY(0x11, OtpSha256 + 16);
    
    // decrypt the sector
    use_aeskey(0x11);
    ecb_decrypt((void*) buffer, (void*) buffer, 0x200 / AES_BLOCK_SIZE, mode);
}

int ReadNandBytes(void* buffer, u64 offset, u64 count, u32 keyslot)
{
    if (!(offset % 0x200) && !(count % 0x200)) { // aligned data -> simple case 
        // simple wrapper function for ReadNandSectors(...)
        return ReadNandSectors(buffer, offset / 0x200, count / 0x200, keyslot);
    } else { // misaligned data -> -___-
        u8* buffer8 = (u8*) buffer;
        u8 l_buffer[0x200];
        int errorcode = 0;
        if (offset % 0x200) { // handle misaligned offset
            u32 offset_fix = 0x200 - (offset % 0x200);
            errorcode = ReadNandSectors(l_buffer, offset / 0x200, 1, keyslot);
            if (errorcode != 0) return errorcode;
            memcpy(buffer8, l_buffer + 0x200 - offset_fix, min(offset_fix, count));
            if (count <= offset_fix) return 0;
            offset += offset_fix;
            buffer8 += offset_fix;
            count -= offset_fix;
        } // offset is now aligned and part of the data is read
        if (count >= 0x200) { // otherwise this is misaligned and will be handled below
            errorcode = ReadNandSectors(buffer8, offset / 0x200, count / 0x200, keyslot);
            if (errorcode != 0) return errorcode;
        }
        if (count % 0x200) { // handle misaligned count
            u32 count_fix = count % 0x200;
            errorcode = ReadNandSectors(l_buffer, (offset + count) / 0x200, 1, keyslot);
            if (errorcode != 0) return errorcode;
            memcpy(buffer8 + count - count_fix, l_buffer, count_fix);
        }
        return errorcode;
    }
}

int WriteNandBytes(const void* buffer, u64 offset, u64 count, u32 keyslot)
{
    if (!(offset % 0x200) && !(count % 0x200)) { // aligned data -> simple case 
        // simple wrapper function for WriteNandSectors(...)
        return WriteNandSectors(buffer, offset / 0x200, count / 0x200, keyslot);
    } else { // misaligned data -> -___-
        u8* buffer8 = (u8*) buffer8;
        u8 l_buffer[0x200];
        int errorcode = 0;
        if (offset % 0x200) { // handle misaligned offset
            u32 offset_fix = 0x200 - (offset % 0x200);
            errorcode = ReadNandSectors(l_buffer, offset / 0x200, 1, keyslot);
            if (errorcode != 0) return errorcode;
            memcpy(l_buffer + 0x200 - offset_fix, buffer8, min(offset_fix, count));
            errorcode = WriteNandSectors((const u8*) l_buffer, offset / 0x200, 1, keyslot);
            if (errorcode != 0) return errorcode;
            if (count <= offset_fix) return 0;
            offset += offset_fix;
            buffer8 += offset_fix;
            count -= offset_fix;
        } // offset is now aligned and part of the data is written
        if (count >= 0x200) { // otherwise this is misaligned and will be handled below
            errorcode = WriteNandSectors(buffer8, offset / 0x200, count / 0x200, keyslot);
            if (errorcode != 0) return errorcode;
        }
        if (count % 0x200) { // handle misaligned count
            u32 count_fix = count % 0x200;
            errorcode = ReadNandSectors(l_buffer, (offset + count) / 0x200, 1, keyslot);
            if (errorcode != 0) return errorcode;
            memcpy(l_buffer, buffer8 + count - count_fix, count_fix);
            errorcode = WriteNandSectors((const u8*) l_buffer, (offset + count) / 0x200, 1, keyslot);
            if (errorcode != 0) return errorcode;
        }
        return errorcode;
    }
}

int ReadNandSectors(void* buffer, u32 sector, u32 count, u32 keyslot)
{   
    u8* buffer8 = (u8*) buffer;
    if (!count) return 0; // <--- just to be safe
    int errorcode = sdmmc_nand_readsectors(sector, count, buffer8);
    if (errorcode) return errorcode;
    if ((keyslot == 0x11) && (sector == SECTOR_SECRET)) CryptSector0x96(buffer8, false);
    else if (keyslot < 0x40) CryptNand(buffer8, sector, count, keyslot);
    
    return 0;
}

int WriteNandSectors(const void* buffer, u32 sector, u32 count, u32 keyslot)
{
    u8* buffer8 = (u8*) buffer;
    // buffer must not be changed, so this is a little complicated
    for (u32 s = 0; s < count; s += (NAND_BUFFER_SIZE / 0x200)) {
        u32 pcount = min((NAND_BUFFER_SIZE/0x200), (count - s));
        memcpy(NAND_BUFFER, buffer8 + (s*0x200), pcount * 0x200);
        if ((keyslot == 0x11) && (sector == SECTOR_SECRET)) CryptSector0x96(NAND_BUFFER, true);
        else if (keyslot < 0x40) CryptNand(NAND_BUFFER, sector + s, pcount, keyslot);
        int errorcode = sdmmc_nand_writesectors(sector + s, pcount, NAND_BUFFER);
        if (errorcode) return errorcode;
    }
    
    return 0;
}

// shamelessly stolen from myself
// see: https://github.com/d0k3/GodMode9/blob/master/source/game/ncsd.c#L4
u32 ValidateNandNcsdHeader(NandNcsdHeader* header)
{
    u8 zeroes[16] = { 0 };
    if ((memcmp(header->magic, "NCSD", 4) != 0) || // check magic number
        (memcmp(header->partitions_fs_type, zeroes, 8) == 0) || header->mediaId) // prevent detection of cart NCSD images
        return 1;
    
    u32 data_units = 0;
    u32 firm_count = 0;
    for (u32 i = 0; i < 8; i++) {
        NandNcsdPartition* partition = header->partitions + i;
        u8 np_type = header->partitions_fs_type[i];
        if ((i == 0) && !partition->size) return 1; // first content must be there
        else if (!partition->size) continue;
        if (!np_type) return 1; // partition must have a type
        if (partition->offset < data_units)
            return 1; // overlapping partitions, failed
        data_units = partition->offset + partition->size;
        if (np_type == NP_TYPE_FIRM) firm_count++; // count firms
    }
    if (data_units > header->size) return 1;
    if (!firm_count) return 1; // at least one firm is required
     
    return 0;
}

u32 GetNandNcsdMinSizeSectors(NandNcsdHeader* ncsd)
{
    u32 nand_minsize = 0;
    for (u32 prt_idx = 0; prt_idx < 8; prt_idx++) {
        u32 prt_end = ncsd->partitions[prt_idx].offset + ncsd->partitions[prt_idx].size;
        if (prt_end > nand_minsize) nand_minsize = prt_end;
    }
    
    return nand_minsize;
}

u32 GetNandMinSizeSectors(void)
{
    NandNcsdHeader ncsd;
    if ((ReadNandSectors((u8*) &ncsd, 0, 1, 0xFF) != 0) ||
        (ValidateNandNcsdHeader(&ncsd) != 0)) return 0;
    
    return GetNandNcsdMinSizeSectors(&ncsd);
}

u32 GetNandSizeSectors(void)
{
    return getMMCDevice(0)->total_size;
}

u32 GetNandNcsdPartitionInfo(NandPartitionInfo* info, u32 type, u32 subtype, u32 index, NandNcsdHeader* ncsd)
{
    // safety / set keyslot
    if ((type == NP_TYPE_FAT) || (type > NP_TYPE_BONUS) || (subtype > NP_SUBTYPE_CTR_N)) return 1;
    info->keyslot = np_keyslots[type][subtype];
    
    // full (minimum) NAND "partition"
    if (type == NP_TYPE_NONE) {
        info->sector = 0x00;
        info->count = GetNandNcsdMinSizeSectors(ncsd);
        return 0;
    }
    
    // special, custom partition types, not in NCSD
    if (type >= NP_TYPE_NCSD) {
        if (type == NP_TYPE_NCSD) {
            info->sector = 0x00; // hardcoded
            info->count = 0x01;
        } else if (type == NP_TYPE_D0K3) {
            info->sector = SECTOR_D0K3; // hardcoded
            info->count = SECTOR_SECRET - info->sector;
        } else if (type == NP_TYPE_SECRET) {
            info->sector = SECTOR_SECRET;
            info->count = 0x01;
        } else if (type == NP_TYPE_BONUS) {
            info->sector = GetNandNcsdMinSizeSectors(ncsd);
            info->count = 0x00; // placeholder, actual size needs info from NAND chip
        } else return 1;
        return 0;
    }
    
    u32 prt_idx = 8;
    for (prt_idx = 0; prt_idx < 8; prt_idx++) {
        if ((ncsd->partitions_fs_type[prt_idx] != type) ||
            (ncsd->partitions_crypto_type[prt_idx] != subtype)) continue;
        if (index == 0) break;
        index--;
    }
    
    if (prt_idx >= 8) return 1; // not found
    info->sector = ncsd->partitions[prt_idx].offset;
    info->count = ncsd->partitions[prt_idx].size;
    
    return 0;
}

u32 GetNandPartitionInfo(NandPartitionInfo* info, u32 type, u32 subtype, u32 index)
{
    // workaround for info == NULL
    NandPartitionInfo dummy;
    if (!info) info = &dummy;
    
    // find type & subtype in NCSD header
    u8 header[0x200];
    ReadNandSectors(header, 0x00, 1, 0xFF);
    NandNcsdHeader* ncsd = (NandNcsdHeader*) header;
    if ((ValidateNandNcsdHeader(ncsd) != 0) ||
        ((type == NP_TYPE_FAT) && (GetNandNcsdPartitionInfo(info, NP_TYPE_STD, subtype, 0, ncsd) != 0)) ||
        ((type != NP_TYPE_FAT) && (GetNandNcsdPartitionInfo(info, type, subtype, index, ncsd) != 0)))
        return 1; // not found
    
    if (type == NP_TYPE_BONUS) { // size of bonus partition
        info->count = GetNandSizeSectors() - info->sector;
    } else if (type == NP_TYPE_FAT) { // FAT type specific stuff
        ReadNandSectors(header, info->sector, 1, info->keyslot);
        MbrHeader* mbr = (MbrHeader*) header;
        if ((ValidateMbrHeader(mbr) != 0) || (index >= 4) ||
            (mbr->partitions[index].sector == 0) || (mbr->partitions[index].count == 0) ||
            (mbr->partitions[index].sector + mbr->partitions[index].count > info->count))
            return 1;
        info->sector += mbr->partitions[index].sector;
        info->count = mbr->partitions[index].count;
    }
    
    return 0;
}
