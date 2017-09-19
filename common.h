#ifndef __COMMON_H__
#define __COMMON_H__
struct MemoryRegion {
	void *startAddr;
	void *endAddr;
	int isReadable;
	int isWriteable;
	int isExecutable;
	int isPrivate;
	char name[2000];
};

#endif
