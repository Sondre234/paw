// SPDX-License-Identifier: GPL-3.0-or-later
#include "paw/backend.h"
#include <algorithm>
#include <cmath>

extern "C" bool sh_placement(sh_action action, sh_rect area, int gap, int index, int count,
                             sh_rect *result) {
    if (!result || area.width < 1 || area.height < 1 || gap < 0 || index < 0 || count < 1 ||
        index >= count)
        return false;
    if (action == SH_MAXIMIZE) {
        *result = area;
        return true;
    }
    int columns = 2, rows = 1, column = 0, row = 0;
    if (action == SH_SNAP_RIGHT)
        column = 1;
    else if (action >= SH_SNAP_TOP_LEFT && action <= SH_SNAP_BOTTOM_RIGHT) {
        rows = 2;
        column = action == SH_SNAP_TOP_RIGHT || action == SH_SNAP_BOTTOM_RIGHT;
        row = action == SH_SNAP_BOTTOM_LEFT || action == SH_SNAP_BOTTOM_RIGHT;
    } else if (action == SH_TILE) {
        columns = static_cast<int>(std::ceil(std::sqrt(count)));
        rows = (count + columns - 1) / columns;
        column = index % columns;
        row = index / columns;
    } else if (action != SH_SNAP_LEFT)
        return false;
    if (area.width < columns || area.height < rows)
        return false;
    gap =
        std::min({gap, (area.width - columns) / (columns + 1), (area.height - rows) / (rows + 1)});
    const int width = area.width - (columns + 1) * gap;
    const int height = area.height - (rows + 1) * gap;
    const int left = width * column / columns;
    const int right = width * (column + 1) / columns;
    const int top = height * row / rows;
    const int bottom = height * (row + 1) / rows;
    *result = {area.x + gap * (column + 1) + left, area.y + gap * (row + 1) + top, right - left,
               bottom - top};
    return true;
}

extern "C" sh_action sh_snap_zone(sh_rect area, double x, double y, int distance, bool corners,
                                  uint32_t shared) {
    if (area.width < 1 || area.height < 1 || distance < 1)
        return SH_NONE;
    const bool left = x < area.x + distance && !(shared & SH_EDGE_LEFT);
    const bool right = !left && x >= area.x + area.width - distance && !(shared & SH_EDGE_RIGHT);
    const bool top = y < area.y + distance && !(shared & SH_EDGE_TOP);
    const bool bottom = !top && y >= area.y + area.height - distance && !(shared & SH_EDGE_BOTTOM);
    if (corners && (left || right) && (top || bottom))
        return top ? (left ? SH_SNAP_TOP_LEFT : SH_SNAP_TOP_RIGHT)
                   : (left ? SH_SNAP_BOTTOM_LEFT : SH_SNAP_BOTTOM_RIGHT);
    if (left || right)
        return left ? SH_SNAP_LEFT : SH_SNAP_RIGHT;
    return top ? SH_MAXIMIZE : SH_NONE;
}

extern "C" sh_snap_step sh_snap_cycle(sh_action from, sh_action direction, sh_action *to) {
    *to = SH_NONE;
    const bool left = direction == SH_SNAP_CYCLE_LEFT, right = direction == SH_SNAP_CYCLE_RIGHT;
    const bool up = direction == SH_SNAP_CYCLE_UP, down = direction == SH_SNAP_CYCLE_DOWN;
    if (!left && !right && !up && !down)
        return SH_SNAP_STEP_STAY;
    auto place = [&](sh_action arrangement, sh_snap_step step = SH_SNAP_STEP_PLACE) {
        *to = arrangement;
        return step;
    };
    switch (from) {
    case SH_MAXIMIZE:
        if (left || right)
            return place(left ? SH_SNAP_LEFT : SH_SNAP_RIGHT);
        return down ? SH_SNAP_STEP_RESTORE : SH_SNAP_STEP_STAY;
    case SH_SNAP_LEFT:
    case SH_SNAP_RIGHT: {
        const bool on_left = from == SH_SNAP_LEFT;
        if (up || down)
            return place(on_left ? (up ? SH_SNAP_TOP_LEFT : SH_SNAP_BOTTOM_LEFT)
                                 : (up ? SH_SNAP_TOP_RIGHT : SH_SNAP_BOTTOM_RIGHT));
        if (left != on_left)
            return SH_SNAP_STEP_RESTORE;
        return place(on_left ? SH_SNAP_RIGHT : SH_SNAP_LEFT, SH_SNAP_STEP_NEXT_OUTPUT);
    }
    case SH_SNAP_TOP_LEFT:
    case SH_SNAP_TOP_RIGHT:
    case SH_SNAP_BOTTOM_LEFT:
    case SH_SNAP_BOTTOM_RIGHT: {
        const bool on_left = from == SH_SNAP_TOP_LEFT || from == SH_SNAP_BOTTOM_LEFT;
        const bool top = from == SH_SNAP_TOP_LEFT || from == SH_SNAP_TOP_RIGHT;
        if (up)
            return place(top ? SH_MAXIMIZE : (on_left ? SH_SNAP_LEFT : SH_SNAP_RIGHT));
        if (down)
            return top ? place(on_left ? SH_SNAP_LEFT : SH_SNAP_RIGHT) : SH_SNAP_STEP_MINIMIZE;
        const sh_action beside = top ? (on_left ? SH_SNAP_TOP_RIGHT : SH_SNAP_TOP_LEFT)
                                     : (on_left ? SH_SNAP_BOTTOM_RIGHT : SH_SNAP_BOTTOM_LEFT);
        // Toward its own side it goes on to the next output, into the quarter facing back.
        return place(beside, left == on_left ? SH_SNAP_STEP_NEXT_OUTPUT : SH_SNAP_STEP_PLACE);
    }
    default:
        if (left || right)
            return place(left ? SH_SNAP_LEFT : SH_SNAP_RIGHT);
        return up ? place(SH_MAXIMIZE) : SH_SNAP_STEP_MINIMIZE;
    }
}

extern "C" unsigned sh_snap_quarters(sh_action arrangement) {
    switch (arrangement) {
    case SH_SNAP_LEFT:
        return 1 | 4;
    case SH_SNAP_RIGHT:
        return 2 | 8;
    case SH_SNAP_TOP_LEFT:
        return 1;
    case SH_SNAP_TOP_RIGHT:
        return 2;
    case SH_SNAP_BOTTOM_LEFT:
        return 4;
    case SH_SNAP_BOTTOM_RIGHT:
        return 8;
    default:
        return 0;
    }
}

extern "C" sh_action sh_snap_assist_slot(sh_action snapped, unsigned taken) {
    sh_action candidates[3] = {SH_NONE, SH_NONE, SH_NONE};
    switch (snapped) {
    case SH_SNAP_LEFT:
        candidates[0] = SH_SNAP_RIGHT, candidates[1] = SH_SNAP_TOP_RIGHT;
        candidates[2] = SH_SNAP_BOTTOM_RIGHT;
        break;
    case SH_SNAP_RIGHT:
        candidates[0] = SH_SNAP_LEFT, candidates[1] = SH_SNAP_TOP_LEFT;
        candidates[2] = SH_SNAP_BOTTOM_LEFT;
        break;
    case SH_SNAP_TOP_LEFT:
        candidates[0] = SH_SNAP_TOP_RIGHT, candidates[1] = SH_SNAP_BOTTOM_LEFT;
        candidates[2] = SH_SNAP_BOTTOM_RIGHT;
        break;
    case SH_SNAP_TOP_RIGHT:
        candidates[0] = SH_SNAP_TOP_LEFT, candidates[1] = SH_SNAP_BOTTOM_RIGHT;
        candidates[2] = SH_SNAP_BOTTOM_LEFT;
        break;
    case SH_SNAP_BOTTOM_LEFT:
        candidates[0] = SH_SNAP_BOTTOM_RIGHT, candidates[1] = SH_SNAP_TOP_LEFT;
        candidates[2] = SH_SNAP_TOP_RIGHT;
        break;
    case SH_SNAP_BOTTOM_RIGHT:
        candidates[0] = SH_SNAP_BOTTOM_LEFT, candidates[1] = SH_SNAP_TOP_RIGHT;
        candidates[2] = SH_SNAP_TOP_LEFT;
        break;
    default:
        return SH_NONE;
    }
    for (sh_action candidate : candidates)
        if (!(sh_snap_quarters(candidate) & taken))
            return candidate;
    return SH_NONE;
}
