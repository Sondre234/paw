// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* Scene-graph pieces of the desktop effects that need wlroots; the arithmetic is in effects.h. */
#include <stdbool.h>

struct wlr_buffer;
struct wlr_scene_buffer;
struct wlr_scene_tree;

/* A 1x1 opaque black buffer, to be stretched over a window and faded with the scene node's
 * opacity. Returns NULL when out of memory. */
struct wlr_buffer *sh_black_buffer(void);

/* A node in `tree`, initially transparent, that dims what is under it as its opacity rises, and lets
 * pointer input through. */
struct wlr_scene_buffer *sh_dim_create(struct wlr_scene_tree *tree, struct wlr_buffer *black);
