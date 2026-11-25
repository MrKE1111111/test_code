#define _POSIX_C_SOURCE 200809L

#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <time.h>

#include "bxi_pci_drv.h"
#include "terminal.h"

/* This driver exports the GPIO output latch reader outside its public header. */
extern int pcie_gpio_tx_get(void);

enum {
    CYCLE_ON_S = 4,
    CYCLE_OFF_S = 6,
    POWER_OUTPUT_MASK = 1 << 2,
};

static const int64_t ns_per_second = 1000000000;
static const int64_t ns_per_millisecond = 1000000;

struct test_run {
    bool dry_run;
    struct terminal_input input;
    unsigned simulated_power;
    struct timespec simulated_time;
    struct timespec started_at;
    struct timespec switched_at;
};

static volatile sig_atomic_t stop_signal;

static void request_stop(int signal_number)
{
    stop_signal = signal_number;
}

static int discard_can_frame(void *arg, canfd_packet *packet)
{
    (void)arg;
    (void)packet;
    return 0;
}

static int read_time(const struct test_run *run, struct timespec *value)
{
    if (run->dry_run) {
        *value = run->simulated_time;
    } else if (clock_gettime(CLOCK_MONOTONIC, value) != 0) {
        perror("clock_gettime");
        return -1;
    }
    return 0;
}

static int read_power_output(const struct test_run *run)
{
    if (run->dry_run) {
        return (int)run->simulated_power;
    }
    int gpio = pcie_gpio_tx_get();
    return gpio < 0 ? -1 : (gpio & POWER_OUTPUT_MASK) != 0;
}

static int verify_power_output(const struct test_run *run, unsigned expected)
{
    int output = read_power_output(run);
    if (output != (int)expected) {
        fprintf(stderr, "Power output GPIO2 mismatch: expected=%u read=%d\n", expected, output);
        return -1;
    }
    return 0;
}

static int set_power(struct test_run *run, unsigned power, const char *phase)
{
    if (run->dry_run) {
        run->simulated_power = power;
    } else if (motor_pwr_set(power) != 0) {
        fprintf(stderr, "%s: motor_pwr_set(%u) failed\n", phase, power);
        return -1;
    }
    if (read_time(run, &run->switched_at) != 0 || verify_power_output(run, power) != 0) {
        return -1;
    }

    const double elapsed_s = (double)(run->switched_at.tv_sec - run->started_at.tv_sec) +
        (double)(run->switched_at.tv_nsec - run->started_at.tv_nsec) / 1e9;
    printf("t=%9.3f s %s power=%s gpio2=%u", elapsed_s, phase, power ? "ON" : "OFF", power);
    if (!run->dry_run) {
        printf(" gpio0=%d", motor_pwr_get());
    }
    putchar('\n');
    return 0;
}

static int64_t time_ns(struct timespec time)
{
    return time.tv_sec * ns_per_second + time.tv_nsec;
}

/* Duration zero holds this phase until quit or a stop signal. */
static int wait_power(struct test_run *run, unsigned power, unsigned duration_s)
{
    if (run->dry_run) {
        run->simulated_time.tv_sec += duration_s;
        return 0;
    }

    const bool timed = duration_s != 0;
    bool paused = false;
    int64_t remaining_ns = duration_s * ns_per_second;
    int64_t deadline_ns = time_ns(run->switched_at) + remaining_ns;
    int64_t next_check_ns = time_ns(run->switched_at) + ns_per_second;
    while (!stop_signal) {
        struct timespec now;
        if (read_time(run, &now) != 0) {
            return -1;
        }
        int64_t now_ns = time_ns(now);
        if (now_ns >= next_check_ns) {
            if (verify_power_output(run, paused ? 0 : power) != 0) {
                return -1;
            }
            next_check_ns = now_ns + ns_per_second;
        }
        if (timed && !paused && now_ns >= deadline_ns) {
            return 0;
        }

        int64_t wake_ns = timed && !paused && deadline_ns < next_check_ns ?
                          deadline_ns : next_check_ns;
        int timeout_ms = (int)((wake_ns - now_ns + ns_per_millisecond - 1) / ns_per_millisecond);
        enum terminal_command command = terminal_wait_command(&run->input, timeout_ms);
        if (stop_signal) {
            return 1;
        }
        if (command == TERMINAL_ERROR) {
            return -1;
        }
        if (command == TERMINAL_QUIT) {
            puts("Quit requested.");
            return 1;
        }
        if (command == TERMINAL_STOP && !paused) {
            if (set_power(run, 0, "Paused") != 0) {
                return -1;
            }
            now_ns = time_ns(run->switched_at);
            remaining_ns = timed && deadline_ns > now_ns ? deadline_ns - now_ns : 0;
            paused = true;
            next_check_ns = now_ns + ns_per_second;
            if (timed) {
                printf("Phase time remaining: %.3f s\n", (double)remaining_ns / ns_per_second);
            }
        } else if (command == TERMINAL_START && paused) {
            if (set_power(run, power, "Resumed") != 0) {
                return -1;
            }
            now_ns = time_ns(run->switched_at);
            paused = false;
            deadline_ns = now_ns + remaining_ns;
            next_check_ns = now_ns + ns_per_second;
        }
    }
    return 1;
}

static int power_phase(struct test_run *run, unsigned power, unsigned duration_s, const char *phase)
{
    if (stop_signal) {
        return 1;
    }
    if (set_power(run, power, phase) != 0) {
        return -1;
    }
    return wait_power(run, power, duration_s);
}

static int run_cycles(struct test_run *run, int count, const char *group)
{
    for (int cycle = 0; cycle < count; ++cycle) {
        char phase[96];
        snprintf(phase, sizeof(phase), "%s cycle %02d/%d", group, cycle + 1, count);
        int status = power_phase(run, 1, CYCLE_ON_S, phase);
        if (status != 0) {
            return status;
        }
        status = power_phase(run, 0, CYCLE_OFF_S, phase);
        if (status != 0) {
            return status;
        }
    }
    return 0;
}

static int run_test(struct test_run *run, const struct terminal_options *options)
{
    if (read_time(run, &run->started_at) != 0) {
        return -1;
    }
    int initial_output = read_power_output(run);
    if (initial_output < 0) {
        fputs("Cannot read initial power output GPIO2\n", stderr);
        return -1;
    }
    if (initial_output == 1 && options->mode != TEST_MODE_CONTINUOUS) {
        int status = power_phase(run, 0, CYCLE_OFF_S, "Initial power-off interval");
        if (status != 0) {
            return status;
        }
    }

    if (options->mode == TEST_MODE_BOTH) {
        const bool unlimited = options->rounds == UNLIMITED_ROUNDS;
        for (uint64_t round = 1; unlimited || round <= (uint64_t)options->rounds; ++round) {
            char group[48], hold[80];
            if (unlimited) {
                snprintf(group, sizeof(group), "round %" PRIu64 " (no_stop)", round);
            } else {
                snprintf(group, sizeof(group), "round %" PRIu64 "/%d", round, options->rounds);
            }
            int status = run_cycles(run, options->cycles_per_group, group);
            if (status != 0) {
                return status;
            }
            snprintf(hold, sizeof(hold), "%s timed hold (%d s)", group, options->hold_s);
            status = power_phase(run, 1, options->hold_s, hold);
            if (status != 0) {
                return status;
            }
            status = power_phase(run, 0, CYCLE_OFF_S, "Power-off before next group");
            if (status != 0) {
                return status;
            }
            if (unlimited && run->dry_run) {
                puts("DRY RUN: this round repeats indefinitely in no_stop mode.");
                return 0;
            }
        }
    }

    int status;
    if (options->mode == TEST_MODE_CONTINUOUS) {
        char hold[64];
        snprintf(hold, sizeof(hold), "Timed hold (%d s)", options->hold_s);
        status = power_phase(run, 1, options->hold_s, hold);
    } else {
        const char *group = options->mode == TEST_MODE_BOTH ? "final group" : "on/off group";
        status = run_cycles(run, options->cycles_per_group, group);
    }
    if (status != 0 || stop_signal) {
        return status != 0 ? status : 1;
    }
    if (options->mode != TEST_MODE_CONTINUOUS && set_power(run, 1, "Final power-on") != 0) {
        return -1;
    }

    struct timespec finished_at;
    if (read_time(run, &finished_at) != 0) {
        return -1;
    }
    const double elapsed_s = (double)(finished_at.tv_sec - run->started_at.tv_sec) +
        (double)(finished_at.tv_nsec - run->started_at.tv_nsec) / 1e9;
    printf("t=%9.3f s Schedule complete. Maintaining power ON.\n", elapsed_s);
    run->switched_at = finished_at;
    return wait_power(run, 1, 0);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    struct terminal_options options;
    int action_status = terminal_parse(argc, argv, &options);
    if (action_status <= 0) {
        return action_status < 0 ? 2 : 0;
    }
    struct test_run run = { .dry_run = options.dry_run };
    if (run.dry_run) {
        puts("DRY RUN: simulated GPIO and elapsed time; PCI unopened.");
        return run_test(&run, &options) == 0 ? 0 : 1;
    }

    struct sigaction action = { .sa_handler = request_stop };
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) != 0 || sigaction(SIGTERM, &action, NULL) != 0 ||
        sigaction(SIGHUP, &action, NULL) != 0) {
        perror("sigaction");
        return 1;
    }
    if (bxi_pci_init(discard_can_frame, NULL, -1) != 0) {
        fputs("BXI PCI initialization failed.\n", stderr);
        return 1;
    }

    terminal_input_init(&run.input);
    int status = run_test(&run, &options);
    if (status > 0 && stop_signal) {
        printf("Stop requested by signal %d.\n", (int)stop_signal);
    }
    int power_off_status = set_power(&run, 0, "Shutdown");
    bxi_pci_exit();
    return status < 0 || power_off_status != 0 ? 1 : 0;
}
