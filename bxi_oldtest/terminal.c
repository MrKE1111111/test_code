#define _POSIX_C_SOURCE 200809L

#include "terminal.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#define CONFIG_PATH BXI_OLDTEST_CONFIG_DIR "/terminal.conf"

enum {
    DEFAULT_CYCLES = 50,
    DEFAULT_HOLD_S = 7200,
    DEFAULT_ROUNDS = 3,
};

static void print_help(FILE *output, const char *program)
{
    fprintf(output,
        "Usage: %s [--run [on_off_power|continuous_power]] [options]\n"
        "\n"
        "  --run                     Run alternating groups and holds, then one final group.\n"
        "  --run on_off_power        Run one group of power cycles (4 s ON, 6 s OFF per cycle).\n"
        "  --run continuous_power   Run one timed power-on hold.\n"
        "                            Every mode stays powered ON after completing its schedule.\n"
        "  --on_off_power COUNT      Power cycles per group (default %d).\n"
        "  --continuous_power SEC    Duration of each timed hold in seconds (default %d).\n"
        "  --round COUNT|no_stop     Alternating rounds (default %d), or repeat indefinitely.\n"
        "  --dry-run                 Preview the schedule without touching hardware or saving changes.\n"
        "  --help, -h                Show this help.\n"
        "\n"
        "Foreground commands (press Enter): stop = pause and switch OFF,\n"
        "start = resume, quit = switch OFF and exit.\n"
        "Settings are saved individually to %s.\n"
        "Omitted settings retain their saved values, or defaults on first use.\n"
        "Settings alone do not start a test. Real runs require sudo.\n",
        program, DEFAULT_CYCLES, DEFAULT_HOLD_S, DEFAULT_ROUNDS, CONFIG_PATH);
}

static bool positive_integer(const char *text, int *value)
{
    if (*text == '\0' || text[strspn(text, "0123456789")] != '\0') {
        return false;
    }
    errno = 0;
    const long number = strtol(text, NULL, 10);
    if (errno == ERANGE || number <= 0 || number > INT_MAX) {
        return false;
    }
    *value = (int)number;
    return true;
}

static bool parse_rounds(const char *text, int *rounds)
{
    if (strcmp(text, "no_stop") == 0) {
        *rounds = UNLIMITED_ROUNDS;
        return true;
    }
    return positive_integer(text, rounds);
}

static void format_rounds(int rounds, char text[32])
{
    if (rounds == UNLIMITED_ROUNDS) {
        strcpy(text, "no_stop");
    } else {
        snprintf(text, 32, "%d", rounds);
    }
}

static int load_settings(struct terminal_options *options)
{
    options->cycles_per_group = DEFAULT_CYCLES;
    options->hold_s = DEFAULT_HOLD_S;
    options->rounds = DEFAULT_ROUNDS;

    int fd = open(CONFIG_PATH, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        if (errno == ENOENT) {
            return 0;
        }
        perror("Open " CONFIG_PATH);
        return -1;
    }
    struct stat info;
    if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode)) {
        fprintf(stderr, "Expected a regular settings file at %s\n", CONFIG_PATH);
        close(fd);
        return -1;
    }
    FILE *file = fdopen(fd, "r");
    if (file == NULL) {
        perror("Read " CONFIG_PATH);
        close(fd);
        return -1;
    }

    char cycles[32], hold[32], rounds[32], extra;
    int fields = fscanf(file, "on_off_power=%31s continuous_power=%31s round=%31s %c",
                        cycles, hold, rounds, &extra);
    bool valid = fields == 3 && !ferror(file) &&
        positive_integer(cycles, &options->cycles_per_group) &&
        positive_integer(hold, &options->hold_s) && parse_rounds(rounds, &options->rounds);
    fclose(file);
    if (!valid) {
        fprintf(stderr, "Invalid settings in %s\n", CONFIG_PATH);
        return -1;
    }
    return 0;
}

static int save_settings(const struct terminal_options *options, int directory_fd)
{
    char temporary_path[] = CONFIG_PATH ".XXXXXX";
    int fd = mkstemp(temporary_path);
    if (fd < 0) {
        perror("Create temporary settings file");
        return -1;
    }

    FILE *file = fdopen(fd, "w");
    if (file == NULL) {
        perror("Write " CONFIG_PATH);
        close(fd);
        unlink(temporary_path);
        return -1;
    }
    char rounds[32];
    format_rounds(options->rounds, rounds);
    bool ok = fprintf(file, "on_off_power=%d\ncontinuous_power=%d\nround=%s\n",
                      options->cycles_per_group, options->hold_s, rounds) >= 0 &&
        fflush(file) == 0 && fchmod(fd, 0644) == 0 && fsync(fd) == 0;
    if (!ok) {
        perror("Write " CONFIG_PATH);
    }
    if (fclose(file) != 0) {
        perror("Close temporary settings file");
        ok = false;
    }
    if (ok && rename(temporary_path, CONFIG_PATH) == 0) {
        if (fsync(directory_fd) == 0) {
            return 0;
        }
        perror("Sync settings directory");
        return -1;
    }
    if (ok) {
        perror("Replace " CONFIG_PATH);
    }
    unlink(temporary_path);
    return -1;
}

static int apply_settings(struct terminal_options *options, int cycles, int hold, int rounds)
{
    bool save = !options->dry_run && (cycles != 0 || hold != 0 || rounds != 0);
    int directory_fd = -1;
    if (save) {
        directory_fd = open(BXI_OLDTEST_CONFIG_DIR, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (directory_fd < 0 || flock(directory_fd, LOCK_EX) != 0) {
            perror("Lock settings directory");
            if (directory_fd >= 0) {
                close(directory_fd);
            }
            return -1;
        }
    }

    int status = load_settings(options);
    if (status == 0) {
        if (cycles != 0) options->cycles_per_group = cycles;
        if (hold != 0) options->hold_s = hold;
        if (rounds != 0) options->rounds = rounds;
        if (save) {
            status = save_settings(options, directory_fd);
            if (status == 0) {
                char round_text[32];
                format_rounds(options->rounds, round_text);
                printf("Saved settings: on_off_power=%d continuous_power=%d s round=%s\n",
                       options->cycles_per_group, options->hold_s, round_text);
            }
        }
    }
    if (directory_fd >= 0) {
        close(directory_fd);
    }
    return status;
}

int terminal_parse(int argc, char *argv[], struct terminal_options *options)
{
    if (argc == 1 || (argc == 2 &&
        (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0))) {
        print_help(stdout, argv[0]);
        return 0;
    }

    bool start = false;
    int cycles = 0, hold = 0, rounds = 0;
    *options = (struct terminal_options){ .mode = TEST_MODE_BOTH };
    for (int i = 1; i < argc; ++i) {
        const char *arg = argv[i];
        if (strcmp(arg, "--run") == 0 && !start) {
            start = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                const char *mode = argv[++i];
                if (strcmp(mode, "on_off_power") == 0) {
                    options->mode = TEST_MODE_ON_OFF;
                } else if (strcmp(mode, "continuous_power") == 0) {
                    options->mode = TEST_MODE_CONTINUOUS;
                } else {
                    fprintf(stderr, "Unknown --run mode: %s\n", mode);
                    return -1;
                }
            }
        } else if (strcmp(arg, "--dry-run") == 0 && !options->dry_run) {
            options->dry_run = true;
        } else {
            int *value = NULL;
            if (strcmp(arg, "--on_off_power") == 0) value = &cycles;
            if (strcmp(arg, "--continuous_power") == 0) value = &hold;
            if (strcmp(arg, "--round") == 0) value = &rounds;
            if (value == NULL || *value != 0) {
                fprintf(stderr, "Unknown or repeated option: %s (see --help)\n", arg);
                return -1;
            }
            const bool round_option = value == &rounds;
            if (++i >= argc || !(round_option ? parse_rounds(argv[i], value) :
                                                positive_integer(argv[i], value))) {
                fprintf(stderr, "%s requires a positive integer (up to %d)%s\n", arg, INT_MAX,
                        round_option ? " or no_stop" : "");
                return -1;
            }
        }
    }

    if (start && !options->dry_run && geteuid() != 0) {
        fputs("Run with sudo to access the BXI PCI driver.\n", stderr);
        return -1;
    }
    if (apply_settings(options, cycles, hold, rounds) != 0) {
        return -1;
    }
    return start || options->dry_run;
}

void terminal_input_init(struct terminal_input *input)
{
    *input = (struct terminal_input){ .enabled = isatty(STDIN_FILENO) == 1 };
    if (input->enabled) {
        puts("Type stop, start, or quit and press Enter.");
    }
}

static enum terminal_command parse_command(struct terminal_input *input)
{
    if (input->discard_line) {
        puts("Invalid command. Enter stop, start, or quit.");
        return TERMINAL_NONE;
    }

    while (input->length > 0 && isspace((unsigned char)input->line[input->length - 1])) {
        --input->length;
    }
    input->line[input->length] = '\0';
    char *command = input->line;
    while (isspace((unsigned char)*command)) {
        ++command;
    }
    if (strcmp(command, "stop") == 0) return TERMINAL_STOP;
    if (strcmp(command, "start") == 0) return TERMINAL_START;
    if (strcmp(command, "quit") == 0) return TERMINAL_QUIT;
    if (*command != '\0') {
        puts("Unknown command. Enter stop, start, or quit.");
    }
    return TERMINAL_NONE;
}

enum terminal_command terminal_wait_command(struct terminal_input *input, int timeout_ms)
{
    struct pollfd fd = { .fd = input->enabled ? STDIN_FILENO : -1, .events = POLLIN };
    int ready = poll(&fd, 1, timeout_ms);
    if (ready == 0 || (ready < 0 && errno == EINTR)) return TERMINAL_NONE;
    if (ready < 0) {
        perror("Poll terminal input");
        return TERMINAL_ERROR;
    }
    if (!(fd.revents & POLLIN)) {
        input->enabled = false;
        return TERMINAL_NONE;
    }

    char byte;
    ssize_t count = read(STDIN_FILENO, &byte, 1);
    if (count < 0) {
        if (errno == EINTR || errno == EAGAIN) return TERMINAL_NONE;
        perror("Read terminal input");
        return TERMINAL_ERROR;
    }
    if (count == 0) {
        input->enabled = false;
        return TERMINAL_NONE;
    }
    if (byte == '\n') {
        enum terminal_command command = parse_command(input);
        input->length = 0;
        input->discard_line = false;
        return command;
    }
    if (byte == '\0' || input->length == sizeof(input->line) - 1) {
        input->discard_line = true;
    } else if (!input->discard_line) {
        input->line[input->length++] = byte;
    }
    return TERMINAL_NONE;
}
