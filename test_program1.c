#include <stdio.h>

long fibonacci(int n) {
    if (n <= 1) return n;
    return fibonacci(n - 1) + fibonacci(n - 2);
}

int main() {
    int n = 40; // High enough to cause a considerable calculation time
    printf("Fibonacci of %d is %ld\n", n, fibonacci(n));
    return 0;
}
