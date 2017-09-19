#include <assert.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <ucontext.h>
#include <unistd.h>
#include "common.h"

#define MAX_MR_PER_PROC 1000  // max number of memory regions in a process
#define CKPT_NAME "myckpt"    // check point image name

// flag
int flag = 0;

// main processes
void my_handler(int sig);
int catch_a_check();
int put_a_check(struct MemoryRegion *mrs, int mrsc, ucontext_t *ucp);
int write_context(int fp, ucontext_t *ucp);
int write_memory(int fp, int mrsc, struct MemoryRegion *mrs);

// utility functions
int get_memory_regions(struct MemoryRegion *mrs);

__attribute__((constructor)) void checkpointer()
{
    signal(SIGUSR2, my_handler);
}

void my_handler(int sig)
{
    signal(sig, SIG_DFL);  // react with default behavior
    int catch_result = catch_a_check();
    if (catch_result == 0) return;
    return;
}

/*
 * return 0 if failed to catch
 * return 1 if succeeded
 */
int catch_a_check()
{
    struct MemoryRegion *mrs;  // memory regions, to be saved
    int mrs_count;             // number of mr(s), to be saved
    ucontext_t uc;             // the context of the calling thread, to be saved
    int register_res;          // result of getcontext
    int write_res;             // result of writing image

    // get memory regions
    mrs = (struct MemoryRegion *)malloc(MAX_MR_PER_PROC *
                                        sizeof(struct MemoryRegion));
    mrs_count = get_memory_regions(mrs);

    if (mrs_count == 0) {
        printf("Failed to read memory regions.\n");  // just be sure
        return 0;
    }

    // get registersf
    register_res = getcontext(&uc);

    if (flag == 1) return 0;
    flag = 1;

    if (register_res != 0) {
        printf("Failed to get context.\n");
        return 0;
    }

    // write it
    write_res = put_a_check(mrs, mrs_count, &uc);
    if (write_res == 0) {
        printf("Failed to write image.\n");
        return 0;
    }
    printf("Checkpoint image created.\n");
    return 1;
}

/*
 * return 0 if failed
 * return 1 if successfully written
 */
int put_a_check(struct MemoryRegion *mrs, int mrsc, ucontext_t *ucp)
{
    int wfp;    // file descriptor of the image file
    int w_res;  // writing result

    wfp = open(CKPT_NAME, O_CREAT | O_WRONLY | O_TRUNC, S_IRWXU);
    if (wfp < 0) return 0;

    // write context
    w_res = write_context(wfp, ucp);
    if (w_res == 0) return 0;

    // write memory meta & actual
    w_res += write_memory(wfp, mrsc, mrs);
    if (w_res == 0) return 0;

    // close the file descriptor
    int res = close(wfp);
    if (res < 0) return 0;

    return 1;
}

/*
 * return 0 if failed
 */
int write_context(int fp, ucontext_t *ucp)
{
    int n = write(fp, ucp, sizeof(ucontext_t));
    if (n < 0) {
        printf("Failed to write context\n");
        exit(1);
    }
    return n;
}

/*
 * return 0 if failed
 */
int write_memory(int fp, int mrsc, struct MemoryRegion *mrs)
{
    int i;
    int n;
    n = 0;
    for (i = 0; i < mrsc; i++) {
        struct MemoryRegion mr = mrs[i];
        if (strstr(mr.name, "vsyscall") ||
            (!mr.isReadable && !mr.isWriteable && !mr.isExecutable)) {
            continue;
        }
        // write meta
        ssize_t ret = write(fp, &mr, sizeof(mr));
        n += ret;
        assert(ret == sizeof(mr));
        // write actual
        ret = write(fp, mr.startAddr, (mr.endAddr - mr.startAddr));
        assert(ret == mr.endAddr - mr.startAddr);
        n += ret;
    }
    return n;
}

/*
 * allocate value for MemoryRegion[] found in /proc/${pid}/maps
 * return 0 when reading error
 * return the number of memory regions when all good
 */
int get_memory_regions(struct MemoryRegion *mrs)
{
    int rfp;       // file descriptor for reading
    int count;     // memory region count
    char *rfpath;  // file path for read
    struct MemoryRegion
        *pmr;  // the pointer to the currently read memory region

    // get memory maps file path
    rfpath = "/proc/self/maps";

    // get memory maps file descriptor
    rfp = open(rfpath, O_RDONLY);
    if (rfp < 0) return 0;

    // read memoery regions
    count = 0;
    while ((pmr = get_line(rfp))) {
        mrs[count] = (*pmr);
        ++count;
    }

    // close the file descriptor
    int res = close(rfp);
    if (res < 0) return 0;

    return count;
}