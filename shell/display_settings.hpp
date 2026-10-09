// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDeadlineTimer>
#include <QList>
#include <QObject>
#include <QRect>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <functional>

// The display settings window: every monitor's settings as the compositor gives them (`get
// monitors`), changed in the window, and applied all at once on trial (`monitors apply`), which
// the compositor takes back after a while unless they are kept (`monitors keep`). The window
// draws this; the compositor tests, applies, takes back and keeps.
//
// The arrangement is in logical pixels, starting at 0, 0, of the monitors that are on and mirror
// none; display_layout.hpp keeps them touching and apart as they are dragged or change size.
class DisplaySettings : public QObject {
    Q_OBJECT
    // Whether the window is open, and the output it was opened on.
    Q_PROPERTY(bool open READ open NOTIFY openChanged)
    Q_PROPERTY(QString output READ output NOTIFY openChanged)
    // Each monitor as {name, title, description, builtIn, source, enabled, state, mirror,
    // mirroredBy, x, y, width, height, refresh, scale, transform, logicalWidth, logicalHeight,
    // inLayout, vrrSupported, vrr, bitDepth, drawnDepth, hdr, hdrActive, hdrPossible, hdrWhy,
    // primary, resolutions, rates, modeLabel}: its settings as the window has them now, what it
    // can have (`resolutions` as {width, height, preferred, label}, `rates` at its resolution as
    // {refresh, label}), and where its settings come from (`source`: "override", "window",
    // "config" or "default"). In the compositor's order.
    Q_PROPERTY(QVariantList monitors READ monitors NOTIFY monitorsChanged)
    // The selected monitor's name, and its entry of `monitors` (empty with none).
    Q_PROPERTY(QString selected READ selected WRITE select NOTIFY selectedChanged)
    Q_PROPERTY(QVariantMap current READ current NOTIFY selectedChanged)
    // The arrangement's size, and how many monitors are in it.
    Q_PROPERTY(QSize extent READ extent NOTIFY monitorsChanged)
    Q_PROPERTY(int layoutCount READ layoutCount NOTIFY monitorsChanged)
    // Whether the settings differ from those in force, which Apply puts on trial.
    Q_PROPERTY(bool changed READ changed NOTIFY monitorsChanged)
    // Whether a monitor's settings are the window's kept ones, which Reset to configuration
    // takes back.
    Q_PROPERTY(bool kept READ kept NOTIFY monitorsChanged)
    // Waiting for the monitors, or for an answer to a request.
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    // Settings on trial, and the whole seconds left before they go back.
    Q_PROPERTY(bool trial READ trial NOTIFY trialChanged)
    Q_PROPERTY(int secondsLeft READ secondsLeft NOTIFY secondsLeftChanged)
    // What went wrong, or what happened while the window was not asked: "" for nothing.
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)

  public:
    // Sends the compositor a request and hands its whole reply on, as ShellController::ask.
    using Ask = std::function<void(const QByteArray &, std::function<void(const QByteArray &)>)>;
    explicit DisplaySettings(Ask ask, QObject *parent = nullptr);
    // Asks another stand-in for the compositor, as a preview, which has none, does.
    void setAsk(Ask ask) { ask_ = std::move(ask); }

    bool open() const { return open_; }
    QString output() const { return output_; }
    QVariantList monitors() const;
    QString selected() const { return selected_; }
    QVariantMap current() const;
    QSize extent() const;
    int layoutCount() const;
    bool changed() const;
    bool kept() const;
    bool busy() const { return pending_ > 0; }
    bool trial() const { return trial_; }
    int secondsLeft() const { return secondsLeft_; }
    QString message() const { return message_; }

    // Opens the window on `output`, reading the monitors anew.
    Q_INVOKABLE void show(const QString &output);
    // Closes it; settings on trial go back first, as Escape on the question does.
    Q_INVOKABLE void close();
    // Reads the monitors anew, dropping changes not applied.
    Q_INVOKABLE void reload();
    // Reads `get monitors`'s reply ("ok", then a line per monitor); false for one that is not.
    bool load(const QByteArray &reply);
    // Reads a line of the compositor's: "display-settings OUTPUT" opens the window there,
    // "monitors-trial MILLISECONDS", "monitors-kept" and "monitors-reverted REASON" follow a
    // trial. False for any other line.
    bool handle(const QString &line);

    Q_INVOKABLE void select(const QString &name);
    // On or off; the last monitor showing the desktop stays on. One turned on goes right of the
    // others; one turned off takes its mirrors with it into the arrangement.
    Q_INVOKABLE void setEnabled(const QString &name, bool enabled);
    // Shows `source`'s picture, or with "" the desktop of its own beside the others.
    Q_INVOKABLE void setMirror(const QString &name, const QString &source);
    // A resolution, at the refresh rate it has where that one is offered there, else the
    // fastest; and a refresh rate at the resolution it has (mHz).
    Q_INVOKABLE void setResolution(const QString &name, int width, int height);
    Q_INVOKABLE void setRefresh(const QString &name, int refresh);
    Q_INVOKABLE void setScale(const QString &name, double scale);
    Q_INVOKABLE void setTransform(const QString &name, int transform);
    Q_INVOKABLE void setVrr(const QString &name, bool on);
    Q_INVOKABLE void setPrimary(const QString &name);
    Q_INVOKABLE void setBitDepth(const QString &name, int depth);
    Q_INVOKABLE void setHdr(const QString &name, bool on);
    // Where a monitor of the arrangement dragged to x, y (in its logical pixels) would go, and
    // putting it there; edges line up within `threshold`.
    Q_INVOKABLE QPoint snapped(const QString &name, int x, int y, int threshold) const;
    Q_INVOKABLE void place(const QString &name, int x, int y, int threshold);

    // Puts the settings on trial; keeps them, takes them back, or puts the configuration's on
    // trial.
    Q_INVOKABLE void apply();
    Q_INVOKABLE void keep();
    Q_INVOKABLE void revert();
    Q_INVOKABLE void reset();
    // The request apply() sends.
    QByteArray applyRequest() const;

    // The common scales, as fractions, and a transform's name.
    Q_INVOKABLE static QVariantList scales();
    Q_INVOKABLE static QString transformName(int transform);

  Q_SIGNALS:
    void openChanged();
    void monitorsChanged();
    void selectedChanged();
    void busyChanged();
    void trialChanged();
    void secondsLeftChanged();
    void messageChanged();

  private:
    struct Mode {
        int width = 0, height = 0, refresh = 0;
        bool preferred = false;
    };
    struct Monitor {
        QString name, description, source, state, mirror, hdrWhy;
        bool builtIn = false, enabled = true, vrrSupported = false, vrr = false, hdr = false,
             hdrActive = false, primary = false;
        int x = 0, y = 0, width = 0, height = 0, refresh = 0, transform = 0, bitDepth = 8,
            drawnDepth = 8;
        double scale = 1;
        QList<Mode> modes;
        bool inLayout() const { return enabled && mirror.isEmpty(); }
        QRect rect() const;
    };
    Ask ask_;
    bool open_ = false, trial_ = false;
    QString output_, selected_, message_;
    QList<Monitor> monitors_, loaded_;
    int pending_ = 0, secondsLeft_ = 0;
    QDeadlineTimer deadline_;
    QTimer countdown_;
    Monitor *find(const QString &name);
    const Monitor *find(const QString &name) const;
    QVariantMap record(const Monitor &monitor) const;
    // Asks the compositor, counting the request as pending until its reply.
    void request(const QByteArray &line, std::function<void(const QByteArray &)> done);
    // Settles the arrangement after a monitor changed size or came or went, `first` staying
    // where it is (else the top left one), and starts it at 0, 0.
    void arrange(const QString &first = {});
    // Puts a monitor joining the arrangement right of the others.
    void placeRight(Monitor &monitor);
    void setMessage(const QString &message);
    void setTrial(bool trial, int milliseconds = 0);
    void tick();
    // A trial ended: the monitors as they are now, and why, unless asked.
    void ended(const QString &message);
    void edited();
};
