// SPDX-License-Identifier: GPL-3.0-or-later
#include "paw/backend.h"
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

static bool operator==(const sh_rect &a, const sh_rect &b) {
    return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
}

namespace {
void require(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
using Placement = std::map<void *, sh_rect>;
Placement arrange(sh_tiling *tiling, sh_rect area, int gap, const char *output = "DP-1",
                  int workspace = 0) {
    Placement placed;
    sh_tiling_arrange(
        tiling, output, workspace, area, gap,
        [](void *data, void *window, sh_rect rect) {
            (*static_cast<Placement *>(data))[window] = rect;
        },
        &placed);
    return placed;
}
bool overlaps(const sh_rect &a, const sh_rect &b) {
    return a.x < b.x + b.width && b.x < a.x + a.width && a.y < b.y + b.height &&
           b.y < a.y + a.height;
}
} // namespace

int main() {
    try {
        const sh_rect area{-1920, 20, 1920, 1040};
        int windows[64];
        sh_tiling *tiling = sh_tiling_create();
        require(tiling, "cannot create tiling");
        require(arrange(tiling, area, 8).empty(), "empty tree placed windows");
        require(sh_tiling_count(tiling, "DP-1", 0) == 0 && sh_tiling_count(nullptr, "DP-1", 0) == 0 &&
                    sh_tiling_count(tiling, nullptr, 0) == 0,
                "an empty tree counts windows");

        sh_tiling_insert(tiling, "DP-1", 0, &windows[0], nullptr, false, 0, 0);
        auto placed = arrange(tiling, area, 8);
        require(placed.size() == 1, "single window missing");
        const sh_rect full = placed[&windows[0]];
        require(full.x == area.x + 8 && full.y == area.y + 8 && full.width == area.width - 16 &&
                    full.height == area.height - 16,
                "single window does not fill the area inside the gap");

        // A landscape split puts the new window on the right, with one gap between.
        sh_tiling_insert(tiling, "DP-1", 0, &windows[1], nullptr, false, 0, 0);
        placed = arrange(tiling, area, 8);
        sh_rect left = placed[&windows[0]], right = placed[&windows[1]];
        require(left.y == right.y && left.height == right.height, "first split not side by side");
        require(left.x + left.width + 8 == right.x, "inner gap wrong");
        require(right.x + right.width + 8 == area.x + area.width, "outer gap wrong");

        // The right half is portrait, so the third window goes below the second.
        sh_tiling_insert(tiling, "DP-1", 0, &windows[2], nullptr, false, 0, 0);
        placed = arrange(tiling, area, 8);
        require(placed[&windows[2]].x == placed[&windows[1]].x &&
                    placed[&windows[2]].y > placed[&windows[1]].y,
                "dwindle did not alternate the split direction");

        for (int count = 4; count <= 12; ++count) {
            sh_tiling_insert(tiling, "DP-1", 0, &windows[count - 1], nullptr, false, 0, 0);
            placed = arrange(tiling, area, 8);
            require(static_cast<int>(placed.size()) == count, "window lost");
            for (auto &[window, rect] : placed) {
                require(rect.width > 0 && rect.height > 0, "empty tile");
                require(rect.x >= area.x && rect.y >= area.y &&
                            rect.x + rect.width <= area.x + area.width &&
                            rect.y + rect.height <= area.y + area.height,
                        "tile outside the area");
                for (auto &[other, other_rect] : placed)
                    require(window == other || !overlaps(rect, other_rect), "tiles overlap");
            }
        }
        require(std::string(sh_tiling_output(tiling, &windows[5])) == "DP-1", "output lost");
        for (int i = 11; i >= 1; --i)
            sh_tiling_remove(tiling, &windows[i]);
        require(!sh_tiling_output(tiling, &windows[1]), "removed window still tiled");
        require(sh_tiling_count(tiling, "DP-1", 0) == 1, "removal did not count down");
        placed = arrange(tiling, area, 8);
        require(placed.size() == 1 && placed[&windows[0]].width == full.width &&
                    placed[&windows[0]].height == full.height,
                "removal did not give the space back");

        // A point in the left half of a landscape window puts the new window on its left.
        sh_tiling_insert(tiling, "DP-1", 0, &windows[1], &windows[0], true, area.x + 100, 500);
        placed = arrange(tiling, area, 8);
        require(placed[&windows[1]].x < placed[&windows[0]].x, "point did not choose the side");
        // Without a target, the point picks the window it is over.
        sh_tiling_insert(tiling, "DP-1", 0, &windows[2], nullptr, true, area.x + 100, 1000);
        placed = arrange(tiling, area, 8);
        require(placed[&windows[2]].x == placed[&windows[1]].x &&
                    placed[&windows[2]].y > placed[&windows[1]].y,
                "point did not choose the window under it");
        require(placed[&windows[0]].height == full.height, "unrelated window was split");

        // Dragging the right edge of the left column moves the split under the pointer.
        sh_rect column = placed[&windows[1]];
        column.width = area.x + 1200 - column.x;
        require(sh_tiling_resize(tiling, &windows[1], SH_EDGE_RIGHT, column), "resize ignored");
        placed = arrange(tiling, area, 8);
        require(std::abs(placed[&windows[0]].x - (area.x + 1200 + 4)) <= 4,
                "split did not follow the resized edge");
        require(!sh_tiling_resize(tiling, &windows[0], SH_EDGE_RIGHT, placed[&windows[0]]),
                "the outer edge of the output moved");

        // A preview gives the tile a new window would get, without changing the tree.
        const Placement before = arrange(tiling, area, 8);
        sh_rect preview{};
        require(sh_tiling_preview(tiling, "DP-1", 0, &windows[5], &windows[1], false, 0, 0, area, 8,
                                  &preview),
                "preview failed");
        require(!sh_tiling_preview(tiling, "DP-1", 0, &windows[1], nullptr, false, 0, 0, area, 8,
                                   &preview),
                "previewed a window that is already tiled");
        require(arrange(tiling, area, 8) == before, "preview changed the tree");
        sh_tiling_insert(tiling, "DP-1", 0, &windows[5], &windows[1], false, 0, 0);
        placed = arrange(tiling, area, 8);
        require(placed[&windows[5]] == preview, "preview differs from the actual tile");
        sh_tiling_remove(tiling, &windows[5]);
        require(arrange(tiling, area, 8) == before, "removal did not restore the tree");

        // Trees are separate per output and workspace.
        sh_tiling_insert(tiling, "DP-1", 1, &windows[3], nullptr, false, 0, 0);
        sh_tiling_insert(tiling, "HDMI-A-1", 0, &windows[4], nullptr, false, 0, 0);
        require(arrange(tiling, area, 8, "DP-1", 1).size() == 1, "workspace trees shared");
        require(arrange(tiling, area, 8, "HDMI-A-1", 0).size() == 1, "output trees shared");
        require(arrange(tiling, area, 8).size() == 3, "other trees changed this one");
        sh_tiling_insert(tiling, "DP-1", 0, &windows[3], nullptr, false, 0, 0);
        require(arrange(tiling, area, 8).size() == 3, "a window was tiled twice");
        // Each tree counts its own windows.
        require(sh_tiling_count(tiling, "DP-1", 0) == 3 && sh_tiling_count(tiling, "DP-1", 1) == 1 &&
                    sh_tiling_count(tiling, "HDMI-A-1", 0) == 1 &&
                    sh_tiling_count(tiling, "HDMI-A-1", 1) == 0 &&
                    sh_tiling_count(tiling, "DP-2", 0) == 0,
                "tile counts wrong");

        // Keyboard resizing moves a split beside the tile in the arrow's direction: the tile's
        // own edge on that side when it is a split (growing it), else the opposite one.
        {
            sh_tiling *keys = sh_tiling_create();
            const sh_rect screen{0, 0, 1200, 800};
            sh_tiling_insert(keys, "DP-1", 0, &windows[0], nullptr, false, 0, 0);
            sh_tiling_insert(keys, "DP-1", 0, &windows[1], nullptr, false, 0, 0);
            sh_tiling_insert(keys, "DP-1", 0, &windows[2], nullptr, false, 0, 0);
            // [0] | ([1] over [2]), without gaps so the splits sit at exact pixels.
            placed = arrange(keys, screen, 0);
            require(placed[&windows[0]].width == 600 && placed[&windows[1]].height == 400,
                    "unexpected starting layout");
            auto after = [&](void *window, uint32_t direction, int amount) {
                bool changed = sh_tiling_resize_by(keys, window, direction, amount);
                placed = arrange(keys, screen, 0);
                return changed;
            };
            auto near = [](int value, int expected) { return std::abs(value - expected) <= 1; };
            require(after(&windows[0], SH_EDGE_RIGHT, 40) && near(placed[&windows[0]].width, 640),
                    "resize_right did not grow the left tile");
            require(near(placed[&windows[1]].x, 640) && near(placed[&windows[2]].width, 560),
                    "the neighbours did not give way");
            require(after(&windows[0], SH_EDGE_LEFT, 80) && near(placed[&windows[0]].width, 560),
                    "resize_left at the output edge did not shrink the tile from the right");
            require(after(&windows[1], SH_EDGE_LEFT, 40) && near(placed[&windows[1]].x, 520) &&
                        near(placed[&windows[0]].width, 520),
                    "resize_left did not grow the right column");
            require(after(&windows[2], SH_EDGE_RIGHT, 20) && near(placed[&windows[2]].x, 540),
                    "resize_right at the output edge did not move the left split right");
            require(after(&windows[1], SH_EDGE_BOTTOM, 50) && near(placed[&windows[1]].height, 450),
                    "resize_down did not grow the upper tile");
            require(after(&windows[2], SH_EDGE_BOTTOM, 50) && near(placed[&windows[2]].y, 500) &&
                        near(placed[&windows[2]].height, 300),
                    "resize_down at the bottom did not shrink the lower tile from above");
            require(after(&windows[1], SH_EDGE_TOP, 100) && near(placed[&windows[1]].height, 400),
                    "resize_up at the top did not shrink the upper tile");
            require(after(&windows[2], SH_EDGE_TOP, 30) && near(placed[&windows[2]].y, 370),
                    "resize_up did not grow the lower tile");
            // Splits stop at the same limits as a mouse resize, then report no change.
            require(after(&windows[0], SH_EDGE_RIGHT, 5000), "large resize ignored");
            require(near(placed[&windows[0]].width, 1080), "split not limited to 90%");
            require(!after(&windows[0], SH_EDGE_RIGHT, 40), "resize past the limit changed it");
            require(!sh_tiling_resize_by(keys, &windows[5], SH_EDGE_RIGHT, 40),
                    "resized a window that is not tiled");
            require(!sh_tiling_resize_by(keys, &windows[0], SH_EDGE_LEFT, 0),
                    "resized by nothing");
            // A single tile has no split to move.
            sh_tiling_insert(keys, "DP-1", 1, &windows[6], nullptr, false, 0, 0);
            arrange(keys, screen, 0, "DP-1", 1);
            require(!sh_tiling_resize_by(keys, &windows[6], SH_EDGE_RIGHT, 40),
                    "a lone tile changed");
            sh_tiling_destroy(keys);
        }

        // Other layouts read the windows in order and leave the dwindle tree as it was.
        {
            sh_tiling *lay = sh_tiling_create();
            const sh_rect screen{0, 0, 1000, 800};
            int w[8];
            for (int i = 0; i < 5; ++i)
                sh_tiling_insert(lay, "DP-1", 0, &w[i], nullptr, false, 0, 0);
            auto dwindle = arrange(lay, screen, 0);
            require(sh_tiling_layout(lay, "DP-1", 0) == SH_LAYOUT_DWINDLE, "dwindle not default");

            // Master-stack: one master on the left at 55%, the rest stacked on the right.
            sh_tiling_set_layout(lay, "DP-1", 0, SH_LAYOUT_MASTER);
            placed = arrange(lay, screen, 0);
            require(placed[&w[0]] == sh_rect({0, 0, 550, 800}), "master column wrong");
            for (int i = 1; i < 5; ++i)
                require(placed[&w[i]] == sh_rect({550, (i - 1) * 200, 450, 200}),
                        "stack tile wrong");
            // Two masters, and the ratio.
            require(sh_tiling_adjust(lay, "DP-1", 0, 0, 1), "master count not raised");
            require(sh_tiling_adjust(lay, "DP-1", 0, -0.05, 0), "ratio not lowered");
            placed = arrange(lay, screen, 0);
            require(placed[&w[0]] == sh_rect({0, 0, 500, 400}) &&
                        placed[&w[1]] == sh_rect({0, 400, 500, 400}),
                    "two masters wrong");
            require(placed[&w[4]] == sh_rect({500, 533, 500, 267}), "three stack tiles wrong");
            // Limits.
            for (int i = 0; i < 20; ++i)
                sh_tiling_adjust(lay, "DP-1", 0, 0.05, 1);
            require(sh_tiling_ratio(lay, "DP-1", 0) <= 0.9 && sh_tiling_master_count(lay, "DP-1", 0) == 8,
                    "adjust limits not kept");
            // Every window a master: they fill the width.
            placed = arrange(lay, screen, 0);
            require(placed[&w[0]].width == 1000, "all-master tiles do not fill the width");
            for (int i = 0; i < 20; ++i)
                sh_tiling_adjust(lay, "DP-1", 0, -0.05, -1);
            require(sh_tiling_ratio(lay, "DP-1", 0) >= 0.1 && sh_tiling_master_count(lay, "DP-1", 0) == 1,
                    "lower limits not kept");
            sh_tiling_adjust(lay, "DP-1", 0, 0.45 - sh_tiling_ratio(lay, "DP-1", 0) + 0.1, 0);
            // Mouse and keyboard resizing move the master boundary.
            placed = arrange(lay, screen, 0);
            int boundary = placed[&w[0]].width;
            require(sh_tiling_resize_by(lay, &w[0], SH_EDGE_RIGHT, 100), "master resize failed");
            placed = arrange(lay, screen, 0);
            require(placed[&w[0]].width == boundary + 100, "resize_right did not widen master");
            require(sh_tiling_resize_by(lay, &w[3], SH_EDGE_LEFT, 100) &&
                        arrange(lay, screen, 0)[&w[0]].width == boundary,
                    "resize_left on the stack did not narrow master");
            require(!sh_tiling_resize_by(lay, &w[0], SH_EDGE_BOTTOM, 100), "vertical resize");
            require(sh_tiling_resize(lay, &w[0], SH_EDGE_RIGHT, {0, 0, 300, 800}),
                    "mouse resize of master failed");
            require(arrange(lay, screen, 0)[&w[0]].width == 300, "mouse resize wrong");
            require(sh_tiling_resize(lay, &w[2], SH_EDGE_LEFT, {600, 200, 400, 200}),
                    "mouse resize of stack failed");
            require(arrange(lay, screen, 0)[&w[0]].width == 600, "stack mouse resize wrong");

            // Spiral: left, top, right, bottom, each `ratio` of what is left.
            sh_tiling_set_layout(lay, "DP-1", 0, SH_LAYOUT_SPIRAL);
            sh_tiling_adjust(lay, "DP-1", 0, 0.5 - sh_tiling_ratio(lay, "DP-1", 0), 0);
            placed = arrange(lay, screen, 0);
            require(placed[&w[0]] == sh_rect({0, 0, 500, 800}), "spiral first wrong");
            require(placed[&w[1]] == sh_rect({500, 0, 500, 400}), "spiral second wrong");
            require(placed[&w[2]] == sh_rect({750, 400, 250, 400}), "spiral third wrong");
            require(placed[&w[3]] == sh_rect({500, 600, 250, 200}), "spiral fourth wrong");
            require(placed[&w[4]] == sh_rect({500, 400, 250, 200}), "spiral last wrong");
            for (int i = 0; i < 5; ++i)
                for (int j = i + 1; j < 5; ++j)
                    require(!overlaps(placed[&w[i]], placed[&w[j]]), "spiral tiles overlap");

            // Monocle: everything fills the area, inside the gap.
            sh_tiling_set_layout(lay, "DP-1", 0, SH_LAYOUT_MONOCLE);
            placed = arrange(lay, screen, 10);
            for (int i = 0; i < 5; ++i)
                require(placed[&w[i]] == sh_rect({10, 10, 980, 780}), "monocle tile wrong");
            require(!sh_tiling_resize_by(lay, &w[0], SH_EDGE_RIGHT, 40), "monocle resized");

            // Layouts are per workspace, and cycle both ways.
            require(sh_tiling_layout(lay, "DP-1", 1) == SH_LAYOUT_DWINDLE, "layout leaked");
            require(sh_tiling_cycle_layout(lay, "DP-1", 0, 1) == SH_LAYOUT_SCROLL, "cycle to scroll");
            require(sh_tiling_cycle_layout(lay, "DP-1", 0, 1) == SH_LAYOUT_DWINDLE, "cycle wraps");
            require(sh_tiling_cycle_layout(lay, "DP-1", 0, -1) == SH_LAYOUT_SCROLL, "cycle back");
            require(sh_tiling_cycle_layout(lay, "DP-1", 0, -1) == SH_LAYOUT_MONOCLE, "cycle back again");

            // New windows join the end of the list in any other layout, wherever dropped.
            sh_tiling_set_layout(lay, "DP-1", 0, SH_LAYOUT_MASTER);
            sh_tiling_insert(lay, "DP-1", 0, &w[5], &w[0], true, 1, 1);
            require(sh_tiling_neighbour(lay, &w[4], 1) == &w[5], "new window not appended");
            require(sh_tiling_neighbour(lay, &w[5], 1) == &w[0], "neighbour does not wrap");
            require(sh_tiling_neighbour(lay, &w[0], -1) == &w[5], "neighbour back does not wrap");
            require(sh_tiling_master(lay, "DP-1", 0) == &w[0], "master window wrong");
            sh_tiling_remove(lay, &w[5]);

            // Swapping trades places and survives a return to dwindle.
            require(sh_tiling_swap(lay, &w[0], &w[3]), "swap failed");
            require(sh_tiling_master(lay, "DP-1", 0) == &w[3], "swap did not promote");
            require(!sh_tiling_swap(lay, &w[0], &w[0]) && !sh_tiling_swap(lay, &w[0], &w[7]),
                    "invalid swap accepted");
            sh_tiling_adjust(lay, "DP-1", 0, 0, -8);
            placed = arrange(lay, screen, 0);
            require(placed[&w[3]].x == 0 && placed[&w[0]].x > 0, "swapped master not first");
            sh_tiling_set_layout(lay, "DP-1", 0, SH_LAYOUT_DWINDLE);
            auto back = arrange(lay, screen, 0);
            require(back[&w[3]] == dwindle[&w[0]] && back[&w[0]] == dwindle[&w[3]] &&
                        back[&w[1]] == dwindle[&w[1]],
                    "dwindle tree lost its shape after a swap");

            // Replacing takes over the slot; the old window is no longer tiled.
            require(sh_tiling_replace(lay, &w[1], &w[7]), "replace failed");
            require(!sh_tiling_output(lay, &w[1]) && sh_tiling_output(lay, &w[7]),
                    "replace kept the wrong window tiled");
            back = arrange(lay, screen, 0);
            require(back[&w[7]] == dwindle[&w[1]] && !back.count(&w[1]), "replacement moved the slot");
            require(sh_tiling_count(lay, "DP-1", 0) == 5, "a replacement changed the count");
            require(!sh_tiling_replace(lay, &w[1], &w[8]) && !sh_tiling_replace(lay, &w[7], &w[0]) &&
                        !sh_tiling_replace(lay, &w[7], &w[7]),
                    "invalid replace accepted");
            require(sh_tiling_replace(lay, &w[7], &w[1]), "replace back failed");

            // Defaults apply to workspaces that did not choose; chosen ones keep theirs.
            sh_tiling_set_defaults(lay, SH_LAYOUT_SPIRAL, 0.6, 2);
            require(sh_tiling_layout(lay, "DP-1", 0) == SH_LAYOUT_DWINDLE, "default overrode a choice");
            require(sh_tiling_layout(lay, "DP-2", 3) == SH_LAYOUT_SPIRAL, "default layout unused");
            require(sh_tiling_master_count(lay, "DP-2", 3) == 2, "default count unused");

            // An output's own defaults sit between the global ones and a workspace's choice, and
            // reach every workspace that has not chosen, even after it was arranged.
            sh_tiling_set_output_defaults(lay, "DP-2", SH_LAYOUT_SCROLL, 0.7, 3);
            require(sh_tiling_layout(lay, "DP-2", 3) == SH_LAYOUT_SCROLL &&
                        sh_tiling_ratio(lay, "DP-2", 3) == 0.7 &&
                        sh_tiling_master_count(lay, "DP-2", 3) == 3,
                    "output defaults unused");
            require(sh_tiling_layout(lay, "DP-3", 3) == SH_LAYOUT_SPIRAL &&
                        sh_tiling_ratio(lay, "DP-3", 3) == 0.6,
                    "output defaults leaked to another output");
            require(sh_tiling_layout(lay, "DP-1", 0) == SH_LAYOUT_DWINDLE,
                    "output defaults beat a workspace's own layout");
            sh_tiling_set_layout(lay, "DP-2", 1, SH_LAYOUT_MONOCLE);
            sh_tiling_adjust(lay, "DP-2", 2, 0.1, 0); // chooses the ratio, not the layout
            sh_tiling_set_output_defaults(lay, "DP-2", SH_LAYOUT_MASTER, 0, 0);
            require(sh_tiling_layout(lay, "DP-2", 1) == SH_LAYOUT_MONOCLE &&
                        sh_tiling_layout(lay, "DP-2", 2) == SH_LAYOUT_MASTER &&
                        sh_tiling_layout(lay, "DP-2", 3) == SH_LAYOUT_MASTER,
                    "a new output default did not reach unchosen workspaces");
            require(std::abs(sh_tiling_ratio(lay, "DP-2", 2) - 0.8) < 1e-9 &&
                        std::abs(sh_tiling_ratio(lay, "DP-2", 3) - 0.6) < 1e-9,
                    "ratio default and choice mixed up");
            {
                enum sh_tile_layout l;
                double r;
                int n;
                sh_tiling_output_defaults(lay, "DP-2", &l, &r, &n);
                require(l == SH_LAYOUT_MASTER && r == 0.6 && n == 2, "defaults readback wrong");
                sh_tiling_output_defaults(lay, "DP-9", &l, &r, &n);
                require(l == SH_LAYOUT_SPIRAL && n == 2, "readback of an unset output wrong");
            }
            sh_tiling_clear_output_defaults(lay);
            require(sh_tiling_layout(lay, "DP-2", 3) == SH_LAYOUT_SPIRAL &&
                        sh_tiling_layout(lay, "DP-2", 1) == SH_LAYOUT_MONOCLE,
                    "clearing output defaults broke a choice");
            // Cycling steps from what the workspace shows, then chooses it.
            require(sh_tiling_cycle_layout(lay, "DP-2", 3, 1) == SH_LAYOUT_MONOCLE, "cycle wrong");
            sh_tiling_set_layout(lay, "DP-2", 3, SH_LAYOUT_SPIRAL);

            // Exchanging two workspaces trades their windows, splits and chosen state, also
            // with an empty one, and leaves what was not chosen to the output's defaults.
            {
                sh_tiling *ex = sh_tiling_create();
                int e[4];
                sh_tiling_set_output_defaults(ex, "DP-2", SH_LAYOUT_MONOCLE, 0, 0);
                sh_tiling_insert(ex, "DP-1", 0, &e[0], nullptr, false, 0, 0);
                sh_tiling_insert(ex, "DP-1", 0, &e[1], &e[0], false, 0, 0);
                sh_tiling_insert(ex, "DP-2", 1, &e[2], nullptr, false, 0, 0);
                sh_tiling_set_layout(ex, "DP-1", 0, SH_LAYOUT_MASTER);
                sh_tiling_adjust(ex, "DP-1", 0, 0.1, 0);
                require(!sh_tiling_exchange(ex, "DP-1", 0, "DP-1", 0), "self exchange accepted");
                require(sh_tiling_exchange(ex, "DP-1", 0, "DP-2", 1), "exchange failed");
                require(!strcmp(sh_tiling_output(ex, &e[0]), "DP-2") &&
                            !strcmp(sh_tiling_output(ex, &e[1]), "DP-2") &&
                            !strcmp(sh_tiling_output(ex, &e[2]), "DP-1"),
                        "windows did not change output");
                require(sh_tiling_layout(ex, "DP-2", 1) == SH_LAYOUT_MASTER &&
                            std::abs(sh_tiling_ratio(ex, "DP-2", 1) - 0.65) < 1e-9 &&
                            sh_tiling_layout(ex, "DP-1", 0) == SH_LAYOUT_DWINDLE,
                        "chosen state did not travel");
                auto moved = arrange(ex, screen, 0, "DP-2", 1);
                require(moved.size() == 2 && moved[&e[0]].width > moved[&e[1]].width,
                        "moved workspace lost its arrangement");
                auto other = arrange(ex, screen, 0, "DP-1", 0);
                require(other.size() == 1 && other.count(&e[2]) && arrange(ex, screen, 0, "DP-2", 0).empty(),
                        "the traded slot holds the wrong windows");
                // Into an empty workspace and back.
                require(sh_tiling_exchange(ex, "DP-2", 1, "DP-1", 3) &&
                            !strcmp(sh_tiling_output(ex, &e[1]), "DP-1") &&
                            sh_tiling_layout(ex, "DP-1", 3) == SH_LAYOUT_MASTER,
                        "exchange with an empty workspace failed");
                sh_tiling_remove(ex, &e[0]);
                sh_tiling_remove(ex, &e[1]);
                sh_tiling_destroy(ex);
            }

            // One window in every layout fills the area.
            sh_tiling_insert(lay, "DP-2", 0, &w[7], nullptr, false, 0, 0);
            for (int layout = 0; layout < SH_LAYOUT_COUNT; ++layout) {
                if (layout == SH_LAYOUT_SCROLL) // its columns have their own widths
                    continue;
                sh_tiling_set_layout(lay, "DP-2", 0, static_cast<sh_tile_layout>(layout));
                placed = arrange(lay, screen, 0, "DP-2");
                require(placed[&w[7]] == screen, "a lone tile does not fill the area");
            }
            sh_tiling_destroy(lay);
        }

        // Scroll: columns on a strip, the view following focus.
        {
            const sh_rect screen{0, 0, 1000, 800};
            int c[8];
            sh_tiling *sc = sh_tiling_create();
            const float presets[] = {0.5F, 1.0F, 1.0F / 3};
            sh_tiling_set_scroll(sc, SH_SCROLL_FOLLOW_EDGE, 0.5, 0.1, presets, 3);
            sh_tiling_set_layout(sc, "DP-1", 0, SH_LAYOUT_SCROLL);
            auto open = [&](int index, const void *after = nullptr) {
                sh_tiling_insert(sc, "DP-1", 0, &c[index], after, false, 0, 0);
                sh_tiling_set_focus(sc, &c[index]);
            };
            open(0);
            open(1, &c[0]);
            auto placed = arrange(sc, screen, 0);
            require(placed[&c[0]] == sh_rect({0, 0, 500, 800}) &&
                        placed[&c[1]] == sh_rect({500, 0, 500, 800}),
                    "two half columns do not fill the view");
            // A third column is off the right edge; the view moves just far enough.
            open(2, &c[1]);
            placed = arrange(sc, screen, 0);
            require(placed[&c[2]] == sh_rect({500, 0, 500, 800}) &&
                        placed[&c[1]].x == 0 && placed[&c[0]].x == -500,
                    "edge follow did not reveal the new column");
            // Focus back on a visible column: edge follow leaves the view alone.
            sh_tiling_set_focus(sc, &c[1]);
            placed = arrange(sc, screen, 0);
            require(placed[&c[1]].x == 0 && placed[&c[2]].x == 500, "edge follow moved needlessly");
            // The first column brings the view back.
            require(sh_tiling_set_focus(sc, &c[0]), "focus change asks for no arrangement");
            placed = arrange(sc, screen, 0);
            require(placed[&c[0]].x == 0 && placed[&c[1]].x == 500 && placed[&c[2]].x == 1000,
                    "view did not return");
            // A new window opens right of the focused column, not at the end.
            open(3, &c[0]);
            placed = arrange(sc, screen, 0);
            require(placed[&c[3]].x == 500 && placed[&c[1]].x == 1000 && placed[&c[2]].x == 1500,
                    "new column not opened right of the focused one");
            require(sh_tiling_neighbour(sc, &c[0], 1) == &c[3] &&
                        sh_tiling_neighbour(sc, &c[2], 1) == &c[0],
                    "scroll order wrong");
            require(sh_tiling_master(sc, "DP-1", 0) == &c[0], "first column is not the master");
            require(sh_tiling_scroll_step(sc, &c[0], 1, 0) == &c[3] &&
                        !sh_tiling_scroll_step(sc, &c[0], -1, 0) &&
                        !sh_tiling_scroll_step(sc, &c[2], 1, 0),
                    "column steps wrong or wrap");

            // Always-center: the focused column sits in the middle, even the first.
            sh_tiling_set_scroll(sc, SH_SCROLL_FOLLOW_CENTER, 0.5, 0.1, presets, 3);
            sh_tiling_set_focus(sc, &c[3]);
            sh_tiling_set_focus(sc, &c[1]);
            placed = arrange(sc, screen, 0);
            require(placed[&c[1]] == sh_rect({250, 0, 500, 800}) &&
                        placed[&c[3]].x == -250 && placed[&c[2]].x == 750,
                    "center follow did not center");
            sh_tiling_set_focus(sc, &c[0]);
            placed = arrange(sc, screen, 0);
            require(placed[&c[0]].x == 250, "the first column is not centered");

            // Never: only explicit scrolling and new windows move the view.
            sh_tiling_set_scroll(sc, SH_SCROLL_FOLLOW_NEVER, 0.5, 0.1, presets, 3);
            sh_tiling_set_focus(sc, &c[1]);
            sh_tiling_scroll_action(sc, &c[1], SH_CENTER_COLUMN);
            placed = arrange(sc, screen, 0);
            // Columns are c0 c3 c1 c2; c1 is third, so it centers within the strip's reach.
            require(placed[&c[1]].x == 250, "explicit centering did not center");
            sh_tiling_set_focus(sc, &c[2]);
            placed = arrange(sc, screen, 0);
            require(placed[&c[1]].x == 250, "never follow moved the view on focus");
            require(sh_tiling_scroll_step(sc, &c[2], -1, 0) == &c[1], "step left");
            require(sh_tiling_set_focus(sc, &c[1]), "revealed step needs no arrangement");
            sh_tiling_set_scroll(sc, SH_SCROLL_FOLLOW_EDGE, 0.5, 0.1, presets, 3);

            // Widths cycle through the presets, wider first, and wrap.
            sh_tiling_set_focus(sc, &c[0]);
            arrange(sc, screen, 0);
            require(sh_tiling_scroll_action(sc, &c[0], SH_COLUMN_CYCLE_WIDTH) &&
                        arrange(sc, screen, 0)[&c[0]].width == 1000,
                    "cycle did not go to the full width");
            sh_tiling_scroll_action(sc, &c[0], SH_COLUMN_CYCLE_WIDTH);
            require(arrange(sc, screen, 0)[&c[0]].width == 333, "cycle did not wrap to a third");
            sh_tiling_scroll_action(sc, &c[0], SH_COLUMN_CYCLE_WIDTH);
            require(arrange(sc, screen, 0)[&c[0]].width == 500, "cycle from a third");
            // Widening and narrowing stay within 10% and 100%.
            for (int i = 0; i < 20; ++i)
                sh_tiling_scroll_action(sc, &c[0], SH_COLUMN_WIDEN);
            require(arrange(sc, screen, 0)[&c[0]].width == 1000, "widen not capped");
            for (int i = 0; i < 20; ++i)
                sh_tiling_scroll_action(sc, &c[0], SH_COLUMN_NARROW);
            require(arrange(sc, screen, 0)[&c[0]].width == 100, "narrow not capped");
            sh_tiling_scroll_action(sc, &c[0], SH_COLUMN_CYCLE_WIDTH); // a third
            sh_tiling_scroll_action(sc, &c[0], SH_COLUMN_CYCLE_WIDTH); // half
            require(arrange(sc, screen, 0)[&c[0]].width == 500, "presets drifted");
            // Keyboard and mouse resizing set the width too.
            require(sh_tiling_resize_by(sc, &c[0], SH_EDGE_RIGHT, 100) &&
                        arrange(sc, screen, 0)[&c[0]].width == 600,
                    "resize_right did not widen the column");
            require(sh_tiling_resize(sc, &c[0], SH_EDGE_RIGHT, {0, 0, 400, 800}) &&
                        arrange(sc, screen, 0)[&c[0]].width == 400,
                    "mouse resize did not set the width");
            require(!sh_tiling_resize_by(sc, &c[0], SH_EDGE_BOTTOM, 100), "vertical column resize");
            sh_tiling_scroll_action(sc, &c[0], SH_COLUMN_CYCLE_WIDTH);
            sh_tiling_scroll_action(sc, &c[0], SH_COLUMN_CYCLE_WIDTH);
            sh_tiling_scroll_action(sc, &c[0], SH_COLUMN_CYCLE_WIDTH);

            // Consuming stacks a window into the neighbouring column; expelling undoes it.
            // Columns: c0 c3 c1 c2.
            require(!sh_tiling_scroll_action(sc, &c[0], SH_CONSUME_LEFT), "consumed past the start");
            require(sh_tiling_scroll_action(sc, &c[3], SH_CONSUME_LEFT), "consume_left failed");
            placed = arrange(sc, screen, 0);
            require(placed[&c[0]].x == placed[&c[3]].x && placed[&c[0]].width == placed[&c[3]].width &&
                        placed[&c[0]].height == 400 && placed[&c[3]].y == 400,
                    "consumed window not stacked below");
            require(placed[&c[1]].x == placed[&c[0]].x + placed[&c[0]].width,
                    "an emptied column left a hole");
            require(sh_tiling_scroll_step(sc, &c[0], 0, 1) == &c[3] &&
                        !sh_tiling_scroll_step(sc, &c[3], 0, 1) &&
                        sh_tiling_scroll_step(sc, &c[3], 1, 0) == &c[1],
                    "stack steps wrong");
            require(sh_tiling_scroll_action(sc, &c[1], SH_CONSUME_LEFT), "consume into stack");
            placed = arrange(sc, screen, 0);
            require(placed[&c[1]].y == 533 && placed[&c[0]].height == 266, "three stacked");
            require(sh_tiling_scroll_action(sc, &c[1], SH_EXPEL) &&
                        !sh_tiling_scroll_action(sc, &c[2], SH_EXPEL),
                    "expel rules wrong");
            placed = arrange(sc, screen, 0);
            require(placed[&c[1]].x > placed[&c[0]].x && placed[&c[1]].height == 800 &&
                        placed[&c[2]].x > placed[&c[1]].x,
                    "expelled window not in a column of its own");
            require(sh_tiling_scroll_action(sc, &c[2], SH_CONSUME_RIGHT) == false,
                    "consumed past the end");
            require(sh_tiling_scroll_action(sc, &c[0], SH_CONSUME_RIGHT), "consume_right failed");
            placed = arrange(sc, screen, 0);
            require(placed[&c[0]].y == 0 && placed[&c[1]].y == 400 && placed[&c[1]].height == 400,
                    "consume_right did not go on top");
            sh_tiling_scroll_action(sc, &c[0], SH_EXPEL);

            // Moving a column trades it with its neighbour; whole stacks move together.
            require(sh_tiling_scroll_move(sc, &c[1], -1) && !sh_tiling_scroll_move(sc, &c[1], -1) &&
                        !sh_tiling_scroll_move(sc, &c[2], 1),
                    "column moves wrong");
            require(sh_tiling_swap(sc, &c[0], &c[2]) && sh_tiling_master(sc, "DP-1", 0) != nullptr,
                    "swap in the scroll layout failed");

            // Gaps: outer gaps at the ends of the strip, one gap between neighbours.
            sh_tiling *gapped = sh_tiling_create();
            sh_tiling_set_scroll(gapped, SH_SCROLL_FOLLOW_EDGE, 0.5, 0.1, presets, 3);
            sh_tiling_set_layout(gapped, "DP-1", 0, SH_LAYOUT_SCROLL);
            sh_tiling_insert(gapped, "DP-1", 0, &c[4], nullptr, false, 0, 0);
            sh_tiling_insert(gapped, "DP-1", 0, &c[5], &c[4], false, 0, 0);
            placed = arrange(gapped, screen, 10);
            require(placed[&c[4]] == sh_rect({10, 10, 485, 780}) &&
                        placed[&c[5]] == sh_rect({505, 10, 485, 780}),
                    "gaps differ from the other layouts");
            sh_tiling_scroll_action(gapped, &c[5], SH_CONSUME_LEFT);
            placed = arrange(gapped, screen, 10);
            require(placed[&c[4]] == sh_rect({10, 10, 485, 385}) &&
                        placed[&c[5]] == sh_rect({10, 405, 485, 385}),
                    "stacked gaps wrong");
            sh_tiling_destroy(gapped);

            // Removing windows drops their columns; the view stays within the strip.
            for (int i : {0, 1, 2, 3})
                sh_tiling_remove(sc, &c[i]);
            require(arrange(sc, screen, 0).empty(), "windows left after removal");
            // A layout switched to scroll later gets a column per window, in order.
            sh_tiling *late = sh_tiling_create();
            for (int i = 0; i < 3; ++i)
                sh_tiling_insert(late, "DP-1", 0, &c[i], nullptr, false, 0, 0);
            sh_tiling_set_layout(late, "DP-1", 0, SH_LAYOUT_SCROLL);
            placed = arrange(late, screen, 0);
            require(placed[&c[0]].x == 0 && placed[&c[1]].x == 500 && placed[&c[2]].x == 1000,
                    "existing windows not given columns");
            // Saved columns come back: c2 and c0 share the first column (c2 on top), c1 is
            // alone in the third; the widths are those of the saved column numbers.
            {
                int c_of[3] = {0, 2, 0}, row_of[3] = {1, 0, 0};
                void *win[3] = {&c[0], &c[1], &c[2]};
                double widths[3] = {0.4, 0.2, 0.6};
                require(sh_tiling_scroll_restore(late, "DP-1", 0, win, c_of, row_of, 3, widths, 3),
                        "scroll restore failed");
                placed = arrange(late, screen, 0);
                require(placed[&c[2]].x == 0 && placed[&c[0]].x == 0 && placed[&c[2]].y == 0 &&
                            placed[&c[0]].y == 400 && placed[&c[2]].width == 400 &&
                            placed[&c[1]].x == 400 && placed[&c[1]].width == 600,
                        "restored columns wrong");
                int row = -1;
                require(sh_tiling_scroll_column(late, &c[0], &row) == 0 && row == 1 &&
                            sh_tiling_scroll_column(late, &c[1], &row) == 1 && row == 0 &&
                            sh_tiling_scroll_column(late, &c[7], &row) == -1,
                        "column readback wrong");
                double back[4];
                require(sh_tiling_scroll_widths(late, "DP-1", 0, back, 4) == 2 &&
                            std::abs(back[0] - 0.4) < 1e-9 && std::abs(back[1] - 0.6) < 1e-9 &&
                            sh_tiling_scroll_widths(late, "DP-1", 1, back, 4) == 0,
                        "width readback wrong");
                // A window not listed keeps a column of its own after the restored ones.
                sh_tiling_insert(late, "DP-1", 0, &c[3], nullptr, false, 0, 0);
                require(sh_tiling_scroll_restore(late, "DP-1", 0, win, c_of, row_of, 2, widths, 1) &&
                            arrange(late, screen, 0)[&c[3]].x > 0,
                        "unlisted window lost");
                // Windows of another workspace and an unarranged workspace are ignored.
                require(!sh_tiling_scroll_restore(late, "DP-1", 5, win, c_of, row_of, 3, widths, 3) &&
                            !sh_tiling_scroll_restore(late, "DP-9", 0, win, c_of, row_of, 3, widths, 3),
                        "restore accepted foreign windows");
                sh_tiling_remove(late, &c[3]);
            }
            // A preview leaves the layout as it was.
            sh_rect preview;
            require(sh_tiling_preview(late, "DP-1", 0, &c[6], nullptr, false, 0, 0, screen, 0,
                                      &preview),
                    "no preview");
            require(arrange(late, screen, 0).size() == 3 && !sh_tiling_output(late, &c[6]),
                    "preview left a window behind");
            // Other layouts are unaffected by scroll actions.
            sh_tiling_set_layout(late, "DP-1", 0, SH_LAYOUT_MASTER);
            require(!sh_tiling_scroll_action(late, &c[0], SH_COLUMN_WIDEN) &&
                        !sh_tiling_scroll_step(late, &c[0], 1, 0) &&
                        !sh_tiling_set_focus(late, &c[0]),
                    "scroll actions worked outside the scroll layout");
            sh_tiling_destroy(late);
            sh_tiling_destroy(sc);
        }

        // A long random sequence of operations keeps every window placed exactly once, with
        // no overlaps (but in monocle), whatever the follow mode.
        {
            const sh_rect screen{0, 0, 1000, 800};
            int c[12];
            bool present[12] = {};
            unsigned seed = 12345;
            auto random = [&](unsigned bound) {
                seed = seed * 1664525U + 1013904223U;
                return (seed >> 16) % bound;
            };
            sh_tiling *sc = sh_tiling_create();
            sh_tiling_set_layout(sc, "DP-1", 0, SH_LAYOUT_SCROLL);
            const sh_action actions[] = {SH_COLUMN_WIDEN,   SH_COLUMN_NARROW, SH_COLUMN_CYCLE_WIDTH,
                                         SH_CONSUME_LEFT,   SH_CONSUME_RIGHT, SH_EXPEL,
                                         SH_CENTER_COLUMN};
            for (int step = 0; step < 4000; ++step) {
                unsigned window = random(12);
                switch (random(9)) {
                case 0:
                case 1:
                    if (!present[window]) {
                        sh_tiling_insert(sc, "DP-1", 0, &c[window], &c[random(12)], false, 0, 0);
                        sh_tiling_set_focus(sc, &c[window]);
                        present[window] = true;
                    }
                    break;
                case 2:
                    sh_tiling_remove(sc, &c[window]);
                    present[window] = false;
                    break;
                case 3:
                case 4:
                    sh_tiling_scroll_action(sc, &c[window], actions[random(7)]);
                    break;
                case 5:
                    sh_tiling_set_focus(sc, &c[window]);
                    break;
                case 6:
                    if (void *other = sh_tiling_scroll_step(sc, &c[window], int(random(3)) - 1,
                                                             int(random(3)) - 1))
                        sh_tiling_set_focus(sc, other);
                    break;
                case 7:
                    sh_tiling_scroll_move(sc, &c[window], random(2) ? 1 : -1);
                    sh_tiling_swap(sc, &c[window], &c[random(12)]);
                    break;
                default:
                    sh_tiling_set_scroll(sc, static_cast<sh_scroll_follow>(random(3)), 0.1 + random(10) / 10.0,
                                         0.1, nullptr, 0);
                    if (random(4) == 0)
                        sh_tiling_set_layout(sc, "DP-1", 0,
                                             static_cast<sh_tile_layout>(random(SH_LAYOUT_COUNT)));
                    else
                        sh_tiling_set_layout(sc, "DP-1", 0, SH_LAYOUT_SCROLL);
                    break;
                }
                int count = 0;
                for (int i = 0; i < 12; ++i)
                    count += present[i];
                auto placed = arrange(sc, screen, int(random(3)) * 6);
                require(int(placed.size()) == count, "a window went missing or was placed twice");
                for (auto &[window_ptr, rect] : placed) {
                    require(rect.width > 0 && rect.height > 0, "empty scroll tile");
                    require(sh_tiling_output(sc, window_ptr), "a placed window is not tiled");
                    for (auto &[other_ptr, other] : placed)
                        require(window_ptr == other_ptr ||
                                    sh_tiling_layout(sc, "DP-1", 0) == SH_LAYOUT_MONOCLE ||
                                    !overlaps(rect, other),
                                "scroll tiles overlap");
                }
            }
            sh_tiling_destroy(sc);
        }

        // Tiny areas still produce positive sizes.
        placed = arrange(tiling, {0, 0, 3, 2}, 100);
        for (auto &[window, rect] : placed)
            require(rect.width > 0 && rect.height > 0, "tiny area produced an empty tile");
        sh_tiling_destroy(tiling);
        std::cout << "Dwindle, master-stack, spiral, monocle, and scroll tiling passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
