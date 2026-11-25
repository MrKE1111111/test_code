#ifndef BXI_OLDTEST_TERMINAL_H
#define BXI_OLDTEST_TERMINAL_H

#include <stdbool.h>

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

/* Returns 1 to run the selected mode, 0 for help or saved settings, -1 on error. */
int terminal_parse(int argc, char *argv[], struct terminal_options *options);

#endif
