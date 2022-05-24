#include "safewrite.h"
#include "nand.h"
#include "sha.h"

// safe file writer function, warnings:
// (1) file must be opened in RW mode for this to work
// (2) contents of buffer may change on corruption
// (3) uses SHA register
u32 SafeWriteFile(FIL* file, void* buff, FSIZE_t ofs, UINT btw) {
    u8 sha_in[0x20];
    UINT bw;
    
    sha_quick(sha_in, buff, btw, SHA256_MODE);
    if ((f_lseek(file, ofs) != FR_OK) || (f_write(file, buff, btw, &bw) != FR_OK) || (btw != bw) ||
        (f_lseek(file, ofs) != FR_OK) || (f_read(file, buff, btw, &bw) != FR_OK) || (btw != bw))
        return 1;
    
    return (sha_cmp(sha_in, buff, btw, SHA256_MODE) == 0) ? 0 : 1;
}

// safe file quick writer function
// same as SafeWriteFile(), but also handles file create, too, same warnings apply
u32 SafeQWriteFile(const TCHAR* path, void* buff, UINT btw) {
    FIL fp;
    u32 ret = 0;
    if (f_open(&fp, path, FA_READ|FA_WRITE|FA_CREATE_ALWAYS) != FR_OK)
        return 1;
    ret = SafeWriteFile(&fp, buff, 0, btw);
    f_close(&fp);
    return ret;
}

// safe NAND writer function, warnings:
// (1) contents of buffer may change on corruption
// (2) uses SHA register
u32 SafeWriteNand(void* buff, u32 ofs, u32 btw, u32 keyslot) {
    u8 sha_in[0x20];
    
    sha_quick(sha_in, buff, btw, SHA256_MODE);
    if ((WriteNandBytes(buff, ofs, btw, keyslot) != 0) || (ReadNandBytes(buff, ofs, btw, keyslot) != 0))
        return 1;
    
    return (sha_cmp(sha_in, buff, btw, SHA256_MODE) == 0) ? 0 : 1;
}