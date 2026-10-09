// SPDX-License-Identifier: GPL-3.0-or-later
#define _GNU_SOURCE
#include "ext-foreign-toplevel-list-v1-client-protocol.h"
#include "ext-image-capture-source-v1-client-protocol.h" // before the window control's, which names its interface
#include "ext-image-copy-capture-v1-client-protocol.h"
#include "paw-window-control-v1-client-protocol.h"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wayland-client.h>

/* A taskbar's view of one window through paw-window-control-v1, for the tests: finds the
 * window titled TITLE through wlr-foreign-toplevel, then prints its state, sends one request,
 * prints each state the compositor sends until the window closes, or captures the window.
 *
 *   window_probe TITLE                   prints "OUTPUT WORKSPACE STATE"
 *   window_probe TITLE watch             prints that line after every done, until it closes
 *   window_probe TITLE workspace N       move_to_workspace
 *   window_probe TITLE output NAME       move_to_output
 *   window_probe TITLE sticky 0|1        set_sticky or unset_sticky
 *   window_probe TITLE floating 0|1      set_floating or unset_floating
 *   window_probe TITLE above 0|1         set_above or unset_above (version 6)
 *   window_probe TITLE minimize          minimizes it through its wlr-foreign-toplevel handle
 *   window_probe TITLE pid               prints the process id the pid event gives (version 3)
 *   window_probe TITLE id                prints the number the id event gives (version 4)
 *   window_probe TITLE capture [watch]   captures it through get_capture_source (version 2)
 *   window_probe TITLE capture-listed [watch]
 *                                        captures it through ext-foreign-toplevel-list and
 *                                        ext-foreign-toplevel-image-capture-source-v1, as screen
 *                                        sharing does
 *   window_probe TITLE capture-scaled WIDTH HEIGHT [watch | twice | closed]
 *                                        captures it through get_scaled_capture_source (version
 *                                        3), within WIDTH by HEIGHT; twice copies a frame from a
 *                                        second session on the same source too, closed prints
 *                                        the state and asks once the window has closed
 *   window_probe TITLE peek              peeks at it with set_peek (version 4), then reads lines
 *                                        "peek OTHER", "unpeek OTHER" and "destroy OTHER" from
 *                                        its input, each sending set_peek, unset_peek or destroy
 *                                        on the object of the window titled OTHER (made the
 *                                        first time, and kept once that window has closed),
 *                                        and prints "ok" once the compositor has had each; it
 *                                        exits at the end of its input
 *   window_probe TITLE icon [watch]      prints the icon the window supplies itself, from the
 *                                        icon and icon_image events of its first state (version
 *                                        5), and with watch again after each done that brings
 *                                        them, until the window closes
 *
 * STATE is the flags joined by commas ("floating,tiling"), or "-" for none; OUTPUT is "-" before
 * the window is on one.
 *
 * An icon is printed as "NAME SIZE FIRST SUM": its name or "-" for none, then for its pixels
 * WIDTHxHEIGHT, the first pixel and the sum of all of them as 0xAARRGGBB words (modulo 2^32),
 * each "-" without pixels; "none" when the first state brings no icon.
 * PAW_WINDOW_PROBE_VERSION binds the window control at that version at most.
 *
 * A capture copies a frame into shared memory with ext-image-copy-capture-v1 and prints
 * "WIDTHxHEIGHT TOPLEFT CENTRE": the frame's size and the colours (RRGGBB) of its top-left and
 * centre pixels. With watch it goes on, printing a line for each frame as the window redraws. It
 * prints "stopped" when the session stops (an inert source's at once), and then exits. */
#ifdef __SANITIZE_ADDRESS__
/* A short-lived test client exits without tearing its protocol objects down. */
const char *__asan_default_options(void) { return "detect_leaks=0"; }
#endif

struct handle {
    struct zwlr_foreign_toplevel_handle_v1 *object;
    char *title;
    struct handle *next;
};
/* The same window's handle in ext-foreign-toplevel-list, which capture sources are made from. */
struct listed {
    struct ext_foreign_toplevel_handle_v1 *object;
    char *title;
    struct listed *next;
};
/* A capture session and the frame it is copying, into one shared-memory buffer made to the
 * session's constraints. */
struct capture {
    struct wl_shm *shm;
    struct ext_image_copy_capture_session_v1 *session;
    struct ext_image_copy_capture_frame_v1 *frame; // the one being copied, or NULL
    /* The constraints the session last sent, the shm format chosen from them (or NO_FORMAT),
     * whether they changed since the buffer was made, and whether they are complete. */
    int width, height;
    uint32_t format;
    bool changed, done;
    struct wl_buffer *buffer;
    int buffer_width, buffer_height;
    uint32_t *pixels;
    size_t size;
    bool watch, stopped, finished;
};
#define NO_FORMAT UINT32_MAX
struct probe {
    struct zwlr_foreign_toplevel_manager_v1 *manager;
    struct paw_window_control_v1 *control;
    struct ext_foreign_toplevel_list_v1 *list;
    struct ext_foreign_toplevel_image_capture_source_manager_v1 *sources;
    struct ext_image_copy_capture_manager_v1 *copy;
    struct wl_shm *shm;
    struct handle *handles;
    struct listed *listed;
    bool closed, watch;
    char output[64];
    uint32_t workspace, state, pid, id;
    int ids; // how many id events came, which should be one
    /* The icon the icon events last described: its name ("-" for none), and the size, first
     * pixel and sum of its pixels (a width of 0 for none); whether an icon event came since the
     * last done, and with watch_icons whether to print the icon at each done that brings one. */
    char icon_name[64];
    uint32_t icon_width, icon_height, icon_first, icon_sum;
    bool icon_heard, watch_icons;
};

static void die(const char *message) {
    fprintf(stderr, "window probe: %s\n", message);
    exit(1);
}

static void handle_title(void *data, struct zwlr_foreign_toplevel_handle_v1 *object,
                         const char *title) {
    struct handle *handle = data;
    free(handle->title);
    handle->title = strdup(title);
}
static void handle_app_id(void *data, struct zwlr_foreign_toplevel_handle_v1 *object,
                          const char *app_id) {}
static void handle_output(void *data, struct zwlr_foreign_toplevel_handle_v1 *object,
                          struct wl_output *output) {}
static void handle_state(void *data, struct zwlr_foreign_toplevel_handle_v1 *object,
                         struct wl_array *states) {}
static void handle_done(void *data, struct zwlr_foreign_toplevel_handle_v1 *object) {}
static void handle_closed(void *data, struct zwlr_foreign_toplevel_handle_v1 *object) {
    struct handle *handle = data;
    free(handle->title);
    handle->title = NULL; // matches no title any more
}
static void handle_parent(void *data, struct zwlr_foreign_toplevel_handle_v1 *object,
                          struct zwlr_foreign_toplevel_handle_v1 *parent) {}
static const struct zwlr_foreign_toplevel_handle_v1_listener handle_listener = {
    .title = handle_title,
    .app_id = handle_app_id,
    .output_enter = handle_output,
    .output_leave = handle_output,
    .state = handle_state,
    .done = handle_done,
    .closed = handle_closed,
    .parent = handle_parent};

static void manager_toplevel(void *data, struct zwlr_foreign_toplevel_manager_v1 *manager,
                             struct zwlr_foreign_toplevel_handle_v1 *object) {
    struct probe *probe = data;
    struct handle *handle = calloc(1, sizeof(*handle));
    if (!handle)
        die("out of memory");
    handle->object = object;
    handle->next = probe->handles;
    probe->handles = handle;
    zwlr_foreign_toplevel_handle_v1_add_listener(object, &handle_listener, handle);
}
static void manager_finished(void *data, struct zwlr_foreign_toplevel_manager_v1 *manager) {}
static const struct zwlr_foreign_toplevel_manager_v1_listener manager_listener = {
    .toplevel = manager_toplevel, .finished = manager_finished};

static void print_state(struct probe *probe) {
    static const char *names[] = {"sticky", "floating", "tiled", "tiling", "above"};
    char state[64] = "";
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (probe->state & 1u << i)
            snprintf(state + strlen(state), sizeof(state) - strlen(state), "%s%s",
                     state[0] ? "," : "", names[i]);
    printf("%s %u %s\n", probe->output[0] ? probe->output : "-", probe->workspace,
           state[0] ? state : "-");
    fflush(stdout);
}

static void window_output(void *data, struct paw_window_v1 *window, const char *name) {
    struct probe *probe = data;
    snprintf(probe->output, sizeof(probe->output), "%s", name);
}
static void window_workspace(void *data, struct paw_window_v1 *window, uint32_t number) {
    ((struct probe *)data)->workspace = number;
}
static void window_state(void *data, struct paw_window_v1 *window, uint32_t state) {
    ((struct probe *)data)->state = state;
}
static void window_pid(void *data, struct paw_window_v1 *window, uint32_t pid) {
    ((struct probe *)data)->pid = pid;
}
static void window_id(void *data, struct paw_window_v1 *window, uint32_t id) {
    struct probe *probe = data;
    if (!id)
        die("the id event gave 0");
    probe->id = id;
    ++probe->ids;
}
/* The window's number came once, with its first state, from version 4. */
static void check_id(const struct probe *probe) {
    if (paw_window_control_v1_get_version(probe->control) < 4)
        return;
    if (!probe->ids)
        die("no id event came with the window's state");
    if (probe->ids > 1)
        die("the id event came more than once");
}
/* A new icon: its name, and no pixels until an icon_image event gives them. */
static void window_icon(void *data, struct paw_window_v1 *window, const char *name) {
    struct probe *probe = data;
    if (probe->icon_heard)
        die("two icon events came before one done");
    snprintf(probe->icon_name, sizeof(probe->icon_name), "%s", name ? name : "-");
    probe->icon_width = probe->icon_height = 0;
    probe->icon_heard = true;
}
/* The icon's pixels, read from the file the event passes, which must hold them all and be sealed
 * against writing. */
static void window_icon_image(void *data, struct paw_window_v1 *window, int32_t fd,
                              uint32_t width, uint32_t height) {
    struct probe *probe = data;
    if (!probe->icon_heard || probe->icon_width)
        die("an icon_image event came without an icon event of its own before it");
    if (!width || !height)
        die("an icon_image event gave no pixels");
    size_t size = (size_t)width * height * 4;
    struct stat file;
    if (fstat(fd, &file) != 0 || (size_t)file.st_size != size)
        die("the icon's file does not hold width * height * 4 bytes");
    if (!(fcntl(fd, F_GET_SEALS) & F_SEAL_WRITE))
        die("the icon's file is not sealed against writing");
    const uint32_t *pixels = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (pixels == MAP_FAILED)
        die("cannot map the icon's file");
    probe->icon_width = width;
    probe->icon_height = height;
    probe->icon_first = pixels[0];
    probe->icon_sum = 0;
    for (size_t i = 0; i < (size_t)width * height; ++i)
        probe->icon_sum += pixels[i];
    munmap((void *)pixels, size);
    close(fd);
}
static void print_icon(const struct probe *probe) {
    if (!probe->icon_name[0])
        puts("none");
    else if (probe->icon_width)
        printf("%s %ux%u %08x %08x\n", probe->icon_name, probe->icon_width, probe->icon_height,
               probe->icon_first, probe->icon_sum);
    else
        printf("%s - - -\n", probe->icon_name);
    fflush(stdout);
}
static void window_done(void *data, struct paw_window_v1 *window) {
    struct probe *probe = data;
    if (probe->watch)
        print_state(probe);
    if (probe->watch_icons && probe->icon_heard)
        print_icon(probe);
    probe->icon_heard = false;
}
static const struct paw_window_v1_listener window_listener = {
    .output = window_output,
    .workspace = window_workspace,
    .state = window_state,
    .done = window_done,
    .pid = window_pid,
    .id = window_id,
    .icon = window_icon,
    .icon_image = window_icon_image};

static void listed_closed(void *data, struct ext_foreign_toplevel_handle_v1 *object) {
    struct listed *listed = data;
    free(listed->title);
    listed->title = NULL;
}
static void listed_done(void *data, struct ext_foreign_toplevel_handle_v1 *object) {}
static void listed_title(void *data, struct ext_foreign_toplevel_handle_v1 *object,
                         const char *title) {
    struct listed *listed = data;
    free(listed->title);
    listed->title = strdup(title);
}
static void listed_app_id(void *data, struct ext_foreign_toplevel_handle_v1 *object,
                          const char *app_id) {}
static void listed_identifier(void *data, struct ext_foreign_toplevel_handle_v1 *object,
                              const char *identifier) {}
static const struct ext_foreign_toplevel_handle_v1_listener listed_listener = {
    .closed = listed_closed,
    .done = listed_done,
    .title = listed_title,
    .app_id = listed_app_id,
    .identifier = listed_identifier};

static void list_toplevel(void *data, struct ext_foreign_toplevel_list_v1 *list,
                          struct ext_foreign_toplevel_handle_v1 *object) {
    struct probe *probe = data;
    struct listed *listed = calloc(1, sizeof(*listed));
    if (!listed)
        die("out of memory");
    listed->object = object;
    listed->next = probe->listed;
    probe->listed = listed;
    ext_foreign_toplevel_handle_v1_add_listener(object, &listed_listener, listed);
}
static void list_finished(void *data, struct ext_foreign_toplevel_list_v1 *list) {}
static const struct ext_foreign_toplevel_list_v1_listener list_listener = {
    .toplevel = list_toplevel, .finished = list_finished};

/* The colour of a pixel as RRGGBB; both formats the probe takes are 0xAARRGGBB words. */
static unsigned colour(const struct capture *capture, int x, int y) {
    return capture->pixels[(size_t)y * capture->buffer_width + x] & 0xffffff;
}

/* Makes the buffer again when the constraints changed since it was made. */
static void make_buffer(struct capture *capture) {
    if (!capture->changed)
        return;
    capture->changed = false;
    if (capture->buffer) {
        wl_buffer_destroy(capture->buffer);
        munmap(capture->pixels, capture->size);
    }
    if (capture->format == NO_FORMAT || capture->width < 1 || capture->height < 1)
        die("the session offers no shared-memory buffer the probe can read");
    capture->buffer_width = capture->width;
    capture->buffer_height = capture->height;
    capture->size = (size_t)capture->width * capture->height * 4;
    int fd = memfd_create("paw-window-capture", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)capture->size) != 0)
        die("cannot allocate shm buffer");
    capture->pixels = mmap(NULL, capture->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (capture->pixels == MAP_FAILED)
        die("cannot map shm buffer");
    struct wl_shm_pool *pool = wl_shm_create_pool(capture->shm, fd, (int)capture->size);
    capture->buffer = wl_shm_pool_create_buffer(pool, 0, capture->width, capture->height,
                                                capture->width * 4, capture->format);
    wl_shm_pool_destroy(pool);
    close(fd);
}

static void capture_next(struct capture *capture);

static void frame_transform(void *data, struct ext_image_copy_capture_frame_v1 *frame,
                            uint32_t transform) {}
static void frame_damage(void *data, struct ext_image_copy_capture_frame_v1 *frame, int32_t x,
                         int32_t y, int32_t width, int32_t height) {}
static void frame_presentation_time(void *data, struct ext_image_copy_capture_frame_v1 *frame,
                                    uint32_t sec_hi, uint32_t sec_lo, uint32_t nsec) {}
static void frame_ready(void *data, struct ext_image_copy_capture_frame_v1 *frame) {
    struct capture *capture = data;
    ext_image_copy_capture_frame_v1_destroy(frame);
    capture->frame = NULL;
    printf("%dx%d %06x %06x\n", capture->buffer_width, capture->buffer_height,
           colour(capture, 0, 0),
           colour(capture, capture->buffer_width / 2, capture->buffer_height / 2));
    fflush(stdout);
    if (capture->watch)
        capture_next(capture);
    else
        capture->finished = true;
}
static void frame_failed(void *data, struct ext_image_copy_capture_frame_v1 *frame,
                         uint32_t reason) {
    struct capture *capture = data;
    ext_image_copy_capture_frame_v1_destroy(frame);
    capture->frame = NULL;
    if (reason == EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_STOPPED)
        capture->stopped = true;
    else if (reason != EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_BUFFER_CONSTRAINTS)
        die("capturing a frame failed");
    else if (capture->done)
        capture_next(capture); // the new constraints have come: to a buffer made to them
}
static const struct ext_image_copy_capture_frame_v1_listener frame_listener = {
    .transform = frame_transform,
    .damage = frame_damage,
    .presentation_time = frame_presentation_time,
    .ready = frame_ready,
    .failed = frame_failed};

/* Copies the next frame the window draws into a buffer made to the session's constraints. */
static void capture_next(struct capture *capture) {
    make_buffer(capture);
    capture->frame = ext_image_copy_capture_session_v1_create_frame(capture->session);
    ext_image_copy_capture_frame_v1_add_listener(capture->frame, &frame_listener, capture);
    ext_image_copy_capture_frame_v1_attach_buffer(capture->frame, capture->buffer);
    ext_image_copy_capture_frame_v1_damage_buffer(capture->frame, 0, 0, capture->buffer_width,
                                                  capture->buffer_height);
    ext_image_copy_capture_frame_v1_capture(capture->frame);
}

static void session_buffer_size(void *data, struct ext_image_copy_capture_session_v1 *session,
                                uint32_t width, uint32_t height) {
    struct capture *capture = data;
    // New constraints, complete at the next done.
    capture->width = (int)width;
    capture->height = (int)height;
    capture->format = NO_FORMAT;
    capture->changed = true;
    capture->done = false;
}
static void session_shm_format(void *data, struct ext_image_copy_capture_session_v1 *session,
                               uint32_t format) {
    struct capture *capture = data;
    if (capture->format == NO_FORMAT &&
        (format == WL_SHM_FORMAT_ARGB8888 || format == WL_SHM_FORMAT_XRGB8888))
        capture->format = format;
}
static void session_dmabuf_device(void *data, struct ext_image_copy_capture_session_v1 *session,
                                  struct wl_array *device) {}
static void session_dmabuf_format(void *data, struct ext_image_copy_capture_session_v1 *session,
                                  uint32_t format, struct wl_array *modifiers) {}
static void session_done(void *data, struct ext_image_copy_capture_session_v1 *session) {
    struct capture *capture = data;
    capture->done = true;
    // The first constraints start the capture; later ones apply to the next frame, after the
    // one in flight has failed for the buffer it has.
    if (!capture->frame && !capture->buffer)
        capture_next(capture);
}
static void session_stopped(void *data, struct ext_image_copy_capture_session_v1 *session) {
    ((struct capture *)data)->stopped = true;
}
static const struct ext_image_copy_capture_session_v1_listener session_listener = {
    .buffer_size = session_buffer_size,
    .shm_format = session_shm_format,
    .dmabuf_device = session_dmabuf_device,
    .dmabuf_format = session_dmabuf_format,
    .done = session_done,
    .stopped = session_stopped};

static void registry_global(void *data, struct wl_registry *registry, uint32_t name,
                            const char *interface, uint32_t version) {
    struct probe *probe = data;
    if (!strcmp(interface, zwlr_foreign_toplevel_manager_v1_interface.name)) {
        probe->manager =
            wl_registry_bind(registry, name, &zwlr_foreign_toplevel_manager_v1_interface, 3);
        zwlr_foreign_toplevel_manager_v1_add_listener(probe->manager, &manager_listener, probe);
    } else if (!strcmp(interface, paw_window_control_v1_interface.name)) {
        const char *cap = getenv("PAW_WINDOW_PROBE_VERSION");
        uint32_t most = cap ? (uint32_t)strtoul(cap, NULL, 10) : 6;
        most = most < 6 ? most : 6;
        probe->control = wl_registry_bind(registry, name, &paw_window_control_v1_interface,
                                          version < most ? version : most);
    } else if (!strcmp(interface, ext_foreign_toplevel_list_v1_interface.name)) {
        probe->list = wl_registry_bind(registry, name, &ext_foreign_toplevel_list_v1_interface, 1);
        ext_foreign_toplevel_list_v1_add_listener(probe->list, &list_listener, probe);
    } else if (!strcmp(interface,
                       ext_foreign_toplevel_image_capture_source_manager_v1_interface.name)) {
        probe->sources = wl_registry_bind(
            registry, name, &ext_foreign_toplevel_image_capture_source_manager_v1_interface, 1);
    } else if (!strcmp(interface, ext_image_copy_capture_manager_v1_interface.name)) {
        probe->copy =
            wl_registry_bind(registry, name, &ext_image_copy_capture_manager_v1_interface, 1);
    } else if (!strcmp(interface, wl_shm_interface.name)) {
        probe->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    }
}
static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {}
static const struct wl_registry_listener registry_listener = {
    .global = registry_global, .global_remove = registry_global_remove};

/* Starts a session on `source` and copies one frame, or with watch every frame until the
 * session stops. The session stays until end_session. */
static void run_session(struct probe *probe, struct wl_display *display,
                        struct ext_image_capture_source_v1 *source, bool watch,
                        struct capture *capture) {
    *capture = (struct capture){.shm = probe->shm, .format = NO_FORMAT, .watch = watch};
    capture->session = ext_image_copy_capture_manager_v1_create_session(probe->copy, source, 0);
    ext_image_copy_capture_session_v1_add_listener(capture->session, &session_listener, capture);
    while (!capture->finished && !capture->stopped)
        if (wl_display_dispatch(display) < 0)
            die("dispatch failed");
    if (capture->stopped) {
        puts("stopped");
        fflush(stdout);
    }
}
static void end_session(struct capture *capture) {
    if (capture->frame)
        ext_image_copy_capture_frame_v1_destroy(capture->frame);
    ext_image_copy_capture_session_v1_destroy(capture->session);
    if (capture->buffer) {
        wl_buffer_destroy(capture->buffer);
        munmap(capture->pixels, capture->size);
    }
}

/* Captures the window through `source`: one frame, or with watch every frame until the session
 * stops; with twice, one frame, then one more from a second session on the same source while the
 * first is still there. */
static void capture(struct probe *probe, struct wl_display *display,
                    struct ext_image_capture_source_v1 *source, bool watch, bool twice) {
    struct capture first, second;
    run_session(probe, display, source, watch, &first);
    if (twice) {
        run_session(probe, display, source, false, &second);
        end_session(&second);
    }
    end_session(&first);
    ext_image_capture_source_v1_destroy(source);
}

/* Peeks at windows as the input says (see the usage above), starting with `*window`, which it
 * sets to NULL should the input destroy it. The objects of the other windows are its own. */
static void peek(struct probe *probe, struct wl_display *display, const char *title,
                 struct paw_window_v1 **window) {
    struct {
        char title[128];
        struct paw_window_v1 *object;
        bool destroyed;
    } objects[16] = {{.object = *window}};
    snprintf(objects[0].title, sizeof(objects[0].title), "%s", title);
    size_t count = 1;
    struct probe others = {0}; // what the other objects send, which nothing reads
    paw_window_v1_set_peek(*window);
    char line[256];
    for (;;) {
        if (wl_display_roundtrip(display) < 0)
            die("roundtrip failed");
        puts("ok");
        fflush(stdout);
        if (!fgets(line, sizeof(line), stdin))
            break;
        line[strcspn(line, "\n")] = '\0';
        char *name = strchr(line, ' ');
        if (!name)
            die("peek takes \"peek TITLE\", \"unpeek TITLE\" or \"destroy TITLE\"");
        *name++ = '\0';
        size_t index = 0;
        while (index < count && strcmp(objects[index].title, name))
            ++index;
        if (index == count) {
            // Hearing of the windows opened since.
            if (wl_display_roundtrip(display) < 0)
                die("roundtrip failed");
            struct handle *found = NULL;
            for (struct handle *handle = probe->handles; handle; handle = handle->next)
                if (handle->title && !strcmp(handle->title, name))
                    found = handle;
            if (!found || count == sizeof(objects) / sizeof(*objects))
                die("no window has that title");
            objects[count].object = paw_window_control_v1_get_window(probe->control,
                                                                          found->object);
            paw_window_v1_add_listener(objects[count].object, &window_listener, &others);
            snprintf(objects[count].title, sizeof(objects[count].title), "%s", name);
            ++count;
        }
        if (objects[index].destroyed)
            die("that window's object is destroyed");
        if (!strcmp(line, "peek")) {
            paw_window_v1_set_peek(objects[index].object);
        } else if (!strcmp(line, "unpeek")) {
            paw_window_v1_unset_peek(objects[index].object);
        } else if (!strcmp(line, "destroy")) {
            paw_window_v1_destroy(objects[index].object);
            objects[index].destroyed = true;
            if (index == 0)
                *window = NULL;
        } else {
            die("peek takes \"peek TITLE\", \"unpeek TITLE\" or \"destroy TITLE\"");
        }
    }
    for (size_t i = 1; i < count; ++i)
        if (!objects[i].destroyed)
            paw_window_v1_destroy(objects[i].object);
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 6)
        die("usage: window_probe TITLE [watch | workspace N | output NAME | sticky 0|1 | "
            "floating 0|1 | above 0|1 | minimize | pid | id | capture [watch] | capture-listed [watch] | "
            "capture-scaled WIDTH HEIGHT [watch | twice | closed] | peek | icon [watch]]");
    const char *title = argv[1], *command = argc > 2 ? argv[2] : "", *argument = argc > 3 ? argv[3] : "";
    struct probe probe = {.watch = !strcmp(command, "watch")};
    struct wl_display *display = wl_display_connect(NULL);
    if (!display)
        die("cannot connect to the compositor");
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, &probe);
    // The globals, then the windows, then their titles.
    for (int i = 0; i < 3; ++i)
        if (wl_display_roundtrip(display) < 0)
            die("roundtrip failed");
    if (!probe.manager || !probe.control)
        die("the compositor offers no foreign-toplevel manager or window control");
    struct handle *found = NULL;
    for (struct handle *handle = probe.handles; handle; handle = handle->next)
        if (handle->title && !strcmp(handle->title, title))
            found = handle;
    if (!found)
        die("no window has that title");
    struct paw_window_v1 *window =
        paw_window_control_v1_get_window(probe.control, found->object);
    paw_window_v1_add_listener(window, &window_listener, &probe);
    if (wl_display_roundtrip(display) < 0)
        die("roundtrip failed");
    if (!strcmp(command, "")) {
        print_state(&probe);
    } else if (probe.watch) { // the first done printed the state
        while (found->title)
            if (wl_display_dispatch(display) < 0)
                die("dispatch failed");
        check_id(&probe);
    } else if (!strcmp(command, "workspace") && argument[0]) {
        paw_window_v1_move_to_workspace(window, (uint32_t)strtoul(argument, NULL, 10));
    } else if (!strcmp(command, "output") && argument[0]) {
        paw_window_v1_move_to_output(window, argument);
    } else if (!strcmp(command, "sticky") && argument[0]) {
        if (!strcmp(argument, "1"))
            paw_window_v1_set_sticky(window);
        else
            paw_window_v1_unset_sticky(window);
    } else if (!strcmp(command, "floating") && argument[0]) {
        if (!strcmp(argument, "1"))
            paw_window_v1_set_floating(window);
        else
            paw_window_v1_unset_floating(window);
    } else if (!strcmp(command, "above") && argument[0]) {
        if (paw_window_control_v1_get_version(probe.control) < 6)
            die("the compositor's window control keeps no window above");
        if (!strcmp(argument, "1"))
            paw_window_v1_set_above(window);
        else
            paw_window_v1_unset_above(window);
    } else if (!strcmp(command, "minimize")) {
        zwlr_foreign_toplevel_handle_v1_set_minimized(found->object);
    } else if (!strcmp(command, "pid")) {
        if (paw_window_control_v1_get_version(probe.control) < 3)
            die("the compositor's window control gives no process ids");
        printf("%u\n", probe.pid);
    } else if (!strcmp(command, "id")) {
        if (paw_window_control_v1_get_version(probe.control) < 4)
            die("the compositor's window control gives no window numbers");
        check_id(&probe);
        printf("%u\n", probe.id);
    } else if (!strcmp(command, "capture")) {
        if (paw_window_control_v1_get_version(probe.control) < 2 || !probe.copy || !probe.shm)
            die("the compositor offers no window capture");
        capture(&probe, display, paw_window_v1_get_capture_source(window),
                !strcmp(argument, "watch"), false);
    } else if (!strcmp(command, "capture-listed")) {
        if (!probe.list || !probe.sources || !probe.copy || !probe.shm)
            die("the compositor offers no window capture");
        struct listed *listed = NULL;
        for (struct listed *each = probe.listed; each; each = each->next)
            if (each->title && !strcmp(each->title, title))
                listed = each;
        if (!listed)
            die("no listed window has that title");
        capture(&probe, display,
                ext_foreign_toplevel_image_capture_source_manager_v1_create_source(probe.sources,
                                                                                   listed->object),
                !strcmp(argument, "watch"), false);
    } else if (!strcmp(command, "capture-scaled") && argc >= 5) {
        if (paw_window_control_v1_get_version(probe.control) < 3 || !probe.copy || !probe.shm)
            die("the compositor offers no scaled window capture");
        const char *mode = argc > 5 ? argv[5] : "";
        if (!strcmp(mode, "closed")) {
            print_state(&probe); // ready for the window to close
            while (found->title) // which the handle's closed event clears
                if (wl_display_dispatch(display) < 0)
                    die("dispatch failed");
        }
        capture(&probe, display,
                paw_window_v1_get_scaled_capture_source(
                    window, (uint32_t)strtoul(argument, NULL, 10),
                    (uint32_t)strtoul(argv[4], NULL, 10)),
                !strcmp(mode, "watch"), !strcmp(mode, "twice"));
    } else if (!strcmp(command, "peek")) {
        if (paw_window_control_v1_get_version(probe.control) < 4)
            die("the compositor's window control does not peek");
        peek(&probe, display, title, &window);
    } else if (!strcmp(command, "icon")) {
        // Below version 5 no icon comes, which the tests check too.
        print_icon(&probe);
        if (!strcmp(argument, "watch")) {
            probe.watch_icons = true;
            while (found->title)
                if (wl_display_dispatch(display) < 0)
                    die("dispatch failed");
        }
    } else {
        die("unknown command");
    }
    if (wl_display_roundtrip(display) < 0)
        die("request failed");
    if (window)
        paw_window_v1_destroy(window);
    paw_window_control_v1_destroy(probe.control);
    for (struct handle *handle = probe.handles, *next; handle; handle = next) {
        next = handle->next;
        zwlr_foreign_toplevel_handle_v1_destroy(handle->object);
        free(handle->title);
        free(handle);
    }
    zwlr_foreign_toplevel_manager_v1_destroy(probe.manager);
    for (struct listed *listed = probe.listed, *next; listed; listed = next) {
        next = listed->next;
        ext_foreign_toplevel_handle_v1_destroy(listed->object);
        free(listed->title);
        free(listed);
    }
    if (probe.list)
        ext_foreign_toplevel_list_v1_destroy(probe.list);
    if (probe.sources)
        ext_foreign_toplevel_image_capture_source_manager_v1_destroy(probe.sources);
    if (probe.copy)
        ext_image_copy_capture_manager_v1_destroy(probe.copy);
    if (probe.shm)
        wl_shm_destroy(probe.shm);
    wl_registry_destroy(registry);
    wl_display_disconnect(display);
    return 0;
}
