// SPDX-License-Identifier: GPL-3.0-or-later
/* A wlr-output-power-management client, as wlopm is.
 *   output_power_probe list
 *   output_power_probe set NAME on|off
 *   output_power_probe watch NAME
 * `list` prints "NAME on|off" per output; `set` sets a monitor's mode and prints the mode the
 * compositor reports afterwards, or "failed"; `watch` prints "NAME on|off" (or "failed") each
 * time the mode is reported, until it is ended. */
#define _GNU_SOURCE
#include "wlr-output-power-management-unstable-v1-client-protocol.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

#ifdef __SANITIZE_ADDRESS__
/* A short-lived test client exits without tearing its protocol objects down. */
const char *__asan_default_options(void) { return "detect_leaks=0"; }
#endif

struct output {
    struct wl_output *object;
    struct zwlr_output_power_v1 *power;
    char name[64];
    int mode; // -1 until reported
    bool failed, watching;
    struct output *next;
};
struct probe {
    struct zwlr_output_power_manager_v1 *manager;
    struct output *outputs;
};

static void die(const char *message) {
    fprintf(stderr, "output power probe: %s\n", message);
    exit(1);
}

static void output_geometry(void *d, struct wl_output *o, int32_t x, int32_t y, int32_t pw,
                            int32_t ph, int32_t subpixel, const char *make, const char *model,
                            int32_t transform) {}
static void output_mode(void *d, struct wl_output *o, uint32_t flags, int32_t w, int32_t h,
                        int32_t refresh) {}
static void output_done(void *d, struct wl_output *o) {}
static void output_scale(void *d, struct wl_output *o, int32_t factor) {}
static void output_name(void *d, struct wl_output *o, const char *name) {
    struct output *output = d;
    snprintf(output->name, sizeof(output->name), "%s", name);
}
static void output_description(void *d, struct wl_output *o, const char *description) {}
static const struct wl_output_listener output_listener = {
    .geometry = output_geometry,
    .mode = output_mode,
    .done = output_done,
    .scale = output_scale,
    .name = output_name,
    .description = output_description,
};

static void power_mode(void *d, struct zwlr_output_power_v1 *p, uint32_t mode) {
    struct output *output = d;
    output->mode = (int)mode;
    if (output->watching) {
        printf("%s %s\n", output->name, mode == ZWLR_OUTPUT_POWER_V1_MODE_ON ? "on" : "off");
        fflush(stdout);
    }
}
static void power_failed(void *d, struct zwlr_output_power_v1 *p) {
    struct output *output = d;
    output->failed = true;
    if (output->watching) {
        printf("failed\n");
        fflush(stdout);
    }
}
static const struct zwlr_output_power_v1_listener power_listener = {
    .mode = power_mode,
    .failed = power_failed,
};

static void global(void *data, struct wl_registry *registry, uint32_t name,
                   const char *interface, uint32_t version) {
    struct probe *probe = data;
    if (!strcmp(interface, zwlr_output_power_manager_v1_interface.name)) {
        probe->manager =
            wl_registry_bind(registry, name, &zwlr_output_power_manager_v1_interface, 1);
    } else if (!strcmp(interface, wl_output_interface.name) && version >= 4) {
        struct output *output = calloc(1, sizeof(*output));
        if (!output)
            die("out of memory");
        output->mode = -1;
        output->object = wl_registry_bind(registry, name, &wl_output_interface, 4);
        wl_output_add_listener(output->object, &output_listener, output);
        output->next = probe->outputs;
        probe->outputs = output;
    }
}
static void global_remove(void *data, struct wl_registry *registry, uint32_t name) {}
static const struct wl_registry_listener registry_listener = {global, global_remove};

static void get_power(struct probe *probe, struct output *output) {
    output->power = zwlr_output_power_manager_v1_get_output_power(probe->manager, output->object);
    zwlr_output_power_v1_add_listener(output->power, &power_listener, output);
}

int main(int argc, char **argv) {
    bool list = argc == 2 && !strcmp(argv[1], "list");
    bool set = argc == 4 && !strcmp(argv[1], "set") &&
               (!strcmp(argv[3], "on") || !strcmp(argv[3], "off"));
    bool watch = argc == 3 && !strcmp(argv[1], "watch");
    if (!list && !set && !watch)
        die("usage: output_power_probe list | set NAME on|off | watch NAME");
    struct wl_display *display = wl_display_connect(NULL);
    if (!display)
        die("cannot connect");
    struct probe probe = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &probe);
    wl_display_roundtrip(display);
    wl_display_roundtrip(display); // the outputs' names
    if (!probe.manager)
        die("no zwlr_output_power_manager_v1");
    if (list) {
        for (struct output *output = probe.outputs; output; output = output->next)
            get_power(&probe, output);
        wl_display_roundtrip(display);
        for (struct output *output = probe.outputs; output; output = output->next)
            printf("%s %s\n", output->name,
                   output->failed      ? "failed"
                   : output->mode == 1 ? "on"
                                       : "off");
        return 0;
    }
    struct output *output = probe.outputs;
    while (output && strcmp(output->name, argv[2]))
        output = output->next;
    if (!output)
        die("no such output");
    output->watching = watch;
    get_power(&probe, output);
    wl_display_roundtrip(display);
    if (watch) {
        while (wl_display_dispatch(display) != -1)
            ;
        return 0;
    }
    if (!output->failed) {
        zwlr_output_power_v1_set_mode(output->power, !strcmp(argv[3], "on")
                                                          ? ZWLR_OUTPUT_POWER_V1_MODE_ON
                                                          : ZWLR_OUTPUT_POWER_V1_MODE_OFF);
        wl_display_roundtrip(display);
    }
    printf("%s\n", output->failed ? "failed" : output->mode == 1 ? "on" : "off");
    zwlr_output_power_v1_destroy(output->power);
    wl_display_roundtrip(display);
    wl_display_disconnect(display);
    return 0;
}
