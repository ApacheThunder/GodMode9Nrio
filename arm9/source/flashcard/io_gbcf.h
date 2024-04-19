#ifndef IO_MMCF_H
#define IO_MMCF_H

#ifdef __cplusplus
extern "C" {
#endif

bool CF_IsInserted(void);
bool CF_StartUp(void);
bool CF_ClearStatus(void);
bool CF_Shutdown(void);

bool CF_ReadSectors(u32 sector, u32 numSecs, void* buffer);

/*-----------------------------------------------------------------
CF_WriteSectors
Write 512 byte sector numbered "sector" from "buffer"
u32 sector OUT: address of 512 byte sector on CF card to write
u32 numSecs OUT: number of 512 byte sectors to write
void* buffer IN: pointer to 512 byte buffer to write data to
bool return OUT: true if successful
-----------------------------------------------------------------*/
bool CF_WriteSectors(u32 sector, u32 numSecs, void* buffer);

#ifdef __cplusplus
}
#endif

#endif

