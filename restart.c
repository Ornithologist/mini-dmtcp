#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <ucontext.h>
#include "common.h"

#define MAP_ADDR 0x5300000
#define MAP_LEN 0x100000

// global variables
char filename_g[1000];  // filename from user input
int ckptfp;             // file descriptor for the image file
ucontext_t uc_g;        // context to be continued
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

int main(int argc, char **argv)
{
    void *pmap, *emap;

    if (argc == 1) return 0;
    sprintf(filename_g, "%s", argv[1]);

    // map memory
    pmap = map_memory((void *)MAP_ADDR);
    emap = pmap + MAP_LEN;
    if (pmap == (void *)-1) return 0;
    // swtich stack pointer
    asm volatile("mov %0,%%rsp" : : "g"(emap) : "memory");
    unmap_and_restore();
}

/*
 * return 0 when failed to unmap
 */
void unmap_and_restore()
{
    int fp, read_res, res;
    char filename[1000];
    struct MemoryRegion *pmr;

    unsigned long long length;

    fp = get_fdescriptor("/proc/self/maps");
    if (fp < 0) exit(1);

    while ((pmr = get_line(fp))) {
        if (locate_old_stack(pmr)) break;
    }

    res = close(fp);
    if (res < 0) {
        printf("Failed closing current process map (error:%s)\n",
               strerror(errno));
        exit(1);
    }

    length = pmr->endAddr - pmr->startAddr;
    res = munmap(pmr->startAddr, length);

    ckptfp = get_fdescriptor(filename_g);
    if (ckptfp < 0) exit(1);

    // restore previous memory
    read_res = read_context(ckptfp);
    read_res += restore_memory(ckptfp);

    // revoke
    revoke_context();
    return;
}

void revoke_context()
{
    int res;
    // close checkpoint image
    res = close(ckptfp);
    if (res < 0) {
        printf("Failed closing current process map (error:%s).\n",
               strerror(errno));
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
int locate_old_stack(struct MemoryRegion *pmr)
{
    if (strcmp(pmr->name, "[stack]") == 0) return 1;
    return 0;
}

int restore_memory(int fp)
{
    struct MemoryRegion mr;
    int count, n, ir, iw, ie, ip, mprot_res;
    unsigned long long sai, eai, length;
    void *sa, *ea, *mapped;
    count = 0;

    // pre-read the next start address as test scenario
    while ((n = read(fp, &mr, sizeof(mr))) > 0) {
        length = mr.endAddr - mr.startAddr;
        // load the memory region
        mapped = mmap(mr.startAddr, length, PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_ANONYMOUS | MAP_FIXED | MAP_PRIVATE, -1, 0);
        if (mapped == (void *)-1) {
            printf("Error when mapping memory region %p %p (error:%s)\n", sa,
                   ea, strerror(errno));
            break;
        }
        count += n = read(fp, mr.startAddr, length);
        if (n < 0) break;
        // set the permission
        int permissions = 0;
        if (mr.isReadable) permissions |= PROT_READ;
        if (mr.isWriteable) permissions |= PROT_WRITE;
        if (mr.isExecutable) permissions |= PROT_EXEC;
        mprot_res = mprotect(mr.startAddr, length, permissions);
        if (mprot_res < 0) {
            printf(
                "Error when setting protection on memory region %p %p "
                "(error:%s)\n",
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

int read_context(int fp)
{
    int n = read(fp, &uc_g, sizeof(ucontext_t));
    if (n < 0) {
        printf("Error reading context from image (error:%s)\n",
               strerror(errno));
        exit(1);
    }
    return n;
}

/*
 * return file descriptor
 */
int get_fdescriptor(char *filename)
{
    int fp = open(filename, O_RDONLY);
    return fp;
}

/*
 * return * to the mapped area
 */
void *map_memory(void *addr)
{
    return mmap(addr, MAP_LEN, PROT_READ | PROT_WRITE | PROT_EXEC,
                MAP_ANONYMOUS | MAP_FIXED | MAP_PRIVATE, -1, 0);
}