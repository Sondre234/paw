// SPDX-License-Identifier: GPL-3.0-or-later
#include "paw/power_supply.h"
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

/* The first line of `sysfs`/class/power_supply/`supply`/`field`, without its newline. */
static bool read_field(const char *sysfs, const char *supply, const char *field, char *value,
                       size_t size) {
    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/class/power_supply/%s/%s", sysfs, supply, field) >=
        (int)sizeof(path))
        return false;
    FILE *file = fopen(path, "re");
    if (!file)
        return false;
    bool read = fgets(value, (int)size, file) != NULL;
    fclose(file);
    if (read)
        value[strcspn(value, "\n")] = '\0';
    return read;
}

bool sh_on_battery(const char *sysfs) {
    char path[PATH_MAX];
    if (snprintf(path, sizeof(path), "%s/class/power_supply", sysfs) >= (int)sizeof(path))
        return false;
    DIR *directory = opendir(path);
    if (!directory)
        return false;
    bool online = false, discharging = false;
    char type[32], value[32];
    const struct dirent *entry;
    while ((entry = readdir(directory))) {
        const char *name = entry->d_name;
        if (name[0] == '.' || !read_field(sysfs, name, "type", type, sizeof(type)))
            continue;
        if (!strcmp(type, "Battery")) {
            // A mouse's or a headset's battery powers that device only.
            if (read_field(sysfs, name, "scope", value, sizeof(value)) && !strcmp(value, "Device"))
                continue;
            discharging |= read_field(sysfs, name, "status", value, sizeof(value)) &&
                           !strcasecmp(value, "Discharging");
        } else if (!strcmp(type, "Mains") || !strncmp(type, "USB", 3)) {
            online |= read_field(sysfs, name, "online", value, sizeof(value)) &&
                      !strcmp(value, "1");
        }
    }
    closedir(directory);
    return discharging && !online;
}
