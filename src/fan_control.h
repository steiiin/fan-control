#ifndef FAN_CONTROL_H
#define FAN_CONTROL_H

#include "fan_curve.h"

typedef struct {
    int temperatures[3];
    int next, count;
    int duty, average, step, target;
} fan_control_state;

void fan_control_init(fan_control_state *state, fan_mode mode);
/* hot is the sanitized hotter temperature, or -1 for invalid CPU temperature. */
void fan_control_update(fan_control_state *state, fan_mode profile, int hot);

#endif
