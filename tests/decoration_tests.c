// SPDX-License-Identifier: GPL-3.0-or-later
/* What the compositor draws for a window, as pixels: the window controls in both styles, and
 * shadows put together from slices. */
#include "paw/decoration.h"
#include "paw/shadow.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/interfaces/wlr_buffer.h>
#include "check.h"

static int alpha(uint32_t pixel) { return (int)(pixel >> 24); }
static int red(uint32_t pixel) { return (int)((pixel >> 16) & 0xff); }
static int green(uint32_t pixel) { return (int)((pixel >> 8) & 0xff); }
static int blue(uint32_t pixel) { return (int)(pixel & 0xff); }
static int brightness(uint32_t pixel) { return red(pixel) + green(pixel) + blue(pixel); }

/* The controls painted with `look` at `scale`, malloc'd. */
static uint32_t *paint(const struct sh_deco_look *look, int scale, int *width) {
    int w, h;
    sh_decoration_size(look->style, &w, &h);
    uint32_t *pixels = calloc((size_t)w * scale * h * scale, sizeof(*pixels));
    sh_decoration_paint(pixels, scale, look);
    *width = w * scale;
    return pixels;
}

/* The pixel at logical (x, y), taking the one whose top-left corner is there. */
static uint32_t at(const uint32_t *pixels, int width, int scale, double x, double y) {
    return pixels[(int)(y * scale) * width + (int)(x * scale)];
}

static void flat_strip(void) {
    int w, h;
    sh_decoration_size(SH_DECO_FLAT, &w, &h);
    CHECK(w == SH_DECO_WIDTH && h == SH_DECO_HEIGHT, "flat strip is %dx%d", w, h);
    // Minimize, fullscreen, close from the left, the whole strip shared out.
    CHECK(sh_decoration_part_at(SH_DECO_FLAT, 1, 1) == SH_DECO_MINIMIZE, "left third");
    CHECK(sh_decoration_part_at(SH_DECO_FLAT, 48, 14) == SH_DECO_FULLSCREEN, "middle third");
    CHECK(sh_decoration_part_at(SH_DECO_FLAT, 95, 27) == SH_DECO_CLOSE, "right third");
    CHECK(sh_decoration_part_at(SH_DECO_FLAT, -1, 14) == SH_DECO_NONE &&
              sh_decoration_part_at(SH_DECO_FLAT, 96, 14) == SH_DECO_NONE &&
              sh_decoration_part_at(SH_DECO_FLAT, 50, 28) == SH_DECO_NONE,
          "outside the strip");
    for (int scale = 1; scale <= 2; ++scale) {
        int width;
        struct sh_deco_look look = {SH_DECO_FLAT, true, SH_DECO_NONE, SH_DECO_NONE};
        uint32_t *plain = paint(&look, scale, &width);
        look.hovered = SH_DECO_CLOSE;
        uint32_t *lit = paint(&look, scale, &width);
        // A dark translucent strip with rounded corners; a hovered close button turns red.
        uint32_t background = at(plain, width, scale, 72, 3);
        CHECK(alpha(background) > 200 && alpha(background) < 230 && brightness(background) < 100,
              "strip background %08x at scale %d", background, scale);
        CHECK(alpha(at(plain, width, scale, 0, 0)) == 0, "corner of the strip painted");
        uint32_t close = at(lit, width, scale, 72, 3);
        CHECK(red(close) > 200 && green(close) < 80 && alpha(close) == 255,
              "hovered close %08x at scale %d", close, scale);
        // Focus and a press leave the flat strip as it is.
        look.focused = false;
        look.pressed = SH_DECO_CLOSE;
        uint32_t *other = paint(&look, scale, &width);
        CHECK(!memcmp(other, lit, sizeof(*lit) * width * SH_DECO_HEIGHT * scale),
              "flat strip changed with focus or a press");
        free(plain);
        free(lit);
        free(other);
    }
}

static const struct {
    enum sh_deco_part part;
    int r, g, b;
} lights[3] = {
    {SH_DECO_CLOSE, 0xff, 0x5f, 0x57},
    {SH_DECO_MINIMIZE, 0xfe, 0xbc, 0x2e},
    {SH_DECO_FULLSCREEN, 0x28, 0xc8, 0x40},
};

static void traffic_lights(void) {
    int w, h;
    sh_decoration_size(SH_DECO_TRAFFIC_LIGHTS, &w, &h);
    CHECK(w == SH_LIGHTS_WIDTH && h == SH_LIGHTS_HEIGHT, "traffic lights are %dx%d", w, h);
    // Close, minimize, fullscreen from the left; each takes the pointer on and just around its
    // circle, and halfway to the next.
    for (int i = 0; i < 3; ++i) {
        double cx = SH_LIGHTS_INSET + i * SH_LIGHTS_SPACING, cy = SH_LIGHTS_INSET;
        CHECK(sh_decoration_part_at(SH_DECO_TRAFFIC_LIGHTS, cx, cy) == lights[i].part,
              "light %d not at its centre", i);
        CHECK(sh_decoration_part_at(SH_DECO_TRAFFIC_LIGHTS, cx + 7, cy - 7) == lights[i].part,
              "light %d not just around its circle", i);
    }
    CHECK(sh_decoration_part_at(SH_DECO_TRAFFIC_LIGHTS, 23.9, 14) == SH_DECO_CLOSE &&
              sh_decoration_part_at(SH_DECO_TRAFFIC_LIGHTS, 24.1, 14) == SH_DECO_MINIMIZE,
          "the gap between two lights is not split at its middle");
    CHECK(sh_decoration_part_at(SH_DECO_TRAFFIC_LIGHTS, 14, 24) == SH_DECO_NONE &&
              sh_decoration_part_at(SH_DECO_TRAFFIC_LIGHTS, 14, 3) == SH_DECO_NONE &&
              sh_decoration_part_at(SH_DECO_TRAFFIC_LIGHTS, 3, 14) == SH_DECO_NONE &&
              sh_decoration_part_at(SH_DECO_TRAFFIC_LIGHTS, 65, 14) == SH_DECO_NONE,
          "the lights take the pointer far from their circles");

    for (int scale = 1; scale <= 2; ++scale) {
        int width;
        struct sh_deco_look look = {SH_DECO_TRAFFIC_LIGHTS, true, SH_DECO_NONE, SH_DECO_NONE};
        uint32_t *focused = paint(&look, scale, &width);
        look.focused = false;
        uint32_t *unfocused = paint(&look, scale, &width);
        look.hovered = SH_DECO_MINIMIZE;
        uint32_t *hovered = paint(&look, scale, &width);
        look.pressed = SH_DECO_MINIMIZE;
        uint32_t *pressed = paint(&look, scale, &width);
        for (int i = 0; i < 3; ++i) {
            double cx = SH_LIGHTS_INSET + i * SH_LIGHTS_SPACING, cy = SH_LIGHTS_INSET;
            // Their colours while focused, opaque; a point off the symbol stands for the fill.
            uint32_t fill = at(focused, width, scale, cx + 3, cy - 2);
            CHECK(alpha(fill) == 255 && abs(red(fill) - lights[i].r) <= 2 &&
                      abs(green(fill) - lights[i].g) <= 2 && abs(blue(fill) - lights[i].b) <= 2,
                  "light %d is %08x at scale %d", i, fill, scale);
            // Grey and translucent without focus.
            uint32_t grey = at(unfocused, width, scale, cx + 3, cy - 2);
            CHECK(alpha(grey) > 60 && alpha(grey) < 200 && abs(red(grey) - green(grey)) <= 3 &&
                      abs(green(grey) - blue(grey)) <= 6,
                  "unfocused light %d is %08x at scale %d", i, grey, scale);
            // Hovering one colours all three and shows their symbols: the middle of each is
            // darker than its fill, except fullscreen's, whose triangles leave the centre clear.
            uint32_t lit = at(hovered, width, scale, cx + 3, cy - 2);
            CHECK(abs(red(lit) - lights[i].r) <= 2 && abs(green(lit) - lights[i].g) <= 2,
                  "hovered light %d is %08x at scale %d", i, lit, scale);
            double gx = lights[i].part == SH_DECO_FULLSCREEN ? cx - 2 : cx;
            double gy = lights[i].part == SH_DECO_FULLSCREEN ? cy - 2 : cy;
            CHECK(brightness(at(hovered, width, scale, gx, gy)) < brightness(lit) - 150,
                  "light %d shows no symbol at scale %d", i, scale);
            CHECK(brightness(at(focused, width, scale, gx, gy)) > brightness(lit) - 30,
                  "light %d shows its symbol unhovered at scale %d", i, scale);
            // Only the pressed one darkens.
            uint32_t held = at(pressed, width, scale, cx + 3, cy - 2);
            if (lights[i].part == SH_DECO_MINIMIZE)
                CHECK(brightness(held) < brightness(lit) - 60, "pressed light not darker");
            else
                CHECK(held == lit, "light %d changed by another's press", i);
        }
        // Around and between the circles nothing is drawn.
        for (double x = 0; x < SH_LIGHTS_WIDTH; x += 0.5) {
            CHECK(alpha(at(focused, width, scale, x, 1)) == 0, "drawn above the lights");
            CHECK(alpha(at(focused, width, scale, x, SH_LIGHTS_HEIGHT - 1)) == 0,
                  "drawn below the lights");
        }
        CHECK(alpha(at(focused, width, scale, 24, 14)) == 0 &&
                  alpha(at(focused, width, scale, 44, 14)) == 0 &&
                  alpha(at(focused, width, scale, 66, 14)) == 0,
              "drawn between or after the lights at scale %d", scale);
        free(focused);
        free(unfocused);
        free(hovered);
        free(pressed);
    }
}

/* A rendered buffer holds what painting gives, at the scale asked for. */
static void rendered_buffer(void) {
    struct sh_deco_look look = {SH_DECO_TRAFFIC_LIGHTS, true, SH_DECO_CLOSE, SH_DECO_CLOSE};
    struct wlr_buffer *buffer = sh_decoration_render(2, &look);
    CHECK(buffer && buffer->width == 2 * SH_LIGHTS_WIDTH && buffer->height == 2 * SH_LIGHTS_HEIGHT,
          "buffer size");
    if (!buffer)
        return;
    int width;
    uint32_t *expected = paint(&look, 2, &width);
    void *data;
    uint32_t format;
    size_t stride;
    CHECK(wlr_buffer_begin_data_ptr_access(buffer, WLR_BUFFER_DATA_PTR_ACCESS_READ, &data, &format,
                                           &stride),
          "buffer not readable");
    CHECK(stride == (size_t)width * 4 &&
              !memcmp(data, expected, stride * (size_t)buffer->height),
          "buffer differs from the painted pixels");
    wlr_buffer_end_data_ptr_access(buffer);
    wlr_buffer_drop(buffer);
    free(expected);
}

/* The alpha of a painted shadow at device pixel (x, y) of a window's image, 0 outside it. */
static int shadow_alpha(const uint32_t *pixels, int width, int height, int x, int y) {
    return x < 0 || y < 0 || x >= width || y >= height ? 0 : alpha(pixels[y * width + x]);
}

/* Draws the shadow of a window of `width` by `height` from its slices, as the compositor does,
 * and compares it with the shadow painted for that size whole: they match, the slices never
 * overlap, and they cover all around the window. */
static void shadow_slices_match(const struct sh_shadow *shadow, int width, int height, int scale) {
    struct sh_shadow_layout l;
    sh_shadow_layout(shadow, &l);
    int image_width, image_height;
    sh_shadow_image_size(&l, width, height, &image_width, &image_height);
    int iw = (l.left + image_width + l.right) * scale, ih = (l.top + image_height + l.bottom) * scale;
    int fw = (l.left + width + l.right) * scale, fh = (l.top + height + l.bottom) * scale;
    uint32_t *image = calloc((size_t)iw * ih, sizeof(*image));
    uint32_t *whole = calloc((size_t)fw * fh, sizeof(*whole));
    int *drawn = calloc((size_t)fw * fh, sizeof(*drawn)), *covered = calloc((size_t)fw * fh, sizeof(*covered));
    CHECK(sh_shadow_paint(image, shadow, image_width, image_height, scale) &&
              sh_shadow_paint(whole, shadow, width, height, scale),
          "painting failed");
    struct sh_shadow_slice slices[SH_SHADOW_SLICES];
    sh_shadow_slices(&l, image_width, image_height, width, height, slices);
    int outside = 0;
    for (int i = 0; i < SH_SHADOW_SLICES; ++i) {
        const struct sh_shadow_slice *s = &slices[i];
        for (int dy = 0; dy < s->to_height * scale; ++dy) {
            for (int dx = 0; dx < s->to_width * scale; ++dx) {
                int sx = s->x * scale + (int)((long long)dx * s->width / s->to_width);
                int sy = s->y * scale + (int)((long long)dy * s->height / s->to_height);
                int x = (l.left + s->to_x) * scale + dx, y = (l.top + s->to_y) * scale + dy;
                if (x < 0 || y < 0 || x >= fw || y >= fh || sx >= iw || sy >= ih) {
                    ++outside;
                    continue;
                }
                drawn[y * fw + x] = alpha(image[sy * iw + sx]);
                ++covered[y * fw + x];
            }
        }
    }
    CHECK(outside == 0, "%dx%d at scale %d: slices reach past the images", width, height, scale);
    int worst = 0, overlaps = 0, holes = 0;
    for (int y = 0; y < fh; ++y) {
        for (int x = 0; x < fw; ++x) {
            int c = covered[y * fw + x], expected = alpha(whole[y * fw + x]);
            bool inside = x >= l.left * scale && x < (l.left + width) * scale &&
                          y >= l.top * scale && y < (l.top + height) * scale;
            overlaps += c > 1;
            holes += !inside && c == 0;
            int difference = abs(drawn[y * fw + x] - expected); // 0 where no slice is drawn
            if (difference > worst)
                worst = difference;
        }
    }
    CHECK(overlaps == 0 && holes == 0, "%dx%d at scale %d: %d pixels overlap, %d left out",
          width, height, scale, overlaps, holes);
    CHECK(worst <= 1, "%dx%d at scale %d: slices differ from the whole by %d", width, height,
          scale, worst);
    free(image);
    free(whole);
    free(drawn);
    free(covered);
}

static void shadows(void) {
    struct sh_shadow shadow = {.radius = 10, .blur = 30, .offset_x = 0, .offset_y = 10,
                               .color = {0, 0, 0, 0.35F}};
    struct sh_shadow_layout l;
    sh_shadow_layout(&shadow, &l);
    // It reaches past the blur's 1.5 sigma on each side, further down than up by twice the
    // offset; the image to slice holds both corners and a column and row between them.
    CHECK(l.left == l.right && l.left > 45 && l.bottom - l.top == 20, "reach %d %d %d %d",
          l.left, l.top, l.right, l.bottom);
    CHECK(l.width == l.inner_left + l.inner_right + 1 &&
              l.height == l.inner_top + l.inner_bottom + 1 && l.inner_left >= shadow.radius + 45,
          "corners %d %d %d %d", l.inner_left, l.inner_top, l.inner_right, l.inner_bottom);
    int image_width, image_height;
    sh_shadow_image_size(&l, 800, 600, &image_width, &image_height);
    CHECK(image_width == l.width && image_height == l.height, "a large window paints anew");
    sh_shadow_image_size(&l, 60, 600, &image_width, &image_height);
    CHECK(image_width == 60 && image_height == l.height, "a narrow window shares an image");

    for (int scale = 1; scale <= 2; ++scale) {
        // Large, just large enough, and too small along one side or both.
        shadow_slices_match(&shadow, 400, 250, scale);
        shadow_slices_match(&shadow, l.inner_left + l.inner_right, l.height, scale);
        shadow_slices_match(&shadow, 300, 70, scale);
        shadow_slices_match(&shadow, 40, 30, scale);
    }
    struct sh_shadow sideways = {.radius = 0, .blur = 12, .offset_x = -20, .offset_y = 4,
                                 .color = {0.5F, 0, 0, 0.5F}};
    shadow_slices_match(&sideways, 200, 150, 1);
    struct sh_shadow hard = {.radius = 6, .blur = 0, .offset_x = 0, .offset_y = 3,
                             .color = {0, 0, 0, 1}};
    shadow_slices_match(&hard, 100, 80, 2);

    for (int scale = 1; scale <= 2; ++scale) {
        int width = 300, height = 200;
        int fw = (l.left + width + l.right) * scale, fh = (l.top + height + l.bottom) * scale;
        uint32_t *pixels = calloc((size_t)fw * fh, sizeof(*pixels));
        sh_shadow_paint(pixels, &shadow, width, height, scale);
        int cx = (l.left + width / 2) * scale; // the middle of the window, across
        int top = l.top * scale, bottom = (l.top + height) * scale;
        // Cut out under the window, darkest just below it and never past its color.
        CHECK(shadow_alpha(pixels, fw, fh, cx, (top + bottom) / 2) == 0 &&
                  shadow_alpha(pixels, fw, fh, (l.left + 10) * scale, top + 1) == 0,
              "shadow under the window");
        int below = shadow_alpha(pixels, fw, fh, cx, bottom), above = shadow_alpha(pixels, fw, fh, cx, top - 1);
        CHECK(below > above && below <= 90 && below > 40, "below %d, above %d", below, above);
        // It fades out smoothly: every step away from the window lighter, by little at a time,
        // and nothing at the image's edges.
        int steps = 0;
        for (int y = bottom; y + 1 < fh; ++y) {
            int a = shadow_alpha(pixels, fw, fh, cx, y), b = shadow_alpha(pixels, fw, fh, cx, y + 1);
            steps += a < b || a - b > 3;
        }
        for (int x = (l.left + width) * scale; x + 1 < fw; ++x) {
            int a = shadow_alpha(pixels, fw, fh, x, fh / 2), b = shadow_alpha(pixels, fw, fh, x + 1, fh / 2);
            steps += a < b || a - b > 3;
        }
        CHECK(steps == 0, "%d rough steps fading out at scale %d", steps, scale);
        int rim = 0;
        for (int x = 0; x < fw; ++x)
            rim += shadow_alpha(pixels, fw, fh, x, 0) + shadow_alpha(pixels, fw, fh, x, fh - 1);
        for (int y = 0; y < fh; ++y)
            rim += shadow_alpha(pixels, fw, fh, 0, y) + shadow_alpha(pixels, fw, fh, fw - 1, y);
        CHECK(rim == 0, "the shadow is cut off at the image's edge at scale %d", scale);
        // The corner follows the window's: just outside its curve the shadow shows, where
        // a square window would cover it.
        int corner = shadow_alpha(pixels, fw, fh, (l.left + width) * scale - 2, bottom - 2);
        CHECK(corner > 20, "nothing outside the rounded corner at scale %d (%d)", scale, corner);
        free(pixels);
    }
}

int main(void) {
    flat_strip();
    traffic_lights();
    rendered_buffer();
    shadows();
    if (failures) {
        fprintf(stderr, "%d decoration checks failed\n", failures);
        return EXIT_FAILURE;
    }
    printf("Window controls and shadows passed\n");
    return EXIT_SUCCESS;
}
