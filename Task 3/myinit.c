#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <signal.h>
#include <time.h>
#include <getopt.h>

#define LOG_PATH        "/tmp/myinit.log"
#define PID_PATH        "/tmp/myinit.pid"
#define MAX_LINE_LEN    4096
#define MAX_PROCS       256

typedef struct {
    pid_t pid;
    char cmd[MAX_LINE_LEN];
    char in[MAX_LINE_LEN];
    char out[MAX_LINE_LEN];
} child_t;

static child_t children[MAX_PROCS];
static int nchildren = 0;
static int logfd = -1;
static volatile sig_atomic_t sighup_received = 0;

static void logmsg(const char *fmt, ...) {
    if (logfd < 0) return;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    char timebuf[64];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm);
    dprintf(logfd, "[%s] ", timebuf);

    va_list ap;
    va_start(ap, fmt);
    vdprintf(logfd, fmt, ap);
    va_end(ap);
    dprintf(logfd, "\n");
    fsync(logfd);
}

static void sighup_handler(int sig) {
    (void)sig;
    sighup_received = 1;
}
static void sigchld_handler(int sig) {
    (void)sig;
}

static void daemonize(void) {
    if (getppid() != 1) {
        signal(SIGTTOU, SIG_IGN);
        signal(SIGTTIN, SIG_IGN);
        signal(SIGTSTP, SIG_IGN);
    }

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); exit(EXIT_FAILURE); }
    if (pid > 0) _exit(EXIT_SUCCESS);

    if (setsid() < 0) { perror("setsid"); exit(EXIT_FAILURE); }

    pid = fork();
    if (pid < 0) { perror("fork2"); exit(EXIT_FAILURE); }
    if (pid > 0) _exit(EXIT_SUCCESS);

    if (chdir("/") < 0) { perror("chdir"); exit(EXIT_FAILURE); }

    struct rlimit flim;
    if (getrlimit(RLIMIT_NOFILE, &flim) < 0) { perror("getrlimit"); exit(EXIT_FAILURE); }
    for (int fd = 0; fd < (int)flim.rlim_max; fd++)
        close(fd);

    int fd0 = open("/dev/null", O_RDWR);
    if (fd0 < 0) exit(EXIT_FAILURE);
    int fd1 = dup(fd0);
    int fd2 = dup(fd0);
    if (fd0 != 0 || fd1 != 1 || fd2 != 2) exit(EXIT_FAILURE);

    logfd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (logfd < 0) exit(EXIT_FAILURE);

    int pidfd = open(PID_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (pidfd >= 0) {
        dprintf(pidfd, "%d\n", getpid());
        close(pidfd);
    } else {
        logmsg("Warning: cannot write PID file %s", PID_PATH);
    }
}

static void safe_strncpy(char *dst, const char *src, size_t size) {
    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
}

static void spawn_child(const char *cmd, const char *in, const char *out) {
    pid_t pid = fork();
    if (pid < 0) {
        logmsg("fork failed: %s", strerror(errno));
        return;
    }
    if (pid == 0) {
        struct sigaction sa;
        sa.sa_handler = SIG_DFL;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = 0;
        sigaction(SIGHUP, &sa, NULL);
        sigaction(SIGCHLD, &sa, NULL);

        if (in && in[0]) {
            int fd = open(in, O_RDONLY);
            if (fd < 0) { perror("open stdin"); _exit(EXIT_FAILURE); }
            if (dup2(fd, STDIN_FILENO) < 0) { perror("dup2 stdin"); _exit(EXIT_FAILURE); }
            close(fd);
        }
        if (out && out[0]) {
            int fd = open(out, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (fd < 0) { perror("open stdout"); _exit(EXIT_FAILURE); }
            if (dup2(fd, STDOUT_FILENO) < 0) { perror("dup2 stdout"); _exit(EXIT_FAILURE); }
            close(fd);
        }

        char *args[MAX_LINE_LEN / 2 + 2];
        char *cmd_copy = strdup(cmd);
        if (!cmd_copy) { perror("strdup"); _exit(EXIT_FAILURE); }
        int i = 0;
        char *token = strtok(cmd_copy, " ");
        while (token && i < (int)(sizeof(args)/sizeof(args[0]) - 1)) {
            args[i++] = token;
            token = strtok(NULL, " ");
        }
        args[i] = NULL;
        free(cmd_copy);

        execv(args[0], args);
        perror("execv");
        _exit(EXIT_FAILURE);
    }

    if (nchildren < MAX_PROCS) {
        children[nchildren].pid = pid;
        safe_strncpy(children[nchildren].cmd, cmd, sizeof(children[nchildren].cmd));
        safe_strncpy(children[nchildren].in,  in  ? in  : "", sizeof(children[nchildren].in));
        safe_strncpy(children[nchildren].out, out ? out : "", sizeof(children[nchildren].out));
        nchildren++;
        logmsg("Started child PID %d: %s", pid, cmd);
    } else {
        logmsg("Too many children, cannot start: %s", cmd);
        kill(pid, SIGTERM);
        waitpid(pid, NULL, 0);
    }
}

static void load_config(const char *config_path) {
    FILE *fp = fopen(config_path, "r");
    if (!fp) {
        logmsg("Cannot open config file %s: %s", config_path, strerror(errno));
        return;
    }

    char line[MAX_LINE_LEN];
    int linenum = 0;
    while (fgets(line, sizeof(line), fp)) {
        linenum++;
        line[strcspn(line, "\n")] = '\0';
        if (line[0] == '#' || line[0] == '\0') continue;

        char *saveptr, *token, *tokens[128];
        int ntok = 0;
        char *line_dup = strdup(line);
        if (!line_dup) continue;

        token = strtok_r(line_dup, " \t", &saveptr);
        while (token && ntok < 128) {
            tokens[ntok++] = token;
            token = strtok_r(NULL, " \t", &saveptr);
        }

        if (ntok < 3) {
            logmsg("Config line %d: not enough fields, skipping", linenum);
            free(line_dup);
            continue;
        }

        char *stdin_path  = tokens[ntok - 2];
        char *stdout_path = tokens[ntok - 1];
        char cmd[MAX_LINE_LEN] = "";
        for (int i = 0; i < ntok - 2; i++) {
            if (i > 0) strcat(cmd, " ");
            strcat(cmd, tokens[i]);
        }
        free(line_dup);

        if (cmd[0] != '/' || stdin_path[0] != '/' || stdout_path[0] != '/') {
            logmsg("Config line %d: non-absolute path, skipping", linenum);
            continue;
        }

        spawn_child(cmd, stdin_path, stdout_path);
    }
    fclose(fp);
    logmsg("Configuration loaded from %s", config_path);
}

static void kill_all_children(void) {
    logmsg("Terminating all children...");
    for (int i = 0; i < nchildren; i++) {
        if (children[i].pid > 0)
            kill(children[i].pid, SIGTERM);
    }

    for (int i = 0; i < nchildren; i++) {
        if (children[i].pid > 0) {
            int status;
            if (waitpid(children[i].pid, &status, 0) > 0) {
                if (WIFEXITED(status))
                    logmsg("Child %d exited with code %d", children[i].pid, WEXITSTATUS(status));
                else if (WIFSIGNALED(status))
                    logmsg("Child %d killed by signal %d", children[i].pid, WTERMSIG(status));
            }
        }
    }
    nchildren = 0;
    logmsg("All children terminated.");
}

static void remove_child(int idx) {
    if (idx < 0 || idx >= nchildren) return;
    for (int i = idx; i < nchildren - 1; i++)
        children[i] = children[i+1];
    nchildren--;
}

static void respawn_child(int idx) {
    if (idx < 0 || idx >= nchildren) return;

    if (sighup_received) {
        logmsg("Skipping respawn of child %s due to pending SIGHUP", children[idx].cmd);
        remove_child(idx);
        return;
    }

    logmsg("Respawning child from config: %s", children[idx].cmd);

    char cmd_copy[MAX_LINE_LEN], in_copy[MAX_LINE_LEN], out_copy[MAX_LINE_LEN];
    safe_strncpy(cmd_copy, children[idx].cmd, sizeof(cmd_copy));
    safe_strncpy(in_copy,  children[idx].in,  sizeof(in_copy));
    safe_strncpy(out_copy, children[idx].out, sizeof(out_copy));

    remove_child(idx);

    spawn_child(cmd_copy, in_copy, out_copy);
}

int main(int argc, char *argv[]) {
    const char *config_file = NULL;

    int opt;
    while ((opt = getopt(argc, argv, "c:")) != -1) {
        switch (opt) {
        case 'c': config_file = optarg; break;
        default:
            fprintf(stderr, "Usage: %s [-c] <config_file>\n", argv[0]);
            exit(EXIT_FAILURE);
        }
    }
    if (optind < argc) config_file = argv[optind];
    if (!config_file) {
        fprintf(stderr, "Usage: %s <config_file>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    daemonize();

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sighup_handler;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGHUP, &sa, NULL);

    sa.sa_handler = sigchld_handler;
    sa.sa_flags = SA_NOCLDSTOP | SA_RESTART;
    sigaction(SIGCHLD, &sa, NULL);

    load_config(config_file);

    while (1) {
        if (sighup_received) {
            sighup_received = 0;
            logmsg("SIGHUP received, reloading configuration.");
            kill_all_children();
            load_config(config_file);
            continue;
        }

        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid > 0) {
            int idx = -1;
            for (int i = 0; i < nchildren; i++)
                if (children[i].pid == pid) { idx = i; break; }

            if (idx >= 0) {
                if (WIFEXITED(status))
                    logmsg("Child PID %d exited with code %d", pid, WEXITSTATUS(status));
                else if (WIFSIGNALED(status))
                    logmsg("Child PID %d killed by signal %d", pid, WTERMSIG(status));
                respawn_child(idx);
            } else {
                logmsg("Unknown child PID %d terminated", pid);
            }
        } else if (pid < 0 && errno != ECHILD && errno != EINTR) {
            logmsg("waitpid error: %s", strerror(errno));
        }

        usleep(500000);
    }
    return 0;
}