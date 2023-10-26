#define FUSE_USE_VERSION 30
#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

static const char *remote_server = NULL;

static int run_cmd(const char *cmd) {
    int res = system(cmd);
    if (res != 0) {
        return -EIO; // Input/output error
    }
    return 0;
}

static int netfs_getattr(const char *path, struct stat *stbuf) {
    char cmd[1024];

    snprintf(cmd, sizeof(cmd), "scp %s:%s /tmp/netfs_tempfile", remote_server, path);
    int res = run_cmd(cmd);
    if (res != 0) {
        return res;
    }

    return lstat("/tmp/netfs_tempfile", stbuf);
}

static int netfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    FILE *f;
    char cmd[1024];

    (void) fi;

    snprintf(cmd, sizeof(cmd), "scp %s:%s /tmp/netfs_tempfile", remote_server, path);
    int res = run_cmd(cmd);
    if (res != 0) {
        return res;
    }

    f = fopen("/tmp/netfs_tempfile", "rb");
    if (f == NULL) {
        return -errno;
    }

    fseek(f, offset, SEEK_SET);
    res = fread(buf, 1, size, f);
    fclose(f);

    return res;
}

static int netfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
    FILE *f;
    char cmd[1024];

    (void) fi;

    snprintf(cmd, sizeof(cmd), "scp %s:%s /tmp/netfs_tempfile", remote_server, path);
    int res = run_cmd(cmd);
    if (res != 0) {
        return res;
    }

    f = fopen("/tmp/netfs_tempfile", "r+b");
    if (f == NULL) {
        return -errno;
    }

    fseek(f, offset, SEEK_SET);
    res = fwrite(buf, 1, size, f);
    fclose(f);

    snprintf(cmd, sizeof(cmd), "scp /tmp/netfs_tempfile %s:%s", remote_server, path);
    res = run_cmd(cmd);
    if (res != 0) {
        return res;
    }

    return res;
}

static struct fuse_operations netfs_oper = {
    .getattr    = netfs_getattr,
    .read       = netfs_read,
    .write      = netfs_write,
};

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <server> <mountpoint>\n", argv[0]);
        return 1;
    }

    remote_server = argv[1];
    argv[1] = argv[0]; 
    return fuse_main(argc - 1, &argv[1], &netfs_oper, NULL);
}
