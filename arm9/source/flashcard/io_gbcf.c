#include <nds/ndstypes.h>
#include <nds/system.h>
	
//---------------------------------------------------------------
// DMA
#ifdef _IO_USE_DMA
	#include <nds/dma.h>
	#include <nds/arm9/cache.h>
#endif

#define BYTES_PER_READ 512

#define REG_EXMEMCNT (*(vu16*)0x04000204)
#define CARD_TIMEOUT	0x00989680				// Updated due to suggestion from SaTa, otherwise card will timeout sometimes on a write
#define CARD_INITTIMEOUT	0x020000			// Shorter time out used during startup.

static bool ShortTimeout = true;

//---------------------------------------------------------------
// CF Addresses (MPCF/SCCF values by default)
static vu16 *CF_STATUS = (vu16*)0x098C0000;			// Status of the CF Card / Device control
static vu16 *CF_FEATURES = (vu16*)0x09020000; 		// Errors / Features
static vu16 *CF_COMMAND = (vu16*)0x090E0000;		// Commands sent to control chip and status return

static vu16 *CF_SECTOR_COUNT = (vu16*)0x09040000;	// Number of sector to transfer
static vu16 *CF_SECTOR_NO = (vu16*)0x09060000;		// 1st byte of sector address
static vu16 *CF_CYLINDER_LOW = (vu16*)0x09080000;	// 2nd byte of sector address
static vu16 *CF_CYLINDER_HIGH = (vu16*)0x090A0000;	// 3rd byte of sector address
static vu16 *CF_SEL_HEAD = (vu16*)0x090C0000;		// last nibble of sector address | 0xE0

static vu16 *CF_DATA = (vu16*)0x09000000;			// Pointer to buffer of CF data transered from card

// Unlock Registers (Not used by MPCF/MMCF)
static vu16 *SC_UnlockAddress = (vu16*)0x09FFFFFE; // SC by default

//---------------------------------------------------------------
// CF Commands
#define CF_STS_40	(u8)0x40
#define CF_STS_BUSY	(u8)0x80

// Mode Switch Registers (SC only)
#define SC_MODE_RAM 0x5
#define SC_MODE_MEDIA 0x3 
#define SC_MODE_RAM_RO 0x1

// Values for changing mode (M3 Only)
#define M3_MODE_ROM 0x00400004
#define M3_MODE_MEDIA 0x00400003 


#ifdef _IO_USEFASTCNT
inline u16 setFastCNT(u16 originData) {
	//  2-3   32-pin GBA Slot ROM 1st Access Time (0-3 = 10, 8, 6, 18 cycles)
	//    4     32-pin GBA Slot ROM 2nd Access Time (0-1 = 6, 4 cycles)
    const u16 mask = ~(7<<2);//~ 000011100, clear bit 2-3 + 4
    const u16 setVal = ((2) << 2) | (1<<4);
    return (originData & mask) | setVal;
}
#endif

/*-----------------------------------------------------------------
changeMode (was SC_Unlock)
Added by MightyMax
Modified by Chishm
Modified again by loopy
1=ram(readonly), 5=ram, 3=SD interface?
-----------------------------------------------------------------*/
void SC_ChangeMode(u8 mode) {
	*SC_UnlockAddress = 0xA55A;
	*SC_UnlockAddress = 0xA55A;
	*SC_UnlockAddress = mode;
	*SC_UnlockAddress = mode;
} 

static u16 M3_ReadHalfWord (u32 addr) { return *((vu16*)addr); }

void M3_ChangeMode(u32 mode) {
	M3_ReadHalfWord(0x08e00002);
	M3_ReadHalfWord(0x0800000e);
	M3_ReadHalfWord(0x08801ffc);
	M3_ReadHalfWord(0x0800104a);
	M3_ReadHalfWord(0x08800612);
	M3_ReadHalfWord(0x08000000);
	M3_ReadHalfWord(0x08801b66);
	M3_ReadHalfWord(0x08000000 + (mode << 1));
	M3_ReadHalfWord(0x0800080e);
	M3_ReadHalfWord(0x08000000);
	if ((mode & 0x0f) != 4) {
		M3_ReadHalfWord(0x09000000);
	} else {
		M3_ReadHalfWord(0x080001e4);
		M3_ReadHalfWord(0x080001e4);
		M3_ReadHalfWord(0x08000188);
		M3_ReadHalfWord(0x08000188);
	}
}


static bool CF_Block_Ready(void) {
	u32 i = 0;
	u32 TIMEOUT = CARD_TIMEOUT;
	if (ShortTimeout)TIMEOUT = CARD_INITTIMEOUT; // Use shorter time out to speed up initial init
	
	while ((*CF_STATUS & CF_STS_BUSY) && (i < TIMEOUT)) {
		i++;
		while ((!(*CF_STATUS & CF_STS_40)) && (i < TIMEOUT)) { i++; }
	} 
	
	if (i >= TIMEOUT)return false;
			
	return true;
}


static bool CF_Set_Features(u32 feature) {
	if (!CF_Block_Ready())return false;
	
	*CF_FEATURES = feature;
	*CF_SECTOR_COUNT = 0x00;  // config???
	*CF_SEL_HEAD = 0x00;
	*CF_COMMAND = 0xEF;
	
	return true;
}


/*-----------------------------------------------------------------
CF_IsInserted
Is a compact flash card inserted?
bool return OUT:  true if a CF card is inserted
-----------------------------------------------------------------*/
bool CF_IsInserted(void) {
	return (CF_Set_Features(0xAA));
}

/*-----------------------------------------------------------------
CF_FindCardType
Try and determine brand slot-2 card being used.
-----------------------------------------------------------------*/
bool CF_FindCardType(void) {
	#ifdef _IO_SCCF
	ShortTimeout = false;
	goto IOSCCF;
	#endif
	#ifdef _IO_M3CF
	ShortTimeout = false;
	goto IOM3CF;
	#endif
	#ifdef _IO_MMCF
	goto IOMMCF;
	#endif
	// Try default values first (for MPCF)
	if (CF_IsInserted())return true;
		
	IOSCCF:
	// Try SCCF (This cart uses same registers as MPCF but with additional mode switch registers)
	SC_ChangeMode(SC_MODE_MEDIA);
	if (CF_IsInserted())return true;
		
	IOM3CF:
	// Try M3CF Registers
	CF_STATUS = (vu16*)0x080C0000;
	CF_FEATURES = (vu16*)0x08820000;
	CF_COMMAND = (vu16*)0x088E0000;
	CF_SECTOR_COUNT = (vu16*)0x08840000;
	CF_SECTOR_NO = (vu16*)0x08860000;
	CF_CYLINDER_LOW = (vu16*)0x08880000;
	CF_CYLINDER_HIGH = (vu16*)0x088A0000;
	CF_SEL_HEAD = (vu16*)0x088C0000;
	CF_DATA = (vu16*)0x08800000;
	
	M3_ChangeMode(M3_MODE_MEDIA);
	if (CF_IsInserted())return true;
	
	IOMMCF:
	
	// Try MMCF Registers
	CF_STATUS = (vu16*)0x080E0000;
	CF_FEATURES = (vu16*)0x08020000;
	CF_COMMAND = (vu16*)0x080E0000;
	CF_SECTOR_COUNT = (vu16*)0x08040000;
	CF_SECTOR_NO = (vu16*)0x08060000;
	CF_CYLINDER_LOW = (vu16*)0x08080000;
	CF_CYLINDER_HIGH = (vu16*)0x080A0000;
	CF_SEL_HEAD = (vu16*)0x080C0000;
	CF_DATA = (vu16*)0x09000000;
	
	ShortTimeout = false;
	
	if (CF_IsInserted())return true;
	
	return false;
}


/*-----------------------------------------------------------------
CF_StartUp
initializes the CF interface, returns true if successful,
otherwise returns false
-----------------------------------------------------------------*/
bool CF_StartUp(void) {
	bool Result = CF_FindCardType();
	ShortTimeout = false;
	return Result;
}


/*-----------------------------------------------------------------
CF_ClearStatus
Tries to make the CF card go back to idle mode
bool return OUT:  true if a CF card is idle
-----------------------------------------------------------------*/
bool CF_ClearStatus(void) { return CF_Block_Ready(); }

/*-----------------------------------------------------------------
CF_Shutdown
unload the GBAMP CF interface
-----------------------------------------------------------------*/
bool CF_Shutdown(void) { return CF_Block_Ready(); }

bool ReadSectors (u32 sector, int numSecs, u16* buff) {
	int i;
#ifdef _IO_ALLOW_UNALIGNED
	u8 *buff_u8 = (u8*)buff;
	int temp;
#endif

#if (defined _IO_USE_DMA) && (defined NDS) && (defined ARM9)
	#ifdef _IO_ALLOW_UNALIGNED
	DC_FlushRange(buff_u8, numSecs * BYTES_PER_READ);
	#else
	DC_FlushRange(buff, numSecs * BYTES_PER_READ);
	#endif
#endif

	if (!CF_Block_Ready())return false;
	
	*CF_SECTOR_COUNT = (numSecs == 256) ? 0 : numSecs;
	*CF_SECTOR_NO = sector;
	*CF_CYLINDER_LOW = sector >> 8;
	*CF_CYLINDER_HIGH = sector >> 16;
	*CF_SEL_HEAD = ((sector >> 24) & 0x0F) | 0xE0;
	*CF_COMMAND = 0x20; // read sectors

	while (numSecs--) {
		if (!CF_Block_Ready())return false;
#ifdef _IO_USE_DMA
	#ifdef NDS
		DMA3_SRC = (u32*)CF_DATA;
		DMA3_DEST = (u32)buff;
		DMA3_CR = 256 | DMA_COPY_HALFWORDS | DMA_SRC_FIX;
	#else
		DMA3COPY (*CF_DATA, buff, 256 | DMA16 | DMA_ENABLE | DMA_SRC_FIXED);
	#endif
		buff += BYTES_PER_READ / 2;
#elif defined _IO_ALLOW_UNALIGNED
		i=256;
		if ((u32)buff_u8 & 1) {
			while(i--) {
				// if (!CF_Block_Ready())return false;
				temp = *CF_DATA;
				*buff_u8++ = temp & 0xFF;
				*buff_u8++ = temp >> 8;
			}
		} else {
			while(i--)*buff++ = *CF_DATA;
		}
#else
		i=256;
		while(i--)*buff++ = *CF_DATA;
#endif
	}

#if (defined _IO_USE_DMA) && (defined NDS)
	// Wait for end of transfer before returning
	while(DMA3_CR & DMA_BUSY);
#endif

	return true;
}


bool WriteSectors(u32 sector, int numSecs, u16* buff) {
		
	int i;
#ifdef _IO_ALLOW_UNALIGNED
	u8 *buff_u8 = (u8*)buff;
	int temp;
#endif
	
#if (defined _IO_USE_DMA) && (defined NDS) && (defined ARM9)
	#ifdef _IO_ALLOW_UNALIGNED
	DC_FlushRange(buff_u8, numSecs * BYTES_PER_READ);
	#else
	DC_FlushRange(buff, numSecs * BYTES_PER_READ);
	#endif
#endif

	if (!CF_Block_Ready())return false;
	
	*CF_SECTOR_COUNT = (numSecs == 256) ? 0 : numSecs;
	*CF_SECTOR_NO = sector;
	*CF_CYLINDER_LOW = sector >> 8;
	*CF_CYLINDER_HIGH = sector >> 16;
	*CF_SEL_HEAD = ((sector >> 24) & 0x0F) | 0xE0;
	*CF_COMMAND = 0x30; // write sectors
	
	while (numSecs--) {
		if (!CF_Block_Ready())return false;

#ifdef _IO_USE_DMA
	#ifdef NDS
		DMA3_SRC = (u32*)buff;
		DMA3_DEST = (u32)CF_DATA;
		DMA3_CR = 256 | DMA_COPY_HALFWORDS | DMA_DST_FIX;
	#else
		DMA3COPY(buff, CF_DATA, 256 | DMA16 | DMA_ENABLE | DMA_DST_FIXED);
	#endif
		buff += BYTES_PER_READ / 2;
#elif defined _IO_ALLOW_UNALIGNED
		i=256;
		if ((u32)buff_u8 & 1) {
			while(i--) {
				// if (!CF_Block_Ready())return false;
				temp = *buff_u8++;
				temp |= *buff_u8++ << 8;
				*CF_DATA = temp;
			}
		} else {
			while(i--)*CF_DATA = *buff++;
		}
#else
		i=256;
		while(i--)*CF_DATA = *buff++;
#endif
	}
#if defined _IO_USE_DMA && defined NDS
	// Wait for end of transfer before returning
	while(DMA3_CR & DMA_BUSY);
#endif
	return true;
}


/*-----------------------------------------------------------------
CF_ReadSectors
Read 512 byte sector numbered "sector" into "buffer"
u32 sector IN: address of first 512 byte sector on CF card to read
u32 numSecs IN: number of 512 byte sectors to read
void* buffer OUT: pointer to 512 byte buffer to store data in
bool return OUT: true if successful
-----------------------------------------------------------------*/
bool CF_ReadSectors(u32 sector, u32 numSecs, void* buffer) {
	bool Result = false;
#ifdef _IO_USEFASTCNT
	u16 originMemStat = REG_EXMEMCNT;
	REG_EXMEMCNT = setFastCNT(originMemStat);
#endif
	while (numSecs > 0) {
		int sector_count = (numSecs > 256) ? 256 : numSecs;
		Result = ReadSectors(sector, sector_count, (u16*)buffer);
		sector += sector_count;
		numSecs -= sector_count;
		buffer += (sector_count * BYTES_PER_READ);
	}
#ifdef _IO_USEFASTCNT
	REG_EXMEMCNT = originMemStat;
#endif	
	return Result;
}

/*-----------------------------------------------------------------
CF_WriteSectors
Write 512 byte sector numbered "sector" from "buffer"
u32 sector OUT: address of 512 byte sector on CF card to write
u32 numSecs OUT: number of 512 byte sectors to write
void* buffer IN: pointer to 512 byte buffer to write data to
bool return OUT: true if successful
-----------------------------------------------------------------*/
bool CF_WriteSectors(u32 sector, u32 numSecs, void* buffer) {
	bool Result = false;
#ifdef _IO_USEFASTCNT
	u16 originMemStat = REG_EXMEMCNT;
	REG_EXMEMCNT = setFastCNT(originMemStat);
#endif
	while (numSecs > 0) {
		int sector_count = (numSecs > 256) ? 256 : numSecs;
		Result = WriteSectors(sector, sector_count, (u16*)buffer);
		sector += sector_count;
		numSecs -= sector_count;
		buffer += (sector_count * BYTES_PER_READ);
	}
#ifdef _IO_USEFASTCNT
	REG_EXMEMCNT = originMemStat;
#endif	
	return Result;
}

