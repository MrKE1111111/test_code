#ifndef BXI_OLDTEST_TERMINAL_H
#define BXI_OLDTEST_TERMINAL_H

#include <stdbool.h>
#include <stddef.h>

enum { UNLIMITED_ROUNDS = -1 };

enum test_mode {
    TEST_MODE_BOTH,
    TEST_MODE_ON_OFF,
    TEST_MODE_CONTINUOUS,
};

struct terminal_options {
    enum test_mode mode;
    bool dry_run;
    int cycles_per_group;
    int hold_s;
    int rounds;
};

enum terminal_command {
    TERMINAL_ERROR = -1,
    TERMINAL_NONE,
    TERMINAL_STOP,
    TERMINAL_START,
    TERMINAL_QUIT,
};

struct terminal_input {
    bool enabled;
    bool discard_line;
    size_t length;
    char line[32];
};

/* Returns 1 to run the selected mode, 0 for help or saved settings, -1 on error. */
int terminal_parse(int argc, char *argv[], struct terminal_options *options);
void terminal_input_init(struct terminal_input *input);
enum terminal_command terminal_wait_command(struct terminal_input *input, int timeout_ms);

#endif
