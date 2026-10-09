/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "paw/overview_scene.h"
#include <math.h>
#include <string.h>
#include <wlr/types/wlr_scene.h>

static uint64_t mix(uint64_t hash, uint64_t value) {
    hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
    return hash;
}

static uint64_t fingerprint(uint64_t hash, const struct wlr_scene_tree *tree,
                            const struct wlr_scene_node *skip) {
    const struct wlr_scene_node *node;
    wl_list_for_each(node, &tree->children, link) {
        if (!node->enabled || node == skip)
            continue;
        hash = mix(hash, (uint64_t)(uint32_t)node->x << 32 | (uint32_t)node->y);
        if (node->type == WLR_SCENE_NODE_TREE) {
            hash = fingerprint(hash, wlr_scene_tree_from_node((struct wlr_scene_node *)node),
                               skip);
        } else if (node->type == WLR_SCENE_NODE_RECT) {
            const struct wlr_scene_rect *rect =
                wlr_scene_rect_from_node((struct wlr_scene_node *)node);
            hash = mix(hash, (uint64_t)(uint32_t)rect->width << 32 | (uint32_t)rect->height);
            for (int i = 0; i < 4; ++i) {
                uint32_t bits;
                memcpy(&bits, &rect->color[i], sizeof(bits));
                hash = mix(hash, bits);
            }
        } else if (node->type == WLR_SCENE_NODE_BUFFER) {
            const struct wlr_scene_buffer *buffer =
                wlr_scene_buffer_from_node((struct wlr_scene_node *)node);
            hash = mix(hash, (uintptr_t)buffer->buffer);
            hash = mix(hash, (uint64_t)(uint32_t)buffer->dst_width << 32 |
                                 (uint32_t)buffer->dst_height);
            uint32_t bits;
            memcpy(&bits, &buffer->opacity, sizeof(bits));
            hash = mix(hash, bits);
            hash = mix(hash, (uint64_t)buffer->transform);
        }
    }
    return hash;
}

uint64_t sh_thumb_fingerprint(const struct wlr_scene_tree *tree,
                              const struct wlr_scene_node *skip) {
    return fingerprint(1469598103934665603ULL, tree, skip);
}

/* A scaled span from `start` of `length` on the same grid as its neighbours, so adjoining
 * buffers stay adjoined instead of gaining or losing a pixel to rounding. */
static void span(double scale, int start, int length, int *out_start, int *out_length) {
    int from = (int)lround(start * scale), to = (int)lround((start + length) * scale);
    *out_start = from;
    *out_length = to - from < 1 ? 1 : to - from;
}

static int clone(struct wlr_scene_tree *target, const struct wlr_scene_tree *tree, int ox, int oy,
                 double scale, float opacity, const struct wlr_scene_node *skip) {
    int made = 0;
    const struct wlr_scene_node *node;
    wl_list_for_each(node, &tree->children, link) {
        if (!node->enabled || node == skip)
            continue;
        int x = ox + node->x, y = oy + node->y;
        if (node->type == WLR_SCENE_NODE_TREE) {
            made += clone(target, wlr_scene_tree_from_node((struct wlr_scene_node *)node), x, y,
                          scale, opacity, skip);
        } else if (node->type == WLR_SCENE_NODE_RECT) {
            const struct wlr_scene_rect *rect =
                wlr_scene_rect_from_node((struct wlr_scene_node *)node);
            int left, width, top, height;
            span(scale, x, rect->width, &left, &width);
            span(scale, y, rect->height, &top, &height);
            float color[4] = {rect->color[0] * opacity, rect->color[1] * opacity,
                              rect->color[2] * opacity, rect->color[3] * opacity};
            struct wlr_scene_rect *copy = wlr_scene_rect_create(target, width, height, color);
            if (copy) {
                wlr_scene_node_set_position(&copy->node, left, top);
                ++made;
            }
        } else if (node->type == WLR_SCENE_NODE_BUFFER) {
            const struct wlr_scene_buffer *buffer =
                wlr_scene_buffer_from_node((struct wlr_scene_node *)node);
            if (!buffer->buffer)
                continue;
            int width = buffer->dst_width, height = buffer->dst_height;
            if (width <= 0 || height <= 0) {
                bool turned = buffer->transform & WL_OUTPUT_TRANSFORM_90;
                width = turned ? buffer->buffer->height : buffer->buffer->width;
                height = turned ? buffer->buffer->width : buffer->buffer->height;
            }
            int left, top, scaled_width, scaled_height;
            span(scale, x, width, &left, &scaled_width);
            span(scale, y, height, &top, &scaled_height);
            // The new node locks the client's buffer, which keeps its texture alive.
            struct wlr_scene_buffer *copy = wlr_scene_buffer_create(target, buffer->buffer);
            if (!copy)
                continue;
            wlr_scene_node_set_position(&copy->node, left, top);
            wlr_scene_buffer_set_dest_size(copy, scaled_width, scaled_height);
            wlr_scene_buffer_set_source_box(copy, &buffer->src_box);
            wlr_scene_buffer_set_transform(copy, buffer->transform);
            wlr_scene_buffer_set_opacity(copy, buffer->opacity * opacity);
            wlr_scene_buffer_set_filter_mode(copy, buffer->filter_mode);
            ++made;
        }
    }
    return made;
}

int sh_thumb_clone(struct wlr_scene_tree *target, const struct wlr_scene_tree *source,
                   double scale, float opacity, const struct wlr_scene_node *skip) {
    return clone(target, source, 0, 0, scale, opacity, skip);
}

void sh_thumb_clear(struct wlr_scene_tree *target) {
    struct wlr_scene_node *node, *next;
    wl_list_for_each_safe(node, next, &target->children, link) wlr_scene_node_destroy(node);
}
