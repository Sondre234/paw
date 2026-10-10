// SPDX-License-Identifier: GPL-3.0-or-later
#include "paw/effects_scene.h"
#include "paw/decoration.h"
#include <stdint.h>
#include <stdlib.h>
#include <wlr/types/wlr_scene.h>

struct wlr_buffer *sh_black_buffer(void) {
    uint32_t *pixel = malloc(sizeof(*pixel));
    if (!pixel)
        return NULL;
    *pixel = 0xff000000;
    return sh_pixel_buffer(pixel, 1, 1);
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
