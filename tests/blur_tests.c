// SPDX-License-Identifier: GPL-3.0-or-later
/* The blur behind windows (src/blur.c): the grid it blurs on, the boxes that make the gaussian,
 * the picture painted from a background, and where a window shows a part of it. */
#include "paw/blur.h"
#include <stdlib.h>
#include <string.h>
#include "check.h"

static int red(uint32_t pixel) { return (int)(pixel >> 16 & 0xff); }
static int green(uint32_t pixel) { return (int)(pixel >> 8 & 0xff); }
static int blue(uint32_t pixel) { return (int)(pixel & 0xff); }

static const struct sh_blur_look PLAIN = {.sigma = 8, .noise = 0, .brightness = 1,
                                          .saturation = 1};

static uint32_t *picture(int width, int height, uint32_t colour) {
    uint32_t *pixels = malloc((size_t)width * height * sizeof(*pixels));
    for (int i = 0; i < width * height; ++i)
        pixels[i] = colour;
    return pixels;
}

static void test_grid(void) {
    CHECK(sh_blur_halvings(0) == 0 && sh_blur_halvings(5.9) == 0, "a small blur on the picture");
    CHECK(sh_blur_halvings(6) == 1, "halved once while the gaussian spans 3 cells");
    CHECK(sh_blur_halvings(15) == 2, "15 pixels: 3.75 cells of a quarter");
    CHECK(sh_blur_halvings(24) == 3 && sh_blur_halvings(23.9) == 2);
    CHECK(sh_blur_halvings(1e6) == 6, "never past 6 halvings");
}

static void test_boxes(void) {
    int widths[3];
    sh_blur_boxes(0, widths);
    CHECK(widths[0] == 1 && widths[1] == 1 && widths[2] == 1, "no blur: boxes of one");
    double sigmas[] = {1, 2.5, 3, 3.75, 7, 12.5, 40};
    for (size_t i = 0; i < sizeof(sigmas) / sizeof(*sigmas); ++i) {
        sh_blur_boxes(sigmas[i], widths);
        double variance = 0;
        for (int b = 0; b < 3; ++b) {
            CHECK(widths[b] % 2 == 1 && widths[b] >= 1, "odd widths, %d", widths[b]);
            variance += (widths[b] * widths[b] - 1) / 12.0;
        }
        double sigma = sqrt(variance);
        CHECK(fabs(sigma - sigmas[i]) <= 0.2 * sigmas[i] + 0.1, "sigma %g blurs as %g", sigmas[i],
              sigma);
    }
}

static void test_flat(void) {
    // A plain background stays exactly its colour, opaque, whatever the blur and the size.
    int sizes[][2] = {{1, 1}, {37, 23}, {200, 120}};
    double sigmas[] = {0, 1, 8, 30, 200};
    for (size_t s = 0; s < 3; ++s) {
        for (size_t g = 0; g < 5; ++g) {
            int width = sizes[s][0], height = sizes[s][1];
            uint32_t *in = picture(width, height, 0x00336699), *out = picture(width, height, 0);
            struct sh_blur_look look = PLAIN;
            look.sigma = sigmas[g];
            CHECK(sh_blur_paint(in, width, height, &look, out));
            bool same = true;
            for (int i = 0; i < width * height; ++i)
                same = same && out[i] == 0xff336699;
            CHECK(same, "%dx%d at sigma %g: %08x", width, height, sigmas[g], out[0]);
            free(in);
            free(out);
        }
    }
    CHECK(!sh_blur_paint(NULL, 0, 10, &PLAIN, NULL), "nothing to paint");
}

static void test_edge(void) {
    // Black on the left, white on the right: a smooth ramp across the middle, the far sides as
    // they were, and the same down every column of a row.
    enum { W = 160, H = 40 };
    uint32_t *in = picture(W, H, 0), *out = picture(W, H, 0);
    for (int y = 0; y < H; ++y)
        for (int x = W / 2; x < W; ++x)
            in[y * W + x] = 0xffffff;
    CHECK(sh_blur_paint(in, W, H, &PLAIN, out));
    int row = H / 2;
    CHECK(red(out[row * W]) <= 1 && red(out[row * W + W - 1]) >= 254,
          "the far sides keep their colour: %d, %d", red(out[row * W]),
          red(out[row * W + W - 1]));
    bool rising = true;
    for (int x = 1; x < W; ++x)
        rising = rising && red(out[row * W + x]) >= red(out[row * W + x - 1]);
    CHECK(rising, "the ramp only rises");
    int middle = red(out[row * W + W / 2]);
    CHECK(middle > 100 && middle < 156, "half way at the edge: %d", middle);
    // About two standard deviations out the ramp has nearly reached each side.
    CHECK(red(out[row * W + W / 2 - 20]) < 20 && red(out[row * W + W / 2 + 20]) > 235,
          "a gaussian of 8 pixels: %d, %d", red(out[row * W + W / 2 - 20]),
          red(out[row * W + W / 2 + 20]));
    CHECK(red(out[row * W + W / 2 - 4]) > 10 && red(out[row * W + W / 2 + 4]) < 245,
          "and spread over several pixels");
    bool level = true;
    for (int y = 0; y < H; ++y)
        level = level && out[y * W + W / 2 + 3] == out[row * W + W / 2 + 3];
    CHECK(level, "the top and bottom edges do not darken");
    free(in);
    free(out);
}

static void test_colour(void) {
    enum { W = 16, H = 16 };
    uint32_t *in = picture(W, H, 0x00c86432), *out = picture(W, H, 0);
    struct sh_blur_look look = PLAIN;
    look.brightness = 0.5f;
    CHECK(sh_blur_paint(in, W, H, &look, out));
    CHECK(red(out[0]) == 100 && green(out[0]) == 50 && blue(out[0]) == 25, "half as bright: %08x",
          out[0]);
    look = PLAIN;
    look.saturation = 0;
    CHECK(sh_blur_paint(in, W, H, &look, out));
    CHECK(red(out[0]) == green(out[0]) && green(out[0]) == blue(out[0]), "grey: %08x", out[0]);
    look.saturation = 1.5f;
    CHECK(sh_blur_paint(in, W, H, &look, out));
    CHECK(red(out[0]) > 0xc8 && blue(out[0]) < 0x32, "more vivid: %08x", out[0]);
    look = PLAIN;
    look.brightness = 2;
    CHECK(sh_blur_paint(in, W, H, &look, out));
    CHECK(red(out[0]) == 255 && green(out[0]) == 200, "brighter, held at white: %08x", out[0]);
    free(in);
    free(out);
}

static void test_noise(void) {
    enum { W = 64, H = 64 };
    uint32_t *in = picture(W, H, 0x00202020), *out = picture(W, H, 0), *again = picture(W, H, 0);
    struct sh_blur_look look = PLAIN;
    look.noise = 0.04f; // up to 0.02 of full brightness: about 5 levels
    CHECK(sh_blur_paint(in, W, H, &look, out) && sh_blur_paint(in, W, H, &look, again));
    CHECK(!memcmp(out, again, sizeof(uint32_t) * W * H), "the same grain every time");
    int low = 255, high = 0;
    long sum = 0;
    bool grey = true;
    for (int i = 0; i < W * H; ++i) {
        int value = red(out[i]);
        low = value < low ? value : low;
        high = value > high ? value : high;
        sum += value;
        grey = grey && red(out[i]) == green(out[i]) && green(out[i]) == blue(out[i]);
    }
    CHECK(low >= 0x20 - 6 && high <= 0x20 + 6 && high - low >= 6, "grain of a few levels: %d to %d",
          low, high);
    CHECK(fabs((double)sum / (W * H) - 0x20) < 0.5, "as light on the whole");
    CHECK(grey, "the grain does not tint");
    free(in);
    free(out);
    free(again);
}

static void test_source(void) {
    struct sh_blur_box output = {1920, 0, 1280, 800}, part;
    struct sh_blur_area source;
    // A picture of half the pixels at scale 2: as many pixels as logical pixels.
    CHECK(sh_blur_source((struct sh_blur_box){2020, 50, 400, 300}, output, 1280, 800, &part,
                         &source));
    CHECK(part.x == 2020 && part.y == 50 && part.width == 400 && part.height == 300);
    CHECK(source.x == 100 && source.y == 50 && source.width == 400 && source.height == 300);
    // A quarter of the pixels at scale 1: half a picture pixel per logical one.
    CHECK(sh_blur_source((struct sh_blur_box){2020, 50, 400, 300}, output, 640, 400, &part,
                         &source));
    CHECK(source.x == 50 && source.y == 25 && source.width == 200 && source.height == 150);
    // Hanging off the left into another output: only the part on this one.
    CHECK(sh_blur_source((struct sh_blur_box){1800, 700, 400, 300}, output, 640, 400, &part,
                         &source));
    CHECK(part.x == 1920 && part.y == 700 && part.width == 280 && part.height == 100,
          "%d,%d %dx%d", part.x, part.y, part.width, part.height);
    CHECK(source.x == 0 && source.y == 350 && source.width == 140 && source.height == 50);
    // Never past the picture's edges, whatever the rounding.
    CHECK(sh_blur_source((struct sh_blur_box){1920, 0, 1280, 800}, output, 427, 267, &part,
                         &source));
    CHECK(source.x == 0 && source.y == 0 && source.x + source.width <= 427 &&
          source.y + source.height <= 267 && source.width > 426.9);
    CHECK(!sh_blur_source((struct sh_blur_box){0, 0, 400, 300}, output, 640, 400, &part, &source),
          "wholly on another output");
    CHECK(!sh_blur_source((struct sh_blur_box){1920, 0, 0, 300}, output, 640, 400, &part,
                          &source),
          "no size");
    CHECK(!sh_blur_source((struct sh_blur_box){1920, 0, 10, 10}, output, 0, 400, &part, &source),
          "no picture");
}

int main(void) {
    test_grid();
    test_boxes();
    test_flat();
    test_edge();
    test_colour();
    test_noise();
    test_source();
    if (!failures)
        puts("Blur passed");
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
