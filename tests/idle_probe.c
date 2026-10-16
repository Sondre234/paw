// SPDX-License-Identifier: GPL-3.0-or-later
/* Clients of the idle protocols, for idle_smoke:
 *   idle_probe inhibit      holds an idle inhibitor on a surface, as a video player does, until it
 *                           is ended
 *   idle_probe window TITLE the same on a window titled TITLE, mapped before "ready"
 *   idle_probe notify MS    asks ext-idle-notify-v1 to say when the seat has been idle for MS
 *                           milliseconds, as swayidle's timeouts do, and prints "idled" and
 *                           "resumed" as they come
 * Each prints "ready" once set up. */
#define _GNU_SOURCE
#include "ext-idle-notify-v1-client-protocol.h"
#include "idle-inhibit-unstable-v1-client-protocol.h"
#include "xdg-shell-client-protocol.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#ifdef __SANITIZE_ADDRESS__
/* A short-lived test client exits without tearing its protocol objects down. */
const char *__asan_default_options(void) { return "detect_leaks=0"; }
#endif

static struct wl_compositor *compositor;
static struct wl_seat *seat;
static struct zwp_idle_inhibit_manager_v1 *inhibit_manager;
static struct ext_idle_notifier_v1 *notifier;
static struct xdg_wm_base *wm_base;
static struct wl_shm *shm;

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
    else if (!strcmp(interface, xdg_wm_base_interface.name))
        wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
    else if (!strcmp(interface, wl_shm_interface.name))
        shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
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

/* The window: a grey buffer at every configure, whatever size it asks for. */
#define WINDOW_WIDTH 320
#define WINDOW_HEIGHT 240
static struct wl_surface *window_surface;
static bool configured;

static void ping(void *data, struct xdg_wm_base *base, uint32_t serial) {
    xdg_wm_base_pong(base, serial);
}
static const struct xdg_wm_base_listener wm_base_listener = {.ping = ping};

static struct wl_buffer *grey_buffer(void) {
    int stride = WINDOW_WIDTH * 4, size = stride * WINDOW_HEIGHT;
    int fd = memfd_create("paw-idle-probe", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, size) < 0)
        die("cannot make a buffer");
    uint32_t *pixels = mmap(NULL, (size_t)size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pixels == MAP_FAILED)
        die("cannot map a buffer");
    for (int i = 0; i < WINDOW_WIDTH * WINDOW_HEIGHT; ++i)
        pixels[i] = 0xff606060;
    munmap(pixels, (size_t)size);
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, WINDOW_WIDTH, WINDOW_HEIGHT,
                                                         stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buffer;
}

static void surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    static struct wl_buffer *buffer;
    xdg_surface_ack_configure(xdg_surface, serial);
    if (!buffer)
        buffer = grey_buffer();
    wl_surface_attach(window_surface, buffer, 0, 0);
    wl_surface_damage(window_surface, 0, 0, WINDOW_WIDTH, WINDOW_HEIGHT);
    wl_surface_commit(window_surface);
    configured = true;
}
static const struct xdg_surface_listener surface_listener = {.configure = surface_configure};
static void toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width,
                               int32_t height, struct wl_array *states) {}
static void toplevel_close(void *data, struct xdg_toplevel *toplevel) {
    exit(0);
}
static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close,
};

int main(int argc, char **argv) {
    bool inhibit = argc == 2 && !strcmp(argv[1], "inhibit");
    bool window = argc == 3 && !strcmp(argv[1], "window");
    bool notify = argc == 3 && !strcmp(argv[1], "notify") && atoi(argv[2]) > 0;
    if (!inhibit && !window && !notify)
        die("usage: idle_probe inhibit | window TITLE | notify MS");
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
    } else if (window) {
        if (!compositor || !inhibit_manager || !wm_base || !shm)
            die("no zwp_idle_inhibit_manager_v1, xdg_wm_base or wl_shm");
        xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);
        window_surface = wl_compositor_create_surface(compositor);
        struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, window_surface);
        xdg_surface_add_listener(xdg_surface, &surface_listener, NULL);
        struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg_surface);
        xdg_toplevel_add_listener(toplevel, &toplevel_listener, NULL);
        xdg_toplevel_set_title(toplevel, argv[2]);
        xdg_toplevel_set_app_id(toplevel, "idle-probe");
        zwp_idle_inhibit_manager_v1_create_inhibitor(inhibit_manager, window_surface);
        wl_surface_commit(window_surface);
        while (!configured)
            if (wl_display_dispatch(display) == -1)
                die("lost the compositor");
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
