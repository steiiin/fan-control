/*
 * main.c
 *
 * Commands:
 *   set  <0..100>   - Set both fans to the same duty
 *   set1 <0..100>   - Set CPU (fan1/right) duty
 *   set2 <0..100>   - Set GPU (fan2/left) duty
 *   dump            - Show CPU/GPU temps and each fan's duty/RPM
 *   auto            - Auto mode: adjust EACH fan from its own temp (independent)
 *
 * DISCLAIMER: Direct EC access can be risky. You assume responsibility.

 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/io.h>
#include <time.h>
#include <unistd.h>

/* --- Program name --- */
#define NAME "fan-cli"

/* --- EC I/O ports and commands --- */
#define EC_SC 0x66
#define EC_DATA 0x62
#define IBF 1
#define OBF 0
#define EC_SC_READ_CMD 0x80

/* --- Common EC register layout (typical Clevo) --- */
#define EC_REG_SIZE           0x100
#define EC_REG_CPU_TEMP       0x07
#define EC_REG_GPU_TEMP       0xCD
#define EC_REG_FAN1_DUTY      0xCE
#define EC_REG_FAN2_DUTY      0xCF
#define EC_REG_FAN1_RPM_HI    0xD0
#define EC_REG_FAN1_RPM_LO    0xD1
#define EC_REG_FAN2_RPM_HI    0xD2
#define EC_REG_FAN2_RPM_LO    0xD3

/* --- Conversions / curve params --- */
#define MAX_FAN_RPM           4400.0
#define MIN_FAN_DUTY_PCT      20     /* lower end % - anything below 16% and the fan will not start sometimes */
#define AUTO_MIN_TEMP_C       40     /* run fan above this */
#define AUTO_MAX_TEMP_C       80     /* 100% at/above this */
#define AUTO_MAX_STEP         5      /* max % change per cycle for smoothing */
#define MIN_DEADBAND_C        2      /* hysteresis around AUTO_MIN_TEMP_C */
#define STEP_PCT              2      /* % per iteration (400 ms) */

/* --- Prototypes --- */
static int   ec_init(void);
static int   ec_io_wait(const uint32_t port, const uint32_t flag, const char value);
static uint8_t ec_io_read(const uint32_t port);
static int   ec_io_do(const uint32_t cmd, const uint32_t port, const uint8_t value);

static int   cpu_temp_ec(void);
static int   gpu_temp(void);         /* driver (sysfs/nvidia-smi) first, then EC fallback */

static int   fan1_duty_read(void);
static int   fan2_duty_read(void);
static int   fan1_rpm_read(void);
static int   fan2_rpm_read(void);
static int   fan1_duty_write(int pct);
static int   fan2_duty_write(int pct);

static int   dump_status(void);
static int   cmd_set_both(int pct);
static int   cmd_set1(int pct);
static int   cmd_set2(int pct);
static int   cmd_auto(void);

static int   clamp(int v, int lo, int hi);
static int   map_temp_to_duty(int temp_c);
static int   rpm_from_raw(int hi, int lo);
static int   read_int_from_file(const char *path);
static int   path_exists(const char *path);
static int   gpu_temp_sysfs(void);
static int   gpu_temp_nvidia_smi(void);

static volatile sig_atomic_t g_stop = 0;
static void on_sigint(int sig){ (void)sig; g_stop = 1; }

/* ------------------------- MAIN ------------------------- */

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr,
            "Usage: %s <command>\n"
            "Commands:\n"
            "  set  <0..100>   Set BOTH fans\n"
            "  set1 <0..100>   Set CPU fan\n"
            "  set2 <0..100>   Set GPU fan\n"
            "  dump            Show CPU/GPU temps and fan status\n"
            "  auto            Auto mode (independent control)\n",
            NAME);
        return EXIT_FAILURE;
    }

    if (ec_init() != 0) {
        fprintf(stderr, "EC init failed: %s\n", strerror(errno));
        return EXIT_FAILURE;
    }
    signal(SIGINT, on_sigint);
    signal(SIGTERM, on_sigint);

    if (strcmp(argv[1], "dump") == 0) {
        return dump_status();
    }

    if (strcmp(argv[1], "set") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: %s set <0..100>\n", NAME); return EXIT_FAILURE; }
        int pct = atoi(argv[2]);
        return cmd_set_both(pct);
    }

    if (strcmp(argv[1], "set1") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: %s set1 <0..100>\n", NAME); return EXIT_FAILURE; }
        int pct = atoi(argv[2]);
        return cmd_set1(pct);
    }

    if (strcmp(argv[1], "set2") == 0) {
        if (argc < 3) { fprintf(stderr, "Usage: %s set2 <0..100>\n", NAME); return EXIT_FAILURE; }
        int pct = atoi(argv[2]);
        return cmd_set2(pct);
    }

    if (strcmp(argv[1], "auto") == 0) {
        return cmd_auto();
    }

    fprintf(stderr, "Unknown command: %s\n", argv[1]);
    return EXIT_FAILURE;
}

/* ---------------------- Commands ------------------------ */

static int dump_status(void) {
    int tc = cpu_temp_ec();
    int tg = gpu_temp();  /* may come from driver; EC fallback if needed */

    int d1 = fan1_duty_read();
    int r1 = fan1_rpm_read();

    int d2 = fan2_duty_read();
    int r2 = fan2_rpm_read();

    printf("CPU: %d°C - Fan: %d%% %dRPM\n", tc, d1, r1);
    printf("GPU: %d°C - Fan: %d%% %dRPM\n", tg, d2, r2);
    return 0;
}

static int cmd_set_both(int pct) {
    if (pct < 0 || pct > 100) { fprintf(stderr, "Duty must be 0..100\n"); return EXIT_FAILURE; }
    if (fan1_duty_write(pct) != 0) { fprintf(stderr, "Failed to set fan1\n"); return EXIT_FAILURE; }
    if (fan2_duty_write(pct) != 0) { fprintf(stderr, "Failed to set fan2\n"); return EXIT_FAILURE; }
    usleep(1000 * 1000);
    return dump_status();
}

static int cmd_set1(int pct) {
    if (pct < 0 || pct > 100) { fprintf(stderr, "Duty must be 0..100\n"); return EXIT_FAILURE; }
    if (fan1_duty_write(pct) != 0) { fprintf(stderr, "Failed to set fan1\n"); return EXIT_FAILURE; }
    usleep(500 * 1000);
    return dump_status();
}

static int cmd_set2(int pct) {
    if (pct < 0 || pct > 100) { fprintf(stderr, "Duty must be 0..100\n"); return EXIT_FAILURE; }
    if (fan2_duty_write(pct) != 0) { fprintf(stderr, "Failed to set fan2\n"); return EXIT_FAILURE; }
    usleep(500 * 1000);
    return dump_status();
}

static int target_duty_from_temp_hot(int temp_c, int prev_pct)
{
    // Hard max: immediate full speed for safety
    if (temp_c >= AUTO_MAX_TEMP_C) return 100;

    // Hysteresis around AUTO_MIN_TEMP_C: keep off until clearly above; turn off only when clearly below
    const int on_thr  = AUTO_MIN_TEMP_C + MIN_DEADBAND_C;
    const int off_thr = AUTO_MIN_TEMP_C - MIN_DEADBAND_C;

    if (prev_pct == 0) {
        if (temp_c <= on_thr) return 0;              // remain off until comfortably above min
    } else {
        if (temp_c < off_thr) return 0;              // turn off only when comfortably below min
    }

    // Linear middle: MIN temp -> MIN duty, MAX temp -> 100% (using your variables)
    if (temp_c <= AUTO_MIN_TEMP_C) return MIN_FAN_DUTY_PCT;

    double x0 = (double)AUTO_MIN_TEMP_C, y0 = (double)MIN_FAN_DUTY_PCT;
    double x1 = (double)AUTO_MAX_TEMP_C, y1 = 100.0;
    double duty = y0 + (y1 - y0) * ((temp_c - x0) / (x1 - x0));
    int pct = (int)(duty + 0.5);
    return clamp(pct, 0, 100);
}

static inline int step_toward(int last_pct, int target_pct)
{
    int delta = target_pct - last_pct;
    if (delta > 0)       return last_pct + (delta < STEP_PCT ? delta : STEP_PCT);
    else if (delta < 0)  return last_pct - ((-delta) < STEP_PCT ? -delta : STEP_PCT);
    else                 return last_pct;
}

static int cmd_auto(void) {

    int last = clamp(fan1_duty_read(), 0, 100);
    fan2_duty_write(last); // keep both fans in sync at start

    printf("Auto mode (hotter-of CPU/GPU) running (Ctrl+C to stop)\n");
    while (!g_stop) {
        int tc = cpu_temp_ec();
        int tg = gpu_temp();
        int th = (tc > tg) ? tc : tg;  // ***only the hotter temp***

        int target = target_duty_from_temp_hot(th, last);

        // Safety: if we're already at/above MAX_TEMP, jump straight to 100%
        int newduty = (th >= AUTO_MAX_TEMP_C) ? 100 : step_toward(last, target);
        newduty = clamp(newduty, 0, 100);

        if (newduty != last) {
            fan1_duty_write(newduty);
            fan2_duty_write(newduty);
            last = newduty;
        }

        int r1 = fan1_rpm_read();
        int r2 = fan2_rpm_read();

        printf("CPU=%d°C  GPU=%d°C  HOT=%d°C  -> Duty=%d%%  (F1=%d RPM, F2=%d RPM)    \r",
               tc, tg, th, newduty, r1, r2);
        fflush(stdout);

        usleep(1000 * 1000); // 1000 ms

    }
    printf("\nStopped.\n");
    return 0;
}

/* ---------------------- EC access ----------------------- */

static int ec_init(void) {
    if (ioperm(EC_DATA, 1, 1) != 0) return -1;
    if (ioperm(EC_SC,   1, 1) != 0) return -1;
    return 0;
}

static int ec_io_wait(const uint32_t port, const uint32_t flag, const char value) {
    uint8_t data = inb(port);
    int i = 0;
    while ((((data >> flag) & 0x1) != value) && (i++ < 100)) {
        usleep(1000);
        data = inb(port);
    }
    return (i >= 100) ? -1 : 0;
}

static uint8_t ec_io_read(const uint32_t port) {
    ec_io_wait(EC_SC, IBF, 0);
    outb(EC_SC_READ_CMD, EC_SC);

    ec_io_wait(EC_SC, IBF, 0);
    outb(port, EC_DATA);

    ec_io_wait(EC_SC, OBF, 1);
    return inb(EC_DATA);
}

static int ec_io_do(const uint32_t cmd, const uint32_t port, const uint8_t value) {
    if (ec_io_wait(EC_SC, IBF, 0) != 0) return -1;
    outb(cmd, EC_SC);

    if (ec_io_wait(EC_SC, IBF, 0) != 0) return -1;
    outb(port, EC_DATA);

    if (ec_io_wait(EC_SC, IBF, 0) != 0) return -1;
    outb(value, EC_DATA);

    return ec_io_wait(EC_SC, IBF, 0);
}

/* ------------------- Sensing helpers -------------------- */

static int cpu_temp_ec(void) {
    /* EC CPU temp in °C */
    return (int)ec_io_read(EC_REG_CPU_TEMP);
}

/* Prefer driver temps for GPU; fall back to EC if nothing else */
static int gpu_temp(void) {
    int t = gpu_temp_sysfs();
    if (t > 0) return t;
    t = gpu_temp_nvidia_smi();
    if (t > 0) return t;
    return (int)ec_io_read(EC_REG_GPU_TEMP); /* may be 0 on some models */
}

/* Fan duty read (0..100) */
static int fan1_duty_read(void) {
    int raw = (int)ec_io_read(EC_REG_FAN1_DUTY);
    int pct = (int)((raw / 255.0) * 100.0 + 0.5);
    return clamp(pct, 0, 100);
}

static int fan2_duty_read(void) {
    int raw = (int)ec_io_read(EC_REG_FAN2_DUTY);
    int pct = (int)((raw / 255.0) * 100.0 + 0.5);
    return clamp(pct, 0, 100);
}

/* Fan RPM read */
static int fan1_rpm_read(void) {
    int hi = (int)ec_io_read(EC_REG_FAN1_RPM_HI);
    int lo = (int)ec_io_read(EC_REG_FAN1_RPM_LO);
    return rpm_from_raw(hi, lo);
}

static int fan2_rpm_read(void) {
    int hi = (int)ec_io_read(EC_REG_FAN2_RPM_HI);
    int lo = (int)ec_io_read(EC_REG_FAN2_RPM_LO);
    return rpm_from_raw(hi, lo);
}

/* Fan duty write (0..100) via EC command 0x99; port 0x01=fan1, 0x02=fan2 */
static int fan1_duty_write(int pct) {
    pct = clamp(pct, 0, 100);
    int v = (int)(pct / 100.0 * 255.0 + 0.5);
    return ec_io_do(0x99, 0x01, (uint8_t)v);
}
static int fan2_duty_write(int pct) {
    pct = clamp(pct, 0, 100);
    int v = (int)(pct / 100.0 * 255.0 + 0.5);
    return ec_io_do(0x99, 0x02, (uint8_t)v);
}

/* ----------------------- Utils -------------------------- */

static int clamp(int v, int lo, int hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Map temperature (°C) to duty (%) with a simple linear curve. */
static int map_temp_to_duty(int temp_c) {

    if (temp_c < AUTO_MIN_TEMP_C) return 0;
    if (temp_c >= AUTO_MAX_TEMP_C) return 100;

    double x0 = (double)AUTO_MIN_TEMP_C, y0 = (double)MIN_FAN_DUTY_PCT;
    double x1 = (double)AUTO_MAX_TEMP_C, y1 = 100.0;
    double duty = y0 + (y1 - y0) * ((temp_c - x0) / (x1 - x0));
    int pct = (int)(duty + 0.5);
    return clamp(pct, 0, 100);

}

/* Convert EC raw RPM value (two bytes) to RPM.
   The original project used: RPM = 2156220 / raw */
static int rpm_from_raw(int hi, int lo) {
    int raw = (hi << 8) | lo;
    return (raw > 0) ? (2156220 / raw) : 0;
}

/* --------------- GPU temp discovery helpers ------------- */

/* Scan /sys/class/hwmon for GPU temps (nvidia/amdgpu/i915/xe) */
static int gpu_temp_sysfs(void) {
    DIR *d = opendir("/sys/class/hwmon");
    if (!d) return -1;

    struct dirent *de;
    int best = -1;
    while ((de = readdir(d)) != NULL) {
        if (strncmp(de->d_name, "hwmon", 5) != 0) continue;

        char base[256];
        snprintf(base, sizeof(base), "/sys/class/hwmon/%s", de->d_name);

        char namepath[300];
        snprintf(namepath, sizeof(namepath), "%s/name", base);
        int fd = open(namepath, O_RDONLY);
        if (fd < 0) continue;

        char namebuf[64] = {0};
        ssize_t n = read(fd, namebuf, sizeof(namebuf) - 1);
        close(fd);
        if (n <= 0) continue;
        for (ssize_t i = 0; i < n; i++) if (namebuf[i] == '\n') { namebuf[i] = 0; break; }

        if (strstr(namebuf, "nvidia") || strstr(namebuf, "amdgpu") ||
            strstr(namebuf, "i915")   || strstr(namebuf, "xe")) {

            for (int i = 1; i <= 10; i++) {
                char tpath[320];
                snprintf(tpath, sizeof(tpath), "%s/temp%d_input", base, i);
                if (!path_exists(tpath)) continue;
                int milli = read_int_from_file(tpath);
                if (milli > 0) {
                    int c = milli / 1000; /* millidegC -> C */
                    if (c > best) best = c; /* choose hottest temp as GPU temp */
                }
            }
        }
    }
    closedir(d);
    return best;
}

/* Fallback for NVIDIA: parse nvidia-smi (if installed) */
static int gpu_temp_nvidia_smi(void) {
    if (!path_exists("/usr/bin/nvidia-smi")) return -1;
    FILE *p = popen("/usr/bin/nvidia-smi --query-gpu=temperature.gpu --format=csv,noheader,nounits 2>/dev/null", "r");
    if (!p) return -1;
    char buf[64] = {0};
    if (!fgets(buf, sizeof(buf), p)) { pclose(p); return -1; }
    pclose(p);
    int t = atoi(buf);
    if (t > 0 && t < 130) return t;
    return -1;
}

/* Small helpers */
static int read_int_from_file(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    char buf[64] = {0};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return -1;
    return atoi(buf);
}
static int path_exists(const char *path) { return access(path, F_OK) == 0; }
