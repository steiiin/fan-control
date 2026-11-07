#include "status.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

int fan_status_format(char *buffer, size_t size, const fan_status *status) {
    char cpu_load[32] = "N/A", gpu_load[32] = "N/A", max_load[32] = "N/A";
    char gpu_temp[64], control[128];
    if (status->cpu_load >= 0)
        snprintf(cpu_load, sizeof(cpu_load), "%.2f%%", status->cpu_load);
    if (status->gpu_load >= 0)
        snprintf(gpu_load, sizeof(gpu_load), "%.2f%%", status->gpu_load);
    if (status->max_load >= 0)
        snprintf(max_load, sizeof(max_load), "%.2f%%", status->max_load);
    if (status->gpu_temp >= 0)
        snprintf(gpu_temp, sizeof(gpu_temp), "%d°C", status->gpu_temp);
    else
        snprintf(gpu_temp, sizeof(gpu_temp), "IGNORED(%d°C raw)", status->gpu_raw);
    if (status->cpu_valid)
        snprintf(control, sizeof(control), "HOT=%d°C AVG3=%d°C STEP=%d TARGET=%d%%",
                 status->hot, status->average, status->step, status->target);
    else
        snprintf(control, sizeof(control), "FAIL-SAFE (invalid CPU temperature)");

    return snprintf(buffer, size,
                    "MODE=%s PROFILE=%s | CPU=%d°C%s GPU=%s | "
                    "LOAD CPU=%s GPU=%s MAX=%s | %s | "
                    "REQUESTED DUTY=%d%% | F1=%d RPM F2=%d RPM",
                    fan_mode_name(status->mode), fan_mode_name(status->profile),
                    status->cpu_temp, status->cpu_valid ? "" : " (INVALID)", gpu_temp,
                    cpu_load, gpu_load, max_load, control,
                    status->duty, status->rpm1, status->rpm2);
}

int fan_status_notify(const char *text) {
    const char *path = getenv("NOTIFY_SOCKET");
    if (!path || !*path) return 0;

    struct sockaddr_un address = { .sun_family = AF_UNIX };
    size_t length = strlen(path);
    if ((path[0] != '/' && path[0] != '@') || length < 2 ||
        length >= sizeof(address.sun_path)) {
        errno = EINVAL;
        return -1;
    }
    memcpy(address.sun_path, path, length + 1);
    socklen_t address_length = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + length);
    if (path[0] == '@')
        address.sun_path[0] = '\0';
    else
        address_length++;

    char message[1024];
    int size = snprintf(message, sizeof(message), "STATUS=%s", text);
    if (size < 0 || (size_t)size >= sizeof(message)) {
        errno = EMSGSIZE;
        return -1;
    }

    int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    ssize_t sent = sendto(fd, message, (size_t)size, MSG_DONTWAIT | MSG_NOSIGNAL,
                          (const struct sockaddr *)&address, address_length);
    int saved_errno = errno;
    close(fd);
    errno = saved_errno;
    return sent == size ? 1 : -1;
}

void fan_status_publish(FILE *output, const fan_status *status) {
    char text[512];
    int length = fan_status_format(text, sizeof(text), status);
    if (length < 0 || (size_t)length >= sizeof(text)) return;
    (void)fan_status_notify(text);
    if (isatty(fileno(output))) {
        fprintf(output, "\r\033[K%s", text);
        fflush(output);
    }
}
