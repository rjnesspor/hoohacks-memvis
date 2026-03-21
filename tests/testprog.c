#include <stdlib.h>

int main() {
    int* arr = malloc(10 * sizeof(int));
    free(arr);
    char* string = calloc(10, sizeof(char));
    string[0] = 'R';
    string[1] = '\0';
    printf("%s\n", string);
    return 0;
}