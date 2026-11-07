#include "fan_curve.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Keep checks active in Release builds too. */
#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return EXIT_FAILURE; \
    } \
} while (0)

/* Frozen pre-mode implementation for default compatibility regression. */
static int original_target(int temp_c) {
    const struct { int t; int p; } points[] = {
        {45, 20}, {55, 30}, {60, 40}, {70, 50}, {80, 70}, {85, 100}
    };
    const int n = sizeof(points) / sizeof(points[0]);
    if (temp_c <= points[0].t) return points[0].p;
    if (temp_c >= points[n - 1].t) return points[n - 1].p;
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

int main(void) {
    const struct { int temp, power, normal, silent; } samples[] = {
        {40, 40, 20, 20}, {45, 43, 20, 20}, {50, 49, 25, 20},
        {55, 59, 30, 20}, {60, 70, 40, 21}, {65, 81, 45, 24},
        {70, 91, 50, 33}, {75, 97, 60, 47}, {79, 99, 68, 63},
        {80, 100, 70, 67}, {85, 100, 100, 89}, {89, 100, 100, 99},
        {90, 100, 100, 100}
    };
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        CHECK(target_duty_from_avg_temp(FAN_MODE_POWER, samples[i].temp) == samples[i].power);
        CHECK(target_duty_from_avg_temp(FAN_MODE_DEFAULT, samples[i].temp) == samples[i].normal);
        CHECK(target_duty_from_avg_temp(FAN_MODE_SILENT, samples[i].temp) == samples[i].silent);
    }

    const fan_mode modes[] = {FAN_MODE_DEFAULT, FAN_MODE_POWER, FAN_MODE_SILENT};
    const int minimums[] = {20, 40, 20};
    const int startups[] = {40, 40, 20};
    const int idle[] = {45, 40, 50};
    const int full[] = {85, 80, 90};
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        CHECK(fan_mode_min_duty(modes[i]) == minimums[i]);
        CHECK(fan_mode_startup_duty(modes[i]) == startups[i]);
        CHECK(target_duty_from_avg_temp(modes[i], INT_MIN) == minimums[i]);
        CHECK(target_duty_from_avg_temp(modes[i], INT_MAX) == 100);
        int previous = minimums[i];
        for (int t = -20; t <= 150; t++) {
            int duty = target_duty_from_avg_temp(modes[i], t);
            CHECK(duty >= minimums[i] && duty <= 100);
            CHECK(duty >= previous);
            CHECK((duty == 100) == (t >= full[i]));
            if (t <= idle[i]) CHECK(duty == minimums[i]);
            previous = duty;
        }
    }

    for (int t = -20; t <= 150; t++) {
        int normal = target_duty_from_avg_temp(FAN_MODE_DEFAULT, t);
        CHECK(normal == original_target(t));
        CHECK(target_duty_from_avg_temp(FAN_MODE_POWER, t) >= normal);
        CHECK(normal >= target_duty_from_avg_temp(FAN_MODE_SILENT, t));
    }

    const struct { const char *command; fan_mode mode; const char *name; } commands[] = {
        {"auto", FAN_MODE_DEFAULT, "default"},
        {"default", FAN_MODE_DEFAULT, "default"},
        {"power", FAN_MODE_POWER, "power"},
        {"silent", FAN_MODE_SILENT, "silent"}
    };
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        fan_mode mode = FAN_MODE_SILENT;
        CHECK(fan_mode_from_command(commands[i].command, &mode));
        CHECK(mode == commands[i].mode);
        CHECK(strcmp(fan_mode_name(mode), commands[i].name) == 0);
    }
    fan_mode mode = FAN_MODE_DEFAULT;
    CHECK(!fan_mode_from_command("turbo", &mode));
    CHECK(!fan_mode_from_command("set", &mode));
    CHECK(!fan_mode_from_command("", &mode));
    return EXIT_SUCCESS;
}
