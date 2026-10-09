// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

class ShellController;

// The power menu: the actions the compositor says may run now (from its "power" state line),
// and running one through the control socket. Entries are {action, title}. Power off, reboot
// and log out wait for a confirmation that counts down (power.countdown seconds) and goes ahead
// when it runs out.
class Power : public QObject {
    Q_OBJECT
    // The actions that may run, in menu order: "lock", "suspend", "hibernate", "reboot",
    // "poweroff", "logout".
    Q_PROPERTY(QStringList available READ available NOTIFY availableChanged)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY availableChanged)
    // The action waiting for confirmation ("" for none), the output asking, what its button and
    // heading say ("Power off"), and the seconds left (0: no countdown, it waits for a click).
    Q_PROPERTY(QString pending READ pending NOTIFY pendingChanged)
    Q_PROPERTY(QString output READ output NOTIFY pendingChanged)
    Q_PROPERTY(QString pendingTitle READ pendingTitle NOTIFY pendingChanged)
    Q_PROPERTY(int countdown READ countdown NOTIFY countdownChanged)
    // The question, with the time left: "The computer powers off in 10 seconds."
    Q_PROPERTY(QString message READ message NOTIFY countdownChanged)
  public:
    explicit Power(ShellController &controller);
    QStringList available() const { return available_; }
    QVariantList entries() const;
    QString pending() const { return pending_; }
    QString output() const { return output_; }
    QString pendingTitle() const;
    int countdown() const { return countdown_; }
    QString message() const;
    // What menus call an action: "Lock screen", "Power off…", ...; empty for one they do not
    // offer.
    static QString title(const QString &action);
    // The compositor's list: "lock,suspend,logout", or "-" for none.
    void setAvailable(const QString &list);
    // Seconds the confirmation counts down; 0 waits for a click.
    void setCountdown(int seconds) { seconds_ = seconds; }
    // Runs `action` if it may run; power off, reboot and log out ask first, on `output` (the
    // focused one when empty).
    Q_INVOKABLE void request(const QString &action, const QString &output = {});
    // Runs the action waiting for confirmation now, or gives it up.
    Q_INVOKABLE void confirm();
    Q_INVOKABLE void cancel();
  Q_SIGNALS:
    void availableChanged();
    void pendingChanged();
    void countdownChanged();
    // The compositor would not run an action; `message` says why.
    void failed(const QString &message);

  private:
    ShellController &controller_;
    QStringList available_;
    QString pending_, output_;
    int seconds_ = 10, countdown_ = 0;
    QTimer tick_;
    void run(const QString &action);
};
