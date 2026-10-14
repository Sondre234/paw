// SPDX-License-Identifier: GPL-3.0-or-later
/* The desktop cat's behaviour: what it stands on, gravity and landing, the startled fall as its
 * window moves away, jumps to the focused window, sleep, chasing the pointer, pawing at an urgent
 * window, and how seldom it needs stepping while it sits or sleeps. */
#include "paw/cat.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "check.h"

enum { W = 40, H = 32, MAX = 64 };

/* A world of up to eight windows (front to back) on one or two 1920 x 1080 monitors side by
 * side, each with a 48-pixel taskbar along its bottom. */
struct scene {
    struct sh_cat_window windows[8];
    int count;
    struct sh_cat_area areas[2];
    int area_count, focused_area;
    uint32_t focused, urgent;
    bool idle;
    struct sh_cat_ledge tops[MAX], ledges[MAX];
    struct sh_cat_world world;
};

static void build(struct scene *scene) {
    int tops, ledges;
    sh_cat_ledges(scene->windows, scene->count, scene->areas, scene->area_count, H, scene->tops,
                  &tops, scene->ledges, &ledges, MAX);
    scene->world = (struct sh_cat_world){
        .areas = scene->areas, .area_count = scene->area_count,
        .focused_area = scene->focused_area, .tops = scene->tops, .ledges = scene->ledges,
        .top_count = tops, .ledge_count = ledges, .focused = scene->focused,
        .urgent = scene->urgent, .idle = scene->idle};
}

static struct scene monitors(int count) {
    struct scene scene = {.area_count = count};
    for (int i = 0; i < count; ++i)
        scene.areas[i] = (struct sh_cat_area){1920.0 * i, 0, 1920, 1080, 0, 1080 - 48};
    build(&scene);
    return scene;
}

static void add(struct scene *scene, uint32_t window, double x, double y, double width,
                double height) {
    scene->windows[scene->count++] = (struct sh_cat_window){window, x, y, width, height};
    build(scene);
}

/* Steps the cat as the compositor's timer would, for `ms` milliseconds. */
static void run(struct sh_cat *cat, struct scene *scene, int64_t *now, int64_t ms) {
    int64_t end = *now + ms;
    while (*now < end) {
        int next = sh_cat_step(cat, &scene->world, *now);
        *now += next < end - *now ? next : end - *now;
    }
    sh_cat_step(cat, &scene->world, *now);
}

/* Steps it until it is in `state`, for at most `limit` milliseconds; false if it never is. */
static bool run_until(struct sh_cat *cat, struct scene *scene, int64_t *now,
                      enum sh_cat_state state, int64_t limit) {
    int64_t end = *now + limit;
    while (*now < end) {
        int next = sh_cat_step(cat, &scene->world, *now);
        if (cat->state == state)
            return true;
        *now += next;
    }
    return false;
}

/* A cat dropped in over x, settled: landed and sitting. */
static struct sh_cat settled(struct scene *scene, double x, int64_t *now) {
    struct sh_cat cat;
    sh_cat_init(&cat, W, H, &scene->areas[0], x, 7, *now);
    CHECK(run_until(&cat, scene, now, SH_CAT_SIT, 3000), "it lands and sits");
    return cat;
}

static void test_ledges(void) {
    struct scene scene = monitors(1);
    add(&scene, 1, 100, 300, 600, 400);  // in front
    add(&scene, 2, 400, 200, 800, 600);  // behind it, its top showing right of it
    add(&scene, 3, 0, 0, 1920, 1032);    // maximized, at the back
    const struct sh_cat_world *world = &scene.world;
    CHECK(world->top_count == 4 && world->ledge_count == 4, "%d tops, %d ledges",
          world->top_count, world->ledge_count);
    CHECK(world->tops[0].window == 1 && world->tops[0].y == 300 && world->tops[0].left == 100 &&
          world->tops[0].right == 700);
    // Window 1 in front covers nothing of 2's top at 200, which lies above it.
    CHECK(world->ledges[1].window == 2 && world->ledges[1].left == 400 &&
          world->ledges[1].right == 1200);
    // The maximized window's top is held down by the cat's height, inside its title bar, and
    // what shows of it is left of window 2's... and right of it.
    CHECK(world->tops[2].window == 3 && world->tops[2].y == H, "y %g", world->tops[2].y);
    CHECK(world->ledges[2].window == 3 && world->ledges[2].right == 1920);
    // The floor: the top of the taskbar.
    CHECK(world->tops[3].window == 0 && world->tops[3].y == 1032 && world->tops[3].left == 0 &&
          world->tops[3].right == 1920);

    // A window in front reaching over a top hides that part of it.
    scene = monitors(1);
    add(&scene, 1, 300, 250, 200, 400);
    add(&scene, 2, 100, 300, 800, 400);
    CHECK(scene.world.ledge_count == 4, "%d", scene.world.ledge_count);
    CHECK(scene.world.ledges[1].window == 2 && scene.world.ledges[1].left == 100 &&
          scene.world.ledges[1].right == 300);
    CHECK(scene.world.ledges[2].window == 2 && scene.world.ledges[2].left == 500 &&
          scene.world.ledges[2].right == 900);
    // One whose bottom is well above it does not.
    scene = monitors(1);
    add(&scene, 1, 300, 50, 200, 200);
    add(&scene, 2, 100, 300, 800, 400);
    CHECK(scene.world.ledges[1].left == 100 && scene.world.ledges[1].right == 900);

    // Two monitors: each its own floor.
    scene = monitors(2);
    CHECK(scene.world.top_count == 2 && scene.world.tops[1].left == 1920);
    CHECK(sh_cat_area_at(&scene.world, 2000, 1032) == 1);
    CHECK(sh_cat_area_at(&scene.world, 1919, 1032) == 0);
    CHECK(sh_cat_area_at(&scene.world, -50, 500) == 0, "the nearest one");
}

static void test_lands_on_a_window(void) {
    struct scene scene = monitors(1);
    add(&scene, 1, 600, 400, 700, 500);
    scene.focused = 1;
    build(&scene);
    int64_t now = 1000;
    struct sh_cat cat;
    sh_cat_init(&cat, W, H, &scene.areas[0], 900, 7, now);
    CHECK(cat.state == SH_CAT_FALL && cat.y == H, "it turns up at the top, falling");
    CHECK(run_until(&cat, &scene, &now, SH_CAT_LAND, 2000), "it lands");
    CHECK(cat.grounded && cat.on == 1 && cat.y == 400 && cat.x == 900);
    // Falling 368 pixels at 2600 px/s² takes about 530 ms.
    CHECK(now - 1000 > 450 && now - 1000 < 650, "fell for %lld ms", (long long)(now - 1000));
    struct sh_cat_pose pose;
    sh_cat_pose(&cat, now, &pose);
    CHECK(pose.squash > 0.2, "squashed as it lands: %g", pose.squash);
    run(&cat, &scene, &now, 300);
    CHECK(cat.state == SH_CAT_SIT, "%s", sh_cat_state_name(cat.state));
    sh_cat_pose(&cat, now, &pose);
    CHECK(pose.squash == 0);
}

static void test_falls_when_the_window_moves(void) {
    struct scene scene = monitors(1);
    add(&scene, 2, 500, 700, 900, 200); // below, behind
    add(&scene, 1, 600, 400, 700, 200);
    scene.focused = 1;
    build(&scene);
    int64_t now = 0;
    struct sh_cat cat = settled(&scene, 900, &now);
    CHECK(cat.on == 1 && cat.y == 400);
    // Window 1 is dragged away to the left: the cat hops, falls, and lands on window 2.
    scene.windows[1].x = 0;
    scene.windows[1].width = 300;
    build(&scene);
    sh_cat_step(&cat, &scene.world, now);
    CHECK(cat.state == SH_CAT_FALL && cat.vy < 0 && !cat.grounded, "a startled hop");
    CHECK(run_until(&cat, &scene, &now, SH_CAT_LAND, 2000));
    CHECK(cat.on == 2 && cat.y == 700, "on %u at %g", cat.on, cat.y);
    // Window 2 is minimized: on down to the top of the taskbar.
    run(&cat, &scene, &now, 400);
    scene.count = 1;
    scene.windows[0] = scene.windows[1];
    build(&scene);
    CHECK(run_until(&cat, &scene, &now, SH_CAT_LAND, 2000));
    CHECK(cat.on == 0 && cat.y == 1032, "on %u at %g", cat.on, cat.y);
    // Moving a window it does not sit on changes nothing.
    run(&cat, &scene, &now, 400);
    scene.windows[0].y = 100;
    build(&scene);
    sh_cat_step(&cat, &scene.world, now);
    CHECK(cat.grounded && cat.on == 0);
}

static void test_lands_only_where_the_top_shows(void) {
    struct scene scene = monitors(1);
    add(&scene, 1, 700, 300, 400, 600); // in front, covering part of 2's top
    add(&scene, 2, 500, 500, 900, 300);
    int64_t now = 0;
    struct sh_cat cat;
    sh_cat_init(&cat, W, H, &scene.areas[0], 1000, 7, now);
    CHECK(run_until(&cat, &scene, &now, SH_CAT_LAND, 2000));
    CHECK(cat.on == 1 && cat.y == 300, "on the window in front: %u", cat.on);
    // Over the covered part of window 2's top, but past window 1: it falls through to the floor.
    sh_cat_init(&cat, W, H, &scene.areas[0], 1500, 7, now);
    CHECK(run_until(&cat, &scene, &now, SH_CAT_LAND, 2000));
    CHECK(cat.on == 0, "on %u", cat.on);
    sh_cat_init(&cat, W, H, &scene.areas[0], 600, 7, now);
    CHECK(run_until(&cat, &scene, &now, SH_CAT_LAND, 2000));
    CHECK(cat.on == 2 && cat.y == 500, "on the part that shows: %u", cat.on);
}

static void test_jumps_to_the_focused_window(void) {
    struct scene scene = monitors(1);
    add(&scene, 1, 100, 600, 600, 300);
    add(&scene, 2, 1000, 300, 600, 500);
    scene.focused = 1;
    build(&scene);
    int64_t now = 0;
    struct sh_cat cat = settled(&scene, 400, &now);
    CHECK(cat.on == 1);
    scene.focused = 2;
    build(&scene);
    CHECK(run_until(&cat, &scene, &now, SH_CAT_JUMP, 8000), "it goes to the focused window");
    CHECK(cat.facing == 1 && cat.target_window == 2 && cat.target_y == 300);
    double highest = cat.y;
    while (cat.state == SH_CAT_JUMP) {
        now += sh_cat_step(&cat, &scene.world, now);
        if (cat.y < highest)
            highest = cat.y;
    }
    CHECK(highest < 300, "it clears the top it jumps to: %g", highest);
    CHECK(cat.state == SH_CAT_LAND && cat.on == 2 && cat.y == 300);
    CHECK(cat.x >= 1000 + W / 2 && cat.x <= 1600 - W / 2);

    // The window goes while it is on its way: it falls on from there.
    scene.focused = 1;
    build(&scene);
    CHECK(run_until(&cat, &scene, &now, SH_CAT_JUMP, 8000));
    run(&cat, &scene, &now, 200);
    scene.windows[0].y = 900;
    scene.focused = 0;
    build(&scene);
    CHECK(run_until(&cat, &scene, &now, SH_CAT_FALL, 2000));
    CHECK(run_until(&cat, &scene, &now, SH_CAT_LAND, 2000));
    CHECK(cat.on == 1 && cat.y == 900, "on %u at %g", cat.on, cat.y);
}

static void test_walks_without_falling_off(void) {
    struct scene scene = monitors(1);
    add(&scene, 1, 800, 500, 200, 300);
    scene.focused = 1;
    build(&scene);
    int64_t now = 0;
    struct sh_cat cat = settled(&scene, 900, &now);
    // A minute of idling: it walks, grooms and looks about, but stays on its window.
    double least = cat.x, most = cat.x;
    bool walked = false;
    for (int64_t end = now + 60000; now < end;) {
        now += sh_cat_step(&cat, &scene.world, now);
        walked |= cat.state == SH_CAT_WALK;
        least = fmin(least, cat.x);
        most = fmax(most, cat.x);
        CHECK(cat.grounded && cat.on == 1, "%s", sh_cat_state_name(cat.state));
    }
    CHECK(walked);
    CHECK(least >= 800 + W / 2 - 1 && most <= 1000 - W / 2 + 1, "%g to %g", least, most);
}

static void test_sleeps_while_idle(void) {
    struct scene scene = monitors(1);
    add(&scene, 1, 600, 400, 700, 500);
    scene.focused = 1;
    build(&scene);
    int64_t now = 0;
    struct sh_cat cat = settled(&scene, 900, &now);
    scene.idle = true;
    build(&scene);
    CHECK(run_until(&cat, &scene, &now, SH_CAT_YAWN, 100), "it yawns at once");
    CHECK(run_until(&cat, &scene, &now, SH_CAT_SLEEP, 3000), "then sleeps");
    // Asleep it is stepped only for its breaths, each with a "z".
    int steps = 0;
    for (int64_t end = now + 60000; now < end; ++steps)
        now += sh_cat_step(&cat, &scene.world, now);
    CHECK(steps <= 30, "%d steps a minute", steps);
    struct sh_cat_pose pose;
    sh_cat_pose(&cat, now, &pose);
    CHECK(pose.z && (pose.frame == SH_CAT_FRAME_SLEEP_1 || pose.frame == SH_CAT_FRAME_SLEEP_2));
    // Input wakes it.
    CHECK(sh_cat_wake(&cat, now) && cat.state == SH_CAT_WAKE);
    CHECK(!sh_cat_wake(&cat, now), "awake already");
    scene.idle = false;
    build(&scene);
    CHECK(run_until(&cat, &scene, &now, SH_CAT_SIT, 2000));
    // It wakes too as the world says it is no longer idle.
    scene.idle = true;
    build(&scene);
    CHECK(run_until(&cat, &scene, &now, SH_CAT_SLEEP, 3000));
    scene.idle = false;
    build(&scene);
    CHECK(run_until(&cat, &scene, &now, SH_CAT_WAKE, 100));
}

static void test_sits_seldom_stepped(void) {
    struct scene scene = monitors(1);
    add(&scene, 1, 600, 400, 40, 500); // too narrow to walk along
    scene.focused = 1;
    build(&scene);
    int64_t now = 0;
    struct sh_cat cat = settled(&scene, 620, &now);
    int steps = 0;
    for (int64_t end = now + 60000; now < end; ++steps)
        now += sh_cat_step(&cat, &scene.world, now);
    // Blinks, grooming and scratching want a few frames each; sitting about, very few.
    CHECK(steps < 1200, "%d steps a minute", steps);
}

static void test_chases_the_pointer(void) {
    struct scene scene = monitors(1);
    add(&scene, 1, 600, 400, 700, 500);
    scene.focused = 1;
    build(&scene);
    int64_t now = 0;
    struct sh_cat cat = settled(&scene, 900, &now);
    // A slow move nearby is no flick; a fast one far away is none it sees.
    for (int i = 0; i < 20; ++i)
        CHECK(!sh_cat_pointer(&cat, 950 + i, 300, now + 16 * i));
    for (int i = 0; i < 20; ++i)
        CHECK(!sh_cat_pointer(&cat, 100 + 30 * i, 900, now + 400 + 8 * i));
    now += 600;
    // A flick past it: it notices, then runs to the pointer where it rests, over window 1's
    // top, and sits beside it there looking at it.
    bool chasing = false;
    for (int i = 0; i < 10; ++i)
        chasing |= sh_cat_pointer(&cat, 1000 + 25 * i, 380, now + 8 * i);
    CHECK(chasing && cat.state == SH_CAT_CHASE);
    now += 80;
    struct sh_cat_pose pose;
    sh_cat_pose(&cat, now, &pose);
    CHECK(pose.frame == SH_CAT_FRAME_ALERT);
    CHECK(run_until(&cat, &scene, &now, SH_CAT_LOOK, 3000), "%s", sh_cat_state_name(cat.state));
    CHECK(cat.grounded && cat.on == 1 && cat.y == 400);
    CHECK(fabs(cat.x - (1225 - W / 2 - 4)) < 1 && cat.facing == 1, "at %g", cat.x);

    // Flicked over bare space, it paws at the pointer, then drops to the floor.
    now += 1000;
    for (int i = 0; i < 10; ++i)
        sh_cat_pointer(&cat, 1100 - 20 * i, 200, now + 8 * i);
    CHECK(cat.state == SH_CAT_CHASE);
    CHECK(run_until(&cat, &scene, &now, SH_CAT_POUNCE, 3000));
    CHECK(run_until(&cat, &scene, &now, SH_CAT_FALL, 2000));
    CHECK(run_until(&cat, &scene, &now, SH_CAT_LAND, 2000));
    CHECK(cat.on == 1, "on the window under the pointer: %u", cat.on);

    // Asleep, a flick does not set it running.
    scene.idle = true;
    build(&scene);
    CHECK(run_until(&cat, &scene, &now, SH_CAT_SLEEP, 5000));
    for (int i = 0; i < 10; ++i)
        CHECK(!sh_cat_pointer(&cat, 900 + 25 * i, 380, now + 8 * i));
}

static void test_paws_at_an_urgent_window(void) {
    struct scene scene = monitors(1);
    add(&scene, 1, 100, 400, 600, 500);
    add(&scene, 2, 1100, 200, 700, 500);
    scene.focused = 1;
    build(&scene);
    int64_t now = 0;
    struct sh_cat cat = settled(&scene, 400, &now);
    scene.urgent = 2;
    build(&scene);
    CHECK(run_until(&cat, &scene, &now, SH_CAT_RUSH, 100), "it runs over");
    CHECK(run_until(&cat, &scene, &now, SH_CAT_KNOCK, 3000), "and paws at it");
    CHECK(cat.on == 2 && cat.y == 200 && cat.grounded);
    struct sh_cat_pose pose;
    sh_cat_pose(&cat, now, &pose);
    CHECK(pose.frame == SH_CAT_FRAME_PAW_1 || pose.frame == SH_CAT_FRAME_PAW_2);
    // Until the window has attention.
    run(&cat, &scene, &now, 1000);
    CHECK(cat.state == SH_CAT_KNOCK);
    scene.urgent = 0;
    scene.focused = 2;
    build(&scene);
    CHECK(run_until(&cat, &scene, &now, SH_CAT_SIT, 100));
    // Left alone, it gives up after a while and does not start again for the same window.
    scene.focused = 1;
    scene.urgent = 2;
    build(&scene);
    CHECK(run_until(&cat, &scene, &now, SH_CAT_KNOCK, 4000));
    run(&cat, &scene, &now, 7000);
    CHECK(cat.state != SH_CAT_KNOCK && cat.state != SH_CAT_RUSH);
    run(&cat, &scene, &now, 4000);
    CHECK(cat.state != SH_CAT_RUSH);
    // An urgent window wakes it.
    scene.urgent = 0;
    scene.idle = true;
    build(&scene);
    CHECK(run_until(&cat, &scene, &now, SH_CAT_SLEEP, 20000));
    scene.urgent = 2;
    build(&scene);
    CHECK(run_until(&cat, &scene, &now, SH_CAT_WAKE, 100));
    CHECK(run_until(&cat, &scene, &now, SH_CAT_RUSH, 3000));
}

static void test_follows_the_focused_monitor(void) {
    struct scene scene = monitors(2);
    add(&scene, 1, 300, 500, 600, 400);
    add(&scene, 2, 2300, 400, 600, 400);
    scene.focused = 1;
    scene.focused_area = 0;
    build(&scene);
    int64_t now = 0;
    struct sh_cat cat = settled(&scene, 600, &now);
    // The focus goes to the other monitor: the cat turns up there, over the focused window.
    scene.focused = 2;
    scene.focused_area = 1;
    build(&scene);
    int64_t start = now;
    CHECK(run_until(&cat, &scene, &now, SH_CAT_FALL, 12000));
    CHECK(now - start >= 1200, "not at once");
    CHECK(sh_cat_area_at(&scene.world, cat.x, cat.y) == 1 && cat.x == 2600);
    CHECK(run_until(&cat, &scene, &now, SH_CAT_LAND, 2000));
    CHECK(cat.on == 2 && cat.y == 400);

    // A window across both monitors takes it over by walking.
    scene = monitors(2);
    add(&scene, 3, 1500, 500, 1000, 400);
    scene.focused = 3;
    scene.focused_area = 0;
    build(&scene);
    cat = settled(&scene, 1600, &now);
    scene.focused_area = 1;
    build(&scene);
    CHECK(run_until(&cat, &scene, &now, SH_CAT_WALK, 12000));
    CHECK(cat.target_x > 1920, "%g", cat.target_x);
    CHECK(run_until(&cat, &scene, &now, SH_CAT_SIT, 20000));
    CHECK(sh_cat_area_at(&scene.world, cat.x, cat.y) == 1 && cat.on == 3);
}

static void test_stays_on_its_monitor(void) {
    struct scene scene = monitors(1);
    int64_t now = 0;
    struct sh_cat cat;
    sh_cat_init(&cat, W, H, &scene.areas[0], 1910, 7, now);
    CHECK(run_until(&cat, &scene, &now, SH_CAT_LAND, 3000));
    CHECK(cat.x <= 1920 - W / 2, "kept inside: %g", cat.x);
    // Chasing a pointer at the very edge, it stops short of it.
    sh_cat_init(&cat, W, H, &scene.areas[0], 1000, 7, now);
    CHECK(run_until(&cat, &scene, &now, SH_CAT_SIT, 3000));
    for (int i = 0; i < 10; ++i)
        sh_cat_pointer(&cat, 1000 + 30 * i, 900, now + 8 * i);
    CHECK(cat.state == SH_CAT_CHASE);
    for (int i = 0; i < 10; ++i)
        sh_cat_pointer(&cat, 1300 + 60 * i, 1070, now + 100 + 8 * i);
    run(&cat, &scene, &now, 3000);
    CHECK(cat.x <= 1920 - W / 2 && cat.y <= 1032, "%g, %g", cat.x, cat.y);
}

static void test_names(void) {
    CHECK(!strcmp(sh_cat_state_name(SH_CAT_SLEEP), "sleep"));
    CHECK(!strcmp(sh_cat_state_name(SH_CAT_KNOCK), "knock"));
    CHECK(!strcmp(sh_cat_state_name(SH_CAT_STATES), "sit"));
}

int main(void) {
    test_ledges();
    test_lands_on_a_window();
    test_falls_when_the_window_moves();
    test_lands_only_where_the_top_shows();
    test_jumps_to_the_focused_window();
    test_walks_without_falling_off();
    test_sleeps_while_idle();
    test_sits_seldom_stepped();
    test_chases_the_pointer();
    test_paws_at_an_urgent_window();
    test_follows_the_focused_monitor();
    test_stays_on_its_monitor();
    test_names();
    if (failures)
        fprintf(stderr, "%d failures\n", failures);
    return failures != 0;
}
