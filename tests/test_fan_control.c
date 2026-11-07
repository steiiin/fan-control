#include "fan_control.h"
#include "system_load.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return EXIT_FAILURE; \
    } \
} while (0)

int main(void) {
    fan_control_state state;
    fan_control_init(&state, FAN_MODE_AUTO);
    CHECK(state.duty == 40 && state.count == 0);
    fan_control_update(&state, fan_mode_from_load(20), 40);
    CHECK(state.duty == 39 && state.average == 40 && state.count == 1);
    fan_control_update(&state, fan_mode_from_load(60), 50);
    CHECK(state.duty == 38 && state.average == 45 && state.count == 2);
    fan_control_update(&state, fan_mode_from_load(95), 60);
    CHECK(state.duty == 40 && state.average == 50 && state.count == 3);
    CHECK(state.target == 49); /* Power minimum applies immediately. */
    fan_control_update(&state, FAN_MODE_POWER, 70);
    CHECK(state.duty == 41 && state.average == 60 && state.count == 3);
    fan_control_update(&state, FAN_MODE_SILENT, 50);
    CHECK(state.duty == 40 && state.average == 60);
    fan_control_update(&state, FAN_MODE_DEFAULT, 60);
    CHECK(state.duty == 40 && state.average == 60);

    int next = state.next;
    fan_control_update(&state, FAN_MODE_SILENT, -1);
    CHECK(state.duty == 100 && state.count == 3 && state.next == next);
    fan_control_update(&state, FAN_MODE_SILENT, 40);
    CHECK(state.duty == 99 && state.average == 50);

    const fan_mode modes[] = {FAN_MODE_DEFAULT, FAN_MODE_POWER, FAN_MODE_SILENT};
    const int minimums[] = {20, 40, 20};
    for (int i = 0; i < 3; i++) {
        fan_control_init(&state, modes[i]);
        CHECK(state.duty == (i == 2 ? 20 : 40));
        for (int j = 0; j < 100; j++) fan_control_update(&state, modes[i], 40);
        CHECK(state.duty == minimums[i]);
        for (int j = 0; j < 100; j++) fan_control_update(&state, modes[i], 95);
        CHECK(state.duty == 100);
        fan_control_update(&state, modes[i], -1);
        CHECK(state.duty == 100);
    }

    fan_control_init(&state, FAN_MODE_DEFAULT);
    fan_control_update(&state, FAN_MODE_DEFAULT, 70);
    CHECK(state.step == 2 && state.duty == 42);
    fan_control_init(&state, FAN_MODE_DEFAULT);
    fan_control_update(&state, FAN_MODE_DEFAULT, 80);
    CHECK(state.step == 4 && state.duty == 44);
    state.duty = 69;
    fan_control_update(&state, FAN_MODE_DEFAULT, 80);
    CHECK(state.duty == 70); /* Do not overshoot a target closer than one step. */

    /* A fully loaded CPU selects power even when the GPU is idle. */
    fan_mode profile = fan_mode_from_load(system_load_max(100, 0));
    CHECK(profile == FAN_MODE_POWER);
    fan_control_init(&state, FAN_MODE_AUTO);
    fan_control_update(&state, profile, 80);
    CHECK(state.target == 100); /* Default would target only 70% at 80°C. */
    CHECK(state.average == 80 && state.step == 4 && state.duty == 44);
    return EXIT_SUCCESS;
}
