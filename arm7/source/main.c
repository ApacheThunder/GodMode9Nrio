/*---------------------------------------------------------------------------------

	default ARM7 core

		Copyright (C) 2005 - 2010
		Michael Noland (joat)
		Jason Rogers (dovoto)
		Dave Murphy (WinterMute)

	This software is provided 'as-is', without any express or implied
	warranty.  In no event will the authors be held liable for any
	damages arising from the use of this software.

	Permission is granted to anyone to use this software for any
	purpose, including commercial applications, and to alter it and
	redistribute it freely, subject to the following restrictions:

	1.	The origin of this software must not be misrepresented; you
		must not claim that you wrote the original software. If you use
		this software in a product, an acknowledgment in the product
		documentation would be appreciated but is not required.

	2.	Altered source versions must be plainly marked as such, and
		must not be misrepresented as being the original software.

	3.	This notice may not be removed or altered from any source
		distribution.

---------------------------------------------------------------------------------*/
#include <nds.h>
#include <string.h>

#include "gba.h"

#define SD_IRQ_STATUS (*(vu32*)0x400481C)

#define BASE_DELAY (100)

void my_installSystemFIFO(void);
void my_sdmmc_get_cid(int devicenumber, u32 *cid);

u8 my_i2cReadRegister(u8 device, u8 reg);
u8 my_i2cWriteRegister(u8 device, u8 reg, u8 data);

extern bool __dsimode;
bool forceNTRMode = true;


void EnableSlot1() {
	int oldIME = enterCriticalSection();
	while((REG_SCFG_MC & 0x0c) == 0x0c) swiDelay(1 * BASE_DELAY);
	if(!(REG_SCFG_MC & 0x0c)) {
		REG_SCFG_MC = (REG_SCFG_MC & ~0x0c) | 4;
		swiDelay(10 * BASE_DELAY);
		REG_SCFG_MC = (REG_SCFG_MC & ~0x0c) | 8;
		swiDelay(10 * BASE_DELAY);
	}
	leaveCriticalSection(oldIME);
}

void DisableSlot1() {
	int oldIME = enterCriticalSection();

	while((REG_SCFG_MC & 0x0c) == 0x0c) swiDelay(1 * BASE_DELAY);

	if((REG_SCFG_MC & 0x0c) == 8) {

		REG_SCFG_MC = (REG_SCFG_MC & ~0x0c) | 0x0c;
		while((REG_SCFG_MC & 0x0c) != 0) swiDelay(1 * BASE_DELAY);
	}

	leaveCriticalSection(oldIME);
}

//---------------------------------------------------------------------------------
void ReturntoDSiMenu() {
//---------------------------------------------------------------------------------
	if (isDSiMode()) {
		my_i2cWriteRegister(0x4A, 0x70, 0x01);		// Bootflag = Warmboot/SkipHealthSafety
		my_i2cWriteRegister(0x4A, 0x11, 0x01);		// Reset to DSi Menu
	} else {
		u8 readCommand = readPowerManagement(0x10);
		readCommand |= BIT(0);
		writePowerManagement(0x10, readCommand);
	}
}

void VblankHandler(void) { if(fifoCheckValue32(FIFO_USER_02))ReturntoDSiMenu(); }

void VcountHandler() { inputGetAndSend(); }

volatile bool exitflag = false;

void powerButtonCB() { exitflag = true; }

//---------------------------------------------------------------------------------
int main() {
//---------------------------------------------------------------------------------
	*(vu32*)0x400481C = 0;				// Clear SD IRQ stat register
	*(vu32*)0x4004820 = 0;				// Clear SD IRQ mask register

	if (REG_SNDEXTCNT != 0) {
		my_i2cWriteRegister(0x4A, 0x12, 0x00);	// Press power-button for auto-reset
		my_i2cWriteRegister(0x4A, 0x70, 0x01);	// Bootflag = Warmboot/SkipHealthSaf
	}

	// clear sound registers
	dmaFillWords(0, (void*)0x04000400, 0x100);

	REG_SOUNDCNT |= SOUND_ENABLE;
	writePowerManagement(PM_CONTROL_REG, (readPowerManagement(PM_CONTROL_REG) & ~PM_SOUND_MUTE ) | PM_SOUND_AMP);
	powerOn(POWER_SOUND);

	readUserSettings();
	ledBlink(0);

	irqInit();
	// Start the RTC tracking IRQ
	initClockIRQ();

	touchInit();

	fifoInit();
	
	SetYtrigger(80);
	
	my_installSystemFIFO();

	irqSet(IRQ_VCOUNT, VcountHandler);
	irqSet(IRQ_VBLANK, VblankHandler);

	irqEnable( IRQ_VBLANK | IRQ_VCOUNT );

	setPowerButtonCB(powerButtonCB);

	// Check for 3DS
	if(isDSiMode() || (REG_SCFG_EXT & BIT(22))) {
		u8 byteBak = my_i2cReadRegister(0x4A, 0x71);
		my_i2cWriteRegister(0x4A, 0x71, 0xD2);
		fifoSendValue32(FIFO_USER_05, my_i2cReadRegister(0x4A, 0x71));
		my_i2cWriteRegister(0x4A, 0x71, byteBak);
	}
	
	// bool scfgUnlocked = false;
	if (forceNTRMode && (REG_SCFG_EXT & BIT(31))) {
		__dsimode = false;
		REG_MBK9=0xFCFFFF0F;
		REG_MBK6=0x09403900;
		REG_MBK7=0x09803940;
		REG_MBK8=0x09C03980;
		// if (!(REG_SCFG_MC & BIT(0)) || !(REG_SCFG_MC & BIT(2)))EnableSlot1();
		// if (!(REG_SCFG_CLK & BIT(0)))REG_SCFG_CLK |= BIT(0);
		DisableSlot1();
		for (int i = 0; i < 10; i++) { while(REG_VCOUNT!=191); while(REG_VCOUNT==191); }
		EnableSlot1();
		
		if (REG_SCFG_ROM != 0x703) {
			REG_SCFG_ROM = 0x703;
			for (int i = 0; i < 10; i++) { while(REG_VCOUNT!=191); while(REG_VCOUNT==191); }
		}
		
		REG_SCFG_CLK = 0x187;
		REG_SCFG_EXT = 0x92A40000;
		for (int i = 0; i < 10; i++) { while(REG_VCOUNT!=191); while(REG_VCOUNT==191); }
		// scfgUnlocked = true;
	}
	
	fifoSendValue32(FIFO_USER_03, REG_SCFG_EXT);
	fifoSendValue32(FIFO_USER_07, *(u16*)(0x4004700));
	fifoSendValue32(FIFO_USER_06, 1);
		
	// Keep the ARM7 mostly idle
	while (!exitflag) {
		if ( 0 == (REG_KEYINPUT & (KEY_SELECT | KEY_START | KEY_L | KEY_R)))exitflag = true;
		if (*(u32*)(0x2FFFD0C) == 0x454D4D43) {
			my_sdmmc_get_cid(true, (u32*)0x2FFD7BC);	// Get eMMC CID
			*(u32*)(0x2FFFD0C) = 0;
		}
		resyncClock();
		// Send SD status
		if(isDSiMode() || *(u16*)(0x4004700) != 0)fifoSendValue32(FIFO_USER_04, SD_IRQ_STATUS);
		// Dump EEPROM save
		if(fifoCheckAddress(FIFO_USER_01)) {
			switch(fifoGetValue32(FIFO_USER_01)) {
				case 0x44414552: // 'READ'
					readEeprom((u8 *)fifoGetAddress(FIFO_USER_01), fifoGetValue32(FIFO_USER_01), fifoGetValue32(FIFO_USER_01));
					break;
				case 0x54495257: // 'WRIT'
					writeEeprom(fifoGetValue32(FIFO_USER_01), (u8 *)fifoGetAddress(FIFO_USER_01), fifoGetValue32(FIFO_USER_01));
					break;
			}
		}
		swiWaitForVBlank();
	}
	return 0;
}

