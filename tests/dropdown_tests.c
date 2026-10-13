// SPDX-License-Identifier: GPL-3.0-or-later
/* The scratchpad's drop-down: the edge it comes from, how far it slides, and where it is cut. */
#include "paw/dropdown.h"
#include <stdio.h>
#include "check.h"

static const struct sh_drop_box OUTPUT = {0, 0, 1920, 1080};

/* The usable area with strips of these widths along each edge. */
static struct sh_drop_box usable(int top, int bottom, int left, int right) {
    return (struct sh_drop_box){left, top, 1920 - left - right, 1080 - top - bottom};
}

static void test_edge(void) {
    CHECK(sh_drop_edge(OUTPUT, usable(0, 0, 0, 0)) == SH_DROP_TOP, "no bar: the top");
    CHECK(sh_drop_edge(OUTPUT, usable(40, 0, 0, 0)) == SH_DROP_TOP, "a top bar");
    CHECK(sh_drop_edge(OUTPUT, usable(0, 48, 0, 0)) == SH_DROP_BOTTOM, "a bottom bar");
    CHECK(sh_drop_edge(OUTPUT, usable(0, 0, 60, 0)) == SH_DROP_LEFT, "a bar along the left");
    CHECK(sh_drop_edge(OUTPUT, usable(0, 0, 0, 60)) == SH_DROP_RIGHT, "a bar along the right");
    // macOS: the menu bar along the top wins over the wider dock along the bottom.
    CHECK(sh_drop_edge(OUTPUT, usable(28, 70, 0, 0)) == SH_DROP_TOP, "menu bar and dock");
    CHECK(sh_drop_edge(OUTPUT, usable(0, 30, 50, 0)) == SH_DROP_LEFT, "the widest strip");
    struct sh_drop_box right = {1920, 0, 1280, 720}, its = {1920, 0, 1280, 680};
    CHECK(sh_drop_edge(right, its) == SH_DROP_BOTTOM, "on a monitor right of another");
}

static void test_top_bar(void) {
    // washi's floating bar: 46 pixels with a 12-pixel margin above, reserving 58.
    struct sh_drop_box area = usable(58, 0, 0, 0), frame = {558, 268, 804, 604};
    struct sh_drop_plan plan = sh_drop_plan(OUTPUT, area, frame, 14, false, true);
    CHECK(plan.edge == SH_DROP_TOP && plan.clip);
    CHECK(plan.cut == 58 - 14, "cut as deep into the strip as the corners reach: %d", plan.cut);
    CHECK(plan.dx == 0 && plan.dy == plan.cut - (268 + 604), "out of sight past the cut: %d",
          plan.dy);
    // A thin strip takes the corner's depth no further.
    plan = sh_drop_plan(OUTPUT, usable(8, 0, 0, 0), frame, 14, false, true);
    CHECK(plan.cut == 0 && plan.dy == -(268 + 604), "%d %d", plan.cut, plan.dy);
    // Square windows are cut at the bar.
    plan = sh_drop_plan(OUTPUT, area, frame, 0, false, true);
    CHECK(plan.cut == 58);
    // Unable to clip, it still slides the whole way: past the top there is nothing to show it.
    plan = sh_drop_plan(OUTPUT, area, frame, 14, false, false);
    CHECK(!plan.clip && plan.dy == 58 - 14 - (268 + 604), "%d", plan.dy);
}

static void test_bottom_and_sides(void) {
    struct sh_drop_box frame = {600, 300, 700, 500};
    struct sh_drop_plan plan = sh_drop_plan(OUTPUT, usable(0, 48, 0, 0), frame, 0, false, true);
    CHECK(plan.edge == SH_DROP_BOTTOM && plan.clip && plan.cut == 1032);
    CHECK(plan.dy == 1032 - 300 && plan.dx == 0, "below the bar's top: %d", plan.dy);
    plan = sh_drop_plan(OUTPUT, usable(0, 0, 60, 0), frame, 10, false, true);
    CHECK(plan.edge == SH_DROP_LEFT && plan.cut == 50 && plan.dx == 50 - 1300 && plan.dy == 0,
          "%d %d", plan.cut, plan.dx);
    plan = sh_drop_plan(OUTPUT, usable(0, 0, 0, 60), frame, 10, false, true);
    CHECK(plan.edge == SH_DROP_RIGHT && plan.cut == 1870 && plan.dx == 1870 - 600, "%d %d",
          plan.cut, plan.dx);
}

static void test_no_bar(void) {
    struct sh_drop_box frame = {600, 300, 700, 500};
    // Nothing beyond the top edge: no cut needed, the whole way up.
    struct sh_drop_plan plan = sh_drop_plan(OUTPUT, OUTPUT, frame, 14, false, true);
    CHECK(plan.edge == SH_DROP_TOP && !plan.clip && plan.cut == 0 && plan.dy == -800);
    // Another monitor above: cut at the edge, or without clips, stop at it.
    plan = sh_drop_plan(OUTPUT, OUTPUT, frame, 14, true, true);
    CHECK(plan.clip && plan.cut == 0 && plan.dy == -800);
    plan = sh_drop_plan(OUTPUT, OUTPUT, frame, 14, true, false);
    CHECK(!plan.clip && plan.dy == -300, "stops at the monitor's top: %d", plan.dy);
    // Another monitor below a bottom bar, unable to clip: the frame's bottom stops at the edge.
    plan = sh_drop_plan(OUTPUT, usable(0, 48, 0, 0), frame, 0, true, false);
    CHECK(plan.dy == 1080 - 800, "%d", plan.dy);
}

static void test_swept(void) {
    struct sh_drop_box frame = {600, 300, 700, 500};
    struct sh_drop_plan plan = sh_drop_plan(OUTPUT, OUTPUT, frame, 0, false, true);
    struct sh_drop_box swept = sh_drop_swept(&plan, OUTPUT, frame);
    CHECK(swept.x == 600 && swept.width == 700 && swept.y == -500 && swept.height == 500,
          "the frame's height above the top: %d %d", swept.y, swept.height);
    plan = sh_drop_plan(OUTPUT, usable(0, 48, 0, 0), frame, 0, false, true);
    swept = sh_drop_swept(&plan, OUTPUT, frame);
    CHECK(swept.y == 1080 && swept.height == 1032 + 500 - 1080, "%d %d", swept.y, swept.height);
    plan = sh_drop_plan(OUTPUT, usable(0, 0, 60, 0), frame, 0, false, true);
    swept = sh_drop_swept(&plan, OUTPUT, frame);
    CHECK(swept.x == 60 - 700 && swept.width == 700 - 60 && swept.y == 300, "%d %d", swept.x,
          swept.width);
    plan = sh_drop_plan(OUTPUT, usable(0, 0, 0, 60), frame, 0, false, true);
    swept = sh_drop_swept(&plan, OUTPUT, frame);
    CHECK(swept.x == 1920 && swept.width == 1860 + 700 - 1920, "%d %d", swept.x, swept.width);
    // A window that never reaches the edge sweeps nothing past it.
    plan = (struct sh_drop_plan){.edge = SH_DROP_TOP, .dy = -100};
    swept = sh_drop_swept(&plan, OUTPUT, frame);
    CHECK(swept.height <= 0);
}

static void test_cut(void) {
    struct sh_drop_plan plan = {.edge = SH_DROP_TOP, .cut = 44};
    struct sh_drop_box box = {10, 20, 100, 50};
    CHECK(sh_drop_cut(&plan, &box) && box.x == 10 && box.y == 44 && box.width == 100 &&
          box.height == 26);
    box = (struct sh_drop_box){10, 50, 100, 50};
    CHECK(sh_drop_cut(&plan, &box) && box.y == 50 && box.height == 50, "below it: untouched");
    box = (struct sh_drop_box){10, -60, 100, 50};
    CHECK(!sh_drop_cut(&plan, &box), "wholly past it");
    plan = (struct sh_drop_plan){.edge = SH_DROP_BOTTOM, .cut = 1032};
    box = (struct sh_drop_box){0, 1000, 100, 100};
    CHECK(sh_drop_cut(&plan, &box) && box.y == 1000 && box.height == 32);
    plan = (struct sh_drop_plan){.edge = SH_DROP_LEFT, .cut = 50};
    box = (struct sh_drop_box){0, 0, 100, 100};
    CHECK(sh_drop_cut(&plan, &box) && box.x == 50 && box.width == 50);
    plan = (struct sh_drop_plan){.edge = SH_DROP_RIGHT, .cut = 1870};
    box = (struct sh_drop_box){1800, 0, 100, 100};
    CHECK(sh_drop_cut(&plan, &box) && box.x == 1800 && box.width == 70);
    box = (struct sh_drop_box){1870, 0, 100, 100};
    CHECK(!sh_drop_cut(&plan, &box));
}

int main(void) {
    test_edge();
    test_top_bar();
    test_bottom_and_sides();
    test_no_bar();
    test_swept();
    test_cut();
    if (failures)
        return 1;
    puts("dropdown passed");
    return 0;
}
