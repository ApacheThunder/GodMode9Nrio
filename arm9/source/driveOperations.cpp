#include "driveOperations.h"

#include <nds.h>
#include <nds/arm9/dldi.h>
#include <dirent.h>
#include <fat.h>
#include <stdio.h>
#include <string>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include "main.h"
#include "lzss.h"
#include "my_sd.h"
#include "imgio.h"
#include "tonccpy.h"
#include "language.h"

#include "read_card.h"


#define NDS_HEADER 0x027FFE00
ALIGN(4) static tNDSHeader* cartNds;
ALIGN(4) static sNDSHeaderExt* cartNdsExt;
u32 chipID;

static bool nrioMode = true;

bool nandMounted = false;
bool photoMounted = false;
bool sdMounted = false;
bool sdMountedDone = false;				// true if SD mount is successful once
bool flashcardMounted = false;
bool ramdriveMounted = false;
bool imgMounted = false;
bool nitroMounted = false;

Drive currentDrive = Drive::sdCard;
Drive nitroCurrentDrive = Drive::sdCard;
Drive imgCurrentDrive = Drive::sdCard;

char sdLabel[12];
char fatLabel[12];
char imgLabel[12];

u32 photoSize = 0;
u64 sdSize = 0;
u64 fatSize = 0;
u64 imgSize = 0;

const char* getDrivePath(void) {
	switch (currentDrive) {
		case Drive::sdCard:
			return "sd:/";
		case Drive::flashcard:
			return "fat:/";
		case Drive::nitroFS:
			return "nitro:/";
		case Drive::fatImg:
			return "img:/";
	}
	return "";
}

Drive getDriveFromPath(const char *path) {
	if(strncmp(path, "sd:", 3) == 0) {
		return Drive::sdCard;
	} else if(strncmp(path, "fat:", 4) == 0) {
		return Drive::flashcard;
	} else if(strncmp(path, "nitro:", 6)) {
		return Drive::nitroFS;
	} else if(strncmp(path, "img:", 4)) {
		return Drive::fatImg;
	}
	return currentDrive;
}

void fixLabel(char* label) {
	for (int i = strlen(label) - 1; i >= 0; i--) {
		if(label[i] != ' ') {
			label[i + 1] = '\0';
			break;
		}
	}
}

bool photoFound(void) {
	return (access("photo:/", F_OK) == 0);
}

bool sdFound(void) {
	return (access("sd:/", F_OK) == 0);
}

bool flashcardFound(void) {
	return (access("fat:/", F_OK) == 0);
}

bool bothSDandFlashcard(void) {
	if (sdMounted && flashcardMounted) {
		return true;
	} else {
		return false;
	}
}

bool imgFound(void) {
	return (access("img:/", F_OK) == 0);
}

bool sdMount(void) {
	fatMountSimple("sd", __my_io_dsisd());
	if (sdFound()) {
		sdMountedDone = true;
		fatGetVolumeLabel("sd", sdLabel);
		fixLabel(sdLabel);
		struct statvfs st;
		if (statvfs("sd:/", &st) == 0) {
			sdSize = st.f_bsize * st.f_blocks;
		}
		return true;
	}
	return false;
}

u64 getBytesFree(const char* drivePath) {
    struct statvfs st;
    statvfs(drivePath, &st);
    return (u64)st.f_bsize * (u64)st.f_bavail;
}

void sdUnmount(void) {
	if(imgMounted && imgCurrentDrive == Drive::sdCard)
		imgUnmount();
	if(nitroMounted && nitroCurrentDrive == Drive::sdCard)
		nitroUnmount();

	fatUnmount("sd");
	my_sdio_Shutdown();
	sdLabel[0] = '\0';
	sdSize = 0;
	sdMounted = false;
}

DLDI_INTERFACE* dldiLoadFromBin (const u8 dldiAddr[]) {
	// Check that it is a valid DLDI
	if (!dldiIsValid ((DLDI_INTERFACE*)dldiAddr)) {
		return NULL;
	}

	DLDI_INTERFACE* device = (DLDI_INTERFACE*)dldiAddr;
	size_t dldiSize;

	// Calculate actual size of DLDI
	// Although the file may only go to the dldiEnd, the BSS section can extend past that
	if (device->dldiEnd > device->bssEnd) {
		dldiSize = (char*)device->dldiEnd - (char*)device->dldiStart;
	} else {
		dldiSize = (char*)device->bssEnd - (char*)device->dldiStart;
	}
	dldiSize = (dldiSize + 0x03) & ~0x03; 		// Round up to nearest integer multiple
	
	// Clear unused space
	toncset(device+dldiSize, 0, 0x4000-dldiSize);

	dldiFixDriverAddresses (device);

	if (device->ioInterface.features & FEATURE_SLOT_GBA) {
		sysSetCartOwner(BUS_OWNER_ARM9);
	}
	if (device->ioInterface.features & FEATURE_SLOT_NDS) {
		sysSetCardOwner(BUS_OWNER_ARM9);
	}
	
	return device;
}

const DISC_INTERFACE *dldiGet(void) {
	if(io_dldi_data->ioInterface.features & FEATURE_SLOT_GBA)
		sysSetCartOwner(BUS_OWNER_ARM9);
	if(io_dldi_data->ioInterface.features & FEATURE_SLOT_NDS)
		sysSetCardOwner(BUS_OWNER_ARM9);

	return &io_dldi_data->ioInterface;
}

static void NRIOMount() {
	cartNds = (tNDSHeader*)NDS_HEADER;
	cardInit(cartNdsExt);
	chipID = cardGetId();
	
	// ALIGN(4) tDSiHeader* twlHeaderTemp = (tDSiHeader*)TMP_HEADER;
	// cartNds = (tNDSHeader*)NDS_HEADER;
	for(int i = 0; i < 25; i++) { swiWaitForVBlank(); }
	// cartNds = loadHeader(twlHeaderTemp); // copy twlHeaderTemp to ndsHeader location
	
	tonccpy((void*)NDS_HEADER, (u32*)cartNdsExt, 0x160);
	// tonccpy((void*)TWL_HEADER, (u32*)cartNdsExt, 0x160);
	
    // Set memory values expected by loaded NDS
    // from NitroHax, thanks to Chism
	*((u32*)0x027ff800) = chipID;					// CurrentCardID
	*((u32*)0x027ff804) = chipID;					// Command10CardID
	*((u16*)0x027ff808) = cartNds->headerCRC16;	// Header Checksum, CRC-16 of [000h-15Dh]
	*((u16*)0x027ff80a) = cartNds->secureCRC16;	// Secure Area Checksum, CRC-16 of [ [20h]..7FFFh]			
	*((u16*)0x027ff850) = 0x5835;
	
	*((u32*)0x027ffc00) = chipID;					// CurrentCardID
	*((u32*)0x027ffc04) = chipID;					// Command10CardID
	*((u16*)0x027ffc08) = cartNds->headerCRC16;	// Header Checksum, CRC-16 of [000h-15Dh]
	*((u16*)0x027ffc0a) = cartNds->secureCRC16;	// Secure Area Checksum, CRC-16 of [ [20h]..7FFFh]
	*((u16*)0x027ffc10) = 0x5835;
	*((u16*)0x027ffc40) = 0x1;						// Boot Indicator (Booted from card for SDK5) -- EXTREMELY IMPORTANT!!! Thanks to cReDiAr
	
	// tonccpy((void*)0x023FF000, (void*)0x027FF000, 0x1000);
	
}

bool flashcardMount(void) {
	if (nrioMode)NRIOMount();
	fatInitDefault();
	if (flashcardFound()) {
		fatGetVolumeLabel("fat", fatLabel);
		fixLabel(fatLabel);
		struct statvfs st;
		if (statvfs("fat:/", &st) == 0)fatSize = st.f_bsize * st.f_blocks;
		return true;
	}
	return false;
}

void flashcardUnmount(void) {
	if(imgMounted && imgCurrentDrive == Drive::flashcard)
		imgUnmount();
	if(nitroMounted && nitroCurrentDrive == Drive::flashcard)
		nitroUnmount();

	fatUnmount("fat");
	fatLabel[0] = '\0';
	fatSize = 0;
	flashcardMounted = false;
}

void nitroUnmount(void) {
	if(imgMounted && imgCurrentDrive == Drive::nitroFS)imgUnmount();
	ownNitroFSMounted = 2;
	nitroMounted = false;
}

bool imgMount(const char* imgName, bool dsiwareSave) {
	extern char currentImgName[PATH_MAX];

	strcpy(currentImgName, imgName);
	fatMountSimple("img", dsiwareSave ? &io_dsiware_save : &io_img);
	if (imgFound()) {
		fatGetVolumeLabel("img", imgLabel);
		fixLabel(imgLabel);
		struct statvfs st;
		if (statvfs("img:/", &st) == 0)imgSize = st.f_bsize * st.f_blocks;
		return true;
	}
	return false;
}

void imgUnmount(void) {
	if(nitroMounted && nitroCurrentDrive == Drive::fatImg)
		nitroUnmount();

	fatUnmount("img");
	img_shutdown();
	imgLabel[0] = '\0';
	imgSize = 0;
	imgMounted = false;
}

bool driveWritable(Drive drive) {
	switch(drive) {
		case Drive::sdCard:
			return __my_io_dsisd()->features & FEATURE_MEDIUM_CANWRITE;
		case Drive::flashcard:
			return dldiGet()->features & FEATURE_MEDIUM_CANWRITE;
		case Drive::nitroFS:
			return false;
		case Drive::fatImg:
			return io_img.features & FEATURE_MEDIUM_CANWRITE;
	}

	return false;
}

bool driveRemoved(Drive drive) {
	switch(drive) {
		case Drive::sdCard:
			return sdRemoved;
		case Drive::flashcard:
			return isDSiMode() ? REG_SCFG_MC & BIT(0) : !flashcardMounted;
		case Drive::nitroFS:
			return driveRemoved(nitroCurrentDrive);
		case Drive::fatImg:
			return driveRemoved(imgCurrentDrive);
	}

	return false;
}

u64 driveSizeFree(Drive drive) {
	switch(drive) {
		case Drive::sdCard:
			return getBytesFree("sd:/");
		case Drive::flashcard:
			return getBytesFree("fat:/");
		case Drive::nitroFS:
			return 0;
		case Drive::fatImg:
			return getBytesFree("img:/");
	}

	return 0;
}
