#include <stdio.h>
#include <stdlib.h>

#define LARGE_SIZE (1024 * 1024 * 256) // 256 MB

int main() {
    long *large_array = malloc(LARGE_SIZE * sizeof(long));
    if (!large_array) {
        perror("Memory allocation failed");
        return 1;
    }

    // Sparse access - only accessing start and end of the array
    large_array[0] = 1;
    large_array[LARGE_SIZE - 1] = 2;

    printf("Sparse memory operations done.\n");

    free(large_array);
    return 0;
}
