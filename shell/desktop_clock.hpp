// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "paw/config.hpp"
#include <QColor>
#include <QObject>
#include <QString>
#include <optional>

// The desktop clock (shell.desktop_clock, experimental): what the configuration says of it, its
// colour and font resolved against the shell's, and whether it shows now. That is the
// configuration's `enabled` until the `desktop_clock` action turns it on or off, which holds until
// the configuration's own `enabled` changes. DesktopClock.qml draws it on each desktop.
class DesktopClock : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool shown READ shown NOTIFY changed)
    // "center", "top", "bottom", "left", "right", "top-left", "top-right", "bottom-left" or
    // "bottom-right".
    Q_PROPERTY(QString position READ position NOTIFY changed)
    // Qt's date and time formats; an empty dateFormat shows no date.
    Q_PROPERTY(QString timeFormat READ timeFormat NOTIFY changed)
    Q_PROPERTY(QString dateFormat READ dateFormat NOTIFY changed)
    // The time's height in pixels; the date and the greeting are drawn in proportion.
    Q_PROPERTY(int size READ size NOTIFY changed)
    // Its colour, shell.text_color unless it sets one, and its font family, "" for the shell's.
    Q_PROPERTY(QColor color READ color NOTIFY changed)
    Q_PROPERTY(QString font READ font NOTIFY changed)
    // The greeting's language, "en" or "ja", or "" for none.
    Q_PROPERTY(QString greeting READ greeting NOTIFY changed)
  public:
    explicit DesktopClock(QObject *parent = nullptr) : QObject(parent) {}
    // From each configuration loaded, with the colour it is drawn in: its own, else
    // shell.text_color.
    void configure(const paw::DesktopClockConfig &clock, const QColor &color);
    // "toggle", "on" or "off", as the compositor's `desktop-clock` line has it; false, changing
    // nothing, for anything else.
    bool handle(const QString &verb);
    bool shown() const;
    QString position() const { return QString::fromStdString(clock_.position); }
    QString timeFormat() const { return QString::fromStdString(clock_.time_format); }
    QString dateFormat() const { return QString::fromStdString(clock_.date_format); }
    int size() const { return clock_.size; }
    QColor color() const { return color_; }
    QString font() const { return QString::fromStdString(clock_.font); }
    QString greeting() const { return QString::fromStdString(clock_.greeting); }
    // What it greets with at `hour` (0 to 23) of the day: good morning from 5, good afternoon
    // from 12, good evening from 18; "" without a greeting.
    Q_INVOKABLE QString greetingAt(int hour) const;
  Q_SIGNALS:
    void changed();

  private:
    paw::DesktopClockConfig clock_;
    QColor color_;
    // Turned on or off by the action, while the configuration's `enabled` stays `configured_`.
    std::optional<bool> override_;
    bool configured_ = false;
};
