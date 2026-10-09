/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Windows' own icons: the one a Wayland window gives through xdg-toplevel-icon-v1 (an X11
 * window's is read in xwayland.c), kept on it as a name and the pixels of one size for the window
 * control to send the shell. */
#include "server.h"
#include <drm_fourcc.h>

/* How well an icon of this many pixels suits the shell, higher better and 0 for one to pass
 * over: the largest that fits within 256 by 256, else the smallest of the larger ones. An icon
 * is measured by its longer side; one wider or taller than 4096 is passed over too. */
int icon_size_rank(uint32_t width, uint32_t height) {
    uint32_t side = width > height ? width : height;
    if (!width || !height || side > 4096)
        return 0;
    return side <= 256 ? 8192 + (int)side : 8192 - (int)side;
}

void free_icon(struct sh_icon *icon) {
    free(icon->name);
    free(icon->pixels);
    *icon = (struct sh_icon){0};
}

static bool same_icon(const struct sh_icon *a, const struct sh_icon *b) {
    if ((a->name == NULL) != (b->name == NULL) || (a->name && strcmp(a->name, b->name)))
        return false;
    if ((a->pixels == NULL) != (b->pixels == NULL))
        return false;
    return !a->pixels || (a->width == b->width && a->height == b->height &&
                          !memcmp(a->pixels, b->pixels, (size_t)a->width * a->height * 4));
}

/* Gives the window `icon`, which it takes over, an empty one to drop the icon it has. The window
 * control's objects hear of a change; an icon the same as the one the window has is no change,
 * so that a client setting it again, as an X11 window's is read again as it maps, costs them
 * nothing. */
void set_toplevel_icon(struct sh_toplevel *toplevel, struct sh_icon icon) {
    if (same_icon(&toplevel->icon, &icon)) {
        free_icon(&icon);
        return;
    }
    free_icon(&toplevel->icon);
    toplevel->icon = icon;
    ++toplevel->icon_serial;
    window_objects_changed(toplevel->server);
}

/* Whether wl_shm `buffer` holds pixels the shell can take as they are, or with only their alpha
 * filled in: argb8888, or xrgb8888, which is opaque. */
static bool readable_buffer(struct wlr_buffer *buffer) {
    struct wlr_shm_attributes shm;
    return wlr_buffer_get_shm(buffer, &shm) &&
           (shm.format == DRM_FORMAT_ARGB8888 || shm.format == DRM_FORMAT_XRGB8888);
}

/* A copy of the buffer's pixels into `icon`, rows without padding and xrgb8888's alpha made
 * opaque, so that no wlroots buffer is held past the request; false when it cannot be read. */
static bool copy_buffer(struct wlr_buffer *buffer, struct sh_icon *icon) {
    void *data;
    uint32_t format;
    size_t stride;
    size_t row = (size_t)buffer->width * 4;
    if (!wlr_buffer_begin_data_ptr_access(buffer, WLR_BUFFER_DATA_PTR_ACCESS_READ, &data, &format,
                                          &stride))
        return false;
    bool opaque = format == DRM_FORMAT_XRGB8888;
    uint32_t *pixels = (format == DRM_FORMAT_ARGB8888 || opaque) && stride >= row
                           ? malloc(row * (size_t)buffer->height)
                           : NULL;
    if (pixels) {
        for (int y = 0; y < buffer->height; ++y) {
            uint32_t *line = pixels + (size_t)y * buffer->width;
            memcpy(line, (const char *)data + (size_t)y * stride, row);
            for (int x = 0; opaque && x < buffer->width; ++x)
                line[x] |= 0xff000000;
        }
    }
    wlr_buffer_end_data_ptr_access(buffer);
    if (!pixels)
        return false;
    icon->pixels = pixels;
    icon->width = buffer->width;
    icon->height = buffer->height;
    return true;
}

/* A Wayland window gave an icon, or with none dropped its own: its name and the size
 * icon_size_rank prefers of the buffers it gave (by their pixels, whatever their scale) that can
 * be read. wlroots hands the icon over as the client asks, not at its next commit, which a
 * taskbar's icon needs no lining up with. */
void server_set_xdg_icon(struct wl_listener *listener, void *data) {
    struct wlr_xdg_toplevel_icon_manager_v1_set_icon_event *event = data;
    struct wlr_scene_tree *tree = event->toplevel ? event->toplevel->base->data : NULL;
    struct sh_node *node = tree ? tree->node.data : NULL;
    if (!node || node->kind != SH_NODE_TOPLEVEL)
        return;
    struct sh_icon icon = {0};
    if (event->icon) {
        struct wlr_buffer *best = NULL;
        struct wlr_xdg_toplevel_icon_v1_buffer *each;
        wl_list_for_each(each, &event->icon->buffers, link) {
            struct wlr_buffer *buffer = each->buffer;
            int rank = icon_size_rank((uint32_t)buffer->width, (uint32_t)buffer->height);
            if (rank && readable_buffer(buffer) &&
                (!best || rank > icon_size_rank((uint32_t)best->width, (uint32_t)best->height)))
                best = buffer;
        }
        if (best && !copy_buffer(best, &icon))
            wlr_log(WLR_ERROR, "Cannot read a window's icon");
        icon.name = event->icon->name ? strdup(event->icon->name) : NULL;
    }
    set_toplevel_icon(node->owner, icon);
}
