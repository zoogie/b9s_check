# b9s_check
Check b9s version and whatever else

### Instructions
* Just run the b9s_check.firm in a chainloader (aka Luma3DS + Start at boot) and it will show what b9s, fastboot3DS, or even Native Firm version you have installed in firm0 and firm1.
* To build just install the devkitpro.org 3DS toolchain and run make. If you wish to add detected firms, you may add such firms to FIRMS dir (use a descriptive name) and run firms.py with python 3 to rebuild the hash/title database. Then rebuild the main repo with make.

### Thanks
* d0k3 - This is currently based on stripped down version of https://github.com/d0k3/OpenFirmInstaller as an arm9 template
* v1.0 was based on https://github.com/d0k3/SafeB9SInstaller, but that fell out of favor due to nand crypto breaking at some point. I also lost the source for that version, oops.
