#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <ucontext.h>
#include <string.h>
#include <errno.h>
#include "common.h"

#define MAP_ADDR 0x5300000
#define MAP_LEN 0x100000
#define MAX_CELL_LEN 100        // max length of string in a memory maps field

// global variables
char filename_g[1000];                          // filename from user input
int ckptfp;										// file descriptor for the image file
ucontext_t uc_g;                                // context to be continued
struct MemoryRegion mr_g;

// main processes
void unmap_and_restore();
int restore_memory(int fp);
int read_context(int fp);
void revoke_context();

// utility functions
void *map_memory(void *addr);
int get_fdescriptor(char *filename);
int locate_old_stack(struct MemoryRegion *pmr);

// FIXME: copied from ckpt.c, should move to another file
unsigned long long my_pow(int base, int expo);
int get_cell(char *cell, int fp);
void *get_line(int fp);
void convert_addr_cell(struct MemoryRegion *mr, char *cell);
void convert_privilege_cell(struct MemoryRegion *mr, char *cell);
unsigned long long hex_to_long(char *hex_str);

int main(int argc, char **argv)
{
	void *pmap, *emap;

    if (argc == 1)
        return 0;
	sprintf(filename_g, "%s", argv[1]);

	// map memory
	pmap = map_memory((void *) MAP_ADDR);
	emap = pmap + MAP_LEN;
    if (pmap == (void *) -1)
		return 0;
	// swtich stack pointer
	asm volatile ("mov %0,%%rsp" : : "g" (emap) : "memory");
	unmap_and_restore();
}

/*
 * return 0 when failed to unmap
 */
void unmap_and_restore() {
    int fp, read_res, res;
    char filename[1000];
	struct MemoryRegion *pmr;

	unsigned long long length;

    fp = get_fdescriptor("/proc/self/maps");
    if (fp < 0)
        exit(1);

	while((pmr = get_line(fp))) {
		if (locate_old_stack(pmr))
			break;
	}

	res = close(fp);
	if (res < 0) {
		printf("Failed closing current process map (error:%s)\n", strerror(errno));
		exit(1);
	}

	length = pmr->endAddr - pmr->startAddr;
	res = munmap(pmr->startAddr, length);

	ckptfp = get_fdescriptor(filename_g);
	if (ckptfp < 0)
		exit(1);

	// restore previous memory
	read_res = read_context(ckptfp);
	read_res +=	restore_memory(ckptfp);

	// revoke
	revoke_context();
	return;
}

void revoke_context() {
	int res;
	// close checkpoint image
	res = close(ckptfp);
	if (res < 0) {
		printf("Failed closing current process map (error:%s).\n", strerror(errno));
		exit(1);
	}
	// restart
	res = setcontext(&uc_g);
	if (res < 0) {
		printf("Restore failed (error:%s).\n", strerror(errno));
		exit(1);
	}
}

/*
 * return 1 when successfully unmapped
 */
int locate_old_stack(struct MemoryRegion *pmr) {
	if (strcmp(pmr->name, "[stack]") == 0)
		return 1;
	return 0;
}

int restore_memory(int fp) {
    struct MemoryRegion mr;
	int count, n, ir, iw, ie, ip, mprot_res;
	unsigned long long sai, eai, length;
	void *sa, *ea, *mapped;
	count = 0;

	// pre-read the next start address as test scenario
	while((n = read(fp, &mr, sizeof(mr))) > 0) {
		length = mr.endAddr - mr.startAddr;
		// load the memory region
		mapped = mmap(mr.startAddr, length, PROT_READ | PROT_WRITE | PROT_EXEC,
                MAP_ANONYMOUS | MAP_FIXED | MAP_PRIVATE, -1, 0);
		if (mapped == (void *) -1) {
			printf("Error when mapping memory region %p %p (error:%s)\n", sa, ea, strerror(errno));
			break;
		}
		count += n = read(fp, mr.startAddr, length);
		if (n < 0)
			break;
		// set the permission
		int permissions = 0;
		if (mr.isReadable)
			permissions |= PROT_READ;
		if (mr.isWriteable)
			permissions |= PROT_WRITE;
		if (mr.isExecutable)
			permissions |= PROT_EXEC;
		mprot_res = mprotect(mr.startAddr, length, permissions);
		if (mprot_res < 0) {
			printf("Error when setting protection on memory region %p %p (error:%s)\n",
                    mr.startAddr, mr.endAddr, strerror(errno));
			break;
		}
	}
	if (n < 0) {
		printf("Failed to restore memory\n");
		exit(1);
	}
	return count;
}



int read_context(int fp) {
	int n = read(fp, &uc_g, sizeof(ucontext_t));
	if (n < 0) {
		printf("Error reading context from image (error:%s)\n", strerror(errno));
		exit(1);
	}
    return n;
}

/*
 * return file descriptor
 */
int get_fdescriptor(char *filename) {
    int fp = open(filename, O_RDONLY);
    return fp;
}

/*
 * return * to the mapped area
 */
void *map_memory(void *addr) {
    return mmap(
        addr,
        MAP_LEN,
        PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANONYMOUS | MAP_FIXED | MAP_PRIVATE,
        -1,
        0
    );
}

/*
 * return (unsigned long long) (base ^ expo)
 */
unsigned long long my_pow(int base, int expo) {
	unsigned long long result = 1;
	while(expo>0) {
		--expo;
		result = result * base;
	}
	return result;
}

/*
 * return actual pointer to the mr when new line
 * return NULL when end of file
 */
void *get_line(int fp) {
	int read_res;			        // reading result
    int cell_count;			        // cell count
    char *cell;			            // cell content
	struct MemoryRegion *pmr, lmr;	// pointer and actual content
	pmr = NULL;

	cell_count = 0;
	cell = (char *) malloc(MAX_CELL_LEN * sizeof(char));
	while((read_res = get_cell(cell, fp)) > 0) {
		if (cell_count == 0)
			convert_addr_cell(&lmr, cell);
		else if (cell_count == 1)
            convert_privilege_cell(&lmr, cell);
		if (strlen(cell) > 0)
			++cell_count;
		free(cell);
		cell = (char *) malloc(MAX_CELL_LEN * sizeof(char));
	}

	// get the last name when not empty
	if (strlen(cell) > 0)
		strcpy(lmr.name, cell);
	else
		lmr.name[0] = '\0';
	// return valid * to mr when new line
	if (read_res == 0)
		pmr = &lmr;
	return pmr;
}

/*
 * return -1 when end of file
 * return 0 when new line
 * return 1 when new cell
 */
int get_cell(char cell[], int fp) {
	int buf_size = 1;		    // buffer size for reading
	int count = 0;			    // char count
    int read_res;			    // reading result
    int n;				        // number of bytes transmitted when reading
    char buf[buf_size];		    // reading buffer with size one

	while ((n = read(fp, buf, buf_size)) > 0 && buf[0] != EOF && buf[0] !=' ' && buf[0] != '\n' && buf[0] != '\t') {
		cell[count] = buf[0];
		++count;
	}

	if ((buf[0] == ' ' || buf[0] == '\t') && n > 0)
		read_res = 1;
	else if (buf[0] == '\n' && n > 0)
		read_res = 0;
	else
		read_res = -1;

	return read_res;
}

/*
 * convert hex string to (void *), assign it to (*mr)
 */
void convert_addr_cell(struct MemoryRegion *mr, char *cell) {
	int i, delimited_offset;
	int clen = strlen(cell);
	int delimited = 0;
	unsigned long long sa, ea;
	void *sap, *eap;
	char *start_addr = (char *) malloc(MAX_CELL_LEN * sizeof(char));
	char *end_addr = (char *) malloc(MAX_CELL_LEN * sizeof(char));

	for (i=0; i<clen; i++) {
		if (cell[i] == '-') {
			delimited = 1;
			delimited_offset = i + 1;
		}
		if (delimited == 0)
			start_addr[i] = cell[i];
		else if (i >= delimited_offset)
			end_addr[i-delimited_offset] = cell[i];
    }

	sa = hex_to_long(start_addr);
	ea = hex_to_long(end_addr);

	sap = (void *) (long) sa;
	eap = (void *) (long) ea;
	mr->startAddr = sap;
	mr->endAddr = eap;
	return;
}

/*
 * return base 10 value of the hex string
 */
unsigned long long hex_to_long(char *hex_str) {
    int i;
	unsigned long long res = 0;
    unsigned long long num_of_digits = strlen(hex_str);
	for (i=0; i<num_of_digits; i++) {
		char cur_digit = hex_str[i];
		unsigned long long b = 0;
		unsigned long long expo = num_of_digits - 1 - i;
		if (cur_digit - '0' < 10)
			b = cur_digit - '0';
		else
            b = 10 + (cur_digit - 'a');
		res += b * my_pow(16, expo);
	}
	return res;
}

/*
 * convert 'rwxp' to 3 int values and assign them to (*mr)
 */
void convert_privilege_cell(struct MemoryRegion *mr, char *cell) {
	if (strlen(cell) < 4)
		return;
	mr->isReadable = ((cell[0] - 'r') == 0) ? (1) : (0);
	mr->isWriteable = ((cell[1] - 'w') == 0) ? (1) : (0);
	mr->isExecutable = ((cell[2] - 'x') == 0) ? (1) : (0);
	mr->isPrivate = ((cell[2] - 'p') == 0) ? (1) : (0);
	return;
}
