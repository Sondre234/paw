// SPDX-License-Identifier: GPL-3.0-or-later
/* The exploded view's arithmetic: sheets flat on the output and fanned out within the area, the
 * way the stack rises as it is turned, the spread, a lifted sheet, the sheet under a point, the
 * room left for the shell's text and the easing. */
#include "paw/xray_layout.h"
#include <stdio.h>
#include "check.h"

static const double W = 1920, H = 1080;

static struct sh_xray_view view_of(int count) {
    double legend;
    struct sh_xray_view view = {W, H, sh_xray_area(W, H, &legend), count, 0, 0, 1, 1};
    return view;
}

static double middle_x(struct sh_xray_box box) {
    return box.x + box.width / 2;
}
static double middle_y(struct sh_xray_box box) {
    return box.y + box.height / 2;
}

static bool inside(struct sh_xray_box box, struct sh_xray_box area) {
    const double slack = 0.5;
    return box.x >= area.x - slack && box.y >= area.y - slack &&
           box.x + box.width <= area.x + area.width + slack &&
           box.y + box.height <= area.y + area.height + slack;
}

static void test_flat(void) {
    struct sh_xray_view view = view_of(5);
    view.progress = 0;
    struct sh_xray_box sheets[5];
    sh_xray_layout(&view, NULL, sheets);
    for (int i = 0; i < 5; ++i)
        CHECK(sheets[i].x == 0 && sheets[i].y == 0 && sheets[i].width == W &&
                  sheets[i].height == H,
              "sheet %d lies on the output as it opens: %g %g %g %g", i, sheets[i].x,
              sheets[i].y, sheets[i].width, sheets[i].height);
    // Half way, each is half way from the output to its place.
    struct sh_xray_box fanned[5], half[5];
    view.progress = 1;
    sh_xray_layout(&view, NULL, fanned);
    view.progress = 0.5;
    sh_xray_layout(&view, NULL, half);
    for (int i = 0; i < 5; ++i) {
        NEAR(half[i].x, fanned[i].x / 2, 1e-9);
        NEAR(half[i].width, (W + fanned[i].width) / 2, 1e-9);
    }
}

static void test_one(void) {
    struct sh_xray_view view = view_of(1);
    struct sh_xray_box sheet;
    sh_xray_layout(&view, NULL, &sheet);
    CHECK(inside(sheet, view.area), "%g %g %g %g", sheet.x, sheet.y, sheet.width, sheet.height);
    CHECK(sheet.width <= 0.7 * W + 1e-9, "never drawn larger than 0.7 of the output: %g",
          sheet.width);
    // In the middle of the area, but for the room a lift takes up and to the right.
    double dx = middle_x(sheet) - (view.area.x + view.area.width / 2);
    double dy = middle_y(sheet) - (view.area.y + view.area.height / 2);
    CHECK(dx < 0 && dx > -H / 30 && dy > 0 && dy < H / 30, "%g %g", dx, dy);
    NEAR(sheet.width / sheet.height, W / H * (1 - 0.18 * 0.62 * 0.62) / (1 - 0.18 * 0.25),
         1e-6); // foreshortened along the way it is turned
}

static void test_fanned(void) {
    for (int count = 2; count <= 16; ++count) {
        struct sh_xray_view view = view_of(count);
        struct sh_xray_box sheets[16];
        sh_xray_layout(&view, NULL, sheets);
        for (int i = 0; i < count; ++i)
            CHECK(inside(sheets[i], view.area), "%d sheets: sheet %d %g %g %g %g in the area",
                  count, i, sheets[i].x, sheets[i].y, sheets[i].width, sheets[i].height);
        for (int i = 1; i < count; ++i) {
            CHECK(middle_x(sheets[i]) > middle_x(sheets[i - 1]) + 1, "%d: right of the one under",
                  count);
            CHECK(middle_y(sheets[i]) < middle_y(sheets[i - 1]) - 1, "%d: above the one under",
                  count);
            CHECK(sheets[i].width > sheets[i - 1].width, "%d: nearer the eye, larger", count);
        }
        // The stack fills the area along one of its sides at least, unless a few sheets would
        // be drawn larger than 0.7 of the output.
        if (count < 4)
            continue;
        double left = sheets[0].x, top = sheets[count - 1].y;
        double right = sheets[count - 1].x + sheets[count - 1].width;
        double bottom = sheets[0].y + sheets[0].height;
        CHECK(right - left > 0.93 * view.area.width || bottom - top > 0.93 * view.area.height,
              "%d sheets fill the area, but for the room a lift takes: %g by %g", count,
              right - left, bottom - top);
    }
    // Many sheets lie closer together than a few, as a share of a sheet.
    struct sh_xray_box few[3], many[12];
    struct sh_xray_view view = view_of(3);
    sh_xray_layout(&view, NULL, few);
    view = view_of(12);
    sh_xray_layout(&view, NULL, many);
    CHECK((middle_x(many[1]) - middle_x(many[0])) / many[0].width <
          (middle_x(few[1]) - middle_x(few[0])) / few[0].width);
}

static void test_turned(void) {
    struct sh_xray_view view = view_of(4);
    struct sh_xray_box sheets[4];
    // The pointer moved right: the stack leans away from it, the bottom sheets showing their right
    // edges.
    view.tilt_x = 1;
    sh_xray_layout(&view, NULL, sheets);
    CHECK(middle_x(sheets[3]) < middle_x(sheets[0]), "rises to the left");
    CHECK(middle_y(sheets[3]) < middle_y(sheets[0]), "still upward");
    CHECK(sheets[0].x + sheets[0].width > sheets[3].x + sheets[3].width,
          "the bottom sheet comes out on the right");
    struct sh_xray_box straight[4];
    view.tilt_x = 0;
    sh_xray_layout(&view, NULL, straight);
    view.tilt_x = -1;
    sh_xray_layout(&view, NULL, sheets);
    CHECK(middle_x(sheets[3]) - middle_x(sheets[0]) > middle_x(straight[3]) - middle_x(straight[0]),
          "moved left, further to the right");
    view.tilt_x = 0;
    view.tilt_y = -1; // moved up
    sh_xray_layout(&view, NULL, sheets);
    CHECK(middle_y(sheets[3]) > middle_y(sheets[0]), "rises downward");
    CHECK(middle_x(sheets[3]) > middle_x(sheets[0]), "still to the right");
    // Turned to where the way would vanish, the sheets still stand apart.
    view.tilt_x = 0.62 / 0.9;
    view.tilt_y = -0.5 / 0.9;
    sh_xray_layout(&view, NULL, sheets);
    double apart = hypot(middle_x(sheets[1]) - middle_x(sheets[0]),
                         middle_y(sheets[1]) - middle_y(sheets[0]));
    CHECK(apart > 10, "never piled on each other: %g", apart);
    for (int i = 0; i < 4; ++i)
        CHECK(inside(sheets[i], view.area));
    // Turned past the end, as at the end.
    struct sh_xray_box end[4];
    view.tilt_x = 1;
    view.tilt_y = -1;
    sh_xray_layout(&view, NULL, end);
    view.tilt_x = 7;
    view.tilt_y = -3;
    sh_xray_layout(&view, NULL, sheets);
    for (int i = 0; i < 4; ++i)
        NEAR(sheets[i].x, end[i].x, 1e-9);
}

static void test_spread(void) {
    struct sh_xray_view view = view_of(4);
    struct sh_xray_box near[4], far[4];
    view.spread = 0.5;
    sh_xray_layout(&view, NULL, near);
    view.spread = 2;
    sh_xray_layout(&view, NULL, far);
    CHECK((middle_x(far[1]) - middle_x(far[0])) / far[0].width >
              (middle_x(near[1]) - middle_x(near[0])) / near[0].width,
          "spread further apart");
    CHECK(far[0].width < near[0].width, "and smaller, to fit");
    for (int i = 0; i < 4; ++i)
        CHECK(inside(far[i], view.area));

    NEAR(sh_xray_spread(1, -1), 1.2, 1e-9);
    NEAR(sh_xray_spread(1, 1), 1 / 1.2, 1e-9);
    NEAR(sh_xray_spread(1, -100), SH_XRAY_SPREAD_MAX, 1e-9);
    NEAR(sh_xray_spread(1, 100), SH_XRAY_SPREAD_MIN, 1e-9);
    NEAR(sh_xray_spread(2, 0), 2, 1e-9);
}

static void test_lift(void) {
    struct sh_xray_view view = view_of(5);
    struct sh_xray_box rest[5], lifted[5];
    double lift[5] = {0, 0, 1, 0, 0};
    sh_xray_layout(&view, NULL, rest);
    sh_xray_layout(&view, lift, lifted);
    for (int i = 0; i < 5; ++i) {
        if (i == 2)
            continue;
        CHECK(lifted[i].x == rest[i].x && lifted[i].y == rest[i].y, "sheet %d stays", i);
    }
    CHECK(lifted[2].x > rest[2].x && lifted[2].y < rest[2].y, "lifted the way the stack rises");
    CHECK(lifted[2].width == rest[2].width, "at its size");
    // The top one lifted stays in the area.
    lift[2] = 0;
    lift[4] = 1;
    sh_xray_layout(&view, lift, lifted);
    CHECK(inside(lifted[4], view.area), "%g %g %g %g", lifted[4].x, lifted[4].y, lifted[4].width,
          lifted[4].height);
    lift[4] = 0;
    lift[2] = 1;
    sh_xray_layout(&view, lift, lifted);
    // Half way up, half as far.
    lift[2] = 0.5;
    struct sh_xray_box half[5];
    sh_xray_layout(&view, lift, half);
    NEAR(half[2].x - rest[2].x, (lifted[2].x - rest[2].x) / 2, 1e-9);
    // Flat on the output, nothing is lifted.
    view.progress = 0;
    lift[2] = 1;
    sh_xray_layout(&view, lift, lifted);
    CHECK(lifted[2].x == 0 && lifted[2].y == 0);
}

static void test_sheet_at(void) {
    struct sh_xray_view view = view_of(3);
    struct sh_xray_box sheets[3];
    sh_xray_layout(&view, NULL, sheets);
    // The bottom sheet shows below and left of the others; the middle of the stack is the top's.
    CHECK(sh_xray_sheet_at(sheets, 3, sheets[0].x + 2, sheets[0].y + sheets[0].height - 2) == 0);
    CHECK(sh_xray_sheet_at(sheets, 3, middle_x(sheets[2]), middle_y(sheets[2])) == 2);
    CHECK(sh_xray_sheet_at(sheets, 3, middle_x(sheets[1]),
                           sheets[1].y + sheets[1].height - 1) == 1);
    CHECK(sh_xray_sheet_at(sheets, 3, 1, 1) == -1, "the backdrop");
    CHECK(sh_xray_sheet_at(sheets, 3, sheets[2].x + sheets[2].width, middle_y(sheets[2])) == -1,
          "the right edge is outside");
    CHECK(sh_xray_sheet_at(sheets, 0, 5, 5) == -1);
}

static void test_tilt(void) {
    double x, y;
    sh_xray_tilt(0, 0, W, H, &x, &y);
    CHECK(x == 0 && y == 0, "as it opens");
    sh_xray_tilt(W / 4, -H / 2, W, H, &x, &y);
    NEAR(x, 0.5, 1e-9);
    NEAR(y, -1, 1e-9);
    sh_xray_tilt(-W, H, W, H, &x, &y);
    CHECK(x == -1 && y == 1, "no further than the end");
    sh_xray_tilt(10, 10, 0, 0, &x, &y);
    CHECK(x == 0 && y == 0, "no output");
}

static void test_area(void) {
    double legend;
    struct sh_xray_box area = sh_xray_area(W, H, &legend);
    CHECK(legend >= 260 && legend <= 360, "%g", legend);
    CHECK(area.x >= legend && area.y > 0, "right of the legend");
    CHECK(area.x + area.width < W && area.y + area.height <= H - 64, "above the caption");
    area = sh_xray_area(800, 600, &legend);
    CHECK(legend == 0, "no legend on a narrow output");
    CHECK(area.x > 0 && area.x + area.width < 800);
    area = sh_xray_area(10, 10, &legend);
    CHECK(area.width >= 1 && area.height >= 1, "never empty");
}

static void test_ease(void) {
    double v = sh_xray_ease(0, 1, 100, 100, 1e-3);
    NEAR(v, 0.99, 1e-9);
    double a = sh_xray_ease(0, 1, 30, 100, 1e-3), b = sh_xray_ease(a, 1, 30, 100, 1e-3);
    CHECK(a > 0 && b > a && b < 1, "toward it: %g %g", a, b);
    NEAR(b, sh_xray_ease(0, 1, 60, 100, 1e-3), 1e-9);
    CHECK(sh_xray_ease(0, 1, 1000, 100, 1e-3) == 1, "there once within epsilon");
    CHECK(sh_xray_ease(0.3, 1, 5, 0, 1e-3) == 1, "at once without a time");
    CHECK(sh_xray_ease(0.3, 1, 0, 100, 1e-3) == 0.3, "no time passed");
    CHECK(sh_xray_ease(1, -1, 50, 100, 1e-3) < 1, "down too");
}

int main(void) {
    test_flat();
    test_one();
    test_fanned();
    test_turned();
    test_spread();
    test_lift();
    test_sheet_at();
    test_tilt();
    test_area();
    test_ease();
    if (failures)
        fprintf(stderr, "%d check(s) failed\n", failures);
    return failures ? 1 : 0;
}
