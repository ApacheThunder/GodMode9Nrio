#ifndef FLASHCARD_H
#define FLASHCARD_H

#include <string>
#include <nds/ndstypes.h>

enum class Drive : u8 {
	sdCard = 0,
	flashcard,
	nitroFS,
	fatImg
};

extern bool photoMounted;
extern bool sdMounted;
extern bool sdMountedDone;				// true if SD mount is successful once
extern bool flashcardMounted;
extern bool imgMounted;
extern bool nitroMounted;

extern Drive currentDrive;
extern Drive nitroCurrentDrive;
extern Drive imgCurrentDrive;

extern char sdLabel[12];
extern char fatLabel[12];
extern char imgLabel[12];

extern u64 sdSize;
extern u64 fatSize;
extern u64 imgSize;

extern const char* getDrivePath(void);
extern Drive getDriveFromPath(const char *path);

extern bool sdFound(void);
extern bool flashcardFound(void);
extern bool bothSDandFlashcard(void);
extern bool imgFound(void);
extern bool sdMount(void);
extern void sdUnmount(void);
extern bool flashcardMount(void);
extern void flashcardUnmount(void);
extern void nitroUnmount(void);
extern bool imgMount(const char* imgName, bool dsiwareSave);
extern void imgUnmount(void);
extern u64 getBytesFree(const char* drivePath);
extern bool driveWritable(Drive drive);
extern bool driveRemoved(Drive drive);
extern u64 driveSizeFree(Drive drive);


#endif //FLASHCARD_H

