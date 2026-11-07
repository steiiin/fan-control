/* Run real control-loop iterations with all hardware and utilization I/O
 * replaced. No EC access, GPU subprocess, or real sleep is possible. */
#include <dirent.h>
#include <stdio.h>
#include <sys/io.h>
#include <unistd.h>
#include "system_load.h"
#include "status.h"

static unsigned char mock_inb(unsigned short port);
static void mock_outb(unsigned char value, unsigned short port);
static DIR *mock_opendir(const char *path);
static FILE *mock_popen(const char *command, const char *mode);
static int mock_usleep(useconds_t duration);
static int mock_ioperm(unsigned long from, unsigned long num, int turn_on);
static double mock_cpu_load(cpu_load_state *state);
static double mock_gpu_load(void);
static void capture_status(FILE *output, const fan_status *status);

#define main fan_control_main
#define inb mock_inb
#define outb mock_outb
#define ioperm mock_ioperm
#define opendir mock_opendir
#define popen mock_popen
#define usleep mock_usleep
#define cpu_load_read mock_cpu_load
#define gpu_load_read mock_gpu_load
#define fan_status_publish capture_status
#include "../src/main.c"
#undef main
#undef inb
#undef outb
#undef ioperm
#undef opendir
#undef popen
#undef usleep
#undef cpu_load_read
#undef gpu_load_read
#undef fan_status_publish

static int sensor_value, load_queries, reports;
static fan_status last_status;

static unsigned char mock_inb(unsigned short port) {
    return port == EC_SC ? 1 : (unsigned char)sensor_value;
}
static void mock_outb(unsigned char value, unsigned short port) {
    (void)value; (void)port;
}
static int mock_ioperm(unsigned long from, unsigned long num, int turn_on) {
    (void)from; (void)num; (void)turn_on;
    return 0;
}
static DIR *mock_opendir(const char *path) { (void)path; return NULL; }
static FILE *mock_popen(const char *command, const char *mode) {
    (void)command; (void)mode;
    return NULL;
}
static int mock_usleep(useconds_t duration) {
    if (duration == 1000000) g_stop = 1;
    return 0;
}
static double mock_cpu_load(cpu_load_state *state) {
    (void)state; load_queries++; return 18;
}
static double mock_gpu_load(void) { load_queries++; return 3; }
static void capture_status(FILE *output, const fan_status *status) {
    last_status = *status;
    reports++;
    fan_status_publish(output, status);
}

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

int main(void) {
    alarm(10);
    CHECK(unsetenv("NOTIFY_SOCKET") == 0);
    fan_mode modes[] = {FAN_MODE_AUTO, FAN_MODE_DEFAULT, FAN_MODE_POWER, FAN_MODE_SILENT};
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        for (int valid = 0; valid <= 1; valid++) {
            FILE *capture = tmpfile();
            CHECK(capture != NULL);
            fflush(stdout);
            int saved_stdout = dup(STDOUT_FILENO);
            CHECK(saved_stdout >= 0);
            CHECK(dup2(fileno(capture), STDOUT_FILENO) >= 0);
            sensor_value = valid ? 50 : 0;
            g_stop = load_queries = reports = 0;
            CHECK(cmd_auto(modes[i]) == 0);
            CHECK(reports == 1);
            CHECK(last_status.mode == modes[i]);
            CHECK(last_status.cpu_valid == valid);
            CHECK(last_status.cpu_temp == sensor_value);
            CHECK(load_queries == (modes[i] == FAN_MODE_AUTO ? 2 : 0));
            CHECK(last_status.profile == (modes[i] == FAN_MODE_AUTO ? FAN_MODE_SILENT : modes[i]));
            CHECK(last_status.cpu_load == (modes[i] == FAN_MODE_AUTO ? 18 : -1));
            if (!valid) CHECK(last_status.duty == 100);
            fflush(stdout);
            CHECK(dup2(saved_stdout, STDOUT_FILENO) >= 0);
            close(saved_stdout);
            rewind(capture);
            char actual[512] = {0}, expected[128];
            size_t count = fread(actual, 1, sizeof(actual) - 1, capture);
            CHECK(count > 0 && !ferror(capture));
            snprintf(expected, sizeof(expected), "Auto mode running: %s\nStopped.\n", fan_mode_name(modes[i]));
            CHECK(strcmp(actual, expected) == 0);
            fclose(capture);
        }
    }
    return EXIT_SUCCESS;
}
