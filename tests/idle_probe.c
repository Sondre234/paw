// SPDX-License-Identifier: GPL-3.0-or-later
/* Clients of the idle protocols, for idle_smoke:
 *   idle_probe inhibit      holds an idle inhibitor on a surface, as a video player does, until it
 *                           is ended
 *   idle_probe notify MS    asks ext-idle-notify-v1 to say when the seat has been idle for MS
 *                           milliseconds, as swayidle's timeouts do, and prints "idled" and
 *                           "resumed" as they come
 * Each prints "ready" once set up. */
#define _GNU_SOURCE
#include "ext-idle-notify-v1-client-protocol.h"
#include "idle-inhibit-unstable-v1-client-protocol.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>

#ifdef __SANITIZE_ADDRESS__
/* A short-lived test client exits without tearing its protocol objects down. */
const char *__asan_default_options(void) { return "detect_leaks=0"; }
#endif

static struct wl_compositor *compositor;
static struct wl_seat *seat;
static struct zwp_idle_inhibit_manager_v1 *inhibit_manager;
static struct ext_idle_notifier_v1 *notifier;

static void die(const char *message) {
    fprintf(stderr, "idle probe: %s\n", message);
    exit(1);
}

static void global(void *data, struct wl_registry *registry, uint32_t name,
                   const char *interface, uint32_t version) {
    if (!strcmp(interface, wl_compositor_interface.name))
        compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
    else if (!strcmp(interface, wl_seat_interface.name) && !seat)
        seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
    else if (!strcmp(interface, zwp_idle_inhibit_manager_v1_interface.name))
        inhibit_manager =
            wl_registry_bind(registry, name, &zwp_idle_inhibit_manager_v1_interface, 1);
    else if (!strcmp(interface, ext_idle_notifier_v1_interface.name))
        notifier = wl_registry_bind(registry, name, &ext_idle_notifier_v1_interface, 1);
}
static void global_remove(void *data, struct wl_registry *registry, uint32_t name) {}
static const struct wl_registry_listener registry_listener = {global, global_remove};

static void say(const char *line) {
    puts(line);
    fflush(stdout);
}
static void idled(void *data, struct ext_idle_notification_v1 *notification) {
    say("idled");
}
static void resumed(void *data, struct ext_idle_notification_v1 *notification) {
    say("resumed");
}
static const struct ext_idle_notification_v1_listener notification_listener = {
    .idled = idled,
    .resumed = resumed,
};

int main(int argc, char **argv) {
    bool inhibit = argc == 2 && !strcmp(argv[1], "inhibit");
    bool notify = argc == 3 && !strcmp(argv[1], "notify") && atoi(argv[2]) > 0;
    if (!inhibit && !notify)
        die("usage: idle_probe inhibit | notify MS");
    struct wl_display *display = wl_display_connect(NULL);
    if (!display)
        die("cannot connect");
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    wl_display_roundtrip(display);
    if (inhibit) {
        if (!compositor || !inhibit_manager)
            die("no zwp_idle_inhibit_manager_v1");
        struct wl_surface *surface = wl_compositor_create_surface(compositor);
        zwp_idle_inhibit_manager_v1_create_inhibitor(inhibit_manager, surface);
    } else {
        if (!seat || !notifier)
            die("no ext_idle_notifier_v1");
        struct ext_idle_notification_v1 *notification =
            ext_idle_notifier_v1_get_idle_notification(notifier, (uint32_t)atoi(argv[2]), seat);
        ext_idle_notification_v1_add_listener(notification, &notification_listener, NULL);
    }
    wl_display_roundtrip(display);
    say("ready");
    while (wl_display_dispatch(display) != -1)
        ;
    return 0;
}
