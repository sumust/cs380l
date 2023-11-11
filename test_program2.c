#include <stdio.h>
#include <stdlib.h>

int main() {
    printf("Test Program 2 is running...\n");
    int *array = malloc(100 * sizeof(int));
    if (!array) {
        fprintf(stderr, "Memory allocation failed!\n");
        return 1;
    }

    // Use the allocated memory to ensure it's there
    for (int i = 0; i < 100; i++) {
        array[i] = i;
    }

    free(array);
    printf("Memory was successfully allocated and freed.\n");
    return 0;
}
