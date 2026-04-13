#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <sys/stat.h>

#define DEFAULT_BLOCK_SIZE 4096

static void print_usage(const char *progname) {
    fprintf(stderr, "Usage: %s [-b SIZE] [infile] outfile\n", progname);
    fprintf(stderr, "  -b, --block-size SIZE   Set block size in bytes (default %d)\n", DEFAULT_BLOCK_SIZE);
}

static int is_all_zero(const unsigned char *buf, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (buf[i] != 0)
            return 0;
    }
    return 1;
}

static ssize_t read_full(int fd, void *buf, size_t count) {
    size_t total = 0;
    ssize_t n;
    
    while (total < count) {
        n = read(fd, (char *)buf + total, count - total);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            break;
        total += n;
    }
    return total;
}

static ssize_t write_full(int fd, const void *buf, size_t count) {
    size_t total = 0;
    ssize_t n;
    
    while (total < count) {
        n = write(fd, (const char *)buf + total, count - total);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (n == 0)
            break;
        total += n;
    }
    return total;
}

int main(int argc, char **argv) {
    int opt;
    size_t block_size = DEFAULT_BLOCK_SIZE;
    const char *infile = NULL;
    const char *outfile = NULL;

    static struct option long_options[] = {
        {"block-size", required_argument, 0, 'b'},
        {"help",       no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    while ((opt = getopt_long(argc, argv, "b:h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'b': {
                long val = atol(optarg);
                if (val <= 0) {
                    fprintf(stderr, "Error: block size must be > 0\n");
                    exit(EXIT_FAILURE);
                }
                block_size = (size_t)val;
                break;
            }
            case 'h':
                print_usage(argv[0]);
                exit(EXIT_SUCCESS);
            default:
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    if (optind == argc - 1) {
        outfile = argv[optind];
    } else if (optind == argc - 2) {
        infile = argv[optind];
        outfile = argv[optind + 1];
    } else {
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    int in_fd;
    if (infile) {
        in_fd = open(infile, O_RDONLY);
        if (in_fd < 0) {
            fprintf(stderr, "Failed to open input file '%s': %s\n", 
                    infile, strerror(errno));
            exit(EXIT_FAILURE);
        }
    } else {
        in_fd = STDIN_FILENO;
    }

    int out_fd = open(outfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd < 0) {
        fprintf(stderr, "Failed to open output file '%s': %s\n", 
                outfile, strerror(errno));
        if (infile) close(in_fd);
        exit(EXIT_FAILURE);
    }

    unsigned char *buf = malloc(block_size);
    if (!buf) {
        fprintf(stderr, "Memory allocation failed: %s\n", strerror(errno));
        close(in_fd);
        close(out_fd);
        exit(EXIT_FAILURE);
    }

    off_t total_bytes = 0;
    ssize_t nread;

    while ((nread = read_full(in_fd, buf, block_size)) > 0) {
        if (is_all_zero(buf, nread)) {
            if (lseek(out_fd, nread, SEEK_CUR) == (off_t)-1) {
                fprintf(stderr, "lseek error: %s\n", strerror(errno));
                free(buf);
                close(in_fd);
                close(out_fd);
                exit(EXIT_FAILURE);
            }
        } else {
            ssize_t written = write_full(out_fd, buf, nread);
            if (written != nread) {
                if (written < 0) {
                    fprintf(stderr, "write error: %s\n", strerror(errno));
                } else {
                    fprintf(stderr, "write error: expected %zd, wrote %zd\n", 
                            nread, written);
                }
                free(buf);
                close(in_fd);
                close(out_fd);
                exit(EXIT_FAILURE);
            }
        }
        total_bytes += nread;
    }

    if (nread < 0) {
        fprintf(stderr, "read error: %s\n", strerror(errno));
        free(buf);
        close(in_fd);
        close(out_fd);
        exit(EXIT_FAILURE);
    }
  
    if (ftruncate(out_fd, total_bytes) < 0) {
        fprintf(stderr, "ftruncate error: %s\n", strerror(errno));
        free(buf);
        close(in_fd);
        close(out_fd);
        exit(EXIT_FAILURE);
    }

    free(buf);
    if (infile) close(in_fd);
    close(out_fd);
    
    return 0;
}
