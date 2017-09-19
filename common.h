#ifndef __COMMON_H__
#define __COMMON_H__

#define MAX_CELL_LEN 100  // max length of string in a memory maps field
struct MemoryRegion {
    void *startAddr;
    void *endAddr;
    int isReadable;
    int isWriteable;
    int isExecutable;
    int isPrivate;
    char name[2000];
};

unsigned long long my_pow(int base, int expo);
int get_cell(char *cell, int fp);
void *get_line(int fp);
void convert_addr_cell(struct MemoryRegion *mr, char *cell);
void convert_privilege_cell(struct MemoryRegion *mr, char *cell);
unsigned long long hex_to_long(char *hex_str);

/*
 * return (unsigned long long) (base ^ expo)
 */
unsigned long long my_pow(int base, int expo)
{
    unsigned long long result = 1;
    while (expo > 0) {
        --expo;
        result = result * base;
    }
    return result;
}

/*
 * return actual pointer to the mr when new line
 * return NULL when end of file
 */
void *get_line(int fp)
{
    int read_res;                   // reading result
    int cell_count;                 // cell count
    char *cell;                     // cell content
    struct MemoryRegion *pmr, lmr;  // pointer and actual content
    pmr = NULL;

    cell_count = 0;
    cell = (char *)malloc(MAX_CELL_LEN * sizeof(char));
    while ((read_res = get_cell(cell, fp)) > 0) {
        if (cell_count == 0)
            convert_addr_cell(&lmr, cell);
        else if (cell_count == 1)
            convert_privilege_cell(&lmr, cell);
        if (strlen(cell) > 0) ++cell_count;
        free(cell);
        cell = (char *)malloc(MAX_CELL_LEN * sizeof(char));
    }

    // get the last name when not empty
    if (strlen(cell) > 0)
        strcpy(lmr.name, cell);
    else
        lmr.name[0] = '\0';
    // return valid * to mr when new line
    if (read_res == 0) pmr = &lmr;
    return pmr;
}

/*
 * return -1 when end of file
 * return 0 when new line
 * return 1 when new cell
 */
int get_cell(char cell[], int fp)
{
    int buf_size = 1;    // buffer size for reading
    int count = 0;       // char count
    int read_res;        // reading result
    int n;               // number of bytes transmitted when reading
    char buf[buf_size];  // reading buffer with size one

    while ((n = read(fp, buf, buf_size)) > 0 && buf[0] != EOF &&
           buf[0] != ' ' && buf[0] != '\n' && buf[0] != '\t') {
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
void convert_addr_cell(struct MemoryRegion *mr, char *cell)
{
    int i, delimited_offset;
    int clen = strlen(cell);
    int delimited = 0;
    unsigned long long sa, ea;
    void *sap, *eap;
    char *start_addr = (char *)malloc(MAX_CELL_LEN * sizeof(char));
    char *end_addr = (char *)malloc(MAX_CELL_LEN * sizeof(char));

    for (i = 0; i < clen; i++) {
        if (cell[i] == '-') {
            delimited = 1;
            delimited_offset = i + 1;
        }
        if (delimited == 0)
            start_addr[i] = cell[i];
        else if (i >= delimited_offset)
            end_addr[i - delimited_offset] = cell[i];
    }

    sa = hex_to_long(start_addr);
    ea = hex_to_long(end_addr);

    sap = (void *)(long)sa;
    eap = (void *)(long)ea;
    mr->startAddr = sap;
    mr->endAddr = eap;
    return;
}

/*
 * return base 10 value of the hex string
 */
unsigned long long hex_to_long(char *hex_str)
{
    int i;
    unsigned long long res = 0;
    unsigned long long num_of_digits = strlen(hex_str);
    for (i = 0; i < num_of_digits; i++) {
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
void convert_privilege_cell(struct MemoryRegion *mr, char *cell)
{
    if (strlen(cell) < 4) return;
    mr->isReadable = ((cell[0] - 'r') == 0) ? (1) : (0);
    mr->isWriteable = ((cell[1] - 'w') == 0) ? (1) : (0);
    mr->isExecutable = ((cell[2] - 'x') == 0) ? (1) : (0);
    mr->isPrivate = ((cell[2] - 'p') == 0) ? (1) : (0);
    return;
}
#endif
