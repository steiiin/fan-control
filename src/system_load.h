#ifndef SYSTEM_LOAD_H
#define SYSTEM_LOAD_H

#include <stdint.h>
#include "fan_curve.h"

/* Unavailable readings are -1; valid readings include zero. */
typedef struct {
    uint64_t previous[8];
    int initialized;
} cpu_load_state;

double cpu_load_from_line(cpu_load_state *state, const char *line);
double cpu_load_read(cpu_load_state *state);
double gpu_load_from_text(const char *text);
/* Runs an executable directly, with a 500 ms deadline; also usable by tests. */
double gpu_load_command(const char *path, char *const argv[]);
double gpu_load_sysfs(const char *drm_root);
double gpu_load_read(void);
double system_load_average(double cpu, double gpu);
fan_mode fan_mode_from_load(double average);

#endif
