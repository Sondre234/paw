// SPDX-License-Identifier: GPL-3.0-or-later
/* The desktop cat's pictures: every frame paints in every coat, opaque or transparent cell by
 * cell, standing on the bottom row, and the coats differ; a purring cat breathes with its eyes
 * shut, and sends up pink hearts. */
#include "paw/cat_sprite.h"
#include <stdio.h>
#include <string.h>
#include "check.h"

enum { CELLS = SH_CAT_ART_WIDTH * SH_CAT_ART_HEIGHT };

static void test_frames(void) {
    for (int coat = 0; coat < SH_CAT_COATS; ++coat) {
        for (int frame = 0; frame < SH_CAT_FRAMES; ++frame) {
            uint32_t pixels[CELLS];
            memset(pixels, 0xab, sizeof(pixels));
            sh_cat_paint(pixels, frame, coat);
            int opaque = 0, bottom = 0;
            for (int i = 0; i < CELLS; ++i) {
                uint32_t alpha = pixels[i] >> 24;
                CHECK(alpha == 0xff || pixels[i] == 0, "frame %s cell %d: %08x",
                      sh_cat_frame_name(frame), i, pixels[i]);
                opaque += alpha == 0xff;
                if (i >= CELLS - SH_CAT_ART_WIDTH)
                    bottom += alpha == 0xff;
            }
            CHECK(opaque > 60 && opaque < CELLS * 3 / 4, "frame %s: %d cells",
                  sh_cat_frame_name(frame), opaque);
            CHECK(bottom > 0, "frame %s stands on the bottom row", sh_cat_frame_name(frame));
            CHECK(pixels[0] == 0, "frame %s leaves its corner out", sh_cat_frame_name(frame));
        }
    }
}

static void test_coats(void) {
    uint32_t ginger[CELLS], ink[CELLS], cream[CELLS];
    sh_cat_paint(ginger, SH_CAT_FRAME_SIT, SH_CAT_COAT_GINGER);
    sh_cat_paint(ink, SH_CAT_FRAME_SIT, SH_CAT_COAT_INK);
    sh_cat_paint(cream, SH_CAT_FRAME_SIT, SH_CAT_COAT_CREAM);
    // The middle of the forehead is fur: dark on the black cat, light on the cream one.
    uint32_t fur_ink = ink[6 * SH_CAT_ART_WIDTH + 5], fur_cream = cream[6 * SH_CAT_ART_WIDTH + 5];
    int ink_light = ((fur_ink >> 16) & 0xff) + ((fur_ink >> 8) & 0xff) + (fur_ink & 0xff);
    int cream_light = ((fur_cream >> 16) & 0xff) + ((fur_cream >> 8) & 0xff) + (fur_cream & 0xff);
    CHECK(ink_light < 200 && cream_light > 600, "%08x %08x", fur_ink, fur_cream);
    CHECK(memcmp(ginger, ink, sizeof(ginger)) != 0);
    // An unknown frame or coat paints the sitting ginger cat rather than reading past the art.
    uint32_t fallback[CELLS];
    sh_cat_paint(fallback, SH_CAT_FRAMES, SH_CAT_COATS);
    CHECK(memcmp(fallback, ginger, sizeof(ginger)) == 0);
}

static void test_z(void) {
    uint32_t pixels[SH_CAT_Z_SIZE * SH_CAT_Z_SIZE];
    sh_cat_paint_z(pixels, SH_CAT_COAT_CREAM);
    int opaque = 0;
    for (int i = 0; i < SH_CAT_Z_SIZE * SH_CAT_Z_SIZE; ++i)
        opaque += pixels[i] >> 24 == 0xff;
    CHECK(opaque == 13, "%d", opaque);
}

static void test_purr(void) {
    uint32_t sit[CELLS], purr_1[CELLS], purr_2[CELLS];
    sh_cat_paint(sit, SH_CAT_FRAME_SIT, SH_CAT_COAT_GINGER);
    sh_cat_paint(purr_1, SH_CAT_FRAME_PURR_1, SH_CAT_COAT_GINGER);
    sh_cat_paint(purr_2, SH_CAT_FRAME_PURR_2, SH_CAT_COAT_GINGER);
    // Its eyes are shut (no eye colour shows), its cheeks blush pink, and it breathes: its head
    // rises a cell from one frame to the other while its feet stay put.
    uint32_t eye = sit[7 * SH_CAT_ART_WIDTH + 5], pink = sit[2 * SH_CAT_ART_WIDTH + 5];
    for (int i = 0; i < CELLS; ++i)
        CHECK(purr_1[i] != eye && purr_2[i] != eye, "an eye shows at cell %d", i);
    CHECK(purr_1[9 * SH_CAT_ART_WIDTH + 4] == pink && purr_2[8 * SH_CAT_ART_WIDTH + 4] == pink);
    CHECK(purr_1[0 * SH_CAT_ART_WIDTH + 5] == 0 && purr_2[0 * SH_CAT_ART_WIDTH + 5] != 0);
    CHECK(memcmp(purr_1 + CELLS - SH_CAT_ART_WIDTH, purr_2 + CELLS - SH_CAT_ART_WIDTH,
                 SH_CAT_ART_WIDTH * sizeof(uint32_t)) == 0);
}

static void test_heart(void) {
    enum { HEART = SH_CAT_HEART_WIDTH * SH_CAT_HEART_HEIGHT };
    uint32_t pixels[HEART];
    memset(pixels, 0xab, sizeof(pixels));
    sh_cat_paint_heart(pixels);
    int opaque = 0, pinkish = 0;
    for (int i = 0; i < HEART; ++i) {
        uint32_t alpha = pixels[i] >> 24, red = (pixels[i] >> 16) & 0xff;
        uint32_t green = (pixels[i] >> 8) & 0xff, blue = pixels[i] & 0xff;
        CHECK(alpha == 0xff || pixels[i] == 0, "cell %d: %08x", i, pixels[i]);
        opaque += alpha == 0xff;
        pinkish += alpha == 0xff && red > green + 40 && blue > green;
    }
    CHECK(opaque == 27, "%d", opaque);
    CHECK(pinkish >= 25, "%d pink cells", pinkish);
    // A heart: two lobes with a dip between them, coming to a point at the bottom.
    CHECK(pixels[0] == 0 && pixels[3] == 0 && pixels[1] != 0 && pixels[5] != 0);
    CHECK(pixels[HEART - 4] != 0 && pixels[HEART - 3] == 0 && pixels[HEART - 5] == 0);
}

static void test_names(void) {
    for (int a = 0; a < SH_CAT_FRAMES; ++a)
        for (int b = a + 1; b < SH_CAT_FRAMES; ++b)
            CHECK(strcmp(sh_cat_frame_name(a), sh_cat_frame_name(b)) != 0, "%d %d", a, b);
    CHECK(!strcmp(sh_cat_frame_name(SH_CAT_FRAME_SLEEP_2), "sleep_2"));
    CHECK(!strcmp(sh_cat_frame_name(SH_CAT_FRAME_FALL), "fall"));
    CHECK(!strcmp(sh_cat_frame_name(SH_CAT_FRAME_PURR_2), "purr_2"));
}

int main(void) {
    test_frames();
    test_coats();
    test_z();
    test_purr();
    test_heart();
    test_names();
    if (failures)
        fprintf(stderr, "%d failures\n", failures);
    return failures != 0;
}
