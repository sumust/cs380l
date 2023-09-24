#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <stdint.h>
#include <cstring>

#define PAGE_SIZE 4096
#define NUM_PAGES (1024*1024*1024/PAGE_SIZE)

void shuffle(uint64_t* array, size_t n) {
    if (n > 1) {
        size_t i;
        for (i = 0; i < n - 1; i++) {
            size_t j = i + rand() / (RAND_MAX / (n - i) + 1);
            uint64_t t = array[j];
            array[j] = array[i];
            array[i] = t;
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Usage: %s <path_to_file> <mmap_mode>\n", argv[0]);
        exit(1);
    }

    char* path = argv[1];
    char* mode = argv[2];
    int fd;
    char* region;
    int flags;

    uint64_t pages[NUM_PAGES];
    for (uint64_t i = 0; i < NUM_PAGES; i++) {
        pages[i] = i;
    }

    srand(time(NULL)); 
    shuffle(pages, NUM_PAGES);

    if (strcmp(mode, "fb_private") == 0) {
        fd = open(path, O_RDWR);
        flags = MAP_PRIVATE;
    }
    else if (strcmp(mode, "fb_shared") == 0) {
        fd = open(path, O_RDWR);
        flags = MAP_SHARED;
    }
    else if (strcmp(mode, "anon_private") == 0) {
        fd = -1;
        flags = MAP_ANONYMOUS | MAP_PRIVATE;
    }
    else if (strcmp(mode, "anon_shared") == 0) {
        fd = -1;
        flags = MAP_ANONYMOUS | MAP_SHARED;
    }
    else {
        printf("Invalid mmap mode.\n");
        exit(1);
    }

    region = (char*) mmap(NULL, 1024 * 1024 * 1024, PROT_WRITE, flags, fd, 0);
    if (region == MAP_FAILED) {
        perror("mmap");
        exit(2);
    }

    clock_t start_time = clock();

    for (uint64_t i = 0; i < NUM_PAGES; i++) {
        region[pages[i] * PAGE_SIZE] = 'a';
    }

    if (msync(region, 1024 * 1024 * 1024, MS_SYNC) < 0) {
        perror("msync");
        exit(3);
    }

    clock_t end_time = clock();
    double elapsed_time = (double)(end_time - start_time) / CLOCKS_PER_SEC;
    printf("Elapsed time for %s: %f seconds\n", mode, elapsed_time);

    munmap(region, 1024 * 1024 * 1024);
    if (fd != -1) close(fd);

    return 0;
}
