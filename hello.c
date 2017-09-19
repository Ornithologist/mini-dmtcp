#include <stdio.h>
#include <unistd.h>

int main(void)
{
    int count;
    int elapsed_sec;
    int sleep_sec = 1;

    count = 0;
    while ((elapsed_sec = sleep(sleep_sec)) == 0) {
        ++count;
        printf("%s ", ".");
        fflush(stdout);  // force to flush per iteration
    }
    return 0;
}