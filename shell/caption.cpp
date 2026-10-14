// SPDX-License-Identifier: GPL-3.0-or-later
#include "caption.hpp"
#include <algorithm>

Caption::Caption(QObject *parent) : QObject(parent) {
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, &Caption::hide);
}
void Caption::show(const QString &output, const QString &title, const QString &body,
                   const QString &keys, int milliseconds) {
    output_ = output;
    title_ = title.left(100);
    body_ = body.left(300);
    keys_ = keys.left(100);
    active_ = true;
    timer_.start(std::clamp(milliseconds, 1, 600000));
    Q_EMIT changed();
}
bool Caption::showLine(const QString &line, const QString &fallback) {
    const auto first = line.indexOf(' '), second = line.indexOf(' ', first + 1);
    if (first <= 0 || second <= first + 1)
        return false;
    bool ok = false;
    const int milliseconds = line.mid(first + 1, second - first - 1).toInt(&ok);
    const auto fields = line.mid(second + 1).split('\t');
    if (!ok || milliseconds <= 0 || fields[0].trimmed().isEmpty())
        return false;
    const auto output = line.left(first);
    show(output == "-" ? fallback : output, fields[0].trimmed(), fields.value(1).trimmed(),
         fields.value(2).trimmed(), milliseconds);
    return true;
}
void Caption::hide() {
    timer_.stop();
    if (!active_)
        return;
    active_ = false;
    Q_EMIT changed();
}
