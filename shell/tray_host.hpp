// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "tray.hpp"
#include <QDBusConnection>
#include <QDBusPendingCall>
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <map>
#include <memory>

class QDBusArgument;
class QDBusMessage;
class QDBusServiceWatcher;
class TrayWatcher;

// One StatusNotifierItem seen over D-Bus. Reads its properties into the model (with GetAll, or
// one Get each where GetAll fails) once at first and again whenever it announces a change, and
// calls its methods for the panel. The item enters the model once its properties are read.
class TrayItemClient : public QObject {
    Q_OBJECT
  public:
    TrayItemClient(TrayModel &model, const QDBusConnection &bus, const QString &service,
                   const QString &path);
    ~TrayItemClient() override;
    QString service() const { return service_; }
    void activate(int x, int y);
    void secondaryActivate(int x, int y);
    void contextMenu(int x, int y);
    void scroll(int delta, const QString &orientation);
    // The menu: about to show the entries under `id`, no longer showing them, entry `id` picked.
    void openMenu(int id);
    void closeMenu(int id);
    void clickMenu(int id);
    // Reads an item's properties, as GetAll gives them, into `item`; what is missing or of the
    // wrong type takes its default. Returns whether what its icon shows changed.
    static bool read(TrayItem &item, const QVariantMap &properties);
    // Reads a GetLayout answer's layout, (ia{sv}av), into `menu`: at most 1000 entries, 8 deep.
    // Returns false when it is no layout.
    static bool readLayout(const QDBusArgument &layout, std::map<int, TrayMenuEntry> &menu);

  private Q_SLOTS:
    void itemSignal(const QDBusMessage &message);
    void menuSignal(const QDBusMessage &message);

  private:
    TrayModel &model_;
    QDBusConnection bus_;
    QString service_, path_, key_;
    // Gathers a burst of change signals into one read, 10 ms after the first.
    QTimer refresh_;
    bool getAll_ = true, reading_ = false, again_ = false;
    void refresh();
    void readEach();
    void apply(const QVariantMap &properties);
    // The menu at menuPath_, fetched whole, and again when its layout changes.
    QString menuPath_;
    QTimer relayout_;
    bool fetching_ = false, fetchAgain_ = false;
    void setMenuPath(const QString &path);
    void fetchLayout();
    void updateEntries(const QDBusMessage &message);
    QDBusPendingCall callMenu(const QString &method, const QVariantList &arguments);
    QDBusPendingCall call(const QString &interface, const QString &method, const QVariantList &arguments);
};

// The system tray's host on the session bus. It serves org.kde.StatusNotifierWatcher itself while
// nobody else does, and otherwise registers with the watcher that does, taking over the name if
// that one goes. Either way it owns org.kde.StatusNotifierHost-PID, which tells applications a
// tray exists, and keeps `model` to the items the watcher lists.
class TrayHost : public QObject {
    Q_OBJECT
  public:
    explicit TrayHost(TrayModel &model, QObject *parent = nullptr);
    ~TrayHost() override;
    // false, with error() set, when there is no bus or no host name to be had.
    bool start(const QDBusConnection &bus = QDBusConnection::sessionBus());
    QString error() const { return error_; }
    // Whether this host's own watcher serves the session, rather than another program's.
    bool ownsWatcher() const { return watcher_ != nullptr; }
    QString hostName() const { return hostName_; }
    // At most this many items are shown; more are ignored.
    static constexpr int maxItems = 64;

  private Q_SLOTS:
    // A watcher's entry: SERVICE/PATH, or SERVICE alone for /StatusNotifierItem.
    void addItem(const QString &registered);
    void removeItem(const QString &registered);

  private:
    TrayModel &model_;
    QDBusConnection bus_{QString()};
    TrayWatcher *watcher_ = nullptr;
    // The watcher's name changing hands, and the items' services leaving.
    QDBusServiceWatcher *watcherOwner_ = nullptr, *owners_ = nullptr;
    std::map<QString, std::unique_ptr<TrayItemClient>> items_;
    QString hostName_, error_;
    bool following_ = false;
    bool takeWatcher();
    void followWatcher();
    void watcherChanged(const QString &service, const QString &oldOwner, const QString &newOwner);
    void serviceGone(const QString &service);
    TrayItemClient *client(const QString &key);
};
