/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The desktop cat's pixel art. Each frame is a grid of letters, one per cell: '.' is nothing,
 * 'k' the outline, 'b' the fur, 'd' its darker stripes, 'w' the white of the muzzle, chest and
 * paws, 'p' the pink of the nose, ears and tongue, and 'e' the eyes. A coat gives each letter its
 * colour. */
#include "paw/cat_sprite.h"
#include <stddef.h>

typedef const char *const art[SH_CAT_ART_HEIGHT];

static art sit = {
    "....................",
    ".....k.......k......",
    "....kpk.....kpk.....",
    "...kbppk...kppbk....",
    "...kbbbbkkkbbbbk....",
    "...kbbbbdbdbbbbk....",
    "..kbbbbbbdbbbbbbk...",
    "..kbbebbbbbbbebbk...",
    "..kbbebbbbbbbebbk...",
    "..kbbbbbwpwbbbbbk...",
    "...kbbbbwwwbbbbk....",
    "....kkbbbbbbbkk..kk.",
    "....kbbbwwwbbbk.kbbk",
    "...kbdbbwwwbbdbkkbk.",
    "...kbwwbwwwbwwbbbk..",
    "....kkkkkkkkkkkkk...",
};

static art blink = {
    "....................",
    ".....k.......k......",
    "....kpk.....kpk.....",
    "...kbppk...kppbk....",
    "...kbbbbkkkbbbbk....",
    "...kbbbbdbdbbbbk....",
    "..kbbbbbbdbbbbbbk...",
    "..kbbbbbbbbbbbbbk...",
    "..kbkkbbbbbbbkkbk...",
    "..kbbbbbwpwbbbbbk...",
    "...kbbbbwwwbbbbk....",
    "....kkbbbbbbbkk..kk.",
    "....kbbbwwwbbbk.kbbk",
    "...kbdbbwwwbbdbkkbk.",
    "...kbwwbwwwbwwbbbk..",
    "....kkkkkkkkkkkkk...",
};

static art alert = {
    "....k.........k.....",
    "...kpk.......kpk....",
    "...kppk.....kppk....",
    "...kbppkkkkkppbk....",
    "...kbbbbbbbbbbbk....",
    "...kbbbbdbdbbbbk....",
    "..kbbbbbbdbbbbbbk...",
    "..kbkebbbbbbbkebk...",
    "..kbkebbbbbbbkebk...",
    "..kbbbbbwpwbbbbbk...",
    "...kbbbbwwwbbbbk....",
    "....kkbbbbbbbkk..kk.",
    "....kbbbwwwbbbk.kbbk",
    "...kbdbbwwwbbdbkkbk.",
    "...kbwwbwwwbwwbbbk..",
    "....kkkkkkkkkkkkk...",
};

static art look = {
    "....................",
    "..........k..k......",
    ".........kpkkpk.....",
    ".........kbbbbbk....",
    "........kbbbbbbbk...",
    "........kbbbbbebk...",
    "........kbbbbbbbwk..",
    "........kbbbbbwpk...",
    ".......kbkkkkkkk....",
    "......kbbbbbwwk.....",
    ".....kbdbbbbwwk.....",
    "....kbbbdbbbwwk.....",
    "....kbbdbbbkbbk.....",
    "k...kbbbbbbkbbk.....",
    "kbk.kbbbbbbkwwk.....",
    ".kkkkkkkkkkkkkk.....",
};

static art walk_1 = {
    "....................",
    "....................",
    "............k..k....",
    "...........kpkkpk...",
    "...........kbbbbbk..",
    "..........kbbbbbbbk.",
    "kk........kbbbbbebk.",
    "kbk.......kbbbbbbbwk",
    ".kbk......kbbbbbwpk.",
    "..kbkkkkkkbkkkkkkk..",
    "..kbbdbbdbbbbbwwk...",
    "...kbbdbbdbbbbwwk...",
    "...kbbbbbbbbbbbk....",
    "....kbbkkkkkkbbk....",
    "...kbk.......kbk....",
    "...kwk......kwk.....",
};

static art walk_2 = {
    "....................",
    "....................",
    "............k..k....",
    "...........kpkkpk...",
    "k..........kbbbbbk..",
    "kk........kbbbbbbbk.",
    ".kk.......kbbbbbebk.",
    "..kk......kbbbbbbbwk",
    "..kk......kbbbbbwpk.",
    "..kbkkkkkkbkkkkkkk..",
    "..kbbdbbdbbbbbwwk...",
    "...kbbdbbdbbbbwwk...",
    "...kbbbbbbbbbbbk....",
    "....kbbkkkkkkbbk....",
    ".....kbk...kbk......",
    ".....kwk...kwk......",
};

static art run_1 = {
    "....................",
    "....................",
    "............k..k....",
    "...........kpkkpk...",
    "...........kbbbbbk..",
    "kkk.......kbbbbbbbk.",
    "..kk......kbbbbbebk.",
    "...kkkkkkkkbbbbbbbwk",
    "...kbbdbbdbbbbbbwpk.",
    "..kbbbdbbdbbkkkkkk..",
    "..kbbbbbbbbbbbwwk...",
    "...kbbbbbbbbbbwwk...",
    "....kkbbkkkkbbbk....",
    "....kbbk...kbbk.....",
    "...kbwk.....kwbk....",
    "...kk........kk.....",
};

static art run_2 = {
    "....................",
    "....................",
    "............k..k....",
    "k..........kpkkpk...",
    "kk.........kbbbbbk..",
    ".kk.......kbbbbbbbk.",
    "..kk......kbbbbbebk.",
    "..kkkkkkkkkbbbbbbbwk",
    ".kbbbdbbdbbbbbbbwpk.",
    "kbbbbdbbdbbbkkkkkk..",
    "kbbbbbbbbbbbbbwwk...",
    ".kkbbbkkkkkkbbbwwkk.",
    "..kbbk......kkbbbbwk",
    ".kbk..........kkkwk.",
    "kwk.................",
    ".k..................",
};

static art groom_1 = {
    "....................",
    "..........k..k......",
    ".........kpkkpk.....",
    ".........kbbbbbk....",
    "........kbbbbbbbk...",
    "........kbbbbbkbk...",
    "........kbbbbbbbwk..",
    "........kbbbbbwpkkk.",
    ".......kbkkkkkkkwwk.",
    "......kbbbbbwwkwwk..",
    ".....kbdbbbbwwkbk...",
    "....kbbbdbbbwbbk....",
    "....kbbdbbbkbbk.....",
    "k...kbbbbbbkbbk.....",
    "kbk.kbbbbbbkwwk.....",
    ".kkkkkkkkkkkkkk.....",
};

static art groom_2 = {
    "....................",
    "..........k..k......",
    ".........kpkkpk.....",
    ".........kbbbbbk....",
    "........kbbbbbbbk...",
    "........kbbbbbkbk...",
    "........kbbbbbbbwk..",
    "........kbbbbbwpkkk.",
    ".......kbkkkkkkpwwk.",
    "......kbbbbbwwkwwk..",
    ".....kbdbbbbwwkbk...",
    "....kbbbdbbbwbbk....",
    "....kbbdbbbkbbk.....",
    "k...kbbbbbbkbbk.....",
    "kbk.kbbbbbbkwwk.....",
    ".kkkkkkkkkkkkkk.....",
};

static art yawn = {
    "....................",
    "....k.........k.....",
    "...kpk.......kpk....",
    "...kppk.....kppk....",
    "...kbppkkkkkppbk....",
    "...kbbbbdbdbbbbk....",
    "..kbbbbbbdbbbbbbk...",
    "..kbkkkbbbbbkkkbk...",
    "..kbbbbbkkkbbbbbk...",
    "..kbbbbkpppkbbbbk...",
    "...kbbbkpppkbbbk....",
    "....kkbbkkkbbkk..kk.",
    "....kbbbwwwbbbk.kbbk",
    "...kbdbbwwwbbdbkkbk.",
    "...kbwwbwwwbwwbbbk..",
    "....kkkkkkkkkkkkk...",
};

static art sleep_1 = {
    "....................",
    "....................",
    "....................",
    "....................",
    "....................",
    "...........k..k.....",
    "..........kpkkpk....",
    "......kkkkkbbbbbk...",
    "....kkbbdbdbbbbbbk..",
    "...kbbbdbdbbbbbbbk..",
    "..kbbbbbbbbbbkkbwk..",
    "..kbbbbbbbbbbbbwpk..",
    "..kbbbbbbbbbkwwwk...",
    ".kbdkkkkkkkkkkkkkk..",
    ".kbbdbbdbbdbbdbbbbk.",
    "..kkkkkkkkkkkkkkkk..",
};

static art sleep_2 = {
    "....................",
    "....................",
    "....................",
    "....................",
    "....................",
    "...........k..k.....",
    ".......kkkkpkkpk....",
    ".....kkbbdbkbbbbk...",
    "....kbbbdbdbbbbbbk..",
    "...kbbbbbbbbbbbbbk..",
    "..kbbbbbbbbbbkkbwk..",
    "..kbbbbbbbbbbbbwpk..",
    "..kbbbbbbbbbkwwwk...",
    ".kbdkkkkkkkkkkkkkk..",
    ".kbbdbbdbbdbbdbbbbk.",
    "..kkkkkkkkkkkkkkkk..",
};

static art scratch_1 = {
    "....................",
    "..........k..k......",
    ".........kpkkpk.....",
    "......kk.kbbbbbk....",
    ".....kwwkbbbbbbbk...",
    ".....kbkkbbbbbkbk...",
    "......kbkbbbbbbbwk..",
    "......kbkbbbbbwpk...",
    "......kbkkkkkkkk....",
    "......kbbbbbwwk.....",
    ".....kbdbbbbwwk.....",
    "....kbbbdbbbwwk.....",
    "....kbbdbbbkbbk.....",
    "k...kbbbbbbkbbk.....",
    "kbk.kbbbbbbkwwk.....",
    ".kkkkkkkkkkkkkk.....",
};

static art scratch_2 = {
    "....................",
    "..........k..k......",
    ".........kpkkpk.....",
    ".........kbbbbbk....",
    "......kkkbbbbbbbk...",
    ".....kwwkbbbbbkbk...",
    ".....kbkkbbbbbbbwk..",
    "......kbkbbbbbwpk...",
    "......kbkkkkkkkk....",
    "......kbbbbbwwk.....",
    ".....kbdbbbbwwk.....",
    "....kbbbdbbbwwk.....",
    "....kbbdbbbkbbk.....",
    "k...kbbbbbbkbbk.....",
    "kbk.kbbbbbbkwwk.....",
    ".kkkkkkkkkkkkkk.....",
};

static art paw_1 = {
    "....................",
    ".....k.......k......",
    "....kpk.....kpk.....",
    "...kbppk...kppbk....",
    "...kbbbbkkkbbbbk....",
    "...kbbbbdbdbbbbk....",
    "..kbbbbbbdbbbbbbk...",
    "..kbbebbbbbbbebbk...",
    "..kbbebbbbbbbebbkkk.",
    "..kbbbbbwpwbbbbkwwk.",
    "...kbbbbwwwbbbkbbk..",
    "....kkbbbbbbbkbbk...",
    "....kbbbwwwbbbbk..kk",
    "...kbdbbwwwbbbbbkkbk",
    "...kbwwbwwwbbbbbbbk.",
    "....kkkkkkkkkkkkkk..",
};

static art paw_2 = {
    "....................",
    ".....k.......k......",
    "....kpk.....kpk.....",
    "...kbppk...kppbk....",
    "...kbbbbkkkbbbbk....",
    "...kbbbbdbdbbbbk....",
    "..kbbbbbbdbbbbbbk...",
    "..kbbebbbbbbbebbk...",
    "..kbbebbbbbbbebbk...",
    "..kbbbbbwpwbbbbbk...",
    "...kbbbbwwwbbbbk....",
    "....kkbbbbbbbkk..kk.",
    "....kbbbwwwbbbk.kbbk",
    "...kbdbbwwwbbbbkkbk.",
    "...kbwwbwwwbkwwwbk..",
    "....kkkkkkkkkkkkk...",
};

static art fall = {
    "....k.........k.....",
    "...kpk.......kpk....",
    "...kppk.....kppk....",
    "...kbppkkkkkppbk....",
    "...kbbbbbbbbbbbk....",
    "..kbbbdbbbbbdbbbk...",
    "..kbkebbbbbbbkebk...",
    "..kbkebbbbbbbkebk...",
    "..kbbbbbwpwbbbbbk...",
    "kk.kbbbbwkwbbbbk.kk.",
    "kwkkkbbbwwwbbbkkkwk.",
    ".kbbkbbbwwwbbbkbbk..",
    "..kkkbbbbbbbbbkkk...",
    "...kbbkkkkkkkbbk....",
    "...kwk.......kwk....",
    "....k.........k.....",
};

static art *const frames[SH_CAT_FRAMES] = {
    [SH_CAT_FRAME_SIT] = &sit,         [SH_CAT_FRAME_BLINK] = &blink,
    [SH_CAT_FRAME_ALERT] = &alert,     [SH_CAT_FRAME_LOOK] = &look,
    [SH_CAT_FRAME_WALK_1] = &walk_1,   [SH_CAT_FRAME_WALK_2] = &walk_2,
    [SH_CAT_FRAME_RUN_1] = &run_1,     [SH_CAT_FRAME_RUN_2] = &run_2,
    [SH_CAT_FRAME_GROOM_1] = &groom_1, [SH_CAT_FRAME_GROOM_2] = &groom_2,
    [SH_CAT_FRAME_YAWN] = &yawn,       [SH_CAT_FRAME_SLEEP_1] = &sleep_1,
    [SH_CAT_FRAME_SLEEP_2] = &sleep_2, [SH_CAT_FRAME_SCRATCH_1] = &scratch_1,
    [SH_CAT_FRAME_SCRATCH_2] = &scratch_2, [SH_CAT_FRAME_PAW_1] = &paw_1,
    [SH_CAT_FRAME_PAW_2] = &paw_2,     [SH_CAT_FRAME_FALL] = &fall,
};

static const char *const z_art[SH_CAT_Z_SIZE] = {
    "kkkkk",
    "...k.",
    "..k..",
    ".k...",
    "kkkkk",
};

/* A coat's colours, 0xRRGGBB, for the letters of the art. */
struct coat {
    uint32_t outline, fur, shade, white, pink, eye;
};
static const struct coat coats[SH_CAT_COATS] = {
    /* ginger */ {0x3a2216, 0xf2a04e, 0xd4762e, 0xfff3e2, 0xff95a6, 0x2b5d2a},
    /* ink */ {0x101018, 0x2c2c3a, 0x1f1f2b, 0x585870, 0xf28aa0, 0xf5cf3d},
    /* cream */ {0x5b4636, 0xfbf1e1, 0xe8d3b6, 0xffffff, 0xffa9b8, 0x3f86d6},
    /* grey */ {0x23262e, 0x9aa1ad, 0x6d7380, 0xf1f2f4, 0xff9eae, 0x3d8a3a},
};

static uint32_t colour(const struct coat *coat, char cell) {
    switch (cell) {
    case 'k':
        return 0xff000000 | coat->outline;
    case 'b':
        return 0xff000000 | coat->fur;
    case 'd':
        return 0xff000000 | coat->shade;
    case 'w':
        return 0xff000000 | coat->white;
    case 'p':
        return 0xff000000 | coat->pink;
    case 'e':
        return 0xff000000 | coat->eye;
    default:
        return 0;
    }
}

void sh_cat_paint(uint32_t *pixels, enum sh_cat_frame frame, enum sh_cat_coat coat) {
    art *picture = frames[(unsigned)frame < SH_CAT_FRAMES ? frame : SH_CAT_FRAME_SIT];
    const struct coat *colours = &coats[(unsigned)coat < SH_CAT_COATS ? coat : 0];
    for (int y = 0; y < SH_CAT_ART_HEIGHT; ++y)
        for (int x = 0; x < SH_CAT_ART_WIDTH; ++x)
            pixels[y * SH_CAT_ART_WIDTH + x] = colour(colours, (*picture)[y][x]);
}

void sh_cat_paint_z(uint32_t *pixels, enum sh_cat_coat coat) {
    const struct coat *colours = &coats[(unsigned)coat < SH_CAT_COATS ? coat : 0];
    for (int y = 0; y < SH_CAT_Z_SIZE; ++y)
        for (int x = 0; x < SH_CAT_Z_SIZE; ++x)
            pixels[y * SH_CAT_Z_SIZE + x] = colour(colours, z_art[y][x]);
}

const char *sh_cat_frame_name(enum sh_cat_frame frame) {
    static const char *const names[SH_CAT_FRAMES] = {
        "sit",     "blink",     "alert",     "look",  "walk_1", "walk_2",
        "run_1",   "run_2",     "groom_1",   "groom_2", "yawn", "sleep_1",
        "sleep_2", "scratch_1", "scratch_2", "paw_1", "paw_2",  "fall",
    };
    return (unsigned)frame < SH_CAT_FRAMES ? names[frame] : "sit";
}
