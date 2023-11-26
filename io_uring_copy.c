#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <liburing.h>

#define QD  2
#define BS (16 * 1024)

// Recursive copying implementating using io_uring 
// CS380L - Final Project
// Non-recursive implementation used for reference: https://unixism.net/loti/tutorial/cp_liburing.html

static int infd, outfd;

struct io_data {
    int read;
    off_t first_offset, offset;
    size_t first_len;
    struct iovec iov;
};

static int setup_context(unsigned entries, struct io_uring *ring) {
    int ret = io_uring_queue_init(entries, ring, 0);
    if (ret < 0) {
        fprintf(stderr, "queue_init: %s\n", strerror(-ret));
        return -1;
    }
    return 0;
}

static int get_file_size(int fd, off_t *size) {
    struct stat st;
    if (fstat(fd, &st) < 0)
        return -1;

    if (S_ISREG(st.st_mode)) {
        *size = st.st_size;
    } else if (S_ISBLK(st.st_mode)) {
        unsigned long long bytes;
        if (ioctl(fd, BLKGETSIZE64, &bytes) != 0)
            return -1;
        *size = bytes;
    } else {
        return -1;
    }
    return 0;
}

static int copy_file_io_uring(int infd, int outfd, struct io_uring *ring, off_t insize) {
    unsigned long reads, writes;
    struct io_uring_cqe *cqe;
    off_t write_left, offset;
    int ret;
  
    write_left = insize;
    writes = reads = offset = 0;

    while (insize || write_left) {
        int had_reads, got_comp;

        had_reads = reads;
        while (insize) {
            off_t this_size = insize;

            if (reads + writes >= QD)
                break;
            if (this_size > BS)
                this_size = BS;
            else if (!this_size)
                break;

            if (queue_read(ring, this_size, offset, infd))
                break;

            insize -= this_size;
            offset += this_size;
            reads++;
        }

        if (had_reads != reads) {
            ret = io_uring_submit(ring);
            if (ret < 0) {
                fprintf(stderr, "io_uring_submit: %s\n", strerror(-ret));
                break;
            }
        }

        got_comp = 0;
        while (write_left) {
            struct io_data *data;

            if (!got_comp) {
                ret = io_uring_wait_cqe(ring, &cqe);
                got_comp = 1;
            } else {
                ret = io_uring_peek_cqe(ring, &cqe);
                if (ret == -EAGAIN) {
                    cqe = NULL;
                    ret = 0;
                }
            }
            if (ret < 0) {
                fprintf(stderr, "io_uring_peek_cqe: %s\n",
                        strerror(-ret));
                return 1;
            }
            if (!cqe)
                break;

            data = io_uring_cqe_get_data(cqe);
            if (cqe->res < 0) {
                if (cqe->res == -EAGAIN) {
                    queue_prepped(ring, data, outfd);
                    io_uring_cqe_seen(ring, cqe);
                    continue;
                }
                fprintf(stderr, "cqe failed: %s\n",
                        strerror(-cqe->res));
                return 1;
            } else if (cqe->res != data->iov.iov_len) {
                data->iov.iov_base += cqe->res;
                data->iov.iov_len -= cqe->res;
                queue_prepped(ring, data, outfd);
                io_uring_cqe_seen(ring, cqe);
                continue;
            }

            if (data->read) {
                queue_write(ring, data, outfd);
                write_left -= data->first_len;
                reads--;
                writes++;
            } else {
                free(data);
                writes--;
            }
            io_uring_cqe_seen(ring, cqe);
        }
    }

    return 0;
}

static int queue_read(struct io_uring *ring, off_t size, off_t offset, int fd) {
    struct io_uring_sqe *sqe;
    struct io_data *data;

    data = malloc(size + sizeof(*data));
    if (!data)
        return 1;

    sqe = io_uring_get_sqe(ring);
    if (!sqe) {
        free(data);
        return 1;
    }

    data->read = 1;
    data->offset = data->first_offset = offset;

    data->iov.iov_base = data + 1;
    data->iov.iov_len = size;
    data->first_len = size;

    io_uring_prep_readv(sqe, fd, &data->iov, 1, offset);
    io_uring_sqe_set_data(sqe, data);
    return 0;
}

static void queue_write(struct io_uring *ring, struct io_data *data, int fd) {
    data->read = 0;
    data->offset = data->first_offset;

    data->iov.iov_base = data + 1;
    data->iov.iov_len = data->first_len;

    queue_prepped(ring, data, fd);
    io_uring_submit(ring);
}

static void queue_prepped(struct io_uring *ring, struct io_data *data, int fd) {
    struct io_uring_sqe *sqe;

    sqe = io_uring_get_sqe(ring);
    assert(sqe);

    if (data->read)
        io_uring_prep_readv(sqe, fd, &data->iov, 1, data->offset);
    else
        io_uring_prep_writev(sqe, fd, &data->iov, 1, data->offset);

    io_uring_sqe_set_data(sqe, data);
}

int copy_recursive(const char *src, const char *dst, struct io_uring *ring) {
    int src_fd, dst_fd;
    off_t insize;
    struct stat statbuf;

    if (lstat(src, &statbuf) != 0) {
        perror("lstat");
        return -1;
    }

    if (S_ISDIR(statbuf.st_mode)) {
        DIR *dir;
        struct dirent *entry;
        char src_path[PATH_MAX];
        char dst_path[PATH_MAX];

        mkdir(dst, statbuf.st_mode);
        if (!(dir = opendir(src)))
            return -1;

        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
                continue;

            snprintf(src_path, sizeof(src_path), "%s/%s", src, entry->d_name);
            snprintf(dst_path, sizeof(dst_path), "%s/%s", dst, entry->d_name);

            if (copy_recursive(src_path, dst_path, ring) != 0) {
                closedir(dir);
                return -1;
            }
        }
        closedir(dir);
    } else if (S_ISREG(statbuf.st_mode)) {
        src_fd = open(src, O_RDONLY);
        if (src_fd < 0) {
            perror("open src");
            return -1;
        }

        dst_fd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, statbuf.st_mode);
        if (dst_fd < 0) {
            perror("open dst");
            close(src_fd);
            return -1;
        }

        if (get_file_size(src_fd, &insize) != 0) {
            close(src_fd);
            close(dst_fd);
            return -1;
        }

        if (copy_file_io_uring(src_fd, dst_fd, ring, insize) != 0) {
            close(src_fd);
            close(dst_fd);
            return -1;
        }

        close(src_fd);
        close(dst_fd);
    }

    return 0;
}

int main(int argc, char *argv[]) {
    struct io_uring ring;
    int ret;

    if (argc < 3) {
        printf("Usage: %s <source> <destination>\n", argv[0]);
        return 1;
    }

    ret = setup_context(QD, &ring);
    if (ret) {
        fprintf(stderr, "setup_context failed\n");
        return 1;
    }

    ret = copy_recursive(argv[1], argv[2], &ring);

    io_uring_queue_exit(&ring);
    return ret;
}
