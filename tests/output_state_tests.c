// SPDX-License-Identifier: GPL-3.0-or-later
/* The monitors' settings kept from the display settings window: where the file is, writing and
 * reading it back, lines and keys it cannot read passed over, and finding a monitor's line by its
 * connector and description. */
#define _GNU_SOURCE
#include "paw/output_state.h"
#include <stdlib.h>
#include <string.h>

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

/* Reads `text` as the file. */
static bool read_text(const char *text, struct sh_output_state *state) {
    FILE *file = fmemopen((void *)text, strlen(text), "r");
    char error[128] = "";
    bool ok = file && sh_output_state_read(state, file, error, sizeof(error));
    if (file)
        fclose(file);
    return ok;
}

/* What sh_output_state_write writes, in a buffer to free. */
static char *write_text(const struct sh_output_state *state) {
    char *text = NULL;
    size_t length = 0;
    FILE *file = open_memstream(&text, &length);
    if (!file)
        abort();
    CHECK(sh_output_state_write(state, file), "written");
    fclose(file);
    return text;
}

static struct sh_output_saved monitor(const char *name, const char *description) {
    struct sh_output_saved saved = {.monitor = {.enabled = true, .tiling = -1, .bit_depth = 8}};
    snprintf(saved.monitor.name, sizeof(saved.monitor.name), "%s", name);
    snprintf(saved.description, sizeof(saved.description), "%s", description);
    return saved;
}

int main(void) {
    char path[256];
    setenv("XDG_STATE_HOME", "/state", 1);
    CHECK(sh_output_state_path(path, sizeof(path)) && !strcmp(path, "/state/paw/outputs"),
          "%s", path);
    setenv("XDG_STATE_HOME", "relative", 1);
    setenv("HOME", "/home/ada", 1);
    CHECK(sh_output_state_path(path, sizeof(path)) &&
              !strcmp(path, "/home/ada/.local/state/paw/outputs"),
          "a relative XDG_STATE_HOME is passed over: %s", path);
    unsetenv("XDG_STATE_HOME");
    unsetenv("HOME");
    CHECK(!sh_output_state_path(path, sizeof(path)), "no directory");

    // Every setting the window keeps comes back as it was written.
    struct sh_output_state state = {.count = 3};
    state.outputs[0] = monitor("DP-3", "Dell Inc. DELL U2720Q 4KX\tY");
    state.outputs[0].primary = true;
    struct sh_monitor *dp = &state.outputs[0].monitor;
    dp->width = 2560, dp->height = 1440, dp->refresh = 143912, dp->scale = 1.25f;
    dp->transform = 1, dp->positioned = true, dp->x = -2560, dp->y = 120, dp->vrr = true;
    dp->bit_depth = 10, dp->hdr = true;
    state.outputs[1] = monitor("HDMI-A-1", "BNQ BenQ GW2480 ");
    struct sh_monitor *hdmi = &state.outputs[1].monitor;
    snprintf(hdmi->mirror, sizeof(hdmi->mirror), "DP-3");
    hdmi->width = 1920, hdmi->height = 1080, hdmi->scale = 1.0f / 3 * 4;
    state.outputs[2] = monitor("eDP-1", "");
    state.outputs[2].monitor.enabled = false;
    char *text = write_text(&state);
    CHECK(!strncmp(text, "paw-outputs 1\n", strlen("paw-outputs 1\n")), "the header: %s", text);
    CHECK(strstr(text, "description=Dell Inc. DELL U2720Q 4KX Y\t"),
          "a tab in a description is written as a space: %s", text);
    CHECK(strstr(text, "mode=2560x1440@143.912\t"), "%s", text);
    struct sh_output_state back;
    CHECK(read_text(text, &back) && back.count == 3, "read back: %d", back.count);
    const struct sh_monitor *m = &back.outputs[0].monitor;
    CHECK(!strcmp(m->name, "DP-3") && back.outputs[0].primary && m->enabled && m->width == 2560 &&
              m->height == 1440 && m->refresh == 143912 && m->scale == 1.25f && m->transform == 1 &&
              m->positioned && m->x == -2560 && m->y == 120 && m->vrr && !m->mirror[0] &&
              m->bit_depth == 10 && m->hdr && m->tiling == -1,
          "DP-3 as kept");
    m = &back.outputs[1].monitor;
    CHECK(!strcmp(m->mirror, "DP-3") && m->refresh == 0 && m->scale == 1.0f / 3 * 4 &&
              !m->positioned && !back.outputs[1].primary && m->bit_depth == 8,
          "HDMI-A-1 mirrors DP-3 at %g", m->scale);
    CHECK(!strcmp(back.outputs[2].monitor.name, "eDP-1") && !back.outputs[2].monitor.enabled &&
              back.outputs[2].monitor.width == 0,
          "eDP-1 off at its preferred mode");
    free(text);

    // A monitor is found by its connector and the description it was kept for, whose tabs are
    // spaces in the file.
    CHECK(sh_output_state_find(&back, "DP-3", "Dell Inc. DELL U2720Q 4KX\tY") == &back.outputs[0],
          "DP-3 by its description");
    CHECK(!sh_output_state_find(&back, "DP-3", "Dell Inc. DELL U2720Q 4KX"),
          "another monitor on DP-3");
    CHECK(!sh_output_state_find(&back, "DP-1", "Dell Inc. DELL U2720Q 4KX Y"),
          "the monitor on another connector");
    CHECK(sh_output_state_find(&back, "eDP-1", "") == &back.outputs[2], "no description");

    // Lines that cannot be read are passed over whole; keys of a newer paw, comments and
    // blank lines too. A second line for a monitor, and a second primary, count for nothing.
    struct sh_output_state mixed;
    CHECK(read_text("paw-outputs 1\n"
                    "# a comment\n"
                    "\n"
                    "DP-1\tdescription=A\tscale=1.5\tsparkle=yes\tprimary=on\n"
                    "DP-2\tdescription=B\tscale=11\n"
                    "DP-3\tdescription=C\tmode=1920x\n"
                    "DP-4\tdescription=D\ttransform=8\n"
                    "DP-5\tdescription=E\tposition=1\n"
                    "DP-6\tdescription=F\tmirror=DP-6\n"
                    "DP-7\tdescription=G\tbit_depth=9\n"
                    "DP-8\tdescription=H\tenabled=maybe\n"
                    "DP-9\tdescription=I\tnot a field\n"
                    "with space\tdescription=J\n"
                    "DP-1\tdescription=A\tscale=2\n"
                    "HDMI-A-1\tdescription=K\tmode=1280x720@59.94\tprimary=on\tposition=10,-20\n",
                    &mixed),
          "a file with mistakes in it is read");
    CHECK(mixed.count == 2, "two lines read: %d", mixed.count);
    CHECK(!strcmp(mixed.outputs[0].monitor.name, "DP-1") &&
              mixed.outputs[0].monitor.scale == 1.5f && mixed.outputs[0].primary,
          "the first DP-1 line");
    m = &mixed.outputs[1].monitor;
    CHECK(!strcmp(m->name, "HDMI-A-1") && m->refresh == 59940 && m->x == 10 && m->y == -20 &&
              !mixed.outputs[1].primary,
          "HDMI-A-1, not primary after DP-1");

    // The fields one by one, as `monitors apply` sets them.
    struct sh_output_saved one = monitor("DP-1", "A");
    CHECK(sh_output_state_set(&one, "scale", "1.75") == SH_OUTPUT_FIELD_SET &&
              one.monitor.scale == 1.75f,
          "scale");
    CHECK(sh_output_state_set(&one, "mirror", "-") == SH_OUTPUT_FIELD_SET && !one.monitor.mirror[0],
          "no mirror");
    CHECK(sh_output_state_set(&one, "mirror", "DP-1") == SH_OUTPUT_FIELD_BAD, "itself");
    CHECK(sh_output_state_set(&one, "enabled", "off") == SH_OUTPUT_FIELD_SET &&
              !one.monitor.enabled,
          "off");
    CHECK(sh_output_state_set(&one, "sparkle", "yes") == SH_OUTPUT_FIELD_UNKNOWN, "unknown");
    CHECK(sh_output_state_set(&one, "transform", "9") == SH_OUTPUT_FIELD_BAD, "transform 9");

    // A file without the header is not one; an empty one neither.
    CHECK(!read_text("DP-1\tdescription=A\n", &mixed), "no header");
    CHECK(!read_text("", &mixed), "empty");
    // A line too long is passed over, and the next one read.
    char *longer = malloc(8192);
    strcpy(longer, "paw-outputs 1\nDP-1\tdescription=");
    size_t at = strlen(longer);
    memset(longer + at, 'x', 4000);
    strcpy(longer + at + 4000, "\nDP-2\tdescription=B\n");
    CHECK(read_text(longer, &mixed) && mixed.count == 1 &&
              !strcmp(mixed.outputs[0].monitor.name, "DP-2"),
          "the long line passed over: %d", mixed.count);
    free(longer);
    // The empty state writes a file of no monitors.
    struct sh_output_state none = {0};
    text = write_text(&none);
    CHECK(read_text(text, &mixed) && mixed.count == 0, "no monitors");
    free(text);

    if (failures)
        return 1;
    puts("output_state passed");
    return 0;
}
