// test_program1.c
#include <stdio.h>

int main() {
    printf("Test Program 1 is running...\n");
    // Some computation
    int sum = 0;
    for (int i = 0; i < 1000000; i++) {
        sum += i;
    }
    printf("Sum is %d\n", sum);
    return 0;
}
