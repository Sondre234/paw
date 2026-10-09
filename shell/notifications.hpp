// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "paw/config.hpp"
#include <QAbstractListModel>
#include <QDateTime>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <vector>

// One notification as the freedesktop.org notification spec describes it.
struct Notification {
    enum Urgency { Low = 0, Normal = 1, Critical = 2 };
    uint id = 0;
    QString app, icon, summary;
    // The body with the spec's markup reduced to what Qt's styled text draws.
    QString body;
    // Pairs of (key, label), in the order the application listed them; "default" is the action
    // a click on the card runs.
    std::vector<std::pair<QString, QString>> actions;
    int urgency = Normal;
    // The application's timeout in milliseconds: -1 for the server's choice, 0 never.
    int timeout = -1;
    // A progress value 0 to 100 from the "value" hint, else -1.
    int progress = -1;
    QString category, desktopEntry, tag;
    QImage image;
    bool resident = false, transient = false;
    QDateTime time;
    bool read = false;
};

// A list of notifications for QML: the cards on screen, or the history.
class NotificationModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // The notifications by application, as the history lists them: [{key, app, icon,
    // desktopEntry, notifications}], the application with the newest notification first, and
    // each one's notifications newest first as maps of the roles below. An application is known
    // by its desktop entry, else by its name. Changes are announced once the event loop runs, so
    // that marking a long list read rebuilds it once.
    Q_PROPERTY(QVariantList groups READ groups NOTIFY groupsChanged)
  public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        AppRole,
        IconRole,
        SummaryRole,
        BodyRole,
        ActionsRole,
        HasDefaultRole,
        UrgencyRole,
        ProgressRole,
        HasImageRole,
        DesktopEntryRole,
        TimeRole,
        ReadRole
    };
    explicit NotificationModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(items_.size()); }
    QVariantList groups() const;
    const std::vector<Notification> &items() const { return items_; }
    int indexOf(uint id) const;
    const Notification *find(uint id) const;
    // Newest first.
    void prepend(Notification notification);
    void replace(int row, Notification notification);
    void markRead(int row);
    void removeAt(int row);
    void truncate(int size);
    void clear();
  Q_SIGNALS:
    void countChanged();
    void groupsChanged();

  private:
    std::vector<Notification> items_;
    bool groupsPending_ = false;
    void touchGroups();
};

// Reduces the markup notification bodies may carry (<b>, <i>, <u>, <a href>, <br>, <img alt>) to
// Qt styled text: other tags are dropped, stray angle brackets and ampersands are escaped, and
// tags left open are closed.
QString notificationMarkup(const QString &body);

// The notification server without its transport: applications hand it notifications, it keeps
// the cards on screen and the history, runs the timers, and reports closures and invoked
// actions for the transport to send back. Everything happens on the GUI thread.
class NotificationCenter : public QObject {
    Q_OBJECT
    Q_PROPERTY(NotificationModel *cards READ cards CONSTANT)
    Q_PROPERTY(NotificationModel *history READ history CONSTANT)
    // Notifications the history popover has not shown yet.
    Q_PROPERTY(int unread READ unread NOTIFY unreadChanged)
    Q_PROPERTY(bool dnd READ dnd WRITE setDnd NOTIFY dndChanged)
    // Whether something is listening on the session bus for applications' notifications.
    Q_PROPERTY(bool serving READ serving NOTIFY servingChanged)
    Q_PROPERTY(int cardWidth READ cardWidth NOTIFY configChanged)
    Q_PROPERTY(bool bottom READ bottom NOTIFY configChanged)
    Q_PROPERTY(bool left READ left NOTIFY configChanged)
  public:
    // The spec's reasons a notification closed.
    enum Reason { Expired = 1, Dismissed = 2, Closed = 3, Undefined = 4 };
    explicit NotificationCenter(QObject *parent = nullptr);
    // Applies the settings; do-not-disturb comes from them on the first call only, so a reload
    // keeps what the user toggled.
    void configure(const paw::NotificationsConfig &config);
    NotificationModel *cards() { return &cards_; }
    NotificationModel *history() { return &history_; }
    int unread() const { return unread_; }
    bool dnd() const { return dnd_; }
    void setDnd(bool on);
    bool serving() const { return serving_; }
    void setServing(bool serving);
    int cardWidth() const { return config_.width; }
    bool bottom() const {
        return config_.position == paw::Corner::BottomLeft ||
               config_.position == paw::Corner::BottomRight;
    }
    bool left() const {
        return config_.position == paw::Corner::TopLeft ||
               config_.position == paw::Corner::BottomLeft;
    }
    bool enabled() const { return config_.enabled; }
    // Takes a notification and returns its id: a new one, or that of the one it replaces
    // (`replacesId`, or a notification of the same application with the same stack tag).
    // `notification.id` is ignored.
    uint notify(Notification notification, uint replacesId = 0);
    // The spec's CloseNotification: closes the notification with reason Closed and takes it out
    // of the history too. Returns false when there is no such notification.
    bool closeFromApplication(uint id);
    // The user's dismissal of a card (a click, or the close button).
    Q_INVOKABLE void dismiss(uint id);
    // A click on a card: runs the default action when there is one, else dismisses.
    Q_INVOKABLE void activate(uint id);
    // Runs one of a notification's actions.
    Q_INVOKABLE void invoke(uint id, const QString &key);
    // Pauses (or resumes) a card's timer while the pointer is over it.
    Q_INVOKABLE void hold(uint id, bool held);
    Q_INVOKABLE void markAllRead();
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE void removeFromHistory(uint id);
    Q_INVOKABLE void toggleDnd() { setDnd(!dnd_); }
    // Opens a link from a notification body, when it is a web or mail address.
    Q_INVOKABLE void openLink(const QString &url);
    // Milliseconds a card with this timeout and urgency stays, 0 for until dismissed.
    int lifetime(const Notification &notification) const;
    // The lifetime of the card with that id, from when it came or was last replaced: what its
    // countdown runs down. 0 for one that stays, or for no such card.
    Q_INVOKABLE int cardLifetime(uint id) const;
    bool timerRunning(uint id) const;
  Q_SIGNALS:
    void closed(uint id, uint reason);
    void actionInvoked(uint id, const QString &key);
    void unreadChanged();
    void dndChanged();
    void servingChanged();
    void configChanged();
    // A notification was handed over, shown or not.
    void received(uint id);

  private:
    struct Timer {
        QTimer *timer = nullptr;
        int remaining = 0;
    };
    NotificationModel cards_, history_;
    paw::NotificationsConfig config_;
    bool dnd_ = false, configured_ = false, serving_ = false;
    int unread_ = 0;
    uint lastId_ = 0;
    QHash<uint, Timer> timers_;
    QSet<uint> held_;
    void startTimer(uint id, int milliseconds);
    void stopTimer(uint id);
    void removeCard(uint id, uint reason, bool report = true);
    void countUnread();
};
