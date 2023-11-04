#include <nds.h>
#include <nds/arm9/dldi.h>
#include <stdio.h>
#include <fat.h>
#include <sys/stat.h>
#include <limits.h>

#include <string.h>
#include <unistd.h>

#include "nds_loader_arm9.h"
#include "config.h"
#include "date.h"
#include "driveMenu.h"
#include "driveOperations.h"
#include "file_browse.h"
#include "fileOperations.h"
#include "font.h"
#include "language.h"
#include "my_sd.h"
// #include "nitrofs.h"
#include "tonccpy.h"
#include "version.h"

#include "gm9i_logo.h"

char titleName[64] = {" "};

int screenMode = 0;

bool appInited = false;
bool screenSwapped = false;

bool arm7SCFGLocked = false;
bool isRegularDS = true;
bool bios9iEnabled = false;
bool is3DS = false;
int ownNitroFSMounted;
std::string prevTime;

bool applaunch = false;

static int bg3;

//---------------------------------------------------------------------------------
void stop (void) {
//---------------------------------------------------------------------------------
	while (1) {	swiWaitForVBlank(); }
}

//---------------------------------------------------------------------------------
void vblankHandler (void) {
//---------------------------------------------------------------------------------
	// Check if NDS cart ejected
	if(isDSiMode() && (REG_SCFG_MC & BIT(0)) && romTitle[0][0] != '\0') {
		romTitle[0][0] = '\0';
		romSizeTrimmed = romSize[0] = 0;
	}

	// Check if GBA cart ejected
	if(isRegularDS && *(u8*)(0x080000B2) != 0x96 && romTitle[1][0] != '\0') {
		romTitle[1][0] = '\0';
		romSize[1] = 0;
	}

	// Print time
	std::string time = RetTime();
	if(time != prevTime) {
		prevTime = time;
		if(font) {
			font->print(lastCol, 0, true, time, alignEnd, Palette::blackGreen);
			font->update(true);
		}
	}
}

extern bool __dsimode;
bool forceNTRMode = true;

//---------------------------------------------------------------------------------
int main(int argc, char **argv) {
//---------------------------------------------------------------------------------
	if (forceNTRMode && (REG_SCFG_EXT & BIT(31))) {
		__dsimode = false;
		int i;
		// MBK settings for NTR mode games
		*((vu32*)REG_MBK1)=0x8D898581;
		*((vu32*)REG_MBK2)=0x91898581;
		*((vu32*)REG_MBK3)=0x91999591;
		*((vu32*)REG_MBK4)=0x91898581;
		*((vu32*)REG_MBK5)=0x91999591;
		REG_MBK6 = 0x00003000;
		REG_MBK7 = 0x00003000;
		REG_MBK8 = 0x00003000;
		// REG_SCFG_EXT=0x83000000;
		REG_SCFG_EXT &= ~(1UL << 14);
		REG_SCFG_EXT &= ~(1UL << 15);
		for (i = 0; i < 30; i++) { while(REG_VCOUNT!=191); while(REG_VCOUNT==191); }
		fifoWaitValue32(FIFO_USER_06);
		for (i = 0; i < 30; i++) { while(REG_VCOUNT!=191); while(REG_VCOUNT==191); }
		// REG_SCFG_EXT &= ~(1UL << 31);
	}
	
	// overwrite reboot stub identifier
	extern u64 *fake_heap_end;
	*fake_heap_end = 0;

	defaultExceptionHandler();

	std::string filename;
	
	bool yHeld = false;

	// sprintf(titleName, "GodMode9i %s", VER_NUMBER);
	sprintf(titleName, "GodMode9Nrio %s", "v3.4.3-NRIO");

	// initialize video mode
	videoSetMode(MODE_5_2D);
	videoSetModeSub(MODE_5_2D);

	// initialize VRAM banks
	vramSetPrimaryBanks(VRAM_A_MAIN_BG,
	                    VRAM_B_MAIN_SPRITE,
	                    VRAM_C_SUB_BG,
	                    VRAM_D_LCD);
	vramSetBankI(VRAM_I_SUB_SPRITE);

	// Init built-in font
	font = new Font(nullptr);

	// Display GM9i logo
	bg3 = bgInit(3, BgType_Bmp8, BgSize_B8_256x256, 0, 0);
	bgInit(2, BgType_Bmp8, BgSize_B8_256x256, 3, 0);
	bgInitSub(2, BgType_Bmp8, BgSize_B8_256x256, 3, 0);
	decompress(gm9i_logoBitmap, bgGetGfxPtr(bg3), LZ77Vram);
	tonccpy(BG_PALETTE, gm9i_logoPal, gm9i_logoPalLen);

	font->print(1, 1, false, titleName);
	font->print(1, 2, false, "----------------------------------------");
	font->print(1, 3, false, "https://github.com/DS-Homebrew/GodMode9i");

	fifoWaitValue32(FIFO_USER_06);
	if (fifoGetValue32(FIFO_USER_03) == 0) arm7SCFGLocked = true;
	u16 arm7_SNDEXCNT = fifoGetValue32(FIFO_USER_07);
	if (arm7_SNDEXCNT != 0)isRegularDS = false;	// If sound frequency setting is found, then the console is not a DS Phat/Lite
	fifoSendValue32(FIFO_USER_07, 0);

	if (isDSiMode()) {
		bios9iEnabled = true;
		if (!arm7SCFGLocked) {
			font->print(-2, -3, false, " Held - Disable cart access", Alignment::right);
			font->print(-2, -2, false, "Do this if it crashes here", Alignment::right);
		}
	}

	font->update(false);
	for (int i = 0; i < 60; i++) { swiWaitForVBlank(); }

	font->clear(false);
	font->print(1, 1, false, titleName);
	font->print(1, 2, false, "----------------------------------------");
	font->print(1, 3, false, "https://github.com/DS-Homebrew/GodMode9i");
	font->print(-2, -2, false, "Mounting drive(s)...", Alignment::right);
	font->update(false);

	sysSetCartOwner (BUS_OWNER_ARM9);	// Allow arm9 to access GBA ROM

	fifoSetValue32Handler(FIFO_USER_04, sdStatusHandler, nullptr);
	if (!sdRemoved)sdMounted = sdMount();
	
	if (!isDSiMode() || !yHeld) {
		flashcardMounted = flashcardMount();
		flashcardMountSkipped = false;
	}

	nitroMounted = false;
	
	// Ensure gm9i folder exists
	char folderPath[10];
	sprintf(folderPath, "%s:/gm9i", (sdMounted ? "sd" : "fat"));
	if ((sdMounted || flashcardMounted) && access(folderPath, F_OK) != 0)mkdir(folderPath, 0777);

	// Load config
	config = new Config();

	bgHide(bg3);

	// Reinit font, try to load default from SD this time
	delete font;
	if(access(config->fontPath().c_str(), F_OK) == 0)
		font = new Font(config->fontPath().c_str());
	else if(config->languageIniPath().substr(17, 2) == "zh")
		font = new Font("nitro:/fonts/misaki-gothic-8x8.frf");
	else
		font = new Font(nullptr);

	// Load translations
	langInit(false);

	keysSetRepeat(25,5);

	// Top bar
	font->printf(0, 0, true, Alignment::left, Palette::blackGreen, "%*c", 256 / font->width(), ' ');

	// Enable vblank handler
	irqSet(IRQ_VBLANK, vblankHandler);

	appInited = true;

	while(1) {

		if (screenMode == 0) {
			driveMenu();
		} else {
			filename = browseForFile();
		}

		if (applaunch) {
			// Construct a command line
			char filePath[PATH_MAX];
			getcwd(filePath, PATH_MAX);
			int pathLen = strlen(filePath);
			if(pathLen < PATH_MAX && filePath[pathLen - 1] != '/') {
				filePath[pathLen] = '/';
				filePath[pathLen + 1] = '\0';
				pathLen++;
			}

			std::vector<char*> argarray;

			if ((strcasecmp (filename.c_str() + filename.size() - 5, ".argv") == 0)
			|| (strcasecmp (filename.c_str() + filename.size() - 5, ".ARGV") == 0)) {

				FILE *argfile = fopen(filename.c_str(),"rb");
				char str[PATH_MAX], *pstr;
				const char seps[]= "\n\r\t ";

				while( fgets(str, PATH_MAX, argfile) ) {
					// Find comment and end string there
					if( (pstr = strchr(str, '#')) )
						*pstr= '\0';

					// Tokenize arguments
					pstr= strtok(str, seps);

					while( pstr != NULL ) {
						argarray.push_back(strdup(pstr));
						pstr= strtok(NULL, seps);
					}
				}
				fclose(argfile);
				filename = argarray[0];
			} else {
				argarray.push_back(strdup(filename.c_str()));
			}

			if (extension(filename, {"nds", "dsi", "ids", "app", "srl"})) {
				char *name = argarray[0];
				strcpy(filePath + pathLen, name);
				free(argarray[0]);
				argarray[0] = filePath;
				font->clear(false);
				font->printf(firstCol, 0, false, alignStart, Palette::white, STR_RUNNING_X_WITH_N_PARAMETERS.c_str(), argarray[0], argarray.size());
				int err = runNdsFile(argarray[0], argarray.size(), (const char **)&argarray[0]);
				font->printf(firstCol, 1, false, alignStart, Palette::white, STR_START_FAILED_ERROR_N.c_str(), err);
			}

			if (extension(filename, {"firm"})) {
				char *name = argarray[0];
				strcpy(filePath + pathLen, name);
				free(argarray[0]);
				argarray[0] = filePath;
				fcopy(argarray[0], "sd:/bootonce.firm");
				fifoSendValue32(FIFO_USER_02, 1);	// Reboot into selected .firm payload
				swiWaitForVBlank();
			}

			while(argarray.size() !=0 ) {
				free(argarray[0]);
				argarray.erase(argarray.begin());
			}

			while (1) {
				swiWaitForVBlank();
				scanKeys();
				if (!(keysHeld() & KEY_A)) break;
			}
		}

	}

	return 0;
}

