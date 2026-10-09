// SPDX-License-Identifier: GPL-3.0-or-later
#include "osd.hpp"
#include <algorithm>

Osd::Osd(QObject *parent) : QObject(parent) {
    timer_.setSingleShot(true);
    connect(&timer_, &QTimer::timeout, this, &Osd::hide);
}
void Osd::configure(const paw::OsdConfig &config) {
    config_ = config;
    if (!config_.enabled)
        hide();
    Q_EMIT configChanged();
}
void Osd::show(const QString &output, const QString &text, int percent, const QString &kind) {
    if (!config_.enabled)
        return;
    output_ = output;
    text_ = text.left(200);
    percent_ = percent < 0 ? -1 : std::min(percent, 100);
    kind_ = kind;
    active_ = true;
    timer_.start(config_.timeout);
    Q_EMIT changed();
}
void Osd::hide() {
    timer_.stop();
    if (!active_)
        return;
    active_ = false;
    Q_EMIT changed();
}
