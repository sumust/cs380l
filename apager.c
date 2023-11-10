#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <elf.h>

#define STACK_SIZE (1024 * 1024) // 1MB

void *mmap_segment(int fd, Elf64_Phdr *phdr) {
    off_t offset = phdr->p_offset & ~(sysconf(_SC_PAGE_SIZE) - 1);
    size_t memsz_aligned = (phdr->p_memsz + sysconf(_SC_PAGE_SIZE) - 1) & ~(sysconf(_SC_PAGE_SIZE) - 1);
    void *segment = mmap((void *)phdr->p_vaddr, memsz_aligned,
                         PROT_READ | PROT_WRITE | PROT_EXEC, 
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (segment == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    lseek(fd, offset, SEEK_SET);
    if (read(fd, segment, phdr->p_filesz) != phdr->p_filesz) {
        perror("read");
        exit(EXIT_FAILURE);
    }

    return segment;
}

void load_program(const char *program_name) {
    int fd = open(program_name, O_RDONLY);
    if (fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    Elf64_Ehdr ehdr;
    if (read(fd, &ehdr, sizeof(ehdr)) != sizeof(ehdr)) {
        perror("read");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < ehdr.e_phnum; ++i) {
        Elf64_Phdr phdr;
        lseek(fd, ehdr.e_phoff + i * sizeof(phdr), SEEK_SET);
        if (read(fd, &phdr, sizeof(phdr)) != sizeof(phdr)) {
            perror("read");
            exit(EXIT_FAILURE);
        }

        if (phdr.p_type == PT_LOAD) {
            mmap_segment(fd, &phdr);
        } else if (phdr.p_type == PT_DYNAMIC || phdr.p_type == PT_INTERP || phdr.p_type == PT_NOTE) {
            // Handle other types of program headers here
        }
    }

    close(fd);

    void *stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (stack == MAP_FAILED) {
        perror("mmap stack");
        exit(EXIT_FAILURE);
    }

    void *stack_top = stack + STACK_SIZE - 8;

    asm volatile (
        "mov %0, %%rsp\n"
        "jmp *%1\n"
        :
        : "r"(stack_top), "r"(ehdr.e_entry)
        : "memory"
    );

    munmap(stack, STACK_SIZE);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <ELF-file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    load_program(argv[1]);
    return 0;
}
