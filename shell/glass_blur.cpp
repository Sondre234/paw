// SPDX-License-Identifier: GPL-3.0-or-later
#include "glass_blur.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace {
// The picture's pixels at most: a 4K output's blur takes a pixel for three of its own.
constexpr double maxPixels = 1e6;

// The widths of three box blurs that together come close to a Gaussian of `sigma` (Kovesi,
// "Fast almost-Gaussian filtering"): odd widths, the narrower ones first.
std::array<int, 3> boxWidths(double sigma) {
    constexpr int passes = 3;
    const double ideal = std::sqrt(12 * sigma * sigma / passes + 1);
    int lower = static_cast<int>(std::floor(ideal));
    if (lower % 2 == 0)
        --lower;
    lower = std::max(lower, 1);
    const int upper = lower + 2;
    const double narrow =
        (12 * sigma * sigma - passes * lower * lower - 4.0 * passes * lower - 3.0 * passes) /
        (-4.0 * lower - 4);
    const int count = std::clamp(static_cast<int>(std::lround(narrow)), 0, passes);
    std::array<int, 3> widths{};
    for (int i = 0; i < passes; ++i)
        widths[i] = i < count ? lower : upper;
    return widths;
}

// One box blur of `radius` along a line of `length` values `stride` apart, from `from` into
// `to`, the edge values standing in for those past the ends, so that the edges keep their
// brightness.
void boxLine(const float *from, float *to, int length, int stride, int radius) {
    if (radius <= 0) {
        for (int i = 0; i < length; ++i)
            to[i * stride] = from[i * stride];
        return;
    }
    const auto at = [&](int i) { return from[std::clamp(i, 0, length - 1) * stride]; };
    const float scale = 1.0F / static_cast<float>(2 * radius + 1);
    float sum = 0;
    for (int i = -radius; i <= radius; ++i)
        sum += at(i);
    for (int i = 0; i < length; ++i) {
        to[i * stride] = sum * scale;
        sum += at(i + radius + 1) - at(i - radius);
    }
}
} // namespace

double blurStep(QSizeF output, int radius) {
    const double sigma = std::max(1, radius) / 2.0;
    const double area = std::max(0.0, output.width() * output.height());
    return std::max({1.0, sigma / 4, std::sqrt(area / maxPixels)});
}

QImage blurWallpaper(const QImage &picture, QSizeF output, int radius, double brightness,
                     double saturation) {
    if (picture.isNull() || output.isEmpty())
        return {};
    const double step = blurStep(output, radius);
    const int width = std::max(1, static_cast<int>(std::ceil(output.width() / step)));
    const int height = std::max(1, static_cast<int>(std::ceil(output.height() / step)));
    // Down to the size the blur works at, each pixel the average of those it covers.
    const QImage small =
        picture.convertToFormat(QImage::Format_RGB32)
            .scaled(width, height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const size_t count = static_cast<size_t>(width) * height;
    std::array<std::vector<float>, 3> channels;
    for (auto &channel : channels)
        channel.resize(count);
    for (int y = 0; y < height; ++y) {
        const auto *row = reinterpret_cast<const QRgb *>(small.constScanLine(y));
        for (int x = 0; x < width; ++x) {
            const size_t i = static_cast<size_t>(y) * width + x;
            channels[0][i] = static_cast<float>(qRed(row[x]));
            channels[1][i] = static_cast<float>(qGreen(row[x]));
            channels[2][i] = static_cast<float>(qBlue(row[x]));
        }
    }
    // Three box blurs across, then three down, at the picture's scale.
    const auto widths = boxWidths(radius / 2.0 / step);
    std::vector<float> scratch(count);
    for (auto &channel : channels) {
        for (int across : widths) {
            const int box = (across - 1) / 2;
            for (int y = 0; y < height; ++y)
                boxLine(channel.data() + static_cast<size_t>(y) * width,
                        scratch.data() + static_cast<size_t>(y) * width, width, 1, box);
            channel.swap(scratch);
            for (int x = 0; x < width; ++x)
                boxLine(channel.data() + x, scratch.data() + x, height, width, box);
            channel.swap(scratch);
        }
    }
    QImage result(width, height, QImage::Format_RGB32);
    const auto level = [](double value) {
        return static_cast<int>(std::lround(std::clamp(value, 0.0, 255.0)));
    };
    for (int y = 0; y < height; ++y) {
        auto *row = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < width; ++x) {
            const size_t i = static_cast<size_t>(y) * width + x;
            const double r = channels[0][i], g = channels[1][i], b = channels[2][i];
            // Saturation about the pixel's luma (Rec. 709's weights), then the brightness.
            const double luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            const auto tone = [&](double c) {
                return (luma + (c - luma) * saturation) * brightness;
            };
            row[x] = qRgb(level(tone(r)), level(tone(g)), level(tone(b)));
        }
    }
    return result;
}
