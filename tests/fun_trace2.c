#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Exercises malloc/free pairs in a loop with varying sizes
void test_malloc_free() {
    for (int i = 1; i <= 20; i++) {
        void* p = malloc(i * 64);
        memset(p, 0xAB, i * 64);
        free(p);
    }
}

// Exercises calloc with a small array, then reallocs it larger in steps
void test_calloc_realloc() {
    int* arr = calloc(10, sizeof(int));
    for (int i = 0; i < 10; i++) arr[i] = i;

    for (int i = 1; i <= 5; i++) {
        arr = realloc(arr, (10 + i * 10) * sizeof(int));
        for (int j = 10 * i; j < 10 * (i + 1); j++) arr[j] = j;
    }

    free(arr);
}

// Exercises multiple live allocations at the same time, then frees them
void test_concurrent_allocs() {
    int n = 15;
    void** ptrs = malloc(n * sizeof(void*));

    for (int i = 0; i < n; i++) {
        ptrs[i] = malloc((i + 1) * 128);
        memset(ptrs[i], i, (i + 1) * 128);
    }

    for (int i = n - 1; i >= 0; i--) {
        free(ptrs[i]);
    }

    free(ptrs);
}

// Exercises calloc for a 2D grid, rewrites with realloc rows
void test_grid_alloc() {
    int rows = 8, cols = 16;
    int** grid = calloc(rows, sizeof(int*));

    for (int i = 0; i < rows; i++) {
        grid[i] = calloc(cols, sizeof(int));
        for (int j = 0; j < cols; j++) grid[i][j] = i * cols + j;
    }

    // grow each row
    for (int i = 0; i < rows; i++) {
        grid[i] = realloc(grid[i], cols * 2 * sizeof(int));
        for (int j = cols; j < cols * 2; j++) grid[i][j] = -1;
    }

    for (int i = 0; i < rows; i++) free(grid[i]);
    free(grid);
}

int main() {

    test_malloc_free();
    test_calloc_realloc();
    test_concurrent_allocs();
    test_grid_alloc();
    void * a = malloc(30 * sizeof(float));
    return 0;
}