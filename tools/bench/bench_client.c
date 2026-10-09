// SPDX-License-Identifier: GPL-3.0-or-later
// A minimal xdg-shell client for the compositor benchmark: maps one window and, with
// --animate, redraws it on every frame callback, as a video or a terminal under load would.
#define _GNU_SOURCE
#include "xdg-shell-client-protocol.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#ifdef __SANITIZE_ADDRESS__
const char *__asan_default_options(void) { return "detect_leaks=0"; }
#endif

static volatile sig_atomic_t retitle;
static void on_usr1(int signal) { retitle = 1; }

struct client {
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *shell;
    struct wl_surface *surface;
    struct wl_buffer *buffer;
    struct wl_callback *frame;
    uint32_t *pixels;
    size_t mapped;
    int width, height, buffer_width, buffer_height;
    unsigned tick;
    bool animate, closed, configured;
};

static void die(const char *message) {
    fprintf(stderr, "bench client: %s\n", message);
    exit(1);
}

static void registry_global(void *data, struct wl_registry *registry, uint32_t name,
                            const char *interface, uint32_t version) {
    struct client *client = data;
    if (!strcmp(interface, wl_compositor_interface.name))
        client->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    else if (!strcmp(interface, wl_shm_interface.name))
        client->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    else if (!strcmp(interface, xdg_wm_base_interface.name))
        client->shell = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
}
static void registry_remove(void *data, struct wl_registry *registry, uint32_t name) {}
static const struct wl_registry_listener registry_listener = {registry_global, registry_remove};

static void ping(void *data, struct xdg_wm_base *shell, uint32_t serial) {
    xdg_wm_base_pong(shell, serial);
}
static const struct xdg_wm_base_listener shell_listener = {.ping = ping};

static void make_buffer(struct client *client) {
    // Clients keep their buffer while the size stays; a new one per configure would make the
    // compositor pay for freeing memory the way no real application makes it.
    if (client->buffer && client->buffer_width == client->width &&
        client->buffer_height == client->height)
        return;
    client->buffer_width = client->width;
    client->buffer_height = client->height;
    if (client->buffer)
        wl_buffer_destroy(client->buffer);
    if (client->pixels)
        munmap(client->pixels, client->mapped);
    size_t size = (size_t)client->width * client->height * 4;
    int fd = memfd_create("paw-bench-buffer", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)size) != 0)
        die("cannot allocate shm buffer");
    client->pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (client->pixels == MAP_FAILED)
        die("cannot map shm buffer");
    client->mapped = size;
    struct wl_shm_pool *pool = wl_shm_create_pool(client->shm, fd, (int)size);
    client->buffer = wl_shm_pool_create_buffer(pool, 0, client->width, client->height,
                                               client->width * 4, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
}

static void draw(struct client *client) {
    uint32_t shade = 0xff000000u | (client->tick * 5 & 0xff) << 8 | 0x4060u;
    for (int i = 0; i < client->width * client->height; ++i)
        client->pixels[i] = shade;
}

static void commit(struct client *client);
static void frame_done(void *data, struct wl_callback *callback, uint32_t time) {
    struct client *client = data;
    wl_callback_destroy(callback);
    client->frame = NULL;
    ++client->tick;
    commit(client);
}
static const struct wl_callback_listener frame_listener = {.done = frame_done};

static void commit(struct client *client) {
    if (client->animate)
        draw(client);
    wl_surface_attach(client->surface, client->buffer, 0, 0);
    wl_surface_damage_buffer(client->surface, 0, 0, client->width, client->height);
    if (client->animate && !client->frame) {
        client->frame = wl_surface_frame(client->surface);
        wl_callback_add_listener(client->frame, &frame_listener, client);
    }
    wl_surface_commit(client->surface);
}

static void surface_configure(void *data, struct xdg_surface *surface, uint32_t serial) {
    struct client *client = data;
    xdg_surface_ack_configure(surface, serial);
    make_buffer(client);
    draw(client);
    commit(client);
    client->configured = true;
}
static const struct xdg_surface_listener surface_listener = {.configure = surface_configure};

static void toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width,
                               int32_t height, struct wl_array *states) {
    struct client *client = data;
    client->width = width > 0 && width <= 8192 ? width : 320;
    client->height = height > 0 && height <= 8192 ? height : 240;
}
static void toplevel_close(void *data, struct xdg_toplevel *toplevel) {
    ((struct client *)data)->closed = true;
}
static const struct xdg_toplevel_listener toplevel_listener = {.configure = toplevel_configure,
                                                               .close = toplevel_close};

int main(int argc, char **argv) {
    struct client client = {.width = 320, .height = 240};
    const char *app_id = "paw-bench", *title = "paw bench";
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--animate"))
            client.animate = true;
        else if (!strcmp(argv[i], "--app-id") && i + 1 < argc)
            app_id = argv[++i];
        else if (!strcmp(argv[i], "--title") && i + 1 < argc)
            title = argv[++i];
        else
            die("usage: paw-bench-client [--animate] [--app-id ID] [--title TITLE]");
    }
    struct wl_display *display = wl_display_connect(NULL);
    if (!display)
        die("cannot connect to compositor");
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &client);
    if (wl_display_roundtrip(display) < 0 || !client.compositor || !client.shm || !client.shell)
        die("required globals missing");
    xdg_wm_base_add_listener(client.shell, &shell_listener, &client);
    client.surface = wl_compositor_create_surface(client.compositor);
    struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(client.shell, client.surface);
    xdg_surface_add_listener(xdg_surface, &surface_listener, &client);
    struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg_surface);
    xdg_toplevel_add_listener(toplevel, &toplevel_listener, &client);
    xdg_toplevel_set_title(toplevel, title);
    xdg_toplevel_set_app_id(toplevel, app_id);
    wl_surface_commit(client.surface);
    // SIGUSR1 renames the window, for tests of rules that match titles.
    struct sigaction action = {.sa_handler = on_usr1}; // no SA_RESTART: wake the dispatch
    sigaction(SIGUSR1, &action, NULL);
    while (!client.closed) {
        if (retitle) {
            retitle = 0;
            xdg_toplevel_set_title(toplevel, "changed");
            wl_surface_commit(client.surface);
            wl_display_flush(display);
        }
        // libwayland retries a poll that a signal interrupted, so wait here instead.
        while (wl_display_prepare_read(display) != 0)
            if (wl_display_dispatch_pending(display) < 0)
                return 0;
        wl_display_flush(display);
        struct pollfd fd = {.fd = wl_display_get_fd(display), .events = POLLIN};
        if (poll(&fd, 1, -1) > 0 && (fd.revents & POLLIN)) {
            if (wl_display_read_events(display) < 0)
                break;
        } else {
            wl_display_cancel_read(display);
        }
        if (wl_display_dispatch_pending(display) < 0)
            break;
    }
    return 0;
}
