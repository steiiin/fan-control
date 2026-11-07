#ifndef FAN_STATUS_H
#define FAN_STATUS_H

#include <stddef.h>
#include <stdio.h>

#include "fan_curve.h"

typedef struct {
    fan_mode mode, profile;
    int cpu_temp, cpu_valid, gpu_temp, gpu_raw;
    double cpu_load, gpu_load, max_load;
    int hot, average, step, target, duty, rpm1, rpm2;
} fan_status;

/* Returns the snprintf-style length of a single-line status. */
int fan_status_format(char *buffer, size_t size, const fan_status *status);
/* Best effort: 1 sent, 0 not configured, -1 failed. Never waits for a receiver. */
int fan_status_notify(const char *text);
/* Notify systemd; render periodic output only when output is a terminal. */
void fan_status_publish(FILE *output, const fan_status *status);

#endif
