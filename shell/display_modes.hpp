// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>

// The display mode popup (the display_mode action, Windows' Win+P): what the compositor says of
// it, which the views draw, and the choice a click takes. The compositor steps the popup and
// takes its choice; this only shows it.
class DisplayModes : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString output READ output NOTIFY changed)
    // The choice the popup shows and the one in force: "internal", "duplicate", "extend" or
    // "external".
    Q_PROPERTY(QString shown READ shown NOTIFY changed)
    Q_PROPERTY(QString current READ current NOTIFY changed)
    // The choices offered, in the popup's order; "extend" alone with one monitor.
    Q_PROPERTY(QStringList choices READ choices NOTIFY changed)
  public:
    explicit DisplayModes(QObject *parent = nullptr) : QObject(parent) {}
    bool active() const { return active_; }
    QString output() const { return output_; }
    QString shown() const { return shown_; }
    QString current() const { return current_; }
    QStringList choices() const { return choices_; }
    // Reads a line of the compositor's, "display-mode OUTPUT SHOWN CURRENT CHOICES" or
    // "display-mode-close"; false for any other line.
    bool handle(const QString &line);
    // Takes `mode` at once, as a click on it does.
    Q_INVOKABLE void choose(const QString &mode);
  Q_SIGNALS:
    void changed();
    // A request for the compositor: "display_mode MODE".
    void request(const QString &line);

  private:
    bool active_ = false;
    QString output_, shown_, current_;
    QStringList choices_;
};
