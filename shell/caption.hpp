// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QString>
#include <QTimer>

// A caption: a card of a title, a sentence and the keys that do what it tells of, near the bottom
// of one output for as long as the compositor's `caption` asks, as `paw tour` narrates itself.
// The view draws it and fades it out; this holds what to show and when it ends.
class Caption : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(QString output READ output NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString body READ body NOTIFY changed)
    // The keys, as "Super + Tab", or "" for none.
    Q_PROPERTY(QString keys READ keys NOTIFY changed)
  public:
    explicit Caption(QObject *parent = nullptr);
    bool active() const { return active_; }
    QString output() const { return output_; }
    QString title() const { return title_; }
    QString body() const { return body_; }
    QString keys() const { return keys_; }
    // Shows it on `output` for `milliseconds`; showing again replaces it and restarts the time.
    void show(const QString &output, const QString &title, const QString &body, const QString &keys,
              int milliseconds);
    // "OUTPUT MILLISECONDS TITLE<tab>BODY<tab>KEYS", as the compositor's `caption` line has it
    // after its name, OUTPUT "-" meaning `fallback`; false, showing nothing, for anything else.
    bool showLine(const QString &line, const QString &fallback);
    void hide();
  Q_SIGNALS:
    void changed();

  private:
    QTimer timer_;
    bool active_ = false;
    QString output_, title_, body_, keys_;
};
