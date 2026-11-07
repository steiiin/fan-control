#include "system_load.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int valid_load(double value) {
    return isfinite(value) && value >= 0 && value <= 100;
}

double system_load_average(double cpu, double gpu) {
    if (valid_load(cpu) && valid_load(gpu)) return (cpu + gpu) / 2.0;
    if (valid_load(cpu)) return cpu;
    if (valid_load(gpu)) return gpu;
    return -1;
}

fan_mode fan_mode_from_load(double average) {
    if (!valid_load(average)) return FAN_MODE_DEFAULT;
    if (average > 90) return FAN_MODE_POWER;
    if (average < 40) return FAN_MODE_SILENT;
    return FAN_MODE_DEFAULT;
}

double cpu_load_from_line(cpu_load_state *state, const char *line) {
    uint64_t counters[8] = {0};
    int count = 0;
    if (!line || strncmp(line, "cpu", 3) || !isspace((unsigned char)line[3]))
        goto invalid;
    const char *p = line + 3;
    while (*p) {
        while (isspace((unsigned char)*p)) p++;
        if (!*p) break;
        if (!isdigit((unsigned char)*p)) goto invalid;
        errno = 0;
        char *end;
        unsigned long long value = strtoull(p, &end, 10);
        if (errno || (*end && !isspace((unsigned char)*end)) || value > UINT64_MAX)
            goto invalid;
        /* guest/guest_nice are already included in user/nice. */
        if (count < 8) counters[count] = (uint64_t)value;
        count++;
        p = end;
    }
    if (count < 4) goto invalid;

    int usable = state->initialized;
    long double total = 0, idle = 0;
    for (int i = 0; i < 8; i++) {
        if (counters[i] < state->previous[i]) usable = 0;
        if (usable) {
            uint64_t delta = counters[i] - state->previous[i];
            total += delta;
            if (i == 3 || i == 4) idle += delta;
        }
    }
    memcpy(state->previous, counters, sizeof(counters));
    state->initialized = 1;
    if (!usable || total <= 0) return -1;
    return (double)(100.0L * (total - idle) / total);

invalid:
    state->initialized = 0;
    return -1;
}

double cpu_load_read(cpu_load_state *state) {
    FILE *file = fopen("/proc/stat", "r");
    char line[1024];
    if (!file) return cpu_load_from_line(state, NULL);
    int ok = fgets(line, sizeof(line), file) != NULL;
    if (ok && !strchr(line, '\n') && !feof(file)) ok = 0;
    fclose(file);
    return cpu_load_from_line(state, ok ? line : NULL);
}

double gpu_load_from_text(const char *text) {
    double best = -1;
    const char *p = text;
    while (p && *p) {
        const char *newline = strchr(p, '\n');
        const char *limit = newline ? newline : p + strlen(p);
        while (p < limit && isspace((unsigned char)*p)) p++;
        errno = 0;
        char *end;
        double value = strtod(p, &end);
        int parsed = end != p;
        while (end < limit && isspace((unsigned char)*end)) end++;
        if (parsed && end == limit && !errno && valid_load(value) && value > best)
            best = value;
        if (!newline) break;
        p = newline + 1;
    }
    return best;
}

static int64_t monotonic_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

double gpu_load_command(const char *path, char *const argv[]) {
    int64_t start = monotonic_ms();
    int pipefd[2];
    if (start < 0 || pipe(pipefd) != 0) return -1;
    if (fcntl(pipefd[0], F_SETFL, O_NONBLOCK) < 0) {
        close(pipefd[0]); close(pipefd[1]);
        return -1;
    }
    pid_t child = fork();
    if (child == 0) {
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) _exit(127);
        if (pipefd[1] != STDOUT_FILENO) close(pipefd[1]);
        int nullfd = open("/dev/null", O_RDWR);
        if (nullfd < 0 || dup2(nullfd, STDIN_FILENO) < 0 ||
            dup2(nullfd, STDERR_FILENO) < 0) _exit(127);
        if (nullfd > STDERR_FILENO) close(nullfd);
        execv(path, argv);
        _exit(127);
    }
    close(pipefd[1]);
    if (child < 0) { close(pipefd[0]); return -1; }

    char output[8192];
    size_t used = 0;
    int exited = 0, eof = 0, status = 0, failed = 0;
    while (!exited || !eof) {
        int64_t now = monotonic_ms();
        if (now < 0 || now - start >= 500) { failed = 1; break; }
        if (!eof) {
            ssize_t n = read(pipefd[0], output + used, sizeof(output) - 1 - used);
            if (n > 0) {
                used += (size_t)n;
                if (used == sizeof(output) - 1) { failed = 1; break; }
            } else if (n == 0) {
                eof = 1;
            } else if (errno != EAGAIN && errno != EINTR) {
                failed = 1; break;
            }
        }
        if (!exited) {
            pid_t result = waitpid(child, &status, WNOHANG);
            if (result == child) exited = 1;
            else if (result < 0 && errno != EINTR) { failed = 1; break; }
        }
        if (!exited || !eof) {
            /* Keep checking child exit even if it closes stdout early. */
            struct pollfd fd = {pipefd[0], POLLIN, 0};
            int remaining = (int)(500 - (now - start));
            int delay = remaining < 10 ? remaining : 10;
            if (poll(eof ? NULL : &fd, eof ? 0 : 1, delay) < 0 && errno != EINTR) {
                failed = 1; break;
            }
        }
    }
    close(pipefd[0]);
    if (!exited) {
        kill(child, SIGKILL);
        while (waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    }
    if (failed || !WIFEXITED(status) || WEXITSTATUS(status) != 0) return -1;
    output[used] = '\0';
    if (memchr(output, '\0', used)) return -1;
    return gpu_load_from_text(output);
}

double gpu_load_sysfs(const char *drm_root) {
    DIR *dir = opendir(drm_root);
    if (!dir) return -1;
    double best = -1;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "card", 4)) continue;
        const char *suffix = entry->d_name + 4;
        if (!isdigit((unsigned char)*suffix)) continue;
        while (isdigit((unsigned char)*suffix)) suffix++;
        if (*suffix) continue;
        char path[512];
        int n = snprintf(path, sizeof(path), "%s/device/gpu_busy_percent", entry->d_name);
        if (n < 0 || (size_t)n >= sizeof(path)) continue;
        int fd = openat(dirfd(dir), path, O_RDONLY);
        if (fd < 0) continue;
        char text[128];
        ssize_t size = read(fd, text, sizeof(text) - 1);
        close(fd);
        if (size <= 0 || (size_t)size == sizeof(text) - 1) continue;
        text[size] = '\0';
        if (memchr(text, '\0', (size_t)size)) continue;
        double value = gpu_load_from_text(text);
        if (value > best) best = value;
    }
    closedir(dir);
    return best;
}

double gpu_load_read(void) {
    char *const argv[] = {"/usr/bin/nvidia-smi", "--query-gpu=utilization.gpu",
                          "--format=csv,noheader,nounits", NULL};
    double value = -1;
    if (access(argv[0], X_OK) == 0) value = gpu_load_command(argv[0], argv);
    return valid_load(value) ? value : gpu_load_sysfs("/sys/class/drm");
}
