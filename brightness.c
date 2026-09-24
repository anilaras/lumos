#define _POSIX_C_SOURCE 200809L
#include "brightness.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define MAX_DDC_DISPLAYS 16
#ifndef DDC_TIMEOUT_SECONDS
#define DDC_TIMEOUT_SECONDS 10
#endif
#ifndef DDC_SCAN_SECONDS
#define DDC_SCAN_SECONDS 30
#endif

extern char **environ;
static char backlight_path[PATH_MAX];
static int ddc_buses[MAX_DDC_DISPLAYS];
static int ddc_count;
static int log_enabled;
static double last_scan;

static double monotonic_seconds(void) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return now.tv_sec + now.tv_nsec / 1000000000.0;
}

/* No shell interpolation. Capture output without a pipe that could fill while
 * waiting, and bound slow or unresponsive DDC commands. */
static FILE *ddc_command(char *const argv[]) {
    FILE *output = tmpfile();
    if (!output) return NULL;

    posix_spawn_file_actions_t actions;
    int result = posix_spawn_file_actions_init(&actions);
    if (result != 0) {
        fclose(output);
        return NULL;
    }
    result = posix_spawn_file_actions_adddup2(&actions, fileno(output), STDOUT_FILENO);
    if (result == 0)
        result = posix_spawn_file_actions_adddup2(&actions, fileno(output), STDERR_FILENO);
    if (result == 0 && fileno(output) > STDERR_FILENO)
        result = posix_spawn_file_actions_addclose(&actions, fileno(output));

    pid_t pid;
    if (result == 0) result = posix_spawnp(&pid, argv[0], &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (result != 0) {
        if (log_enabled && result != ENOENT)
            fprintf(stderr, "Cannot run ddcutil: %s\n", strerror(result));
        fclose(output);
        return NULL;
    }

    double deadline = monotonic_seconds() + DDC_TIMEOUT_SECONDS;
    int status = 0;
    for (;;) {
        pid_t waited = waitpid(pid, &status, WNOHANG);
        if (waited == pid) break;
        if (waited < 0 && errno != EINTR) {
            fclose(output);
            return NULL;
        }
        if (monotonic_seconds() >= deadline) {
            kill(pid, SIGKILL);
            while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
            if (log_enabled) fprintf(stderr, "ddcutil timed out; skipping this operation.\n");
            fclose(output);
            return NULL;
        }
        struct timespec pause = {.tv_nsec = 20000000};
        nanosleep(&pause, NULL);
    }

    rewind(output);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        if (log_enabled) {
            char line[256];
            fprintf(stderr, "ddcutil operation failed.\n");
            while (fgets(line, sizeof(line), output)) fputs(line, stderr);
        }
        fclose(output);
        return NULL;
    }
    return output;
}

static int read_backlight(const char *name) {
    char path[PATH_MAX + 32];
    snprintf(path, sizeof(path), "%s/%s", backlight_path, name);
    FILE *file = fopen(path, "r");
    if (!file) return -1;
    int value = -1;
    if (fscanf(file, "%d", &value) != 1) value = -1;
    fclose(file);
    return value;
}

static void find_backlight(const char *directory) {
    backlight_path[0] = '\0';
    DIR *dir = opendir(directory);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (entry->d_name[0] == '.') continue;
        int length = snprintf(backlight_path, sizeof(backlight_path), "%s/%s", directory, entry->d_name);
        if (length >= 0 && (size_t)length < sizeof(backlight_path) &&
            read_backlight("max_brightness") > 0 && read_backlight("brightness") >= 0) break;
        backlight_path[0] = '\0';
    }
    closedir(dir);
}

static int read_ddc(int bus, int *current, int *maximum) {
    char bus_arg[16];
    snprintf(bus_arg, sizeof(bus_arg), "%d", bus);
    char *args[] = {"ddcutil", "--bus", bus_arg, "getvcp", "10", "--terse", NULL};
    FILE *output = ddc_command(args);
    if (!output) return 0;
    char line[256];
    int found = 0;
    while (fgets(line, sizeof(line), output)) {
        unsigned int feature;
        char type[8];
        if (sscanf(line, " VCP %x %7s %d %d", &feature, type, current, maximum) == 4 &&
            feature == 0x10 && strcmp(type, "C") == 0 &&
            *maximum > 0 && *maximum <= 65535 && *current >= 0 && *current <= *maximum) {
            found = 1;
            break;
        }
    }
    fclose(output);
    return found;
}

static void scan_ddc(void) {
    char *args[] = {"ddcutil", "detect", "--brief", NULL};
    FILE *output = ddc_command(args);
    if (!output) {
        last_scan = monotonic_seconds();
        return; /* A transient scan failure must not discard known outputs. */
    }

    int buses[MAX_DDC_DISPLAYS];
    int count = 0, valid_display = 0;
    char line[512];
    while (fgets(line, sizeof(line), output)) {
        int display, bus;
        if (sscanf(line, "Display %d", &display) == 1) {
            valid_display = display > 0;
        } else if (strncmp(line, "Invalid display", 15) == 0 || line[0] == '\n') {
            valid_display = 0;
        } else if (valid_display && sscanf(line, " I2C bus: /dev/i2c-%d", &bus) == 1) {
            valid_display = 0;
            if (bus < 0 || count == MAX_DDC_DISPLAYS) continue;
            int duplicate = 0;
            for (int i = 0; i < count; ++i) if (buses[i] == bus) duplicate = 1;
            if (!duplicate) buses[count++] = bus;
        }
    }
    fclose(output);

    ddc_count = 0;
    for (int i = 0; i < count; ++i) {
        int current, maximum;
        if (read_ddc(buses[i], &current, &maximum)) ddc_buses[ddc_count++] = buses[i];
    }
    last_scan = monotonic_seconds();
}

int brightness_init(const char *backlight_dir, int verbose) {
    log_enabled = verbose;
    ddc_count = 0;
    /* Initialization runs before the IPC thread; child output stays parseable. */
    setenv("LC_ALL", "C", 1);
    find_backlight(backlight_dir);
    scan_ddc();
    if (log_enabled) {
        if (backlight_path[0]) printf("Backlight: %s\n", backlight_path);
        printf("DDC/CI brightness outputs: %d\n", ddc_count);
    }
    return (backlight_path[0] != '\0') + ddc_count;
}

void brightness_apply(double percent, double tolerance) {
    if (!isfinite(percent)) return;
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    /* Internal brightness must still work if an external monitor fails. */
    if (backlight_path[0]) {
        int maximum = read_backlight("max_brightness");
        int current = read_backlight("brightness");
        if (maximum > 0 && current >= 0) {
            int target = (int)(percent / 100.0 * maximum);
            if (abs(current - target) > maximum * tolerance / 100.0) {
                char path[PATH_MAX + 32];
                snprintf(path, sizeof(path), "%s/brightness", backlight_path);
                FILE *file = fopen(path, "w");
                if (file) {
                    if (fprintf(file, "%d", target) < 0 && log_enabled) perror("Write brightness");
                    if (fclose(file) != 0 && log_enabled) perror("Write brightness");
                } else if (log_enabled) perror("Open brightness");
            }
        }
    }

    if (monotonic_seconds() - last_scan >= DDC_SCAN_SECONDS) scan_ddc();
    for (int i = 0; i < ddc_count; ++i) {
        int current, maximum;
        if (!read_ddc(ddc_buses[i], &current, &maximum)) continue;
        int target = (int)(percent / 100.0 * maximum);
        if (abs(current - target) <= maximum * tolerance / 100.0) continue;
        char bus_arg[16], value_arg[16];
        snprintf(bus_arg, sizeof(bus_arg), "%d", ddc_buses[i]);
        snprintf(value_arg, sizeof(value_arg), "%d", target);
        char *args[] = {"ddcutil", "--bus", bus_arg, "setvcp", "10", value_arg, NULL};
        FILE *output = ddc_command(args);
        if (output) fclose(output);
    }
}
