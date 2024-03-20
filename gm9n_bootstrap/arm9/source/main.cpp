/*-----------------------------------------------------------------

 Copyright (C) 2010  Dave "WinterMute" Murphy

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU General Public License
 as published by the Free Software Foundation; either version 2
 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program; if not, write to the Free Software
 Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.

------------------------------------------------------------------*/

#include <nds.h>
#include <nds/arm9/decompress.h>
#include <fat.h>
#include <limits.h>

#include <stdio.h>
#include <stdarg.h>

#include "inifile.h"
#include "nds_loader_arm9.h"
// #include "crc.h"
// #include "nds_card.h"

#include "topLoad.h"
#include "topError.h"
#include "subError.h"
#include "subPrompt.h"

#define CONSOLE_SCREEN_WIDTH 32
#define CONSOLE_SCREEN_HEIGHT 24

static bool ScreenInit = false;

void vramcpy_ui (void* dest, const void* src, int size) 
{
	u16* destination = (u16*)dest;
	u16* source = (u16*)src;
	while (size > 0) {
		*destination++ = *source++;
		size-=2;
	}
}

void BootSplashInit() {
	if (ScreenInit)return;
	videoSetMode(MODE_0_2D | DISPLAY_BG0_ACTIVE);
	videoSetModeSub(MODE_0_2D | DISPLAY_BG0_ACTIVE);
	vramSetBankA (VRAM_A_MAIN_BG_0x06000000);
	vramSetBankC (VRAM_C_SUB_BG_0x06200000);
	REG_BG0CNT = BG_MAP_BASE(0) | BG_COLOR_256 | BG_TILE_BASE(2);
	REG_BG0CNT_SUB = BG_MAP_BASE(0) | BG_COLOR_256 | BG_TILE_BASE(2);
	BG_PALETTE[0]=0;
	BG_PALETTE[255]=0xffff;
	u16* bgMapTop = (u16*)SCREEN_BASE_BLOCK(0);
	u16* bgMapSub = (u16*)SCREEN_BASE_BLOCK_SUB(0);
	for (int i = 0; i < CONSOLE_SCREEN_WIDTH*CONSOLE_SCREEN_HEIGHT; i++) { bgMapTop[i] = (u16)i; bgMapSub[i] = (u16)i; }
	ScreenInit = true;
}

void CartridgePrompt() {
	BootSplashInit();
	// Display Load Screen
	decompress((void*)topLoadTiles, (void*)CHAR_BASE_BLOCK(2), LZ77Vram);
	decompress((void*)subPromptTiles, (void*)CHAR_BASE_BLOCK_SUB(2), LZ77Vram);
	vramcpy_ui (&BG_PALETTE[0], topLoadPal, topLoadPalLen);
	vramcpy_ui (&BG_PALETTE_SUB[0], subPromptPal, subPromptPalLen);
	for (int i = 0; i < 20; i++) { swiWaitForVBlank(); }
}


ITCM_CODE void CheckSlot() {
	if (REG_SCFG_MC == 0x11) {
		do { CartridgePrompt(); }
		while (REG_SCFG_MC == 0x11);
		disableSlot1();
		for (int i = 0; i < 25; i++) { swiWaitForVBlank(); }
		enableSlot1();	
	} else {
		if(REG_SCFG_MC == 0x10) { 
			disableSlot1();
			for (int i = 0; i < 25; i++) { swiWaitForVBlank(); }
			enableSlot1();
		}
	}
}

ITCM_CODE int main( int argc, char **argv) {
	defaultExceptionHandler();
	if (fatInitDefault()) {
		CIniFile GM9NBootstrap( "/_nds/GM9N_Bootstrap.ini" );		
		std::string	ndsPath = GM9NBootstrap.GetString( "GM9N_BOOTSTRAP", "SRL", "/NDS/GodMode9Nrio.nds");
		CheckSlot();
		runNdsFile(ndsPath.c_str(), 0, NULL);
	} else {
		BootSplashInit();
		// Display Error Screen
		decompress((void*)topErrorTiles, (void*)CHAR_BASE_BLOCK(2), LZ77Vram);
		decompress((void*)subErrorTiles, (void*)CHAR_BASE_BLOCK_SUB(2), LZ77Vram);
		vramcpy_ui (&BG_PALETTE[0], topErrorPal, topErrorPalLen);
		vramcpy_ui (&BG_PALETTE_SUB[0], subErrorPal, subErrorPalLen);
	}
	while(1) { swiWaitForVBlank(); }
}

