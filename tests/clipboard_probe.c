// SPDX-License-Identifier: GPL-3.0-or-later
/* A program copying and pasting through ext-data-control-v1, as a clipboard manager or wl-copy
 * does, for the shell's clipboard history test:
 *
 *   clipboard_probe set TYPE=VALUE...  copies VALUE as TYPE (VALUE @PATH: the file's bytes),
 *                                      prints "set" once it is copied, and hands it to whoever
 *                                      pastes until something else is copied, printing
 *                                      "sent TYPE" each time
 *   clipboard_probe get TYPE           prints what is copied as TYPE, exit 1 when nothing is
 *   clipboard_probe types              prints the types of what is copied, one a line */
#define _GNU_SOURCE
#include "ext-data-control-v1-client-protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <wayland-client.h>

#ifdef __SANITIZE_ADDRESS__
/* A short-lived test client exits without tearing its protocol objects down. */
const char *__asan_default_options(void) { return "detect_leaks=0"; }
#endif

struct probe {
    struct wl_display *display;
    struct wl_seat *seat;
    struct ext_data_control_manager_v1 *manager;
    struct ext_data_control_device_v1 *device;
    // What it copies.
    int count;
    char **types;
    char **values;
    size_t *sizes;
    bool cancelled;
    // What is copied: the last offer announced, its types, and whether the selection came.
    struct ext_data_control_offer_v1 *offer, *selection;
    char announced[64][128];
    int announced_count;
    bool selection_heard;
};

static void die(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    fprintf(stderr, "clipboard_probe: ");
    vfprintf(stderr, format, arguments);
    fprintf(stderr, "\n");
    va_end(arguments);
    exit(2);
}

static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface,
                   uint32_t version) {
    struct probe *probe = data;
    if (!strcmp(interface, ext_data_control_manager_v1_interface.name))
        probe->manager = wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, 1);
    else if (!strcmp(interface, wl_seat_interface.name) && !probe->seat)
        probe->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
}
static void global_remove(void *data, struct wl_registry *registry, uint32_t name) {}
static const struct wl_registry_listener registry_listener = {global, global_remove};

static void offer_type(void *data, struct ext_data_control_offer_v1 *offer, const char *type) {
    struct probe *probe = data;
    if (offer == probe->offer && probe->announced_count < 64)
        snprintf(probe->announced[probe->announced_count++], 128, "%s", type);
}
static const struct ext_data_control_offer_v1_listener offer_listener = {.offer = offer_type};

static void data_offer(void *data, struct ext_data_control_device_v1 *device,
                       struct ext_data_control_offer_v1 *offer) {
    struct probe *probe = data;
    probe->offer = offer;
    probe->announced_count = 0;
    ext_data_control_offer_v1_add_listener(offer, &offer_listener, probe);
}
static void selection(void *data, struct ext_data_control_device_v1 *device,
                      struct ext_data_control_offer_v1 *offer) {
    struct probe *probe = data;
    probe->selection = offer;
    probe->selection_heard = true;
}
static void finished(void *data, struct ext_data_control_device_v1 *device) {}
static void primary_selection(void *data, struct ext_data_control_device_v1 *device,
                              struct ext_data_control_offer_v1 *offer) {}
static const struct ext_data_control_device_v1_listener device_listener = {
    .data_offer = data_offer,
    .selection = selection,
    .finished = finished,
    .primary_selection = primary_selection,
};

static void send(void *data, struct ext_data_control_source_v1 *source, const char *type,
                 int32_t fd) {
    struct probe *probe = data;
    for (int i = 0; i < probe->count; ++i) {
        if (strcmp(probe->types[i], type))
            continue;
        size_t written = 0;
        while (written < probe->sizes[i]) {
            ssize_t count = write(fd, probe->values[i] + written, probe->sizes[i] - written);
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0)
                break;
            written += (size_t)count;
        }
        break;
    }
    close(fd);
    printf("sent %s\n", type);
    fflush(stdout);
}
static void cancelled(void *data, struct ext_data_control_source_v1 *source) {
    ((struct probe *)data)->cancelled = true;
}
static const struct ext_data_control_source_v1_listener source_listener = {
    .send = send,
    .cancelled = cancelled,
};

// VALUE, or the bytes of the file @PATH names.
static char *value_of(const char *argument, size_t *size) {
    if (argument[0] != '@') {
        *size = strlen(argument);
        return strdup(argument);
    }
    FILE *file = fopen(argument + 1, "rb");
    if (!file)
        die("cannot read %s", argument + 1);
    char *bytes = NULL;
    *size = 0;
    char chunk[65536];
    size_t count;
    while ((count = fread(chunk, 1, sizeof(chunk), file)) > 0) {
        bytes = realloc(bytes, *size + count);
        memcpy(bytes + *size, chunk, count);
        *size += count;
    }
    fclose(file);
    return bytes;
}

int main(int argc, char **argv) {
    if (argc < 2 || (strcmp(argv[1], "set") && strcmp(argv[1], "get") && strcmp(argv[1], "types")) ||
        (!strcmp(argv[1], "set") && argc < 3) || (!strcmp(argv[1], "get") && argc != 3))
        die("usage: clipboard_probe set TYPE=VALUE... | get TYPE | types");
    struct probe probe = {0};
    probe.display = wl_display_connect(NULL);
    if (!probe.display)
        die("cannot connect to the compositor");
    struct wl_registry *registry = wl_display_get_registry(probe.display);
    wl_registry_add_listener(registry, &registry_listener, &probe);
    if (wl_display_roundtrip(probe.display) < 0 || !probe.manager || !probe.seat)
        die("the compositor offers no ext-data-control-v1");
    probe.device = ext_data_control_manager_v1_get_data_device(probe.manager, probe.seat);
    ext_data_control_device_v1_add_listener(probe.device, &device_listener, &probe);

    if (!strcmp(argv[1], "set")) {
        probe.count = argc - 2;
        probe.types = calloc((size_t)probe.count, sizeof(char *));
        probe.values = calloc((size_t)probe.count, sizeof(char *));
        probe.sizes = calloc((size_t)probe.count, sizeof(size_t));
        struct ext_data_control_source_v1 *source =
            ext_data_control_manager_v1_create_data_source(probe.manager);
        ext_data_control_source_v1_add_listener(source, &source_listener, &probe);
        for (int i = 0; i < probe.count; ++i) {
            char *equals = strchr(argv[i + 2], '=');
            if (!equals)
                die("%s is not TYPE=VALUE", argv[i + 2]);
            probe.types[i] = strndup(argv[i + 2], (size_t)(equals - argv[i + 2]));
            probe.values[i] = value_of(equals + 1, &probe.sizes[i]);
            ext_data_control_source_v1_offer(source, probe.types[i]);
        }
        ext_data_control_device_v1_set_selection(probe.device, source);
        if (wl_display_roundtrip(probe.display) < 0)
            die("copying failed");
        printf("set\n");
        fflush(stdout);
        while (!probe.cancelled)
            if (wl_display_dispatch(probe.display) < 0)
                die("the compositor went away");
        return 0;
    }

    // The device announces what is copied at once.
    while (!probe.selection_heard)
        if (wl_display_dispatch(probe.display) < 0)
            die("the compositor went away");
    if (!probe.selection)
        return 1;
    if (!strcmp(argv[1], "types")) {
        for (int i = 0; i < probe.announced_count; ++i)
            printf("%s\n", probe.announced[i]);
        return 0;
    }
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0)
        die("no pipe");
    ext_data_control_offer_v1_receive(probe.selection, argv[2], fds[1]);
    close(fds[1]);
    wl_display_flush(probe.display);
    // Reading waits for the program that copied it to write it all and close.
    char chunk[65536];
    ssize_t count;
    while ((count = read(fds[0], chunk, sizeof(chunk))) != 0) {
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0)
            die("reading failed");
        fwrite(chunk, 1, (size_t)count, stdout);
    }
    return 0;
}
