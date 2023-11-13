// Workload for prediction algorithm 
#include <stdio.h>
#include <stdlib.h>

#define ARRAY_SIZE 1000000 // Size of the array

int main() {
    int *array = (int *)malloc(ARRAY_SIZE * sizeof(int));
    if (array == NULL) {
        fprintf(stderr, "Memory allocation failed\n");
        return 1;
    }

    // Initialize array
    for (int i = 0; i < ARRAY_SIZE; i++) {
        array[i] = i;
    }

    // Sum elements
    long long sum = 0;
    for (int i = 0; i < ARRAY_SIZE; i++) {
        sum += array[i];
    }

    printf("Sum: %lld\n", sum);
    free(array);
    return 0;
}
