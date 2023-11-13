#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <elf.h>

#define PAGE_SIZE 0x1000
#define STACK_SIZE (8 * PAGE_SIZE)

// Auxiliary vector types
#define AT_NULL 0    // End of auxiliary vector
#define AT_ENTRY 9   // Program entry point

void load_segment(int fd, Elf64_Phdr *phdr) {
    size_t aligned_vaddr = phdr->p_vaddr & ~(PAGE_SIZE - 1);
    size_t offset_in_page = phdr->p_vaddr - aligned_vaddr;
    size_t total_mapped_size = (phdr->p_memsz + offset_in_page + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

    int prot = 0;
    if (phdr->p_flags & PF_R) prot |= PROT_READ;
    if (phdr->p_flags & PF_W) prot |= PROT_WRITE;
    if (phdr->p_flags & PF_X) prot |= PROT_EXEC;

    void *segment = mmap((void *)aligned_vaddr, total_mapped_size, prot,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
    if (segment == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    ssize_t read_bytes = pread(fd, (char *)segment + offset_in_page, phdr->p_filesz, phdr->p_offset);
    if (read_bytes != phdr->p_filesz) {
        perror("pread");
        exit(EXIT_FAILURE);
    }

    if (phdr->p_memsz > phdr->p_filesz) {
        memset((char *)segment + offset_in_page + phdr->p_filesz, 0, phdr->p_memsz - phdr->p_filesz);
    }
}

void *setup_stack(void *stack_top, char *const argv[], char *const envp[], Elf64_Addr entry_point) {
    int argc, envc;
    for (argc = 1; argv[argc] != NULL; argc++); // Start from 1 to skip loader's name
    for (envc = 0; envp[envc] != NULL; envc++);

    stack_top = (char *)stack_top - sizeof(char *) * (argc + 1 + envc + 1 + 2); // +2 for auxv null terminator
    char **new_argv = (char **)stack_top;

    for (int i = 1; i < argc; i++) {
        new_argv[i - 1] = strdup(argv[i]);
    }
    new_argv[argc - 1] = NULL;

    char **new_envp = new_argv + argc;
    for (int i = 0; i < envc; i++) {
        new_envp[i] = strdup(envp[i]);
    }
    new_envp[envc] = NULL;

    Elf64_auxv_t *auxv = (Elf64_auxv_t *)(new_envp + envc + 1);
    auxv[0].a_type = AT_ENTRY;
    auxv[0].a_un.a_val = entry_point; // Entry point address
    auxv[1].a_type = AT_NULL;
    auxv[1].a_un.a_val = 0;

    return new_argv - 1; // Return the new stack top, pointing to argc
}

void load_program(const char *program, Elf64_Addr *entry_point) {
    int fd = open(program, O_RDONLY);
    if (fd < 0) {
        perror("open");
        exit(EXIT_FAILURE);
    }

    Elf64_Ehdr ehdr;
    if (read(fd, &ehdr, sizeof(ehdr)) != sizeof(ehdr)) {
        perror("read");
        close(fd);
        exit(EXIT_FAILURE);
    }

    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0 || ehdr.e_ident[EI_CLASS] != ELFCLASS64) {
        fprintf(stderr, "Invalid ELF file.\n");
        close(fd);
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < ehdr.e_phnum; ++i) {
        Elf64_Phdr phdr;
        lseek(fd, ehdr.e_phoff + i * sizeof(phdr), SEEK_SET);
        if (read(fd, &phdr, sizeof(phdr)) != sizeof(phdr)) {
            perror("read");
            close(fd);
            exit(EXIT_FAILURE);
        }

        if (phdr.p_type == PT_LOAD) {
            load_segment(fd, &phdr);
        }
    }

    *entry_point = ehdr.e_entry;
    close(fd);
}

int main(int argc, char *argv[], char *envp[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <executable>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    Elf64_Addr entry_point;
    void *stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
                       MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);
    if (stack == MAP_FAILED) {
        perror("mmap");
        exit(EXIT_FAILURE);
    }

    load_program(argv[1], &entry_point);

    // Align the stack to a 16-byte boundary
    void *stack_top = (char *)stack + STACK_SIZE;
    stack_top = (void *)((uintptr_t)stack_top & ~0xF);

    stack_top = setup_stack(stack_top, argv, envp, entry_point);

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
