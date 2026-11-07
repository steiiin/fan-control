#include "fan_curve.h"

#include <string.h>

int fan_mode_from_command(const char *command, fan_mode *mode) {
    if (strcmp(command, "auto") == 0)
        *mode = FAN_MODE_AUTO;
    else if (strcmp(command, "default") == 0)
        *mode = FAN_MODE_DEFAULT;
    else if (strcmp(command, "power") == 0)
        *mode = FAN_MODE_POWER;
    else if (strcmp(command, "silent") == 0)
        *mode = FAN_MODE_SILENT;
    else
        return 0;
    return 1;
}

const char *fan_mode_name(fan_mode mode) {
    switch (mode) {
    case FAN_MODE_AUTO: return "auto";
    case FAN_MODE_POWER: return "power";
    case FAN_MODE_SILENT: return "silent";
    default: return "default";
    }
}

int fan_mode_startup_duty(fan_mode mode) {
    return mode == FAN_MODE_SILENT ? 20 : 40;
}

int fan_mode_min_duty(fan_mode mode) {
    return mode == FAN_MODE_POWER ? 40 : 20;
}

static int default_target_duty(int temp_c) {
    const struct { int t; int p; } points[] = {
        {45, 20},
        {55, 30},
        {60, 40},
        {70, 50},
        {80, 70},
        {85, 100}
    };
    const int n = sizeof(points) / sizeof(points[0]);

    if (temp_c <= points[0].t)
        return points[0].p;
    if (temp_c >= points[n - 1].t)
        return points[n - 1].p;

    /* Preserve the original linear interpolation and rounding exactly. */
    for (int i = 0; i < n - 1; i++) {
        int t1 = points[i].t, p1 = points[i].p;
        int t2 = points[i + 1].t, p2 = points[i + 1].p;
        if (temp_c >= t1 && temp_c < t2) {
            double fraction = (double)(temp_c - t1) / (double)(t2 - t1);
            double percentage = p1 + (p2 - p1) * fraction;
            return (int)(percentage + 0.5);
        }
    }
    return 100;
}

int target_duty_from_avg_temp(fan_mode mode, int temp_c) {
    if (mode == FAN_MODE_DEFAULT || mode == FAN_MODE_AUTO)
        return default_target_duty(temp_c);

    int idle_c = mode == FAN_MODE_POWER ? 40 : 50;
    int full_c = mode == FAN_MODE_POWER ? 80 : 90;
    int minimum = fan_mode_min_duty(mode);

    /* Clamp before normalization, including extreme integer inputs. */
    if (temp_c <= idle_c) return minimum;
    if (temp_c >= full_c) return 100;

    double x = (double)(temp_c - idle_c) / (double)(full_c - idle_c);
    /* Delay silent's rise while retaining a smooth join at both ends. */
    if (mode == FAN_MODE_SILENT) x *= x;
    double smooth = x * x * (3.0 - 2.0 * x);
    int duty = (int)(minimum + (100 - minimum) * smooth + 0.5);
    /* Rounding must not reach full speed before the temperature threshold. */
    return duty < 100 ? duty : 99;
}
