// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QImage>
#include <QSizeF>

// The blurred wallpaper behind the shell's frosted glass (shell.blur): `picture`, the wallpaper
// as an output `output` logical pixels large shows it (at any size, the output's proportions),
// blurred as a Gaussian of standard deviation radius / 2 of those pixels would, its brightness
// and saturation scaled. The picture it gives is smaller than the output, a pixel for several of
// its own: a blur leaves no detail finer than its spread, so drawn stretched over the output it
// shows what a blur at the output's size would, at a fraction of the work and memory. Opaque
// (RGB32); a null picture or an empty output gives a null image.
QImage blurWallpaper(const QImage &picture, QSizeF output, int radius, double brightness = 1,
                     double saturation = 1);

// How many of the output's logical pixels one pixel of blurWallpaper's picture stands for, at
// least 1: a quarter of the Gaussian's spread, or more on an output so large that the picture
// would pass a million pixels.
double blurStep(QSizeF output, int radius);
