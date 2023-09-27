#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <stdint.h>
#include <string.h>

#define IO_SIZE 4096
#define FILE_SIZE (1024 * 1024 * 1024) // 1GB

void shuffle(uint64_t* array, size_t n) {
    if (n > 1) {
        for (size_t i = 0; i < n - 1; i++) {
            size_t j = i + rand() / (RAND_MAX / (n - i) + 1);
            uint64_t temp = array[j];
            array[j] = array[i];
            array[i] = temp;
        }
    }
}

void do_file_io(int fd, char *buf, uint64_t *offset_array, size_t n, int opt_read) {
    int ret = 0;
    for (int i = 0; i < n; i++) {
        ret = lseek(fd, offset_array[i], SEEK_SET);
        if (ret == -1) {
            perror("lseek");
            exit(-1);
        }
        if (opt_read)
            ret = read(fd, buf, IO_SIZE);
        else
            ret = write(fd, buf, IO_SIZE);
        if (ret == -1) {
            perror("read/write");
            exit(-1);
        }
    }
}

int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Usage: %s <path_to_file> <mode>\n", argv[0]);
        printf("Mode: 1 - Sequential Read, 2 - Sequential Write, 3 - Random Read, 4 - Random Write\n");
        exit(1);
    }

    char* path = argv[1];
    int mode = atoi(argv[2]);

    int fd;
    int flags = O_DIRECT; // For Direct IO

    // Opening the file with O_DIRECT flag
    if (mode == 1 || mode == 3) { // Read Modes
        fd = open(path, O_RDONLY | flags);
    } else { // Write Modes
        fd = open(path, O_WRONLY | flags);
    }

    if (fd == -1) {
        perror("open");
        exit(-1);
    }

    // Allocated aligned buffer
    char *buf;
    if (posix_memalign((void**)&buf, 4096, IO_SIZE)) {
        perror("posix_memalign");
        exit(-1);
    }

    uint64_t offset_array[FILE_SIZE / IO_SIZE];
    size_t n = FILE_SIZE / IO_SIZE;

    // Populate offset_array for sequential IO
    for (size_t i = 0; i < n; i++) {
        offset_array[i] = i * IO_SIZE;
    }

    if (mode == 3 || mode == 4) { // Random Modes
        shuffle(offset_array, n);
    }

    // Perform I/O
    if (mode == 1 || mode == 3) { // Read Modes
        do_file_io(fd, buf, offset_array, n, 1);
    } else { // Write Modes
        memset(buf, 'A', IO_SIZE);
        do_file_io(fd, buf, offset_array, n, 0);
    }

    close(fd);
    free(buf); // Free the allocated buffer
    return 0;
}
