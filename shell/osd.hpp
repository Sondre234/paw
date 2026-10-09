// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "paw/config.hpp"
#include <QObject>
#include <QString>
#include <QTimer>

// The on-screen display: a label and an optional level, shown on one output for a moment. The
// views draw it and fade it out; this holds what to show and when it ends.
class Osd : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString output READ output NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    // 0 to 100, or -1 for a label alone.
    Q_PROPERTY(int percent READ percent NOTIFY changed)
    // "volume", "muted", "brightness", "microphone", "microphone-muted", "dnd", "notifications" or
    // "text": which icon goes with it.
    Q_PROPERTY(QString kind READ kind NOTIFY changed)
    Q_PROPERTY(bool top READ top NOTIFY configChanged)
  public:
    explicit Osd(QObject *parent = nullptr);
    void configure(const paw::OsdConfig &config);
    const paw::OsdConfig &config() const { return config_; }
    bool active() const { return active_; }
    QString output() const { return output_; }
    QString text() const { return text_; }
    int percent() const { return percent_; }
    QString kind() const { return kind_; }
    bool top() const { return config_.top; }
    // Shows it on `output` (the caller picks the focused one) for the configured time; showing
    // again restarts the time and replaces what is drawn.
    void show(const QString &output, const QString &text, int percent, const QString &kind = "text");
    void hide();
  Q_SIGNALS:
    void changed();
    void configChanged();

  private:
    paw::OsdConfig config_;
    QTimer timer_;
    bool active_ = false;
    QString output_, text_, kind_ = "text";
    int percent_ = -1;
};
