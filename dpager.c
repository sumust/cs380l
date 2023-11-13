#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <elf.h>
#include <signal.h>

#define PAGE_SIZE 4096
#define STACK_SIZE (1024 * 1024)  // 1MB
#define MAX_PHDR_COUNT 16

int program_fd = -1;
Elf64_Phdr phdr_table[MAX_PHDR_COUNT];
int phdr_count = 0;
Elf64_Addr entry_point;

void segfault_handler(int sig, siginfo_t *info, void *context) {
    void *fault_addr = info->si_addr;
    void *aligned_addr = (void *)((uintptr_t)fault_addr & ~(PAGE_SIZE - 1));

    for (int i = 0; i < phdr_count; ++i) {
        Elf64_Phdr *phdr = &phdr_table[i];
        if (phdr->p_type != PT_LOAD) continue;

        void *seg_start = (void *)(phdr->p_vaddr & ~(PAGE_SIZE - 1));
        void *seg_end = (void *)((phdr->p_vaddr + phdr->p_memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1));

        if (aligned_addr >= seg_start && aligned_addr < seg_end) {
            size_t offset = aligned_addr - seg_start;
            size_t file_offset = phdr->p_offset + offset;
            if (mmap(aligned_addr, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_FIXED | MAP_PRIVATE, program_fd, file_offset) == MAP_FAILED) {
                perror("mmap in segfault handler");
                exit(EXIT_FAILURE);
            }
            return;
        }
    }

    // Preserving memory access errors
    fprintf(stderr, "Segmentation fault (invalid memory access) at address: %p\n", fault_addr);
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
    for (int i = 0; i < ehdr.e_phnum && i < MAX_PHDR_COUNT; ++i) {
        Elf64_Phdr phdr;
        if (read(program_fd, &phdr, sizeof(phdr)) != sizeof(phdr)) {
            perror("read");
            exit(EXIT_FAILURE);
        }

        if (phdr.p_type == PT_LOAD) {
            phdr_table[phdr_count++] = phdr;
        }
    }

    entry_point = ehdr.e_entry;
}

void *setup_stack(int argc, char *argv[], char *envp[]) {
    void *stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (stack == MAP_FAILED) {
        perror("mmap stack");
        exit(EXIT_FAILURE);
    }

    char **new_argv = (char **)(stack + STACK_SIZE - (argc + 1) * sizeof(char *));
    for (int i = 0; i < argc; i++) {
        new_argv[i] = strdup(argv[i + 1]);  // Skip the loader's name
    }
    new_argv[argc] = NULL;

    // Copy environment variables
    int envc;
    for (envc = 0; envp[envc] != NULL; ++envc);
    char **new_envp = new_argv + argc + 1;
    for (int i = 0; i < envc; i++) {
        new_envp[i] = strdup(envp[i]);
    }
    new_envp[envc] = NULL;

    // Set up auxiliary vectors
    Elf64_auxv_t *auxv = (Elf64_auxv_t *)(new_envp + envc + 1);
    for (; auxv->a_type != AT_NULL; ++auxv) {
        if (auxv->a_type == AT_PHDR) {
            auxv->a_un.a_val = (uintptr_t)phdr_table;  // Address of the program header table
        }
    }

    return new_argv - 1; // Stack pointer should point to argc
}

int main(int argc, char *argv[], char *envp[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <ELF-file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    setup_signal_handler();
    load_program(argv[1]);

    // Align the stack to a 16-byte boundary and setup the stack
    void *stack_top = setup_stack(argc - 1, argv, envp);

    // Zero all registers and jump to the entry point using call/ret semantics
    asm volatile(
        "xor %%rax, %%rax\n"
        "xor %%rbx, %%rbx\n"
        "xor %%rcx, %%rcx\n"
        "xor %%rdx, %%rdx\n"
        "xor %%rsi, %%rsi\n"
        "xor %%rdi, %%rdi\n"
        "xor %%rbp, %%rbp\n"
        "mov %0, %%rsp\n"
        "call *%1\n"
        :
        : "r"(stack_top), "r"(entry_point)
        : "memory"
    );

    return 0;
}
