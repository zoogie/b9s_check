//#include "installer.h"
#include "ui.h"
#include "i2c.h"
#include "nand.h"
#include "sha.h"
#include "firms_bin.h"


void powerOff()
{
    i2cWriteRegister(I2C_DEV_MCU, 0x20, 1 << 0);
    while(true);
}

u8 *top_screen, *bottom_screen;

void main(int argc, char** argv)
{
	u8 buf[0x200];
	u32 sha[0x8];	
	char output[200]={0};
	char *FIRM[2];
	int is_b9s;	
	int found=0;
	
	// Fetch the framebuffer addresses
	if(argc >= 2) {
		// newer entrypoints
		u8 **fb = (u8 **)(void *)argv[1];
		top_screen = fb[0];
		bottom_screen = fb[2];
	} else {
		// outdated entrypoints
		top_screen = (u8*)(*(u32*)0x23FFFE00);
		bottom_screen = (u8*)(*(u32*)0x23FFFE08);
	}
	
	ClearScreenF(true, true, COLOR_STD_BG);
	InitNandCrypto();

	//ReadNandSectors(void* buffer, u32 sector, u32 count, u32 keyslot);
	for(int i=0; i < 2; i++){
		
		ReadNandSectors(buf,(i*0x400000+0xB130000)/0x200, 1, 0x6);
		sha_quick(sha, buf, 0x100, SHA256_MODE);
		is_b9s = memcmp("B9S", buf + 0x3D, 3);
		
		for(int h=0; h < firms_bin_size; h+=0x30){
			if(firms_bin[h+0x2f]){  //guarantee null terminator!
				FIRM[i]="String error";
				found=2;
				break;
			}
			if(!memcmp(sha, firms_bin + h, 0x8)){
				FIRM[i]=&firms_bin[h+8];
				found=1;
				break;
			}
		}
		
		if(!found){
			if(is_b9s) FIRM[i]="Unknown"; 
			else       FIRM[i]="B9S Nightly";			
		}

		found=0;
	}
	
	snprintf(output, 190, "--- b9s_check 2.0 - zoogie\nFIRM0: %s\nFIRM1: %s\n",FIRM[0], FIRM[1]);
	ShowPrompt(false, output);
	
	powerOff();
}
