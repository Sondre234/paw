// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
/* The desktop cat's pictures (src/compositor/cat.c shows them): pixel art, each frame a grid of
 * SH_CAT_ART_WIDTH by SH_CAT_ART_HEIGHT cells, facing right, its feet on the bottom row, painted
 * in one of a few coats. The compositor scales the cells up without smoothing, so the cat stays
 * crisp at any output scale. */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SH_CAT_ART_WIDTH = 20,
    SH_CAT_ART_HEIGHT = 16,
    SH_CAT_Z_SIZE = 5,
    SH_CAT_HEART_WIDTH = 7,
    SH_CAT_HEART_HEIGHT = 6,
};

enum sh_cat_frame {
    SH_CAT_FRAME_SIT,    /* sitting, facing out of the screen */
    SH_CAT_FRAME_BLINK,  /* the same with its eyes shut */
    SH_CAT_FRAME_ALERT,  /* ears up, eyes wide: it noticed something */
    SH_CAT_FRAME_LOOK,   /* sitting sideways, looking ahead */
    SH_CAT_FRAME_WALK_1, /* the two steps of a walk */
    SH_CAT_FRAME_WALK_2,
    SH_CAT_FRAME_RUN_1, /* the two strides of a run, the second one a leap */
    SH_CAT_FRAME_RUN_2,
    SH_CAT_FRAME_GROOM_1, /* licking a raised paw, tongue out and in */
    SH_CAT_FRAME_GROOM_2,
    SH_CAT_FRAME_YAWN,
    SH_CAT_FRAME_SLEEP_1, /* curled up, breathing in and out */
    SH_CAT_FRAME_SLEEP_2,
    SH_CAT_FRAME_PURR_1, /* stroked: eyes shut in content arcs, blushing, breathing in and out */
    SH_CAT_FRAME_PURR_2,
    SH_CAT_FRAME_SCRATCH_1, /* scratching behind an ear with a hind leg */
    SH_CAT_FRAME_SCRATCH_2,
    SH_CAT_FRAME_PAW_1, /* patting what it sits on, paw up and down */
    SH_CAT_FRAME_PAW_2,
    SH_CAT_FRAME_FALL, /* legs spread, falling */
    SH_CAT_FRAMES,
};

/* Its coats: a ginger tabby, a black cat with yellow eyes for light desktops, a cream one with
 * blue eyes for dark ones, and a grey tabby. */
enum sh_cat_coat {
    SH_CAT_COAT_GINGER,
    SH_CAT_COAT_INK,
    SH_CAT_COAT_CREAM,
    SH_CAT_COAT_GREY,
    SH_CAT_COATS,
};

/* Paints `frame` in `coat` into `pixels`, premultiplied ARGB (0xAARRGGBB) in rows of
 * SH_CAT_ART_WIDTH without padding, SH_CAT_ART_HEIGHT of them; cells outside the cat are
 * transparent. */
void sh_cat_paint(uint32_t *pixels, enum sh_cat_frame frame, enum sh_cat_coat coat);

/* Paints the "z" a sleeping cat breathes out, SH_CAT_Z_SIZE cells square, in `coat`'s outline. */
void sh_cat_paint_z(uint32_t *pixels, enum sh_cat_coat coat);

/* Paints a heart a purring cat sends up, SH_CAT_HEART_WIDTH by SH_CAT_HEART_HEIGHT cells, pink
 * in a rose outline with a glint, the same in every coat. */
void sh_cat_paint_heart(uint32_t *pixels);

/* The name of a frame, for `get cat`. */
const char *sh_cat_frame_name(enum sh_cat_frame frame);

#ifdef __cplusplus
}
#endif
