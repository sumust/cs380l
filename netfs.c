#define FUSE_USE_VERSION 30
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

static const char *remote_server = NULL;

char *get_temp_filename(const char *path) {
    char *temp = malloc(strlen("/tmp/netfs_") + strlen(path) + 1);
    sprintf(temp, "/tmp/netfs_%s", path + 1); // path + 1 to skip the initial '/'
    return temp;
}

int run_system_command(const char *cmd) {
    int res = system(cmd);
    if (res != 0) {
        return -EIO; // Input/output error
    }
    return 0;
}

static int netfs_getattr(const char *path, struct stat *stbuf) {
    int res;
    char *temp_filename = get_temp_filename(path);
    char cmd[1024 + strlen(temp_filename)];

    snprintf(cmd, sizeof(cmd), "scp %s:%s %s", remote_server, path, temp_filename);

    res = run_system_command(cmd);
    if (res != 0) {
        free(temp_filename);
        return res;
    }

    res = lstat(temp_filename, stbuf);
    free(temp_filename);

    if (res == -1) {
        return -errno;
    }
    return 0;
}

static int netfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    FILE *f;
    int res;
    char *temp_filename = get_temp_filename(path);
    char cmd[1024 + strlen(temp_filename)];

    snprintf(cmd, sizeof(cmd), "scp %s:%s %s", remote_server, path, temp_filename);
    res = run_system_command(cmd);
    if (res != 0) {
        free(temp_filename);
        return res;
    }

    f = fopen(temp_filename, "rb");
    if (f == NULL) {
        free(temp_filename);
        return -errno;
    }

    fseek(f, offset, SEEK_SET);
    res = fread(buf, 1, size, f);
    fclose(f);

    free(temp_filename);

    if (res == -1) {
        return -errno;
    }
    return res;
}

static int netfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    FILE *f;
    int res;
    char *temp_filename = get_temp_filename(path);
    char cmd[1024 + strlen(temp_filename)];

    snprintf(cmd, sizeof(cmd), "scp %s:%s %s", remote_server, path, temp_filename);
    res = run_system_command(cmd);
    if (res != 0) {
        free(temp_filename);
        return res;
    }

    f = fopen(temp_filename, "r+b");
    if (f == NULL) {
        free(temp_filename);
        return -errno;
    }

    fseek(f, offset, SEEK_SET);
    res = fwrite(buf, 1, size, f);
    fclose(f);

    if (res == -1) {
        free(temp_filename);
        return -errno;
    }

    snprintf(cmd, sizeof(cmd), "scp %s %s:%s", temp_filename, remote_server, path);
    res = run_system_command(cmd);

    free(temp_filename);

    if (res != 0) {
        return res;
    }
    return res;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <mountpoint> <server>\n", argv[0]);
        return 1;
    }

    remote_server = argv[argc - 1];
    argv[argc - 1] = NULL;
    argc--;

    return fuse_main(argc, argv, &netfs_oper, NULL);
}
