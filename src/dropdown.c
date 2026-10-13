/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "paw/dropdown.h"

static int min(int a, int b) { return a < b ? a : b; }
static int max(int a, int b) { return a > b ? a : b; }

/* How wide a strip the bars reserve along `edge`. */
static int strip(struct sh_drop_box output, struct sh_drop_box usable, enum sh_drop_edge edge) {
    switch (edge) {
    case SH_DROP_BOTTOM:
        return output.y + output.height - (usable.y + usable.height);
    case SH_DROP_LEFT:
        return usable.x - output.x;
    case SH_DROP_RIGHT:
        return output.x + output.width - (usable.x + usable.width);
    case SH_DROP_TOP:
    default:
        return usable.y - output.y;
    }
}

enum sh_drop_edge sh_drop_edge(struct sh_drop_box output, struct sh_drop_box usable) {
    if (strip(output, usable, SH_DROP_TOP) > 0)
        return SH_DROP_TOP;
    enum sh_drop_edge best = SH_DROP_TOP;
    int widest = 0;
    const enum sh_drop_edge others[] = {SH_DROP_BOTTOM, SH_DROP_LEFT, SH_DROP_RIGHT};
    for (int i = 0; i < 3; ++i) {
        int width = strip(output, usable, others[i]);
        if (width > widest) {
            widest = width;
            best = others[i];
        }
    }
    return best;
}

struct sh_drop_plan sh_drop_plan(struct sh_drop_box output, struct sh_drop_box usable,
                                 struct sh_drop_box frame, int corner, bool beyond, bool can_clip) {
    struct sh_drop_plan plan = {.edge = sh_drop_edge(output, usable)};
    int reserved = max(0, strip(output, usable, plan.edge));
    int overlap = min(reserved, max(0, corner));
    // The frame starts (and ends) with its near side on the cut, toward the edge from its place.
    switch (plan.edge) {
    case SH_DROP_TOP:
        plan.cut = usable.y - overlap;
        plan.dy = min(0, plan.cut - (frame.y + frame.height));
        break;
    case SH_DROP_BOTTOM:
        plan.cut = usable.y + usable.height + overlap;
        plan.dy = max(0, plan.cut - frame.y);
        break;
    case SH_DROP_LEFT:
        plan.cut = usable.x - overlap;
        plan.dx = min(0, plan.cut - (frame.x + frame.width));
        break;
    case SH_DROP_RIGHT:
        plan.cut = usable.x + usable.width + overlap;
        plan.dx = max(0, plan.cut - frame.x);
        break;
    }
    plan.clip = can_clip && (reserved > 0 || beyond);
    if (can_clip || !beyond)
        return plan;
    // Nothing cuts it, so its far side stops at the monitor's edge rather than cross another.
    switch (plan.edge) {
    case SH_DROP_TOP:
        plan.dy = max(plan.dy, min(0, output.y - frame.y));
        break;
    case SH_DROP_BOTTOM:
        plan.dy = min(plan.dy, max(0, output.y + output.height - (frame.y + frame.height)));
        break;
    case SH_DROP_LEFT:
        plan.dx = max(plan.dx, min(0, output.x - frame.x));
        break;
    case SH_DROP_RIGHT:
        plan.dx = min(plan.dx, max(0, output.x + output.width - (frame.x + frame.width)));
        break;
    }
    return plan;
}

struct sh_drop_box sh_drop_swept(const struct sh_drop_plan *plan, struct sh_drop_box output,
                                 struct sh_drop_box frame) {
    struct sh_drop_box moved = {frame.x + plan->dx, frame.y + plan->dy, frame.width, frame.height};
    switch (plan->edge) {
    case SH_DROP_BOTTOM: {
        int edge = output.y + output.height;
        return (struct sh_drop_box){frame.x, edge, frame.width, moved.y + moved.height - edge};
    }
    case SH_DROP_LEFT:
        return (struct sh_drop_box){moved.x, frame.y, output.x - moved.x, frame.height};
    case SH_DROP_RIGHT: {
        int edge = output.x + output.width;
        return (struct sh_drop_box){edge, frame.y, moved.x + moved.width - edge, frame.height};
    }
    case SH_DROP_TOP:
    default:
        return (struct sh_drop_box){frame.x, moved.y, frame.width, output.y - moved.y};
    }
}

bool sh_drop_cut(const struct sh_drop_plan *plan, struct sh_drop_box *box) {
    switch (plan->edge) {
    case SH_DROP_TOP: {
        int top = max(box->y, plan->cut);
        box->height = box->y + box->height - top;
        box->y = top;
        break;
    }
    case SH_DROP_BOTTOM:
        box->height = min(box->y + box->height, plan->cut) - box->y;
        break;
    case SH_DROP_LEFT: {
        int left = max(box->x, plan->cut);
        box->width = box->x + box->width - left;
        box->x = left;
        break;
    }
    case SH_DROP_RIGHT:
        box->width = min(box->x + box->width, plan->cut) - box->x;
        break;
    }
    return box->width > 0 && box->height > 0;
}
