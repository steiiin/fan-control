#define _GNU_SOURCE
#include "status.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s (errno=%d)\n", __FILE__, __LINE__, #condition, errno); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static void expect_message(int fd, const char *expected) {
    char received[1024];
    ssize_t size = recv(fd, received, sizeof(received) - 1, MSG_DONTWAIT);
    CHECK(size >= 0);
    received[size] = '\0';
    CHECK(strcmp(received, expected) == 0);
}

static void test_socket(const char *path, const fan_status *status) {
    struct sockaddr_un address = { .sun_family = AF_UNIX };
    size_t length = strlen(path);
    CHECK(length < sizeof(address.sun_path));
    memcpy(address.sun_path, path, length + 1);
    socklen_t address_length = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + length);
    if (path[0] == '@') address.sun_path[0] = '\0';
    else address_length++;
    int receiver = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    CHECK(receiver >= 0);
    CHECK(bind(receiver, (struct sockaddr *)&address, address_length) == 0);
    CHECK(setenv("NOTIFY_SOCKET", path, 1) == 0);
    CHECK(fan_status_notify("Running") == 1);
    expect_message(receiver, "STATUS=Running");

    FILE *redirected = tmpfile();
    CHECK(redirected != NULL);
    char text[512], expected[1024];
    CHECK(fan_status_format(text, sizeof(text), status) > 0);
    snprintf(expected, sizeof(expected), "STATUS=%s", text);
    fan_status_publish(redirected, status);
    expect_message(receiver, expected);
    CHECK(fflush(redirected) == 0);
    CHECK(ftell(redirected) == 0);
    fclose(redirected);

    /* A full receiver queue must drop updates rather than block control. */
    int result = 1;
    for (int i = 0; i < 100000 && result == 1; i++)
        result = fan_status_notify("Queue test");
    CHECK(result == -1);
    CHECK(errno == EAGAIN || errno == EWOULDBLOCK || errno == ENOBUFS);
    close(receiver);
    CHECK(fan_status_notify("Receiver gone") == -1);
    if (path[0] != '@') CHECK(unlink(path) == 0);
}

static void test_terminal(const fan_status *status) {
    int master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    CHECK(master >= 0);
    CHECK(grantpt(master) == 0);
    CHECK(unlockpt(master) == 0);
    char *name = ptsname(master);
    CHECK(name != NULL);
    FILE *terminal = fopen(name, "w");
    CHECK(terminal != NULL);
    CHECK(isatty(fileno(terminal)));
    fan_status_publish(terminal, status);
    char output[1024];
    ssize_t size = read(master, output, sizeof(output) - 1);
    CHECK(size > 0);
    output[size] = '\0';
    CHECK(output[0] == '\r');
    CHECK(strstr(output, "MODE=auto PROFILE=default") != NULL);
    CHECK(strstr(output, "F1=2100 RPM F2=2050 RPM") != NULL);
    fclose(terminal);
    close(master);
}

int main(void) {
    /* Also bound failures if a regression accidentally makes sendto blocking. */
    alarm(10);
    fan_status status = {
        .mode = FAN_MODE_AUTO, .profile = FAN_MODE_DEFAULT,
        .cpu_temp = 52, .cpu_valid = 1, .gpu_temp = 46, .gpu_raw = 46,
        .cpu_load = 18, .gpu_load = 0, .max_load = 18,
        .hot = 52, .average = 51, .step = 1, .target = 26, .duty = 27,
        .rpm1 = 2100, .rpm2 = 2050
    };
    char text[512];
    int length = fan_status_format(text, sizeof(text), &status);
    CHECK(length > 0 && (size_t)length < sizeof(text));
    CHECK(strstr(text, "CPU=52°C GPU=46°C") != NULL);
    CHECK(strstr(text, "LOAD CPU=18.00% GPU=0.00% MAX=18.00%") != NULL);
    CHECK(strstr(text, "REQUESTED DUTY=27%") != NULL);
    CHECK(strchr(text, '\r') == NULL && strchr(text, '\n') == NULL);
    char small[8];
    CHECK(fan_status_format(small, sizeof(small), &status) == length);
    CHECK(small[sizeof(small) - 1] == '\0');

    CHECK(unsetenv("NOTIFY_SOCKET") == 0);
    CHECK(fan_status_notify("No systemd") == 0);
    CHECK(setenv("NOTIFY_SOCKET", "", 1) == 0);
    CHECK(fan_status_notify("Empty") == 0);
    CHECK(setenv("NOTIFY_SOCKET", "relative/path", 1) == 0);
    CHECK(fan_status_notify("Invalid") == -1);
    char too_long[256];
    memset(too_long, 'x', sizeof(too_long));
    too_long[0] = '/';
    too_long[sizeof(too_long) - 1] = '\0';
    CHECK(setenv("NOTIFY_SOCKET", too_long, 1) == 0);
    CHECK(fan_status_notify("Overlong") == -1);

    char directory[] = "/tmp/fan-control-status-XXXXXX";
    CHECK(mkdtemp(directory) != NULL);
    char path[108];
    snprintf(path, sizeof(path), "%s/notify", directory);
    CHECK(setenv("NOTIFY_SOCKET", path, 1) == 0);
    CHECK(fan_status_notify("Missing receiver") == -1);
    test_socket(path, &status);
    snprintf(path, sizeof(path), "@fan-control-status-%ld", (long)getpid());
    test_socket(path, &status);
    CHECK(rmdir(directory) == 0);
    CHECK(unsetenv("NOTIFY_SOCKET") == 0);
    test_terminal(&status);

    status.cpu_load = status.gpu_load = status.max_load = -1;
    status.gpu_temp = -1;
    status.gpu_raw = 0;
    CHECK(fan_status_format(text, sizeof(text), &status) > 0);
    CHECK(strstr(text, "GPU=IGNORED(0°C raw)") != NULL);
    CHECK(strstr(text, "LOAD CPU=N/A GPU=N/A MAX=N/A") != NULL);
    status.cpu_valid = 0;
    status.cpu_temp = 0;
    status.duty = 100;
    CHECK(fan_status_format(text, sizeof(text), &status) > 0);
    CHECK(strstr(text, "CPU=0°C (INVALID)") != NULL);
    CHECK(strstr(text, "FAIL-SAFE") != NULL);
    CHECK(strstr(text, "REQUESTED DUTY=100%") != NULL);
    CHECK(strstr(text, "AVG3=") == NULL);

    fan_mode modes[] = {FAN_MODE_DEFAULT, FAN_MODE_POWER, FAN_MODE_SILENT};
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        status.mode = status.profile = modes[i];
        CHECK(fan_status_format(text, sizeof(text), &status) > 0);
        char expected[64];
        snprintf(expected, sizeof(expected), "MODE=%s PROFILE=%s",
                 fan_mode_name(modes[i]), fan_mode_name(modes[i]));
        CHECK(strstr(text, expected) != NULL);
    }
    return EXIT_SUCCESS;
}
