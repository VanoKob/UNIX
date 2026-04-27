#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/stat.h>

#define LOCK_SUFFIX ".lck"
#define STAT_FILE   "statistics.txt"

volatile sig_atomic_t terminating = 0;
volatile sig_atomic_t lock_count = 0;

static void sig_handler(int sig) {
    (void)sig;
    terminating = 1;
}

static int acquire_lock(const char *base) {
    char lockname[4096];
    snprintf(lockname, sizeof(lockname), "%s%s", base, LOCK_SUFFIX);

    while (!terminating) {
        int fd = open(lockname, O_WRONLY | O_CREAT | O_EXCL, 0644);
        if (fd >= 0) {
            char buf[32];
            int len = snprintf(buf, sizeof(buf), "%d\n", getpid());
            if (write(fd, buf, len) != len) {
                perror("write pid to lock");
                close(fd);
                exit(EXIT_FAILURE);
            }
            close(fd);
            return 0;
        }
        if (errno != EEXIST) {
            perror("open lock");
            exit(EXIT_FAILURE);
        }
        usleep(100000);
    }
    return -1;
}

static void release_lock(const char *base) {
    char lockname[4096];
    snprintf(lockname, sizeof(lockname), "%s%s", base, LOCK_SUFFIX);

    int fd = open(lockname, O_RDONLY);
    if (fd < 0) {
        fprintf(stderr, "release_lock: cannot open %s: %s\n", lockname, strerror(errno));
        exit(EXIT_FAILURE);
    }

    char buf[32] = {0};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n < 0) {
        perror("read lock");
        close(fd);
        exit(EXIT_FAILURE);
    }
    close(fd);

    pid_t mypid = getpid();
    pid_t filepid = (pid_t)strtol(buf, NULL, 10);
    if (filepid != mypid) {
        fprintf(stderr, "Lock PID mismatch: expected %d, found %d\n", mypid, filepid);
        exit(EXIT_FAILURE);
    }

    if (unlink(lockname) < 0) {
        perror("unlink lock");
        exit(EXIT_FAILURE);
    }
}

static void do_work(const char *base) {
    int fd = open(base, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        perror("open base file for work");
        exit(EXIT_FAILURE);
    }

    char c;
    if (read(fd, &c, 1) == 1) {
        lseek(fd, 0, SEEK_SET);
        if (write(fd, &c, 1) != 1) {
            perror("write base");
            close(fd);
            exit(EXIT_FAILURE);
        }
    } else {
        c = 'X';
        if (write(fd, &c, 1) != 1) {
            perror("write base (init)");
            close(fd);
            exit(EXIT_FAILURE);
        }
    }
    close(fd);
    sleep(1);
}

static void write_statistics(void) {
    int fd = open(STAT_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        perror("open statistics file");
        exit(EXIT_FAILURE);
    }

    char line[128];
    int len = snprintf(line, sizeof(line), "%d %d\n", getpid(), (int)lock_count);
    if (write(fd, line, len) != len) {
        perror("write statistics");
        close(fd);
        exit(EXIT_FAILURE);
    }
    close(fd);
}

int main(int argc, char *argv[]) {
    int opt;
    while ((opt = getopt(argc, argv, "")) != -1) {
        switch (opt) {
        default:
            fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Error: missing filename argument.\n");
        fprintf(stderr, "Usage: %s <filename>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    const char *basefile = argv[optind];

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sig_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) < 0 || sigaction(SIGTERM, &sa, NULL) < 0) {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }

    while (!terminating) {
        if (acquire_lock(basefile) != 0)
            break;

        lock_count++;

        do_work(basefile);

        release_lock(basefile);

        if (terminating)
            break;
    }

    write_statistics();

    return 0;
}
