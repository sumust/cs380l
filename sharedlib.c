#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <string.h>

#define MAGIC 0xCC

void *malloc(size_t size) {
    void *(*real_malloc)(size_t) = dlsym(RTLD_NEXT, "malloc");
  
    void *p = real_malloc(size);
  
    printf("malloc(%zu) returned %p\n", size, p);

    if (p) {
        memset(p, MAGIC, size);
    }
    return p;
}
