#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int ac, char **av)
{
    if (ac != 2)
    {
        printf("Usage: %s <input executable>\n", av[0]);
        exit(1);
    }

    setenv("LD_PRELOAD", "./wrap.so", 1);
    execlp(av[1], av[1], NULL);
    
    return 0;
}