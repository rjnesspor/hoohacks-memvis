// LEAKY program :(

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

int myALLOCfunc() {
    int* x = malloc(10*sizeof(int));
    char* str = calloc(16, sizeof(char));
    free(x);
    return 2;
}

int main() {
    myALLOCfunc();
    myALLOCfunc();
    void* x = malloc(200);
    return 0;

}