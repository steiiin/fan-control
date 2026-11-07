#ifndef FAN_CURVE_H
#define FAN_CURVE_H

typedef enum {
    FAN_MODE_DEFAULT,
    FAN_MODE_POWER,
    FAN_MODE_SILENT,
    FAN_MODE_AUTO
} fan_mode;

/* Returns 1 for an automatic command (including auto), otherwise 0. */
int fan_mode_from_command(const char *command, fan_mode *mode);
const char *fan_mode_name(fan_mode mode);
int fan_mode_startup_duty(fan_mode mode);
int fan_mode_min_duty(fan_mode mode);
int target_duty_from_avg_temp(fan_mode mode, int temp_c);

#endif
