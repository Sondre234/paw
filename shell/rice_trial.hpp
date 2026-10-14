// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QDeadlineTimer>
#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>

// A rice on trial (`paw rice try`): the compositor's "rice-trial MILLISECONDS NAME" starts the
// question, which counts the seconds down to the previous look coming back, and "rice-kept NAME"
// or "rice-reverted REASON" ends it. Keep and Revert ask the compositor (`rice keep`, `rice
// revert`), which keeps or takes back the rice and says so with those lines.
class RiceTrial : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    // The rice's name as installed, and the author and description its rice.lua gives.
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString author READ author NOTIFY changed)
    Q_PROPERTY(QString description READ description NOTIFY changed)
    // Whole seconds left before it goes back.
    Q_PROPERTY(int secondsLeft READ secondsLeft NOTIFY secondsLeftChanged)
    // The output the question shows on: the one overlays belonged on as the trial started.
    Q_PROPERTY(QString output READ output NOTIFY changed)

  public:
    // Sends the compositor a request.
    using Send = std::function<void(const QByteArray &)>;
    RiceTrial(Send send, std::function<QString()> output, QObject *parent = nullptr);

    bool active() const { return active_; }
    QString name() const { return name_; }
    QString author() const { return author_; }
    QString description() const { return description_; }
    int secondsLeft() const { return secondsLeft_; }
    QString output() const { return output_; }

    // Reads a line of the compositor's; false for one that is not a rice trial's.
    bool handle(const QString &line);
    // Asks about the rice `name` for `milliseconds`, as "rice-trial" does with what its rice.lua
    // says of it.
    void start(int milliseconds, const QString &name, const QString &author,
               const QString &description);
    Q_INVOKABLE void keep();
    Q_INVOKABLE void revert();

  Q_SIGNALS:
    void changed();
    void secondsLeftChanged();

  private:
    Send send_;
    std::function<QString()> overlayOutput_;
    bool active_ = false;
    QString name_, author_, description_, output_;
    int secondsLeft_ = 0;
    QDeadlineTimer deadline_;
    QTimer countdown_;
    void tick();
    void end();
};
