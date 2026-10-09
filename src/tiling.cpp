// SPDX-License-Identifier: GPL-3.0-or-later
#include "paw/backend.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

/* Dwindle tiling, after Hyprland's default layout of the same name:
 * leaves are windows, inner nodes split their box in two at `ratio`. The split direction
 * follows the box shape at every arrangement, so each tree adapts to any output size.
 *
 * Every output and workspace can show its tree in another layout instead: master-stack,
 * spiral, or monocle. Those read the tree's leaves in order (left to right) as a plain list
 * and ignore its splits, so switching back to dwindle finds the tree as it was, with any
 * swaps kept.
 *
 * The scroll layout keeps its own columns beside the tree: a list of columns, each a stack of
 * windows with a width, laid on a strip wider than the output. The tree stays the record of
 * which windows the workspace holds; the columns are reconciled with it whenever they are
 * read, so windows that joined while another layout was showing get a column of their own. */
namespace {
using Key = std::pair<std::string, int>;

struct Node {
    Node *parent = nullptr;
    std::unique_ptr<Node> children[2];
    void *window = nullptr; // set only on leaves
    double ratio = 0.5;     // share of the first child
    bool horizontal = true; // children side by side, from the last arrangement
    sh_rect box{};          // from the last arrangement, without gaps
};

bool contains(const sh_rect &box, double x, double y) {
    return x >= box.x && x < box.x + box.width && y >= box.y && y < box.y + box.height;
}
bool splits_horizontally(const sh_rect &box) { return box.width >= box.height; }

/* Layout state of one output and workspace, outliving its windows. */
struct Mode {
    sh_tile_layout layout;
    double ratio;
    int count;
    sh_rect area{}; // the area of the last arrangement, without gaps
};

/* What a workspace chose for itself; whatever it left unset follows the defaults of its output,
 * then the global ones, so a reload reaches every workspace that was not set by hand. */
struct Chosen {
    std::optional<sh_tile_layout> layout;
    std::optional<double> ratio;
    std::optional<int> count;
    sh_rect area{};
};

/* The defaults one output gives its workspaces in place of the global ones. */
struct OutputDefaults {
    std::optional<sh_tile_layout> layout;
    std::optional<double> ratio;
    std::optional<int> count;
};

struct Column {
    std::vector<void *> windows;
    double width; // share of the area's width
};

/* The scrolling layout of one output and workspace. */
struct Scroll {
    std::vector<Column> columns;
    void *focus = nullptr;  // the window the view follows
    void *reveal = nullptr; // a window that must be brought into view once it takes focus
    int pending = 0;        // 1: reveal the focused column, 2: center it
    int offset = 0;         // strip position of the view's left edge, in pixels
    sh_rect area{};         // from the last arrangement, without gaps
    int gap = 0;
};

constexpr double width_min = 0.1, width_max = 1.0;

void collect(Node *node, std::vector<Node *> &out) {
    if (node->window) {
        out.push_back(node);
        return;
    }
    collect(node->children[0].get(), out);
    collect(node->children[1].get(), out);
}

// Splits `total` into `parts` near-equal lengths; the start and length of part `index`.
std::pair<int, int> share(int total, int parts, int index) {
    int start = static_cast<int>(static_cast<long long>(total) * index / parts);
    int end = static_cast<int>(static_cast<long long>(total) * (index + 1) / parts);
    return {start, std::max(1, end - start)};
}

constexpr double ratio_min = 0.1, ratio_max = 0.9;
constexpr int count_max = 8;

Node *leaf_at(Node *node, double x, double y) {
    if (node->window)
        return contains(node->box, x, y) ? node : nullptr;
    for (auto &child : node->children)
        if (Node *found = leaf_at(child.get(), x, y))
            return found;
    return nullptr;
}
} // namespace

struct sh_tiling {
    std::map<Key, std::unique_ptr<Node>> roots;
    std::map<Key, Chosen> modes; // only workspaces that were arranged or changed by hand
    std::map<std::string, OutputDefaults> output_defaults;
    sh_tile_layout default_layout = SH_LAYOUT_DWINDLE;
    double default_ratio = 0.55;
    int default_count = 1;
    std::map<Key, Scroll> scrolls; // only where the scroll layout has been arranged or used
    sh_scroll_follow follow = SH_SCROLL_FOLLOW_CENTER;
    double scroll_width = 0.5, scroll_step = 0.1;
    std::vector<double> presets{1.0 / 3, 0.5, 2.0 / 3, 1.0};

    Mode mode(const Key &key) const {
        Mode result{default_layout, default_ratio, default_count};
        if (auto own = output_defaults.find(key.first); own != output_defaults.end()) {
            result.layout = own->second.layout.value_or(result.layout);
            result.ratio = own->second.ratio.value_or(result.ratio);
            result.count = own->second.count.value_or(result.count);
        }
        if (auto found = modes.find(key); found != modes.end()) {
            result.layout = found->second.layout.value_or(result.layout);
            result.ratio = found->second.ratio.value_or(result.ratio);
            result.count = found->second.count.value_or(result.count);
            result.area = found->second.area;
        }
        return result;
    }
    Chosen &mode_for_change(const Key &key) { return modes[key]; }
    std::unordered_map<const void *, std::pair<Node *, Key>> leaves;

    std::unique_ptr<Node> &slot(Node *node, const Key &key) {
        if (!node->parent)
            return roots[key];
        auto &children = node->parent->children;
        return children[0].get() == node ? children[0] : children[1];
    }

    void place_leaf(Node *node, sh_rect box, const sh_rect &area, int gap, sh_tile_place place,
                    void *userdata) {
        node->box = box;
        // Outer edges keep the full gap; neighbours share one gap between them.
        auto inset = [gap](bool outer) { return outer ? gap : gap / 2; };
        auto inset_end = [gap](bool outer) { return outer ? gap : gap - gap / 2; };
        int left = inset(box.x == area.x);
        int top = inset(box.y == area.y);
        int right = inset_end(box.x + box.width == area.x + area.width);
        int bottom = inset_end(box.y + box.height == area.y + area.height);
        sh_rect rect{box.x + left, box.y + top, std::max(1, box.width - left - right),
                     std::max(1, box.height - top - bottom)};
        place(userdata, node->window, rect);
    }

    // Sets a column's width, kept within limits; the view then follows the focused column.
    bool set_column_width(Scroll &state, int column, double width) {
        width = std::clamp(width, width_min, width_max);
        Column &target = state.columns[column];
        bool changed = std::abs(width - target.width) > 1e-9;
        target.width = width;
        state.pending = std::max(state.pending, reveal_pending());
        return changed;
    }

    int reveal_pending() const { return follow == SH_SCROLL_FOLLOW_CENTER ? 2 : 1; }

    /* The columns of a workspace, reconciled with its tree; null while it has no windows. */
    Scroll *scroll_state(const Key &key) {
        auto root = roots.find(key);
        if (root == roots.end()) {
            scrolls.erase(key);
            return nullptr;
        }
        Scroll &state = scrolls[key];
        std::vector<Node *> order;
        collect(root->second.get(), order);
        std::unordered_set<const void *> present, known;
        for (Node *node : order)
            present.insert(node->window);
        for (auto &column : state.columns) {
            std::erase_if(column.windows, [&](void *w) { return !present.contains(w); });
            known.insert(column.windows.begin(), column.windows.end());
        }
        std::erase_if(state.columns, [](const Column &c) { return c.windows.empty(); });
        for (Node *node : order)
            if (!known.contains(node->window))
                state.columns.push_back({{node->window}, scroll_width});
        if (!present.contains(state.focus))
            state.focus = nullptr;
        if (!present.contains(state.reveal))
            state.reveal = nullptr;
        return &state;
    }

    // The scroll state of `window` when its workspace tiles with the scroll layout.
    Scroll *scroll_of(const void *window, int *column = nullptr, int *row = nullptr) {
        auto found = leaves.find(window);
        if (found == leaves.end() || mode(found->second.second).layout != SH_LAYOUT_SCROLL)
            return nullptr;
        Scroll *state = scroll_state(found->second.second);
        if (!state)
            return nullptr;
        for (size_t c = 0; c < state->columns.size(); ++c) {
            auto &windows = state->columns[c].windows;
            auto at = std::find(windows.begin(), windows.end(), window);
            if (at != windows.end()) {
                if (column)
                    *column = static_cast<int>(c);
                if (row)
                    *row = static_cast<int>(at - windows.begin());
                return state;
            }
        }
        return nullptr;
    }

    static int column_pixels(const Scroll &state, const Column &column) {
        long avail = std::max(1, state.area.width - state.gap);
        return static_cast<int>(std::clamp<long>(std::lround(column.width * avail), 1, 1L << 24));
    }

    /* Columns side by side from the strip's origin, the view showing [offset, offset + view).
     * Each column's box is `width` of the area less one gap, so two halves fill it with the
     * same gaps dwindle leaves. */
    void arrange_scroll(const Key &key, const sh_rect &area, int gap, sh_tile_place place,
                        void *userdata) {
        Scroll *state = scroll_state(key);
        if (!state)
            return;
        state->area = area;
        state->gap = gap;
        const int view = std::max(1, area.width - gap);
        std::vector<int> start, width;
        int total = 0, focused = -1;
        for (size_t i = 0; i < state->columns.size(); ++i) {
            start.push_back(total);
            width.push_back(column_pixels(*state, state->columns[i]));
            total += width.back();
            auto &windows = state->columns[i].windows;
            if (std::find(windows.begin(), windows.end(), state->focus) != windows.end())
                focused = static_cast<int>(i);
        }
        const int reach = std::max(0, total - view);
        if (state->pending && focused >= 0) {
            int left = start[focused], right = left + width[focused];
            if (state->pending == 2) {
                state->offset = left + width[focused] / 2 - view / 2;
                if (follow != SH_SCROLL_FOLLOW_CENTER)
                    state->offset = std::clamp(state->offset, 0, reach);
            } else if (left < state->offset) {
                state->offset = left;
            } else if (right > state->offset + view) {
                state->offset = right - view;
            }
        }
        state->pending = 0;
        if (follow != SH_SCROLL_FOLLOW_CENTER)
            state->offset = std::clamp(state->offset, 0, reach);
        for (size_t i = 0; i < state->columns.size(); ++i) {
            auto &windows = state->columns[i].windows;
            int count = static_cast<int>(windows.size());
            for (int row = 0; row < count; ++row) {
                auto [y, height] = share(area.height, count, row);
                int top = row == 0 ? gap : gap / 2;
                int bottom = row == count - 1 ? gap : gap - gap / 2;
                place(userdata, windows[row],
                      {area.x + gap + start[i] - state->offset, area.y + y + top,
                       std::max(1, width[i] - gap), std::max(1, height - top - bottom)});
            }
        }
    }

    // The boxes of the layouts other than dwindle, one per window in order.
    static std::vector<sh_rect> boxes(const Mode &mode, size_t count) {
        const sh_rect &area = mode.area;
        std::vector<sh_rect> result(count, area);
        if (mode.layout == SH_LAYOUT_MASTER) {
            size_t masters = std::min<size_t>(std::max(1, mode.count), count);
            int master_width = area.width;
            if (masters < count)
                master_width = std::clamp(static_cast<int>(area.width * mode.ratio), 1,
                                          std::max(1, area.width - 1));
            size_t stack = count - masters;
            for (size_t i = 0; i < count; ++i) {
                bool master = i < masters;
                size_t index = master ? i : i - masters, parts = master ? masters : stack;
                auto [y, height] = share(area.height, static_cast<int>(parts),
                                         static_cast<int>(index));
                result[i] = master ? sh_rect{area.x, area.y + y, master_width, height}
                                   : sh_rect{area.x + master_width, area.y + y,
                                             std::max(1, area.width - master_width), height};
            }
        } else if (mode.layout == SH_LAYOUT_SPIRAL) {
            // Each window takes `ratio` of what is left, turning clockwise from the left edge.
            sh_rect rest = area;
            for (size_t i = 0; i + 1 < count; ++i) {
                sh_rect &box = result[i];
                box = rest;
                int width = std::clamp(static_cast<int>(rest.width * mode.ratio), 1,
                                       std::max(1, rest.width - 1));
                int height = std::clamp(static_cast<int>(rest.height * mode.ratio), 1,
                                        std::max(1, rest.height - 1));
                switch (i % 4) {
                case 0: // left
                    box.width = width;
                    rest.x += width;
                    rest.width = std::max(1, rest.width - width);
                    break;
                case 1: // top
                    box.height = height;
                    rest.y += height;
                    rest.height = std::max(1, rest.height - height);
                    break;
                case 2: // right
                    box.x += rest.width - width;
                    box.width = width;
                    rest.width = std::max(1, rest.width - width);
                    break;
                default: // bottom
                    box.y += rest.height - height;
                    box.height = height;
                    rest.height = std::max(1, rest.height - height);
                    break;
                }
            }
            if (count)
                result[count - 1] = rest;
        }
        return result;
    }

    void layout(Node *node, sh_rect box, const sh_rect &area, int gap, sh_tile_place place,
                void *userdata) {
        node->box = box;
        if (node->window) {
            place_leaf(node, box, area, gap, place, userdata);
            return;
        }
        node->horizontal = splits_horizontally(box);
        sh_rect first = box, second = box;
        if (node->horizontal) {
            first.width = std::clamp(static_cast<int>(box.width * node->ratio), 1,
                                     std::max(1, box.width - 1));
            second.x += first.width;
            second.width = std::max(1, box.width - first.width);
        } else {
            first.height = std::clamp(static_cast<int>(box.height * node->ratio), 1,
                                      std::max(1, box.height - 1));
            second.y += first.height;
            second.height = std::max(1, box.height - first.height);
        }
        layout(node->children[0].get(), first, area, gap, place, userdata);
        layout(node->children[1].get(), second, area, gap, place, userdata);
    }
};

extern "C" {
sh_tiling *sh_tiling_create(void) { return new (std::nothrow) sh_tiling; }
void sh_tiling_destroy(sh_tiling *tiling) { delete tiling; }

void sh_tiling_insert(sh_tiling *tiling, const char *output, int workspace, void *window,
                      const void *target, bool has_point, double x, double y) {
    if (!tiling || !output || !window || tiling->leaves.contains(window))
        return;
    Key key{output, workspace};
    const void *anchor = target;
    if (tiling->mode(key).layout != SH_LAYOUT_DWINDLE) { // the other layouts append
        target = nullptr;
        has_point = false;
    }
    // In the scroll layout the window gets a column right of the focused one.
    auto open_column = [&] {
        if (tiling->mode(key).layout != SH_LAYOUT_SCROLL)
            return;
        Scroll *state = tiling->scroll_state(key);
        if (!state || state->columns.empty())
            return;
        auto column_of = [&](const void *w) {
            for (size_t c = 0; c < state->columns.size(); ++c)
                if (std::find(state->columns[c].windows.begin(), state->columns[c].windows.end(),
                              w) != state->columns[c].windows.end())
                    return static_cast<long>(c);
            return -1L;
        };
        long from = column_of(window), after = anchor ? column_of(anchor) : -1;
        if (after < 0)
            after = column_of(state->focus);
        if (from > 0 && after >= 0 && after + 1 < from)
            std::rotate(state->columns.begin() + after + 1, state->columns.begin() + from,
                        state->columns.begin() + from + 1);
        state->reveal = window;
    };
    auto leaf = std::make_unique<Node>();
    leaf->window = window;
    Node *added = leaf.get();
    auto &root = tiling->roots[key];
    if (!root) {
        root = std::move(leaf);
        tiling->leaves[window] = {added, key};
        open_column();
        return;
    }
    Node *split = nullptr;
    if (auto found = tiling->leaves.find(target);
        target && found != tiling->leaves.end() && found->second.second == key)
        split = found->second.first;
    if (!split && has_point)
        split = leaf_at(root.get(), x, y);
    if (!split) // New windows take the second half, so the newest leaf ends the chain.
        for (split = root.get(); !split->window; split = split->children[1].get())
            ;
    const sh_rect box = split->box;
    bool first = false;
    if (has_point && contains(box, x, y))
        first =
            splits_horizontally(box) ? x < box.x + box.width / 2.0 : y < box.y + box.height / 2.0;
    auto &slot = tiling->slot(split, key);
    auto parent = std::make_unique<Node>();
    parent->parent = split->parent;
    parent->box = box;
    parent->horizontal = splits_horizontally(box);
    leaf->box = box;
    split->parent = leaf->parent = parent.get();
    parent->children[first ? 1 : 0] = std::move(slot);
    parent->children[first ? 0 : 1] = std::move(leaf);
    slot = std::move(parent);
    tiling->leaves[window] = {added, key};
    open_column();
}

void sh_tiling_remove(sh_tiling *tiling, const void *window) {
    if (!tiling)
        return;
    auto found = tiling->leaves.find(window);
    if (found == tiling->leaves.end())
        return;
    auto [node, key] = found->second;
    tiling->leaves.erase(found);
    Node *parent = node->parent;
    if (auto scroll = tiling->scrolls.find(key); scroll != tiling->scrolls.end()) {
        Scroll &state = scroll->second;
        for (auto &column : state.columns)
            std::erase(column.windows, const_cast<void *>(window));
        std::erase_if(state.columns, [](const Column &c) { return c.windows.empty(); });
        if (state.focus == window)
            state.focus = nullptr;
        if (state.reveal == window)
            state.reveal = nullptr;
    }
    if (!parent) {
        tiling->roots.erase(key);
        tiling->scrolls.erase(key);
        return;
    }
    // The sibling takes over the parent's place and box.
    auto sibling = std::move(parent->children[parent->children[0].get() == node ? 1 : 0]);
    sibling->parent = parent->parent;
    tiling->slot(parent, key) = std::move(sibling);
}

const char *sh_tiling_output(const sh_tiling *tiling, const void *window) {
    if (!tiling)
        return nullptr;
    auto found = tiling->leaves.find(window);
    return found == tiling->leaves.end() ? nullptr : found->second.second.first.c_str();
}

int sh_tiling_count(const sh_tiling *tiling, const char *output, int workspace) {
    if (!tiling || !output)
        return 0;
    auto found = tiling->roots.find({output, workspace});
    if (found == tiling->roots.end())
        return 0;
    std::vector<Node *> order;
    collect(found->second.get(), order);
    return static_cast<int>(order.size());
}

void sh_tiling_arrange(sh_tiling *tiling, const char *output, int workspace, sh_rect area, int gap,
                       sh_tile_place place, void *userdata) {
    if (!tiling || !output || !place || area.width < 1 || area.height < 1)
        return;
    auto found = tiling->roots.find({output, workspace});
    if (found == tiling->roots.end())
        return;
    gap = std::clamp(gap, 0, std::min(area.width, area.height) / 4);
    Key key{output, workspace};
    Mode mode = tiling->mode(key);
    if (mode.layout == SH_LAYOUT_SCROLL) {
        tiling->arrange_scroll(key, area, gap, place, userdata);
        return;
    }
    if (mode.layout == SH_LAYOUT_DWINDLE) {
        tiling->layout(found->second.get(), area, area, gap, place, userdata);
        if (auto changed = tiling->modes.find(key); changed != tiling->modes.end())
            changed->second.area = area;
        return;
    }
    mode.area = area;
    tiling->mode_for_change(key).area = area;
    std::vector<Node *> order;
    collect(found->second.get(), order);
    auto boxes = sh_tiling::boxes(mode, order.size());
    for (size_t i = 0; i < order.size(); ++i)
        tiling->place_leaf(order[i], boxes[i], area, gap, place, userdata);
}

bool sh_tiling_preview(sh_tiling *tiling, const char *output, int workspace, void *window,
                       const void *target, bool has_point, double x, double y, sh_rect area,
                       int gap, sh_rect *result) {
    if (!tiling || !output || !window || !result || area.width < 1 || area.height < 1 ||
        tiling->leaves.contains(window))
        return false;
    struct Found {
        void *window;
        sh_rect *rect;
        bool found;
    } found{window, result, false};
    // Arranging the scroll layout moves its view; the preview leaves it where it was.
    std::optional<Scroll> saved;
    if (auto scroll = tiling->scrolls.find({output, workspace}); scroll != tiling->scrolls.end())
        saved = scroll->second;
    sh_tiling_insert(tiling, output, workspace, window, target, has_point, x, y);
    sh_tiling_arrange(
        tiling, output, workspace, area, gap,
        [](void *data, void *placed, sh_rect rect) {
            auto *found = static_cast<Found *>(data);
            if (placed == found->window) {
                *found->rect = rect;
                found->found = true;
            }
        },
        &found);
    sh_tiling_remove(tiling, window);
    // Arranging again restores the boxes that later insertions and resizes read.
    sh_tiling_arrange(
        tiling, output, workspace, area, gap, [](void *, void *, sh_rect) {}, nullptr);
    if (saved)
        tiling->scrolls[{output, workspace}] = std::move(*saved);
    return found.found;
}

bool sh_tiling_resize(sh_tiling *tiling, const void *window, uint32_t edges, sh_rect rect) {
    if (!tiling)
        return false;
    auto found = tiling->leaves.find(window);
    if (found == tiling->leaves.end())
        return false;
    if (Mode mode = tiling->mode(found->second.second); mode.layout != SH_LAYOUT_DWINDLE) {
        if (mode.layout == SH_LAYOUT_SCROLL) {
            // Dragging a side of a column sets its width.
            int column = 0;
            Scroll *state = tiling->scroll_of(window, &column);
            if (!state || !(edges & (SH_EDGE_LEFT | SH_EDGE_RIGHT)) ||
                state->area.width <= state->gap)
                return false;
            return tiling->set_column_width(
                *state, column, (rect.width + state->gap) / double(state->area.width - state->gap));
        }
        // Only the master-stack boundary moves: dragging the master's right edge or the
        // stack's left edge.
        if (mode.layout != SH_LAYOUT_MASTER || mode.area.width < 1)
            return false;
        std::vector<Node *> order;
        collect(tiling->roots[found->second.second].get(), order);
        size_t index = std::find(order.begin(), order.end(), found->second.first) - order.begin();
        bool master = static_cast<int>(index) < std::max(1, mode.count);
        if (order.size() <= static_cast<size_t>(std::max(1, mode.count)))
            return false;
        double position;
        if (master && (edges & SH_EDGE_RIGHT))
            position = rect.x + rect.width;
        else if (!master && (edges & SH_EDGE_LEFT))
            position = rect.x;
        else
            return false;
        double ratio = std::clamp((position - mode.area.x) / mode.area.width, ratio_min, ratio_max);
        bool changed = ratio != mode.ratio;
        tiling->mode_for_change(found->second.second).ratio = ratio;
        return changed;
    }
    bool changed = false, horizontal_done = false, vertical_done = false;
    auto set = [&changed](Node *node, double position, int start, int length) {
        double ratio = std::clamp((position - start) / length, 0.1, 0.9);
        changed |= ratio != node->ratio;
        node->ratio = ratio;
    };
    for (Node *child = found->second.first, *node = child->parent; node;
         child = node, node = node->parent) {
        bool is_first = node->children[0].get() == child;
        const sh_rect &box = node->box;
        if (node->horizontal && !horizontal_done && box.width > 0) {
            if ((edges & SH_EDGE_RIGHT) && is_first) {
                set(node, rect.x + rect.width, box.x, box.width);
                horizontal_done = true;
            } else if ((edges & SH_EDGE_LEFT) && !is_first) {
                set(node, rect.x, box.x, box.width);
                horizontal_done = true;
            }
        } else if (!node->horizontal && !vertical_done && box.height > 0) {
            if ((edges & SH_EDGE_BOTTOM) && is_first) {
                set(node, rect.y + rect.height, box.y, box.height);
                vertical_done = true;
            } else if ((edges & SH_EDGE_TOP) && !is_first) {
                set(node, rect.y, box.y, box.height);
                vertical_done = true;
            }
        }
    }
    return changed;
}

bool sh_tiling_resize_by(sh_tiling *tiling, const void *window, uint32_t direction, int amount) {
    if (!tiling || amount <= 0)
        return false;
    auto found = tiling->leaves.find(window);
    if (found == tiling->leaves.end())
        return false;
    bool horizontal = direction == SH_EDGE_LEFT || direction == SH_EDGE_RIGHT;
    if (!horizontal && direction != SH_EDGE_TOP && direction != SH_EDGE_BOTTOM)
        return false;
    if (Mode mode = tiling->mode(found->second.second); mode.layout != SH_LAYOUT_DWINDLE) {
        if (mode.layout == SH_LAYOUT_SCROLL) {
            int column = 0;
            Scroll *state = tiling->scroll_of(window, &column);
            if (!state || !horizontal || state->area.width <= state->gap)
                return false;
            return tiling->set_column_width(
                *state, column,
                state->columns[column].width + (direction == SH_EDGE_RIGHT ? 1.0 : -1.0) *
                                                   amount / double(state->area.width - state->gap));
        }
        // Right always widens the master column, left narrows it, whichever tile is focused.
        if (mode.layout != SH_LAYOUT_MASTER || !horizontal || mode.area.width < 1)
            return false;
        return sh_tiling_adjust(tiling, found->second.second.first.c_str(),
                                found->second.second.second,
                                (direction == SH_EDGE_RIGHT ? 1.0 : -1.0) * amount /
                                    mode.area.width,
                                0);
    }
    // The window's edge on the `direction` side is a split when some ancestor across that
    // axis holds the window on the other side of it.
    bool toward_end = direction == SH_EDGE_RIGHT || direction == SH_EDGE_BOTTOM;
    bool split_there = false;
    for (Node *child = found->second.first, *node = child->parent; node && !split_there;
         child = node, node = node->parent)
        split_there = node->horizontal == horizontal &&
                      (node->children[0].get() == child) == toward_end;
    uint32_t opposite = horizontal ? SH_EDGE_LEFT | SH_EDGE_RIGHT : SH_EDGE_TOP | SH_EDGE_BOTTOM;
    uint32_t edge = split_there ? direction : opposite & ~direction;
    // Both edges move the same way: the one facing `direction` grows the window, the other
    // shrinks it.
    int shift = toward_end ? amount : -amount;
    sh_rect rect = found->second.first->box;
    if (edge == SH_EDGE_LEFT) {
        rect.x += shift;
        rect.width -= shift;
    } else if (edge == SH_EDGE_RIGHT) {
        rect.width += shift;
    } else if (edge == SH_EDGE_TOP) {
        rect.y += shift;
        rect.height -= shift;
    } else {
        rect.height += shift;
    }
    return sh_tiling_resize(tiling, window, edge, rect);
}

void sh_tiling_set_defaults(sh_tiling *tiling, sh_tile_layout layout, double ratio, int count) {
    if (!tiling)
        return;
    tiling->default_layout = layout >= 0 && layout < SH_LAYOUT_COUNT ? layout : SH_LAYOUT_DWINDLE;
    tiling->default_ratio = std::clamp(ratio, ratio_min, ratio_max);
    tiling->default_count = std::clamp(count, 1, count_max);
}

bool sh_tiling_exchange(sh_tiling *tiling, const char *output_a, int workspace_a,
                        const char *output_b, int workspace_b) {
    if (!tiling || !output_a || !output_b)
        return false;
    Key a{output_a, workspace_a}, b{output_b, workspace_b};
    if (a == b)
        return false;
    auto swap_entry = [&](auto &map) {
        auto first = map.find(a), second = map.find(b);
        if (first != map.end() && second != map.end()) {
            std::swap(first->second, second->second);
        } else if (first != map.end()) {
            map.emplace(b, std::move(first->second));
            map.erase(a);
        } else if (second != map.end()) {
            map.emplace(a, std::move(second->second));
            map.erase(b);
        }
    };
    swap_entry(tiling->roots);
    swap_entry(tiling->modes);
    swap_entry(tiling->scrolls);
    // What the last arrangement measured belongs to the output it was made for.
    for (const Key &key : {a, b}) {
        if (auto found = tiling->modes.find(key); found != tiling->modes.end())
            found->second.area = {};
        if (auto found = tiling->scrolls.find(key); found != tiling->scrolls.end()) {
            found->second.area = {};
            found->second.pending = tiling->reveal_pending();
        }
        if (auto found = tiling->roots.find(key); found != tiling->roots.end()) {
            std::vector<Node *> order;
            collect(found->second.get(), order);
            for (Node *node : order)
                tiling->leaves[node->window].second = key;
        }
    }
    return true;
}

void sh_tiling_set_output_defaults(sh_tiling *tiling, const char *output, int layout,
                                   double ratio, int count) {
    if (!tiling || !output)
        return;
    OutputDefaults defaults;
    if (layout >= 0 && layout < SH_LAYOUT_COUNT)
        defaults.layout = static_cast<sh_tile_layout>(layout);
    if (ratio > 0)
        defaults.ratio = std::clamp(ratio, ratio_min, ratio_max);
    if (count > 0)
        defaults.count = std::clamp(count, 1, count_max);
    if (defaults.layout || defaults.ratio || defaults.count)
        tiling->output_defaults[output] = defaults;
    else
        tiling->output_defaults.erase(output);
}

void sh_tiling_clear_output_defaults(sh_tiling *tiling) {
    if (tiling)
        tiling->output_defaults.clear();
}

void sh_tiling_output_defaults(const sh_tiling *tiling, const char *output,
                               enum sh_tile_layout *layout, double *ratio, int *count) {
    if (!tiling || !output)
        return;
    Mode mode{tiling->default_layout, tiling->default_ratio, tiling->default_count};
    if (auto own = tiling->output_defaults.find(output); own != tiling->output_defaults.end()) {
        mode.layout = own->second.layout.value_or(mode.layout);
        mode.ratio = own->second.ratio.value_or(mode.ratio);
        mode.count = own->second.count.value_or(mode.count);
    }
    if (layout)
        *layout = mode.layout;
    if (ratio)
        *ratio = mode.ratio;
    if (count)
        *count = mode.count;
}

sh_tile_layout sh_tiling_layout(const sh_tiling *tiling, const char *output, int workspace) {
    return tiling && output ? tiling->mode({output, workspace}).layout : SH_LAYOUT_DWINDLE;
}

void sh_tiling_set_layout(sh_tiling *tiling, const char *output, int workspace,
                          sh_tile_layout layout) {
    if (!tiling || !output || layout < 0 || layout >= SH_LAYOUT_COUNT)
        return;
    tiling->mode_for_change({output, workspace}).layout = layout;
}

sh_tile_layout sh_tiling_cycle_layout(sh_tiling *tiling, const char *output, int workspace,
                                      int step) {
    if (!tiling || !output)
        return SH_LAYOUT_DWINDLE;
    Mode mode = tiling->mode({output, workspace});
    auto next = static_cast<sh_tile_layout>(
        ((mode.layout + step) % SH_LAYOUT_COUNT + SH_LAYOUT_COUNT) % SH_LAYOUT_COUNT);
    tiling->mode_for_change({output, workspace}).layout = next;
    return next;
}

bool sh_tiling_adjust(sh_tiling *tiling, const char *output, int workspace, double ratio_delta,
                      int count_delta) {
    if (!tiling || !output)
        return false;
    Mode mode = tiling->mode({output, workspace});
    double ratio = std::clamp(mode.ratio + ratio_delta, ratio_min, ratio_max);
    int count = std::clamp(mode.count + count_delta, 1, count_max);
    bool changed = ratio != mode.ratio || count != mode.count;
    Chosen &chosen = tiling->mode_for_change({output, workspace});
    if (ratio_delta != 0)
        chosen.ratio = ratio;
    if (count_delta != 0)
        chosen.count = count;
    return changed;
}

double sh_tiling_ratio(const sh_tiling *tiling, const char *output, int workspace) {
    return tiling && output ? tiling->mode({output, workspace}).ratio : 0.5;
}

int sh_tiling_master_count(const sh_tiling *tiling, const char *output, int workspace) {
    return tiling && output ? tiling->mode({output, workspace}).count : 1;
}

bool sh_tiling_swap(sh_tiling *tiling, const void *a, const void *b) {
    if (!tiling || a == b)
        return false;
    auto first = tiling->leaves.find(a), second = tiling->leaves.find(b);
    if (first == tiling->leaves.end() || second == tiling->leaves.end() ||
        first->second.second != second->second.second)
        return false;
    std::swap(first->second.first->window, second->second.first->window);
    std::swap(first->second.first, second->second.first);
    if (auto scroll = tiling->scrolls.find(first->second.second); scroll != tiling->scrolls.end())
        for (auto &column : scroll->second.columns)
            for (void *&entry : column.windows)
                entry = entry == a ? const_cast<void *>(b)
                                   : entry == b ? const_cast<void *>(a) : entry;
    return true;
}

bool sh_tiling_replace(sh_tiling *tiling, const void *old_window, void *replacement) {
    if (!tiling || !replacement || old_window == replacement)
        return false;
    auto found = tiling->leaves.find(old_window);
    if (found == tiling->leaves.end() || tiling->leaves.count(replacement))
        return false;
    auto [node, key] = found->second;
    node->window = replacement;
    tiling->leaves.erase(found);
    tiling->leaves[replacement] = {node, key};
    if (auto scroll = tiling->scrolls.find(key); scroll != tiling->scrolls.end()) {
        for (auto &column : scroll->second.columns)
            for (void *&entry : column.windows)
                if (entry == old_window)
                    entry = replacement;
        if (scroll->second.focus == old_window)
            scroll->second.focus = replacement;
        if (scroll->second.reveal == old_window)
            scroll->second.reveal = replacement;
    }
    return true;
}

void *sh_tiling_neighbour(const sh_tiling *tiling, const void *window, int step) {
    if (!tiling)
        return nullptr;
    auto found = tiling->leaves.find(window);
    if (found == tiling->leaves.end())
        return nullptr;
    // Reading the columns reconciles them with the tree, which changes only that cache.
    if (Scroll *state = const_cast<sh_tiling *>(tiling)->scroll_of(window)) {
        std::vector<void *> flat;
        for (auto &column : state->columns)
            flat.insert(flat.end(), column.windows.begin(), column.windows.end());
        if (flat.size() < 2)
            return nullptr;
        long index = std::find(flat.begin(), flat.end(), window) - flat.begin();
        long size = static_cast<long>(flat.size());
        return flat[((index + step) % size + size) % size];
    }
    std::vector<Node *> order;
    collect(tiling->roots.at(found->second.second).get(), order);
    if (order.size() < 2)
        return nullptr;
    long index = std::find(order.begin(), order.end(), found->second.first) - order.begin();
    long size = static_cast<long>(order.size());
    return order[((index + step) % size + size) % size]->window;
}

void *sh_tiling_master(const sh_tiling *tiling, const char *output, int workspace) {
    if (!tiling || !output)
        return nullptr;
    auto found = tiling->roots.find({output, workspace});
    if (found == tiling->roots.end())
        return nullptr;
    if (tiling->mode({output, workspace}).layout == SH_LAYOUT_SCROLL)
        if (Scroll *state = const_cast<sh_tiling *>(tiling)->scroll_state({output, workspace});
            state && !state->columns.empty())
            return state->columns.front().windows.front();
    std::vector<Node *> order;
    collect(found->second.get(), order);
    return order.front()->window;
}

void sh_tiling_set_scroll(sh_tiling *tiling, sh_scroll_follow follow, double width, double step,
                          const float *presets, int preset_count) {
    if (!tiling)
        return;
    tiling->follow = follow;
    tiling->scroll_width = std::clamp(width, width_min, width_max);
    tiling->scroll_step = std::clamp(step, 0.01, 0.5);
    tiling->presets.clear();
    for (int i = 0; presets && i < std::min(preset_count, 8); ++i)
        tiling->presets.push_back(std::clamp<double>(presets[i], width_min, width_max));
    if (tiling->presets.empty())
        tiling->presets = {tiling->scroll_width};
    std::sort(tiling->presets.begin(), tiling->presets.end());
}

int sh_tiling_scroll_column(sh_tiling *tiling, const void *window, int *row) {
    int column = 0, at = 0;
    if (!tiling || !tiling->scroll_of(window, &column, &at))
        return -1;
    if (row)
        *row = at;
    return column;
}

int sh_tiling_scroll_widths(sh_tiling *tiling, const char *output, int workspace, double *widths,
                            int max) {
    if (!tiling || !output || !widths ||
        tiling->mode({output, workspace}).layout != SH_LAYOUT_SCROLL)
        return 0;
    Scroll *state = tiling->scroll_state({output, workspace});
    int count = 0;
    for (size_t i = 0; state && i < state->columns.size() && count < max; ++i)
        widths[count++] = state->columns[i].width;
    return count;
}

bool sh_tiling_scroll_restore(sh_tiling *tiling, const char *output, int workspace,
                              void *const *windows, const int *columns, const int *rows,
                              int count, const double *widths, int width_count) {
    if (!tiling || !output || count < 1 || !windows || !columns || !rows)
        return false;
    Key key{output, workspace};
    Scroll *state = tiling->scroll_state(key);
    if (!state)
        return false;
    // Only windows that are in this workspace's tree can be placed.
    std::vector<int> picked;
    for (int i = 0; i < count; ++i) {
        auto found = tiling->leaves.find(windows[i]);
        if (found != tiling->leaves.end() && found->second.second == key && columns[i] >= 0 &&
            rows[i] >= 0)
            picked.push_back(i);
    }
    if (picked.empty())
        return false;
    std::stable_sort(picked.begin(), picked.end(), [&](int a, int b) {
        return columns[a] != columns[b] ? columns[a] < columns[b] : rows[a] < rows[b];
    });
    std::unordered_set<const void *> used;
    std::vector<Column> rebuilt;
    int previous = -1;
    for (int i : picked) {
        if (columns[i] != previous) {
            double width = columns[i] < width_count ? widths[columns[i]] : tiling->scroll_width;
            rebuilt.push_back({{}, std::clamp(width, width_min, width_max)});
            previous = columns[i];
        }
        rebuilt.back().windows.push_back(windows[i]);
        used.insert(windows[i]);
    }
    // Windows the session did not place keep their columns after the restored ones.
    for (auto &column : state->columns) {
        std::erase_if(column.windows, [&](void *w) { return used.contains(w); });
        if (!column.windows.empty())
            rebuilt.push_back(std::move(column));
    }
    state->columns = std::move(rebuilt);
    state->pending = std::max(state->pending, tiling->reveal_pending());
    return true;
}

bool sh_tiling_set_focus(sh_tiling *tiling, const void *window) {
    Scroll *state = tiling ? tiling->scroll_of(window) : nullptr;
    if (!state)
        return false;
    if (state->focus != window) {
        state->focus = const_cast<void *>(window);
        state->pending = tiling->follow == SH_SCROLL_FOLLOW_NEVER ? 0 : tiling->reveal_pending();
    }
    if (state->reveal == window) { // it arrived or was picked by a scroll action
        state->reveal = nullptr;
        state->pending = tiling->reveal_pending();
    }
    return state->pending != 0;
}

void *sh_tiling_scroll_step(sh_tiling *tiling, const void *window, int columns, int rows) {
    int column = 0, row = 0;
    Scroll *state = tiling ? tiling->scroll_of(window, &column, &row) : nullptr;
    if (!state)
        return nullptr;
    long target_column = column + columns, target_row = row + rows;
    if (target_column < 0 || target_column >= static_cast<long>(state->columns.size()))
        return nullptr;
    auto &windows = state->columns[target_column].windows;
    if (columns)
        target_row = std::min<long>(row, static_cast<long>(windows.size()) - 1);
    if (target_row < 0 || target_row >= static_cast<long>(windows.size()))
        return nullptr;
    state->reveal = windows[target_row];
    return state->reveal;
}

bool sh_tiling_scroll_move(sh_tiling *tiling, const void *window, int step) {
    int column = 0;
    Scroll *state = tiling ? tiling->scroll_of(window, &column) : nullptr;
    long other = column + step;
    if (!state || (step != 1 && step != -1) || other < 0 ||
        other >= static_cast<long>(state->columns.size()))
        return false;
    std::swap(state->columns[column], state->columns[other]);
    state->reveal = const_cast<void *>(window);
    state->pending = tiling->reveal_pending();
    return true;
}

bool sh_tiling_scroll_action(sh_tiling *tiling, const void *window, sh_action action) {
    int column = 0, row = 0;
    Scroll *state = tiling ? tiling->scroll_of(window, &column, &row) : nullptr;
    if (!state)
        return false;
    auto &columns = state->columns;
    state->focus = const_cast<void *>(window);
    switch (action) {
    case SH_COLUMN_WIDEN:
    case SH_COLUMN_NARROW:
        return tiling->set_column_width(*state, column,
                                        columns[column].width + (action == SH_COLUMN_WIDEN ? 1 : -1) *
                                                                    tiling->scroll_step);
    case SH_COLUMN_CYCLE_WIDTH: {
        // The next preset wider than the column, else the narrowest.
        double current = columns[column].width, next = tiling->presets.front();
        for (double preset : tiling->presets)
            if (preset > current + 1e-3) {
                next = preset;
                break;
            }
        tiling->set_column_width(*state, column, next);
        return true; // even at the same width, the view moves to it
    }
    case SH_CONSUME_LEFT:
    case SH_CONSUME_RIGHT: {
        long other = column + (action == SH_CONSUME_LEFT ? -1 : 1);
        if (other < 0 || other >= static_cast<long>(columns.size()))
            return false;
        auto &from = columns[column].windows, &to = columns[other].windows;
        // Joins the bottom of a column on the left, the top of one on the right.
        to.insert(action == SH_CONSUME_LEFT ? to.end() : to.begin(), from[row]);
        from.erase(from.begin() + row);
        if (from.empty())
            columns.erase(columns.begin() + column);
        state->pending = tiling->reveal_pending();
        return true;
    }
    case SH_EXPEL: {
        auto &windows = columns[column].windows;
        if (windows.size() < 2)
            return false;
        Column expelled{{windows[row]}, columns[column].width};
        windows.erase(windows.begin() + row);
        columns.insert(columns.begin() + column + 1, std::move(expelled));
        state->pending = tiling->reveal_pending();
        return true;
    }
    case SH_CENTER_COLUMN:
        state->pending = 2;
        return true;
    default:
        return false;
    }
}
}
