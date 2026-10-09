// SPDX-License-Identifier: GPL-3.0-or-later
/* Where the power comes from, read from a fake /sys/class/power_supply. */
#define _GNU_SOURCE
#include "paw/power_supply.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int failures;
#define CHECK(condition, ...)                                                                      \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            fprintf(stderr, "%s:%d: %s: ", __FILE__, __LINE__, #condition);                        \
            fprintf(stderr, __VA_ARGS__);                                                          \
            fputc('\n', stderr);                                                                   \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

static char root[512]; // the fake sysfs

/* Writes `value` and a newline to class/power_supply/SUPPLY/FIELD under the fake sysfs. */
static void write_field(const char *supply, const char *field, const char *value) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path), "%s/class", root);
    mkdir(path, 0700);
    snprintf(path, sizeof(path), "%s/class/power_supply", root);
    mkdir(path, 0700);
    snprintf(path, sizeof(path), "%s/class/power_supply/%s", root, supply);
    mkdir(path, 0700);
    snprintf(path, sizeof(path), "%s/class/power_supply/%s/%s", root, supply, field);
    FILE *file = fopen(path, "w");
    if (!file) {
        perror(path);
        exit(1);
    }
    fprintf(file, "%s\n", value);
    fclose(file);
}

static void remove_supplies(void) {
    char command[sizeof(root) + 32];
    snprintf(command, sizeof(command), "rm -rf '%s/class'", root);
    if (system(command) != 0)
        exit(1);
}

int main(void) {
    const char *tmp = getenv("TMPDIR");
    snprintf(root, sizeof(root), "%s/paw-power-XXXXXX", tmp && *tmp ? tmp : "/tmp");
    if (!mkdtemp(root)) {
        perror("mkdtemp");
        return 1;
    }

    CHECK(!sh_on_battery(root), "no power supplies at all");
    CHECK(!sh_on_battery("/nonexistent"), "no sysfs");

    // A desktop with a wireless mouse whose battery runs down.
    write_field("hidpp_battery_0", "type", "Battery");
    write_field("hidpp_battery_0", "scope", "Device");
    write_field("hidpp_battery_0", "status", "Discharging");
    CHECK(!sh_on_battery(root), "a mouse's battery is not the machine's");

    // A laptop on mains, charging, then unplugged.
    write_field("BAT0", "type", "Battery");
    write_field("BAT0", "status", "Charging");
    write_field("AC", "type", "Mains");
    write_field("AC", "online", "1");
    CHECK(!sh_on_battery(root), "charging on mains");
    write_field("BAT0", "status", "Not charging");
    CHECK(!sh_on_battery(root), "full on mains");
    write_field("AC", "online", "0");
    write_field("BAT0", "status", "Discharging");
    CHECK(sh_on_battery(root), "unplugged and discharging");
    // A charger too weak for the load: the battery runs down, but mains is there.
    write_field("AC", "online", "1");
    CHECK(!sh_on_battery(root), "discharging with mains online");
    write_field("AC", "online", "0");

    // A USB-C charger instead of a mains adapter.
    write_field("ucsi-source-psy-USBC000:001", "type", "USB");
    write_field("ucsi-source-psy-USBC000:001", "online", "1");
    CHECK(!sh_on_battery(root), "on a USB-C charger");
    write_field("ucsi-source-psy-USBC000:001", "online", "0");
    CHECK(sh_on_battery(root), "the USB-C charger unplugged");

    // A second battery, full, while the first runs down.
    write_field("BAT1", "type", "Battery");
    write_field("BAT1", "status", "Full");
    CHECK(sh_on_battery(root), "one of two batteries discharging");
    write_field("BAT0", "status", "Unknown");
    CHECK(!sh_on_battery(root), "neither battery discharging");

    remove_supplies();
    rmdir(root);
    if (failures)
        return 1;
    puts("power supply passed");
    return 0;
}
