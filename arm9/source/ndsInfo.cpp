#include "ndsInfo.h"

#include "font.h"
#include "language.h"
#include "tonccpy.h"

#include <nds.h>
#include <stdio.h>

constexpr std::string *langNames[8] {
	&STR_JAPANESE,
	&STR_ENGLISH,
	&STR_FRENCH,
	&STR_GERMAN,
	&STR_ITALIAN,
	&STR_SPANISH,
	&STR_CHINESE,
	&STR_KOREAN
};

void ndsInfo(const char *path) {
	FILE *file = fopen(path, "rb");
	if(!file)
		return;

	char headerTitle[0xD] = {0};
	fread(headerTitle, 1, 0xC, file);

	char tid[5] = {0};
	fread(tid, 1, 4, file);

	u32 ofs;
	fseek(file, 0x68, SEEK_SET);
	fread(&ofs, sizeof(u32), 1, file);
	if(ofs < 0x8000 || fseek(file, ofs, SEEK_SET) != 0) {
		fclose(file);
		return;
	}

	u16 version;
	fread(&version, sizeof(u16), 1, file);

	u8 *iconBitmap = new u8[8 * 0x200];
	u16 *iconPalette = new u16[8 * 0x10];
	u16 *iconAnimation = new u16[0x40](); // Initialize to 0 for DS icons

	// Check CRC16s for ROM hacks with partly corrupted banners
	u16 crc16[4];
	u16 realCrc16[4];
	u8 *buffer = new u8[0x1180];
	fread(crc16, sizeof(u16), 4, file);

	fseek(file, ofs + 0x20, SEEK_SET);
	fread(buffer, 1, 0xA20, file);
	realCrc16[0] = swiCRC16(0xFFFF, buffer, 0x820);
	realCrc16[1] = swiCRC16(0xFFFF, buffer, 0x920);
	realCrc16[2] = swiCRC16(0xFFFF, buffer, 0xA20);

	fseek(file, ofs + 0x1240, SEEK_SET);
	fread(buffer, 1, 0x1180, file);
	realCrc16[3] = swiCRC16(0xFFFF, buffer, 0x1180);

	delete[] buffer;

	if(crc16[0] != realCrc16[0]) { // Base banner
		fclose(file);
		return;
	} else if(crc16[1] != realCrc16[1]) { // Chinese
		version = 0x0001;
	} else if(crc16[2] != realCrc16[2]) { // Korean
		version = 0x0002;
	}
	if(crc16[3] != realCrc16[3]) { // DSi
		version &= ~0x100;
	}

	if(version == 0x0103) { // DSi
		fseek(file, ofs + 0x1240, SEEK_SET);
		fread(iconBitmap, 1, 8 * 0x200, file);
		fread(iconPalette, 2, 8 * 0x10, file);
		fread(iconAnimation, 2, 0x40, file);

		fseek(file, ofs + 0x240, SEEK_SET);
	} else if((version & ~3) == 0) { // DS
		fseek(file, ofs + 0x20, SEEK_SET);
		fread(iconBitmap, 1, 0x200, file);
		fread(iconPalette, 2, 0x10, file);
	} else {
		fclose(file);
		return;
	}

	int languages = 5 + (version & 0x3);
	char16_t *titles = new char16_t[languages * 0x80];
	fread(titles, 2, languages * 0x80, file);

	fclose(file);

	oamInit(&oamSub, SpriteMapping_Bmp_1D_128, false);

	u16 *iconGfx = oamAllocateGfx(&oamSub, SpriteSize_32x32, SpriteColorFormat_16Color);
	oamSet(&oamSub, 0, rtl ? 4 : 256 - 36, 4, 0, 0, SpriteSize_32x32, SpriteColorFormat_16Color, iconGfx, -1, false, false, false, false, false);
	
	if(version == 0x0103) {
		tonccpy(iconGfx, iconBitmap + ((iconAnimation[0] >> 8) & 7) * 0x200, 0x200);
		tonccpy(SPRITE_PALETTE_SUB, iconPalette + ((iconAnimation[0] >> 0xB) & 7) * 0x10, 0x20);
		oamSetFlip(&oamSub, 0, iconAnimation[0] & BIT(14), iconAnimation[0] & BIT(15));
	} else {
		tonccpy(iconGfx, iconBitmap, 0x200);
		tonccpy(SPRITE_PALETTE_SUB, iconPalette, 0x20);
	}

	oamUpdate(&oamSub);

	u16 pressed = 0, held = 0;
	int animationFrame = 0, frameDelay = 0, lang = 1;
	while(1) {
		font->clear(false);
		font->printf(firstCol, 0, false, alignStart, Palette::white, STR_HEADER_TITLE.c_str(), headerTitle);
		font->printf(firstCol, 1, false, alignStart, Palette::white, STR_TITLE_ID.c_str(), tid);
		font->printf(firstCol, 2, false, alignStart, Palette::white, STR_TITLE_IN_LANGUAGE.c_str(), langNames[lang]->c_str());
		font->print(rtl ? -3 : 2, 3, false, titles + lang * 0x80, alignStart);
		font->update(false);

		do {
			swiWaitForVBlank();
			scanKeys();
			pressed = keysDown();
			held = keysDownRepeat();

			if(iconAnimation[animationFrame] && iconAnimation[animationFrame + 1] != 0x0100 && animationFrame < 0x40) {
				if(frameDelay < (iconAnimation[animationFrame] & 0xFF) - 1) {
					frameDelay++;
				} else {
					frameDelay = 0;
					if(!iconAnimation[++animationFrame])
						animationFrame = 0;

					tonccpy(iconGfx, iconBitmap + ((iconAnimation[animationFrame] >> 8) & 7) * 0x200, 0x200);
					tonccpy(SPRITE_PALETTE_SUB, iconPalette + ((iconAnimation[animationFrame] >> 0xB) & 7) * 0x10, 0x20);
					oamSetFlip(&oamSub, 0, iconAnimation[animationFrame] & BIT(14), iconAnimation[animationFrame] & BIT(15));
					oamUpdate(&oamSub);
				}
			}
		} while(!held);

		if(held & KEY_UP) {
			if(lang > 0)
				lang--;
		} else if(held & KEY_DOWN) {
			if(lang < languages - 1)
				lang++;
		} else if(pressed & KEY_B) {
			break;
		}
	}

	delete[] iconBitmap;
	delete[] iconPalette;
	delete[] iconAnimation;
	delete[] titles;

	oamFreeGfx(&oamSub, iconGfx);
	oamDisable(&oamSub);
}

