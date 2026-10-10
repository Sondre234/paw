// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* Live thumbnails for the overview, on the plain wlroots scene graph. A thumbnail is a copy of
 * the buffers and rectangles below a window's tree, in a tree of its own, scaled by one factor.
 * The copies share the client's buffers, so a thumbnail costs no more than a few scene nodes;
 * it stays live by being copied again whenever the window's fingerprint changes. Animations
 * copy a window the same way at full size, to fade it out once it is gone. */
#include <stdint.h>

struct wlr_scene_tree;

struct wlr_scene_node;

/* A number that changes whenever something drawn below `tree` does: which buffers, where, how
 * big, how opaque. It says nothing about scale, nor about `skip` and what is below it (NULL for
 * nothing to skip). */
uint64_t sh_thumb_fingerprint(const struct wlr_scene_tree *tree,
                              const struct wlr_scene_node *skip);

/* Copies what is visible below `source` into `target`, everything scaled by `scale` about
 * `source`'s origin and multiplied by `opacity`, but for `skip` and what is below it (NULL for
 * nothing to skip). Returns how many nodes were made. */
int sh_thumb_clone(struct wlr_scene_tree *target, const struct wlr_scene_tree *source,
                   double scale, float opacity, const struct wlr_scene_node *skip);

/* Removes what sh_thumb_clone made. */
void sh_thumb_clear(struct wlr_scene_tree *target);
