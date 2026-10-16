// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop_clock.hpp"
#include <QStringList>

void DesktopClock::configure(const paw::DesktopClockConfig &clock, const QColor &color) {
    clock_ = clock;
    color_ = color;
    if (override_ && configured_ != clock_.enabled)
        override_.reset();
    configured_ = clock_.enabled;
    Q_EMIT changed();
}

bool DesktopClock::handle(const QString &verb) {
    if (verb == "toggle")
        override_ = !shown();
    else if (verb == "on" || verb == "off")
        override_ = verb == "on";
    else
        return false;
    Q_EMIT changed();
    return true;
}

bool DesktopClock::shown() const { return override_.value_or(clock_.enabled); }

QString DesktopClock::greetingAt(int hour) const {
    const int part = hour >= 5 && hour < 12 ? 0 : hour >= 12 && hour < 18 ? 1 : 2;
    if (clock_.greeting == "ja")
        return QStringList{QString::fromUtf8("おはよう"), QString::fromUtf8("こんにちは"),
                           QString::fromUtf8("こんばんは")}[part];
    if (clock_.greeting == "en")
        return QStringList{"Good morning", "Good afternoon", "Good evening"}[part];
    return {};
}
