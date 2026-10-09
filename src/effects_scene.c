// SPDX-License-Identifier: GPL-3.0-or-later
#include "paw/effects_scene.h"
#include <drm_fourcc.h>
#include <stdint.h>
#include <stdlib.h>
#include <wlr/interfaces/wlr_buffer.h>
#include <wlr/types/wlr_scene.h>

struct black_buffer {
    struct wlr_buffer base;
    uint32_t pixel;
};

static void black_destroy(struct wlr_buffer *wlr_buffer) {
    struct black_buffer *buffer = wl_container_of(wlr_buffer, buffer, base);
    wlr_buffer_finish(wlr_buffer);
    free(buffer);
}

static bool black_begin(struct wlr_buffer *wlr_buffer, uint32_t flags, void **data,
                        uint32_t *format, size_t *stride) {
    struct black_buffer *buffer = wl_container_of(wlr_buffer, buffer, base);
    if (flags & WLR_BUFFER_DATA_PTR_ACCESS_WRITE)
        return false;
    *data = &buffer->pixel;
    *format = DRM_FORMAT_ARGB8888;
    *stride = sizeof(buffer->pixel);
    return true;
}

static void black_end(struct wlr_buffer *wlr_buffer) {}

static const struct wlr_buffer_impl black_impl = {
    .destroy = black_destroy,
    .begin_data_ptr_access = black_begin,
    .end_data_ptr_access = black_end,
};

struct wlr_buffer *sh_black_buffer(void) {
    struct black_buffer *buffer = calloc(1, sizeof(*buffer));
    if (!buffer)
        return NULL;
    buffer->pixel = 0xff000000;
    wlr_buffer_init(&buffer->base, &black_impl, 1, 1);
    return &buffer->base;
}

static bool no_input(struct wlr_scene_buffer *buffer, double *sx, double *sy) { return false; }

struct wlr_scene_buffer *sh_dim_create(struct wlr_scene_tree *tree, struct wlr_buffer *black) {
    struct wlr_scene_buffer *dim = wlr_scene_buffer_create(tree, black);
    if (!dim)
        return NULL;
    dim->point_accepts_input = no_input;
    wlr_scene_buffer_set_opacity(dim, 0);
    return dim;
}
