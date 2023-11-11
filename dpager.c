#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <elf.h>
#include <signal.h>

#define PAGE_SIZE 4096
#define STACK_SIZE (1024 * 1024) // 1MB
#define MAX_PHDR_COUNT 16

void *program_base = NULL;
void *program_end = NULL;
int program_fd = -1;
Elf64_Phdr phdr_table[MAX_PHDR_COUNT];
int phdr_count = 0;

void segfault_handler(int sig, siginfo_t *info, void *context) {
    void *fault_addr = info->si_addr;
    void *aligned_addr = (void *)((uintptr_t)fault_addr & ~(PAGE_SIZE - 1));

    for (int i = 0; i < phdr_count; ++i) {
        Elf64_Phdr *phdr = &phdr_table[i];
        if (phdr->p_type == PT_LOAD) {
            void *seg_start = (void *)phdr->p_vaddr;
            void *seg_end = seg_start + phdr->p_memsz;

            if (fault_addr >= seg_start && fault_addr < seg_end) {
                size_t page_offset = (uintptr_t)fault_addr - (uintptr_t)seg_start;
                size_t file_page_offset = phdr->p_offset + page_offset;
                mmap(aligned_addr, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_FIXED | MAP_PRIVATE, program_fd, file_page_offset - (file_page_offset % PAGE_SIZE));
                return;
            }
        }
    }

    fprintf(stderr, "Segmentation fault at address: %p\n", fault_addr);
    exit(EXIT_FAILURE);
}

void setup_signal_handler() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = segfault_handler;

    if (sigaction(SIGSEGV, &sa, NULL) == -1) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

void load_program(const char *program_name) {
    program_fd = open(program_name, O_RDONLY);
    if (program_fd == -1) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    Elf64_Ehdr ehdr;
    if (read(program_fd, &ehdr, sizeof(ehdr)) != sizeof(ehdr)) {
        perror("read");
        exit(EXIT_FAILURE);
    }

    lseek(program_fd, ehdr.e_phoff, SEEK_SET);
    for (int i = 0; i < ehdr.e_phnum; ++i) {
        Elf64_Phdr phdr;
        if (read(program_fd, &phdr, sizeof(phdr)) != sizeof(phdr)) {
            perror("read");
            exit(EXIT_FAILURE);
        }

        if (phdr.p_type == PT_LOAD) {
            if (phdr_count >= MAX_PHDR_COUNT) {
                fprintf(stderr, "Too many program headers\n");
                exit(EXIT_FAILURE);
            }

            phdr_table[phdr_count++] = phdr;

            if (program_base == NULL || (void *)phdr.p_vaddr < program_base) {
                program_base = (void *)phdr.p_vaddr;
            }

            if (program_end == NULL || (void *)(phdr.p_vaddr + phdr.p_memsz) > program_end) {
                program_end = (void *)(phdr.p_vaddr + phdr.p_memsz);
            }
        }
    }

    // Map only the first page of each LOAD segment
    for (int i = 0; i < phdr_count; ++i) {
        Elf64_Phdr *phdr = &phdr_table[i];
        size_t offset = phdr->p_offset - (phdr->p_offset % PAGE_SIZE);
        mmap((void *)phdr->p_vaddr, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_FIXED | MAP_PRIVATE, program_fd, offset);
    }

    void *stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
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
 
    close(program_fd);
    munmap(stack, STACK_SIZE);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <ELF-file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    setup_signal_handler();
    load_program(argv[1]);
    return 0;
}
