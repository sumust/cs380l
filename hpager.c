
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <elf.h>
#include <signal.h>

#define PAGE_SIZE 4096
#define STACK_SIZE (8 * PAGE_SIZE)
#define MAX_PHDR_COUNT 16

int program_fd = -1;
Elf64_Phdr phdr_table[MAX_PHDR_COUNT];
int phdr_count = 0;
Elf64_Addr entry_point;

void segfault_handler(int sig, siginfo_t *info, void *context) {
    void *fault_addr = info->si_addr;
    void *aligned_addr = (void *)((uintptr_t)fault_addr & ~(PAGE_SIZE - 1));

    // Map second page here
    void *next_page_addr = (void *)((uintptr_t)aligned_addr + PAGE_SIZE);

    for (int i = 0; i < phdr_count; ++i) {
        Elf64_Phdr *phdr = &phdr_table[i];
        if (phdr->p_type != PT_LOAD) continue;

        void *seg_start = (void *)(phdr->p_vaddr & ~(PAGE_SIZE - 1));
        void *seg_end = (void *)(((phdr->p_vaddr + phdr->p_memsz - 1) & ~(PAGE_SIZE - 1)) + PAGE_SIZE);

        if (aligned_addr >= seg_start && aligned_addr < seg_end) {
            // Map the required page
            mmap(aligned_addr, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

            // Predict and map the next page if it falls within the segment
            if (next_page_addr < seg_end) {
                mmap(next_page_addr, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            }
            return;
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
    for (int i = 0; i < ehdr.e_phnum && i < MAX_PHDR_COUNT; ++i) {
        Elf64_Phdr phdr;
        if (read(program_fd, &phdr, sizeof(phdr)) != sizeof(phdr)) {
            perror("read");
            exit(EXIT_FAILURE);
        }

        if (phdr.p_type == PT_LOAD) {
            phdr_table[phdr_count++] = phdr;

            if (!(phdr.p_flags & PF_W)) {  // Map text and initialized data segments at startup
                size_t seg_size = (phdr.p_vaddr + phdr.p_filesz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
                mmap((void *)phdr.p_vaddr, seg_size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_FIXED, program_fd, phdr.p_offset);
            }
        }
    }

    entry_point = ehdr.e_entry;
    close(program_fd);
}

void *setup_stack(int argc, char *argv[], char *envp[]) {
    void *stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (stack == MAP_FAILED) {
        perror("mmap stack");
        exit(EXIT_FAILURE);
    }

    char **new_argv = (char **)((char *)stack + STACK_SIZE - (argc + 1) * sizeof(char *));
    char **new_envp = new_argv + argc + 1;

    for (int i = 0; i < argc; i++) {
        new_argv[i] = argv[i];
    }
    new_argv[argc] = NULL;

    for (int i = 0; envp[i] != NULL; i++) {
        new_envp[i] = envp[i];
    }

    // Auxiliary vector setup
    Elf64_auxv_t *auxv;
    for (auxv = (Elf64_auxv_t *)(new_envp); auxv->a_type != AT_NULL; ++auxv) {
        if (auxv->a_type == AT_PHDR) {
            auxv->a_un.a_val = (uintptr_t)phdr_table;  // Address of the program header table
        }
    }

    return new_argv - 1; // Stack pointer should point to argc
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <ELF-file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    setup_signal_handler();
    load_program(argv[1]);

    // Set up stack for the loaded program
    void *stack_top = setup_stack(argc - 1, argv + 1, environ);

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

    // This line should never be reached.
    return 0;
}
