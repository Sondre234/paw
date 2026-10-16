/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Pseudotiling, as Hyprland's pseudotile (pseudo_toggle): a tile of the dwindle layout that
 * keeps its floating size, in the middle of its tile (sh_tiling_pseudo), scaled down to fit when
 * the tile is smaller. Resizing it, with the pointer or the keyboard, changes that size rather
 * than the splits. The other layouts give it its whole tile, and it shows pseudotiled again back
 * in dwindle. */
#include "server.h"

/* The smallest and largest a pseudotiled window is resized to. */
#define PSEUDO_MIN 64
#define PSEUDO_MAX 16384

/* Whether `toplevel` shows pseudotiled: marked so, and a tile of a workspace in dwindle. */
static bool pseudo_shows(struct sh_toplevel *toplevel) {
    return toplevel->pseudo && toplevel->tiled && toplevel_layout(toplevel) == SH_LAYOUT_DWINDLE;
}

/* The size it keeps: its floating size, else, for a window that opened tiled and has none, half
 * its output's usable area each way, as Hyprland gives a window without a size of its own. */
static void pseudo_size(struct sh_toplevel *toplevel, int *width, int *height) {
    if (toplevel->restore_box.width > 0 && toplevel->restore_box.height > 0) {
        *width = toplevel->restore_box.width;
        *height = toplevel->restore_box.height;
        return;
    }
    struct wlr_output *output = tiled_output(toplevel);
    struct sh_rect area = output ? usable_area(toplevel->server, output) : (struct sh_rect){0};
    *width = area.width / 2;
    *height = area.height / 2;
}

/* Where a tile shows in `rect`, its tile inside the border: there, or pseudotiled at its own
 * size in the middle. */
struct sh_rect pseudo_rect(struct sh_toplevel *toplevel, struct sh_rect rect) {
    if (!pseudo_shows(toplevel))
        return rect;
    int width, height;
    pseudo_size(toplevel, &width, &height);
    return sh_tiling_pseudo(rect, width, height);
}

/* Gives a pseudotiled window a new size: its floating size, which it keeps, as Hyprland's
 * resizing a pseudotile sets the size it floats at too. */
static void set_pseudo_size(struct sh_toplevel *toplevel, int width, int height) {
    width = width < PSEUDO_MIN ? PSEUDO_MIN : width > PSEUDO_MAX ? PSEUDO_MAX : width;
    height = height < PSEUDO_MIN ? PSEUDO_MIN : height > PSEUDO_MAX ? PSEUDO_MAX : height;
    int was_width, was_height;
    pseudo_size(toplevel, &was_width, &was_height);
    if (width == was_width && height == was_height && toplevel->restore_box.width > 0)
        return;
    toplevel->restore_box.width = width;
    toplevel->restore_box.height = height;
    struct wlr_output *output = tiled_output(toplevel);
    if (output)
        reflow_output(toplevel->server, output);
}

/* pseudo_toggle: the focused tile shows at its floating size in the middle of its tile, or fills
 * it again. Only in dwindle, and not while it is fullscreen. */
void pseudo_toggle(struct sh_server *server) {
    struct sh_toplevel *toplevel = current_toplevel(server);
    if (!toplevel || server->locked || !toplevel->tiled || toplevel->fullscreen ||
        toplevel_layout(toplevel) != SH_LAYOUT_DWINDLE)
        return;
    toplevel->pseudo = !toplevel->pseudo;
    struct wlr_output *output = tiled_output(toplevel);
    if (output)
        reflow_output(server, output); // it glides to its new place, as tiles do
}

/* Resizing a pseudotiled window with the pointer: `edges` of `from`, the box it was taken at,
 * dragged to those of `rect`. It stays in the middle of its tile, so it grows both ways, as
 * much as keeps the edge dragged under the pointer. False for any other window. */
bool pseudo_resize(struct sh_toplevel *toplevel, uint32_t edges, struct wlr_box from,
                   struct sh_rect rect) {
    if (!pseudo_shows(toplevel))
        return false;
    double middle_x = from.x + from.width / 2.0, middle_y = from.y + from.height / 2.0;
    int width = from.width, height = from.height;
    if (edges & WLR_EDGE_LEFT)
        width = (int)lround(2 * (middle_x - rect.x));
    else if (edges & WLR_EDGE_RIGHT)
        width = (int)lround(2 * (rect.x + rect.width - middle_x));
    if (edges & WLR_EDGE_TOP)
        height = (int)lround(2 * (middle_y - rect.y));
    else if (edges & WLR_EDGE_BOTTOM)
        height = (int)lround(2 * (rect.y + rect.height - middle_y));
    set_pseudo_size(toplevel, width, height);
    return true;
}

/* Resizing a pseudotiled window from the keyboard by `amount` pixels: right and down make it
 * wider and taller, left and up narrower and shorter, about its middle. False for any other
 * window. */
bool pseudo_resize_by(struct sh_toplevel *toplevel, uint32_t direction, int amount) {
    if (!pseudo_shows(toplevel))
        return false;
    int width, height;
    pseudo_size(toplevel, &width, &height);
    // A size larger than its tile shows smaller: resizing starts from what shows.
    struct wlr_box shown = toplevel_box(toplevel);
    if (shown.width > 0 && shown.height > 0 && (shown.width < width || shown.height < height)) {
        width = shown.width;
        height = shown.height;
    }
    if (direction == SH_EDGE_RIGHT || direction == SH_EDGE_LEFT)
        width += direction == SH_EDGE_RIGHT ? amount : -amount;
    else
        height += direction == SH_EDGE_BOTTOM ? amount : -amount;
    set_pseudo_size(toplevel, width, height);
    return true;
}
