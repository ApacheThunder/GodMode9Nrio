#ifndef IO_SCSD_H
#define IO_SCSD_H

#ifdef __cplusplus
extern "C" {
#endif

bool _SCSD_startUp (void);
bool _SCSD_isInserted (void);
bool _SCSD_readSectors (u32 sector, u32 numSectors, void* buffer);
bool _SCSD_writeSectors (u32 sector, u32 numSectors, const void* buffer);
bool _SCSD_clearStatus (void);
bool _SCSD_shutdown (void);


#ifdef __cplusplus
}
#endif

#endif

