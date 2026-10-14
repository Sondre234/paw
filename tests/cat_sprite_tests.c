// SPDX-License-Identifier: GPL-3.0-or-later
/* The desktop cat's pictures: every frame paints in every coat, opaque or transparent cell by
 * cell, standing on the bottom row, and the coats differ. */
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

static void test_names(void) {
    for (int a = 0; a < SH_CAT_FRAMES; ++a)
        for (int b = a + 1; b < SH_CAT_FRAMES; ++b)
            CHECK(strcmp(sh_cat_frame_name(a), sh_cat_frame_name(b)) != 0, "%d %d", a, b);
    CHECK(!strcmp(sh_cat_frame_name(SH_CAT_FRAME_SLEEP_2), "sleep_2"));
    CHECK(!strcmp(sh_cat_frame_name(SH_CAT_FRAME_FALL), "fall"));
}

int main(void) {
    test_frames();
    test_coats();
    test_z();
    test_names();
    if (failures)
        fprintf(stderr, "%d failures\n", failures);
    return failures != 0;
}
