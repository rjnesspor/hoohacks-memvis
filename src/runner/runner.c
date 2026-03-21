#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int ac, char** av) {
    if (ac != 2) {
        printf("Usage: %s <input executable>\n", av[0]);
        exit(1);
    }

    if (!setenv("LD_PRELOAD", "./wrap.so", 1)) {
        puts("Successfully set LD_PRELOAD");
    }
    execlp(av[1], av[1], NULL);
    
    return 0;
}