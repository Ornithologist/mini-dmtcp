#include <stdio.h>
#include <signal.h>
#include <sys/types.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <ucontext.h>
#include <assert.h>
#include "common.h"

#define MAX_CELL_LEN 100        // max length of string in a memory maps field
#define MAX_MR_PER_PROC 1000    // max number of memory regions in a process
#define CKPT_NAME "myckpt"		// check point image name

// flag
int flag = 0;

// main processes
void my_handler(int sig);
int catch_a_check(int pid);
int put_a_check(struct MemoryRegion *mrs, int mrsc, ucontext_t *ucp);
int write_context(int fp, ucontext_t *ucp);
int write_memory(int fp, int mrsc, struct MemoryRegion *mrs);

// utility functions
int get_memory_regions(int pid, struct MemoryRegion *mrs);
char *get_file_path(int pid);
char *stringify_pid(int pid);
long int my_pow(int base, int expo);
int get_cell(char *cell, int fp);
void *get_line(int fp);
void convert_addr_cell(struct MemoryRegion *mr, char *cell);
void convert_privilege_cell(struct MemoryRegion *mr, char *cell);
long int hex_to_int(char *hex_str);


__attribute__ ((constructor))
void checkpointer() {
  signal(SIGUSR2, my_handler);
}

void my_handler(int sig) {
	signal(sig, SIG_DFL);       // react with default behavior
	int catch_result = catch_a_check((int) getpid());
    if (catch_result == 0)
		return;
	return;
}

/*
 * return 0 if failed to catch
 * return 1 if succeeded
 */
int catch_a_check(int pid) {
    struct MemoryRegion *mrs;   // memory regions, to be saved
    int mrs_count;              // number of mr(s), to be saved
    ucontext_t uc;            	// the context of the calling thread, to be saved
	int register_res;           // result of getcontext
	int write_res;				// result of writing image

    // get memory regions
    mrs = (struct MemoryRegion *) malloc(MAX_MR_PER_PROC * sizeof(struct MemoryRegion));
    mrs_count = get_memory_regions(pid, mrs);

    if (mrs_count == 0) {
		printf("Failed to read memory regions.\n");  // just be sure
		return 0;
    }

	// get registersf
	register_res = getcontext(&uc);

	if (flag == 1)
		return 0;
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
int put_a_check(struct MemoryRegion *mrs, int mrsc, ucontext_t *ucp) {
	int wfp;					// file descriptor of the image file
	int w_res;					// writing result

	wfp = open(CKPT_NAME, O_CREAT|O_WRONLY|O_TRUNC, S_IRWXU);
	if (wfp < 0)
		return 0;

	// write context
	w_res = write_context(wfp, ucp);
	if (w_res == 0)
		return 0;

	// write memory meta & actual
	w_res += write_memory(wfp, mrsc, mrs);
	if (w_res == 0)
		return 0;

	// close the file descriptor
	int res = close(wfp);
	if (res < 0)
		return 0;

	return 1;
}

/*
 * return 0 if failed
 */
int write_context(int fp, ucontext_t *ucp) {
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
int write_memory(int fp, int mrsc, struct MemoryRegion *mrs) {
	int i;
	int n;
	n = 0;
	for (i=0; i<mrsc; i++) {
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
int get_memory_regions(int pid, struct MemoryRegion *mrs) {
    int rfp;					// file descriptor for reading
    int count;                  // memory region count
    char *rfpath;				// file path for read
    struct MemoryRegion *pmr;	// the pointer to the currently read memory region

	// get memory maps file path
	rfpath = get_file_path(pid);

	// get memory maps file descriptor
	rfp = open(rfpath, O_RDONLY);
	if (rfp < 0)
		return 0;

    // read memoery regions
    count = 0;
	while ((pmr = get_line(rfp))) {
        mrs[count] = (*pmr);
        ++count;
	}

	// close the file descriptor
	int res = close(rfp);
	if (res < 0)
		return 0;

    return count;
}

/*
 * return "/proc/${pid}/maps"
 */
char *get_file_path(int pid) {
	char *pid_str = stringify_pid(pid);
	char *dir_prefix = "/proc/";
	char *dir_suffix = "/maps";

	int total_len = strlen(dir_prefix) + strlen(pid_str) + strlen(dir_suffix);
	char *fpath = (char *) malloc(total_len * sizeof(char));
	strcpy(fpath, dir_prefix);
	strcat(fpath, pid_str);
	strcat(fpath, dir_suffix);
	free(pid_str);
	return fpath;
}

/*
 * return stringified pit_int
 */
char *stringify_pid(int pid_int) {
	int num_of_digits;
	int pid_to_slice = pid_int;
	int pid_divided = pid_int;
	int i;

	num_of_digits = 0;
	while(pid_divided>=1) {
		++num_of_digits;
		pid_divided = pid_divided / 10;
	}

	char *pid_string = (char *) malloc(num_of_digits * sizeof(char));
	for (i=0; i<num_of_digits; i++) {
		long int power = my_pow(10, (num_of_digits - 1 - i));
		long int digit = pid_to_slice / power;
		pid_to_slice = pid_to_slice - digit * power;
		pid_string[i] = '0' + digit;
	}
	return pid_string;
}

/*
 * return (long int) (base ^ expo)
 */
long int my_pow(int base, int expo) {
	long int result = 1;
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
	int i;
	int clen = strlen(cell);
	int delimited = 0;
	int delimited_offset;
	long int sa, ea;
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

	sa = hex_to_int(start_addr);
	ea = hex_to_int(end_addr);

	mr->startAddr = (void *) (long) sa;
	mr->endAddr = (void *) (long) ea;
	return;
}

/*
 * return base 10 value of the hex string
 */
long int hex_to_int(char *hex_str) {
    int i;
	long int res = 0;
    long int num_of_digits = strlen(hex_str);
	for (i=0; i<num_of_digits; i++) {
		char cur_digit = hex_str[i];
		long int b = 0;
		long int expo = num_of_digits - 1 - i;
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
