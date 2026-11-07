#include "fan_control.h"

void fan_control_init(fan_control_state *state, fan_mode mode) {
    *state = (fan_control_state){0};
    state->duty = fan_mode_startup_duty(mode);
}

void fan_control_update(fan_control_state *state, fan_mode profile, int hot) {
    if (hot < 0) {
        state->duty = 100;
        return;
    }
    state->temperatures[state->next] = hot;
    state->next = (state->next + 1) % 3;
    if (state->count < 3) state->count++;
    int sum = 0;
    for (int i = 0; i < state->count; i++) sum += state->temperatures[i];
    state->average = sum / state->count;
    state->step = state->average < 70 ? 1 : state->average < 80 ? 2 : 4;
    state->target = target_duty_from_avg_temp(profile, state->average);
    int diff = state->target - state->duty;
    if (diff > 0) state->duty += diff >= state->step ? state->step : diff;
    else if (diff < 0) state->duty--;
    int minimum = fan_mode_min_duty(profile);
    if (state->duty < minimum) state->duty = minimum;
    if (state->duty > 100) state->duty = 100;
}
