// SPDX-License-Identifier: GPL-3.0-or-later
/* A wlr-output-management client, as wlr-randr is.
 *   output_management_probe list
 *   output_management_probe apply|test NAME [enabled=0|1] [scale=F] [x=N] [y=N] [transform=N]
 * `list` prints "NAME enabled x y scale transform" per head; apply and test print
 * "succeeded", "failed" or "cancelled". */
#define _GNU_SOURCE
#include "wlr-output-management-unstable-v1-client-protocol.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

#ifdef __SANITIZE_ADDRESS__
/* A short-lived test client exits without tearing its protocol objects down. */
const char *__asan_default_options(void) { return "detect_leaks=0"; }
#endif

struct head {
    struct zwlr_output_head_v1 *object;
    char name[64];
    bool enabled;
    int x, y, transform;
    double scale;
    struct head *next;
};
struct probe {
    struct zwlr_output_manager_v1 *manager;
    struct head *heads;
    uint32_t serial;
    bool done, finished;
    const char *result;
};

static void die(const char *message) {
    fprintf(stderr, "output management probe: %s\n", message);
    exit(1);
}
static void head_name(void *d, struct zwlr_output_head_v1 *h, const char *name) {
    snprintf(((struct head *)d)->name, sizeof(((struct head *)d)->name), "%s", name);
}
static void head_description(void *d, struct zwlr_output_head_v1 *h, const char *v) {}
static void head_physical_size(void *d, struct zwlr_output_head_v1 *h, int32_t w, int32_t v) {}
static void head_mode(void *d, struct zwlr_output_head_v1 *h, struct zwlr_output_mode_v1 *m) {}
static void head_enabled(void *d, struct zwlr_output_head_v1 *h, int32_t v) {
    ((struct head *)d)->enabled = v;
}
static void head_current_mode(void *d, struct zwlr_output_head_v1 *h, struct zwlr_output_mode_v1 *m) {}
static void head_position(void *d, struct zwlr_output_head_v1 *h, int32_t x, int32_t y) {
    ((struct head *)d)->x = x;
    ((struct head *)d)->y = y;
}
static void head_transform(void *d, struct zwlr_output_head_v1 *h, int32_t v) {
    ((struct head *)d)->transform = v;
}
static void head_scale(void *d, struct zwlr_output_head_v1 *h, wl_fixed_t v) {
    ((struct head *)d)->scale = wl_fixed_to_double(v);
}
static void head_finished(void *d, struct zwlr_output_head_v1 *h) {}
static void head_make(void *d, struct zwlr_output_head_v1 *h, const char *v) {}
static void head_model(void *d, struct zwlr_output_head_v1 *h, const char *v) {}
static void head_serial(void *d, struct zwlr_output_head_v1 *h, const char *v) {}
static void head_sync(void *d, struct zwlr_output_head_v1 *h, uint32_t v) {}
static const struct zwlr_output_head_v1_listener head_listener = {
    .name = head_name,
    .description = head_description,
    .physical_size = head_physical_size,
    .mode = head_mode,
    .enabled = head_enabled,
    .current_mode = head_current_mode,
    .position = head_position,
    .transform = head_transform,
    .scale = head_scale,
    .finished = head_finished,
    .make = head_make,
    .model = head_model,
    .serial_number = head_serial,
    .adaptive_sync = head_sync,
};
static void manager_head(void *data, struct zwlr_output_manager_v1 *manager,
                         struct zwlr_output_head_v1 *object) {
    struct probe *probe = data;
    struct head *head = calloc(1, sizeof(*head));
    head->object = object;
    head->next = probe->heads;
    probe->heads = head;
    zwlr_output_head_v1_add_listener(object, &head_listener, head);
}
static void manager_done(void *data, struct zwlr_output_manager_v1 *manager, uint32_t serial) {
    struct probe *probe = data;
    probe->serial = serial;
    probe->done = true;
}
static void manager_finished(void *data, struct zwlr_output_manager_v1 *manager) {}
static const struct zwlr_output_manager_v1_listener manager_listener = {
    .head = manager_head, .done = manager_done, .finished = manager_finished};

static void result(void *data, struct zwlr_output_configuration_v1 *c, const char *text) {
    struct probe *probe = data;
    probe->result = text;
    probe->finished = true;
}
static void succeeded(void *data, struct zwlr_output_configuration_v1 *c) { result(data, c, "succeeded"); }
static void failed(void *data, struct zwlr_output_configuration_v1 *c) { result(data, c, "failed"); }
static void cancelled(void *data, struct zwlr_output_configuration_v1 *c) { result(data, c, "cancelled"); }
static const struct zwlr_output_configuration_v1_listener configuration_listener = {
    .succeeded = succeeded, .failed = failed, .cancelled = cancelled};

static void registry_global(void *data, struct wl_registry *registry, uint32_t name,
                            const char *interface, uint32_t version) {
    struct probe *probe = data;
    if (!strcmp(interface, zwlr_output_manager_v1_interface.name))
        probe->manager = wl_registry_bind(registry, name, &zwlr_output_manager_v1_interface, 1);
}
static void registry_remove(void *data, struct wl_registry *registry, uint32_t name) {}
static const struct wl_registry_listener registry_listener = {registry_global, registry_remove};

int main(int argc, char **argv) {
    if (argc < 2 || (strcmp(argv[1], "list") && (argc < 3 || (strcmp(argv[1], "apply") && strcmp(argv[1], "test")))))
        die("usage: output_management_probe list | apply|test NAME [key=value...]");
    struct wl_display *display = wl_display_connect(NULL);
    if (!display)
        die("cannot connect to compositor");
    struct probe probe = {0};
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &probe);
    wl_display_roundtrip(display);
    if (!probe.manager)
        die("zwlr_output_manager_v1 is not advertised");
    zwlr_output_manager_v1_add_listener(probe.manager, &manager_listener, &probe);
    while (!probe.done)
        if (wl_display_dispatch(display) < 0)
            die("dispatch failed");
    if (!strcmp(argv[1], "list")) {
        for (struct head *head = probe.heads; head; head = head->next)
            printf("%s %d %d %d %g %d\n", head->name, head->enabled, head->x, head->y, head->scale,
                   head->transform);
        return 0;
    }
    struct head *target = NULL;
    for (struct head *head = probe.heads; head; head = head->next)
        if (!strcmp(head->name, argv[2]))
            target = head;
    if (!target)
        die("no such head");
    struct zwlr_output_configuration_v1 *configuration =
        zwlr_output_manager_v1_create_configuration(probe.manager, probe.serial);
    zwlr_output_configuration_v1_add_listener(configuration, &configuration_listener, &probe);
    bool enabled = target->enabled;
    for (int i = 3; i < argc; ++i)
        if (!strncmp(argv[i], "enabled=", 8))
            enabled = atoi(argv[i] + 8);
    if (!enabled) {
        zwlr_output_configuration_v1_disable_head(configuration, target->object);
    } else {
        struct zwlr_output_configuration_head_v1 *head =
            zwlr_output_configuration_v1_enable_head(configuration, target->object);
        for (int i = 3; i < argc; ++i) {
            if (!strncmp(argv[i], "scale=", 6))
                zwlr_output_configuration_head_v1_set_scale(head, wl_fixed_from_double(atof(argv[i] + 6)));
            else if (!strncmp(argv[i], "transform=", 10))
                zwlr_output_configuration_head_v1_set_transform(head, atoi(argv[i] + 10));
        }
        int x = target->x, y = target->y;
        for (int i = 3; i < argc; ++i) {
            if (!strncmp(argv[i], "x=", 2))
                x = atoi(argv[i] + 2);
            else if (!strncmp(argv[i], "y=", 2))
                y = atoi(argv[i] + 2);
        }
        zwlr_output_configuration_head_v1_set_position(head, x, y);
    }
    if (!strcmp(argv[1], "apply"))
        zwlr_output_configuration_v1_apply(configuration);
    else
        zwlr_output_configuration_v1_test(configuration);
    while (!probe.finished)
        if (wl_display_dispatch(display) < 0)
            die("dispatch failed");
    puts(probe.result);
    return 0;
}
