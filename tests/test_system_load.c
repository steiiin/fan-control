#include "system_load.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return EXIT_FAILURE; \
    } \
} while (0)

static double monotonic_seconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec + now.tv_nsec / 1e9;
}

static int write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "w");
    if (!file) return 0;
    int ok = fputs(text, file) >= 0;
    return fclose(file) == 0 && ok;
}

int main(int argc, char **argv) {
    /* This executable doubles as a deterministic nvidia-smi substitute. */
    if (argc == 2) {
        if (!strcmp(argv[1], "valid")) { puts("20\n91.5"); return 0; }
        if (!strcmp(argv[1], "invalid")) { puts("N/A"); return 0; }
        if (!strcmp(argv[1], "failed")) { puts("100"); return 1; }
        if (!strcmp(argv[1], "timeout") || !strcmp(argv[1], "closed")) {
            if (!strcmp(argv[1], "closed")) close(STDOUT_FILENO);
            else { puts("100"); fflush(stdout); }
            sleep(10);
            return 0;
        }
        if (!strcmp(argv[1], "overflow")) {
            for (int i = 0; i < 10000; i++) puts("100");
            return 0;
        }
        if (!strcmp(argv[1], "nul")) {
            const char output[] = {'9', '0', '\0', 'x', '\n'};
            return fwrite(output, 1, sizeof(output), stdout) == sizeof(output) ? 0 : 1;
        }
        return 1;
    }

    const struct { double cpu, gpu, average; fan_mode mode; } cases[] = {
        {0, 0, 0, FAN_MODE_SILENT}, {100, 100, 100, FAN_MODE_POWER},
        {100, 0, 50, FAN_MODE_DEFAULT}, {39, 40, 39.5, FAN_MODE_SILENT},
        {40, 40, 40, FAN_MODE_DEFAULT}, {90, 90, 90, FAN_MODE_DEFAULT},
        {90, 91, 90.5, FAN_MODE_POWER}, {39.99, 39.99, 39.99, FAN_MODE_SILENT},
        {90.01, 90.01, 90.01, FAN_MODE_POWER},
        {-1, 95, 95, FAN_MODE_POWER}, {10, -1, 10, FAN_MODE_SILENT},
        {-1, 0, 0, FAN_MODE_SILENT}, {0, -1, 0, FAN_MODE_SILENT},
        {-1, -1, -1, FAN_MODE_DEFAULT}, {NAN, 60, 60, FAN_MODE_DEFAULT},
        {101, 100, 100, FAN_MODE_POWER}, {INFINITY, NAN, -1, FAN_MODE_DEFAULT}
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        double mean = system_load_average(cases[i].cpu, cases[i].gpu);
        CHECK(fabs(mean - cases[i].average) < 1e-9);
        CHECK(fan_mode_from_load(mean) == cases[i].mode);
    }
    CHECK(fan_mode_from_load(NAN) == FAN_MODE_DEFAULT);
    CHECK(fan_mode_from_load(101) == FAN_MODE_DEFAULT);

    cpu_load_state cpu = {0};
    CHECK(cpu_load_from_line(&cpu, "cpu 100 20 30 400 50 6 7 8 90 10\n") == -1);
    /* 100 busy ticks / 200 total; guest counters must not be counted twice. */
    CHECK(cpu_load_from_line(&cpu, "cpu 150 30 50 480 70 11 12 18 140 20\n") == 50);
    CHECK(cpu_load_from_line(&cpu, "cpu 150 30 50 480 70 11 12 18 140 20\n") == -1);
    CHECK(cpu_load_from_line(&cpu, "cpu 150 30 50 490 70 11 12 18\n") == 0);
    CHECK(cpu_load_from_line(&cpu, "cpu 151 30 50 490 70 11 12 18\n") == 100);
    CHECK(cpu_load_from_line(&cpu, "cpu 1 0 0 10 0 0 0 0\n") == -1);
    CHECK(cpu_load_from_line(&cpu, "cpu 2 0 0 11 0 0 0 0\n") == 50);
    /* A single decreasing field invalidates the interval even if total rises. */
    CHECK(cpu_load_from_line(&cpu, "cpu 3 0 0 9 0 0 0 0\n") == -1);
    CHECK(cpu_load_from_line(&cpu, "cpu 4 0 0 10 0 0 0 0\n") == 50);

    const char *bad_cpu[] = {NULL, "", "cpu0 1 2 3 4", "cpu 1 2 3", "cpu -1 2 3 4",
        "cpu 1 2 3 4x", "cpu 1 2 3 NaN", "cpu 18446744073709551616 0 0 0"};
    for (size_t i = 0; i < sizeof(bad_cpu) / sizeof(bad_cpu[0]); i++) {
        CHECK(cpu_load_from_line(&cpu, bad_cpu[i]) == -1);
        CHECK(!cpu.initialized);
        CHECK(cpu_load_from_line(&cpu, "cpu 1 0 0 1") == -1);
    }
    CHECK(fabs(cpu_load_from_line(&cpu, "cpu 2 0 0 3") - 100.0 / 3) < 1e-9);

    CHECK(gpu_load_from_text(NULL) == -1);
    CHECK(gpu_load_from_text("0\n") == 0);
    CHECK(gpu_load_from_text(" 10 \n95.5\r\n80\n") == 95.5);
    CHECK(gpu_load_from_text("N/A\n45\n[Not Supported]\n") == 45);
    const char *bad_gpu[] = {"", " \n", "N/A", "NaN", "inf", "-1", "101",
                             "90%", "50garbage", "10 20", "1e999"};
    for (size_t i = 0; i < sizeof(bad_gpu) / sizeof(bad_gpu[0]); i++)
        CHECK(gpu_load_from_text(bad_gpu[i]) == -1);

    char *command[] = {"/proc/self/exe", "valid", NULL};
    CHECK(gpu_load_command(command[0], command) == 91.5);
    const char *failures[] = {"invalid", "failed", "overflow", "nul", "timeout", "closed"};
    for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); i++) {
        command[1] = (char *)failures[i];
        double start = monotonic_seconds();
        CHECK(gpu_load_command(command[0], command) == -1);
        CHECK(monotonic_seconds() - start < 2.0);
        /* The child must already have been reaped, including on timeout. */
        errno = 0;
        CHECK(waitpid(-1, NULL, WNOHANG) == -1 && errno == ECHILD);
    }
    CHECK(gpu_load_command("/nonexistent/fan-control-test", command) == -1);

    char root[] = "/tmp/fan-control-load-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    CHECK(gpu_load_sysfs(root) == -1);
    const char *cards[] = {"card0", "card12", "card1-HDMI-A-1", "card2"};
    const char *values[] = {"0\n", "77.5\n", "100\n", "N/A\n"};
    for (int i = 0; i < 4; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", root, cards[i]);
        CHECK(mkdir(path, 0700) == 0);
        snprintf(path, sizeof(path), "%s/%s/device", root, cards[i]);
        CHECK(mkdir(path, 0700) == 0);
        snprintf(path, sizeof(path), "%s/%s/device/gpu_busy_percent", root, cards[i]);
        CHECK(write_text(path, values[i]));
        if (i == 0) CHECK(gpu_load_sysfs(root) == 0);
    }
    CHECK(gpu_load_sysfs(root) == 77.5);
    for (int i = 0; i < 4; i++) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s/device/gpu_busy_percent", root, cards[i]);
        CHECK(unlink(path) == 0);
        snprintf(path, sizeof(path), "%s/%s/device", root, cards[i]);
        CHECK(rmdir(path) == 0);
        snprintf(path, sizeof(path), "%s/%s", root, cards[i]);
        CHECK(rmdir(path) == 0);
    }
    CHECK(rmdir(root) == 0);
    CHECK(gpu_load_sysfs(root) == -1);
    return EXIT_SUCCESS;
}
