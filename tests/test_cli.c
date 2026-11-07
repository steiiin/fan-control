/* Exercise the real CLI, but always deny EC initialization through a stub.
 * No permission syscall or port I/O can occur, even when run as root. */
#define main fan_control_main
#define ioperm test_ioperm
#include "../src/main.c"
#undef ioperm
#undef main

static int ec_init_attempts;

int test_ioperm(unsigned long from, unsigned long num, int turn_on) {
    (void)from;
    (void)num;
    (void)turn_on;
    ec_init_attempts++;
    errno = EPERM;
    return -1;
}

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
        return EXIT_FAILURE; \
    } \
} while (0)

int main(void) {
    char *commands[] = {"power", "default", "silent", "auto"};
    for (size_t i = 0; i < sizeof(commands) / sizeof(commands[0]); i++) {
        char *valid[] = {"fan-control", commands[i], NULL};
        ec_init_attempts = 0;
        CHECK(fan_control_main(2, valid) == EXIT_FAILURE);
        CHECK(ec_init_attempts == 1); /* Valid command reaches the denied init. */

        char *extra[] = {"fan-control", commands[i], "extra", NULL};
        ec_init_attempts = 0;
        CHECK(fan_control_main(3, extra) == EXIT_FAILURE);
        CHECK(ec_init_attempts == 0); /* Invalid arguments fail before init. */
    }

    char *unknown[] = {"fan-control", "turbo", NULL};
    ec_init_attempts = 0;
    CHECK(fan_control_main(2, unknown) == EXIT_FAILURE);
    CHECK(ec_init_attempts == 0);

    char *missing[] = {"fan-control", NULL};
    CHECK(fan_control_main(1, missing) == EXIT_FAILURE);
    CHECK(ec_init_attempts == 0);
    return EXIT_SUCCESS;
}
