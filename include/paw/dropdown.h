// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* The scratchpad's drop-down as arithmetic: the edge of a monitor's usable area a window slides
 * in from and back out to, how far it goes to be out of sight, and the line past which nothing of
 * it is drawn meanwhile, so that it comes out from behind the bar. Everything is in layout
 * coordinates; src/compositor/scratchpad.c moves and clips the window. */
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum sh_drop_edge { SH_DROP_TOP, SH_DROP_BOTTOM, SH_DROP_LEFT, SH_DROP_RIGHT };

struct sh_drop_box {
    int x, y, width, height;
};

struct sh_drop_plan {
    enum sh_drop_edge edge;
    /* With `clip`, nothing of the window is drawn past this line (a y along the top and bottom
     * edges, an x along the sides), toward the edge. */
    int cut;
    bool clip;
    /* Where the window is out of sight, from its place: it slides in from there and out to
     * there. */
    int dx, dy;
};

/* The edge a window drops from on a monitor whose box is `output` and whose bars leave `usable`:
 * the top when a bar reserves a strip along it, else the side with the widest strip, else the
 * top. */
enum sh_drop_edge sh_drop_edge(struct sh_drop_box output, struct sh_drop_box usable);

/* The drop of a window whose frame (its geometry and border) is `frame` and whose rounded corners
 * reach `corner` pixels in from its edges (the radius and the border; 0 when square). It is cut
 * inside the bar's strip, as deep as its corners reach (no deeper than the strip), so that the
 * corners pass behind the bar while the sides come out straight, and starts with its whole frame
 * past the cut. With no bar along the edge it is cut at the monitor's edge only when another
 * monitor lies `beyond` it on the way; past an edge with nothing beyond, nothing is drawn anyway.
 * Without `can_clip` (a renderer that cannot clip) nothing is cut, and a window that would pass
 * over another monitor goes no further than its own monitor's edge. */
struct sh_drop_plan sh_drop_plan(struct sh_drop_box output, struct sh_drop_box usable,
                                 struct sh_drop_box frame, int corner, bool beyond, bool can_clip);

/* The part of the way to the plan's (dx, dy) that lies past the monitor's own edge, where another
 * monitor would show the frame passing; its width or height is 0 or less when there is none. */
struct sh_drop_box sh_drop_swept(const struct sh_drop_plan *plan, struct sh_drop_box output,
                                 struct sh_drop_box frame);

/* Takes off `box` what lies past the plan's cut; false when nothing is left. */
bool sh_drop_cut(const struct sh_drop_plan *plan, struct sh_drop_box *box);

#ifdef __cplusplus
}
#endif
