// SPDX-License-Identifier: GPL-3.0-or-later
#include "tray_host.hpp"
#include "dbus_util.hpp"
#include "tray_watcher.hpp"
#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QTextDocumentFragment>
#include <memory>

namespace {
constexpr auto watcherService = "org.kde.StatusNotifierWatcher";
constexpr auto watcherPath = "/StatusNotifierWatcher";
constexpr auto itemInterface = "org.kde.StatusNotifierItem";
constexpr auto propertiesInterface = "org.freedesktop.DBus.Properties";
constexpr auto menuInterface = "com.canonical.dbusmenu";
// An application that does not answer within this long is left alone.
constexpr int callTimeout = 5000;

// A property that should be a string: what it is, else empty.
QString text(const QVariantMap &properties, const char *name, int limit) {
    const QVariant value = properties.value(name);
    return value.metaType().id() == QMetaType::QString ? value.toString().left(limit) : QString();
}
// The tooltip's text may use the specification's markup; the panel shows plain text.
QString plain(const QString &markup, int limit) {
    QString result = markup.left(4 * limit);
    if (result.contains('<') || result.contains('&'))
        result = QTextDocumentFragment::fromHtml(result).toPlainText();
    return result.replace(QChar::LineSeparator, '\n').replace(QChar::ParagraphSeparator, '\n').trimmed().left(limit);
}
// ToolTip, (sa(iiay)ss): an icon name, an icon, a title and a text.
void readToolTip(const QVariant &value, QString &title, QString &body) {
    title.clear();
    body.clear();
    if (value.metaType() != QMetaType::fromType<QDBusArgument>())
        return;
    const auto argument = value.value<QDBusArgument>();
    if (argument.currentSignature() != "(sa(iiay)ss)")
        return;
    QString icon;
    argument.beginStructure();
    argument >> icon;
    argument.beginArray(); // the tooltip's own icon is not shown
    argument.endArray();
    argument >> title >> body;
    argument.endStructure();
    title = plain(title, 200);
    body = plain(body, 1000);
}
// IconPixmap and the like, a(iiay): at most 16 of them, those of a sane size.
QList<QImage> readPixmaps(const QVariant &value) {
    QList<QImage> pixmaps;
    if (value.metaType() != QMetaType::fromType<QDBusArgument>())
        return pixmaps;
    const auto argument = value.value<QDBusArgument>();
    if (argument.currentSignature() != "a(iiay)")
        return pixmaps;
    argument.beginArray();
    while (!argument.atEnd() && pixmaps.size() < 16) {
        int width = 0, height = 0;
        QByteArray data;
        argument.beginStructure();
        argument >> width >> height >> data;
        argument.endStructure();
        if (QImage image = trayImageFromArgb32(width, height, data); !image.isNull())
            pixmaps << image;
    }
    argument.endArray();
    return pixmaps;
}
// Menu: an object path; some items send it as a string. "/" and Chromium's "/NO_DBUSMENU" mean
// none.
QString menuPath(const QVariant &value) {
    QString path;
    if (value.metaType() == QMetaType::fromType<QDBusObjectPath>())
        path = value.value<QDBusObjectPath>().path();
    else if (value.metaType().id() == QMetaType::QString)
        path = value.toString();
    return trayValidPath(path) && path != "/" && path != "/NO_DBUSMENU" ? path : QString();
}
} // namespace

TrayItemClient::TrayItemClient(TrayModel &model, const QDBusConnection &bus, const QString &service,
                               const QString &path)
    : model_(model), bus_(bus), service_(service), path_(path), key_(service + path) {
    for (const char *signal : {"NewTitle", "NewIcon", "NewAttentionIcon", "NewOverlayIcon", "NewToolTip",
                               "NewStatus", "NewIconThemePath", "NewMenu"})
        bus_.connect(service_, path_, itemInterface, signal, this, SLOT(itemSignal(QDBusMessage)));
    refresh_.setSingleShot(true);
    refresh_.setInterval(10);
    connect(&refresh_, &QTimer::timeout, this, &TrayItemClient::refresh);
    relayout_.setSingleShot(true);
    relayout_.setInterval(10);
    connect(&relayout_, &QTimer::timeout, this, &TrayItemClient::fetchLayout);
    refresh();
}
TrayItemClient::~TrayItemClient() { model_.remove(key_); }
QDBusPendingCall TrayItemClient::call(const QString &interface, const QString &method,
                                      const QVariantList &arguments) {
    auto message = QDBusMessage::createMethodCall(service_, path_, interface, method);
    message.setArguments(arguments);
    return bus_.asyncCall(message, callTimeout);
}
// The first signal of a burst starts the wait; the rest join it rather than put it off, so that
// an item announcing changes all the time is still read every few milliseconds.
void TrayItemClient::itemSignal(const QDBusMessage &) {
    if (!refresh_.isActive())
        refresh_.start();
}
void TrayItemClient::refresh() {
    if (reading_) {
        again_ = true;
        return;
    }
    reading_ = true;
    if (!getAll_) {
        readEach();
        return;
    }
    dbus::whenAnswered(call(propertiesInterface, "GetAll", {QString(itemInterface)}), this,
                       [this](QDBusPendingCallWatcher *watch) {
                           const QDBusMessage reply = watch->reply();
                           QVariantMap properties;
                           if (reply.type() == QDBusMessage::ReplyMessage &&
                               reply.signature() == "a{sv}")
                               properties = qdbus_cast<QVariantMap>(reply.arguments().at(0));
                           // Some items answer GetAll with an error, or with nothing: ask for each
                           // property.
                           if (properties.isEmpty()) {
                               getAll_ = false;
                               readEach();
                               return;
                           }
                           apply(properties);
                       });
}
void TrayItemClient::readEach() {
    static const QStringList names{"Id",
                                   "Title",
                                   "Status",
                                   "IconName",
                                   "IconPixmap",
                                   "IconThemePath",
                                   "AttentionIconName",
                                   "AttentionIconPixmap",
                                   "OverlayIconName",
                                   "OverlayIconPixmap",
                                   "ToolTip",
                                   "ItemIsMenu",
                                   "Menu"};
    auto gathered = std::make_shared<QVariantMap>();
    auto pending = std::make_shared<int>(int(names.size()));
    for (const auto &name : names) {
        dbus::whenAnswered(
            call(propertiesInterface, "Get", {QString(itemInterface), name}), this,
            [this, name, gathered, pending](QDBusPendingCallWatcher *watch) {
                const QDBusMessage reply = watch->reply();
                if (reply.type() == QDBusMessage::ReplyMessage && reply.signature() == "v")
                    (*gathered)[name] = reply.arguments().at(0).value<QDBusVariant>().variant();
                if (--*pending == 0)
                    apply(*gathered);
            });
    }
}
void TrayItemClient::apply(const QVariantMap &properties) {
    reading_ = false;
    // Nothing read is an item that does not answer: it keeps what it showed, or never shows.
    if (!properties.isEmpty()) {
        if (TrayItem *item = model_.find(key_)) {
            model_.changed(key_, read(*item, properties));
        } else {
            TrayItem fresh;
            fresh.key = key_;
            read(fresh, properties);
            model_.add(std::move(fresh));
        }
        setMenuPath(model_.find(key_)->menuPath);
    }
    if (again_) {
        again_ = false;
        refresh();
    }
}
bool TrayItemClient::read(TrayItem &item, const QVariantMap &properties) {
    const TrayItem before = item;
    item.id = text(properties, "Id", 200);
    item.title = text(properties, "Title", 200);
    const QString status = text(properties, "Status", 32);
    item.status = status == "Passive" || status == "NeedsAttention" ? status : "Active";
    item.iconName = text(properties, "IconName", 1024);
    item.attentionIconName = text(properties, "AttentionIconName", 1024);
    item.overlayIconName = text(properties, "OverlayIconName", 1024);
    item.iconThemePath = text(properties, "IconThemePath", 4096);
    item.icon = readPixmaps(properties.value("IconPixmap"));
    item.attentionIcon = readPixmaps(properties.value("AttentionIconPixmap"));
    item.overlayIcon = readPixmaps(properties.value("OverlayIconPixmap"));
    readToolTip(properties.value("ToolTip"), item.toolTipTitle, item.toolTipText);
    const QVariant isMenu = properties.value("ItemIsMenu");
    item.itemIsMenu = isMenu.metaType().id() == QMetaType::Bool && isMenu.toBool();
    item.menuPath = menuPath(properties.value("Menu"));
    return item.status != before.status || item.iconName != before.iconName ||
           item.attentionIconName != before.attentionIconName ||
           item.overlayIconName != before.overlayIconName || item.iconThemePath != before.iconThemePath ||
           item.icon != before.icon || item.attentionIcon != before.attentionIcon ||
           item.overlayIcon != before.overlayIcon;
}
void TrayItemClient::activate(int x, int y) {
    // Plasma's way: an item that cannot be activated shows its menu instead.
    dbus::whenAnswered(call(itemInterface, "Activate", {x, y}), this,
                       [this](QDBusPendingCallWatcher *watch) {
                           const auto type = watch->error().type();
                           if (watch->isError() && type != QDBusError::NoReply &&
                               type != QDBusError::Timeout && type != QDBusError::TimedOut)
                               Q_EMIT model_.activationRefused(key_);
                       });
}
void TrayItemClient::secondaryActivate(int x, int y) { call(itemInterface, "SecondaryActivate", {x, y}); }
void TrayItemClient::contextMenu(int x, int y) { call(itemInterface, "ContextMenu", {x, y}); }
void TrayItemClient::scroll(int delta, const QString &orientation) {
    call(itemInterface, "Scroll", {delta, orientation});
}

QDBusPendingCall TrayItemClient::callMenu(const QString &method, const QVariantList &arguments) {
    auto message = QDBusMessage::createMethodCall(service_, menuPath_, menuInterface, method);
    message.setArguments(arguments);
    return bus_.asyncCall(message, callTimeout);
}
void TrayItemClient::setMenuPath(const QString &path) {
    if (path == menuPath_)
        return;
    for (const char *signal : {"LayoutUpdated", "ItemsPropertiesUpdated"}) {
        if (!menuPath_.isEmpty())
            bus_.disconnect(service_, menuPath_, menuInterface, signal, this, SLOT(menuSignal(QDBusMessage)));
        if (!path.isEmpty())
            bus_.connect(service_, path, menuInterface, signal, this, SLOT(menuSignal(QDBusMessage)));
    }
    menuPath_ = path;
    if (TrayItem *item = model_.find(key_); item && !item->menu.empty()) {
        item->menu.clear();
        model_.menuEdited(key_);
    }
    if (!menuPath_.isEmpty())
        fetchLayout();
}
void TrayItemClient::fetchLayout() {
    if (fetching_) {
        fetchAgain_ = true;
        return;
    }
    if (menuPath_.isEmpty())
        return;
    fetching_ = true;
    dbus::whenAnswered(callMenu("GetLayout", {0, -1, QStringList()}), this,
                       [this](QDBusPendingCallWatcher *watch) {
                           fetching_ = false;
                           const QDBusMessage reply = watch->reply();
                           std::map<int, TrayMenuEntry> menu;
                           TrayItem *item = model_.find(key_);
                           if (item && reply.type() == QDBusMessage::ReplyMessage &&
                               reply.signature() == "u(ia{sv}av)" &&
                               readLayout(reply.arguments().at(1).value<QDBusArgument>(), menu)) {
                               item->menu = std::move(menu);
                               model_.menuEdited(key_);
                           }
                           if (fetchAgain_) {
                               fetchAgain_ = false;
                               fetchLayout();
                           }
                       });
}
namespace {
bool readEntry(const QDBusArgument &layout, std::map<int, TrayMenuEntry> &menu, int depth, int &budget, int &id) {
    if (budget <= 0 || layout.currentSignature() != "(ia{sv}av)")
        return false;
    --budget;
    TrayMenuEntry entry;
    layout.beginStructure();
    layout >> id >> entry.properties;
    layout.beginArray();
    while (!layout.atEnd()) {
        QDBusVariant child;
        layout >> child;
        const QVariant value = child.variant();
        int childId = 0;
        if (depth < 8 && value.metaType() == QMetaType::fromType<QDBusArgument>() &&
            readEntry(value.value<QDBusArgument>(), menu, depth + 1, budget, childId) && childId != id &&
            std::find(entry.children.begin(), entry.children.end(), childId) == entry.children.end())
            entry.children.push_back(childId);
    }
    layout.endArray();
    layout.endStructure();
    entry.read();
    menu[id] = std::move(entry);
    return true;
}
} // namespace
bool TrayItemClient::readLayout(const QDBusArgument &layout, std::map<int, TrayMenuEntry> &menu) {
    int budget = 1000, id = 0;
    return readEntry(layout, menu, 0, budget, id);
}
void TrayItemClient::menuSignal(const QDBusMessage &message) {
    if (message.member() == "LayoutUpdated") {
        if (!relayout_.isActive())
            relayout_.start();
    }
    else if (message.member() == "ItemsPropertiesUpdated" && message.signature() == "a(ia{sv})a(ias)") {
        updateEntries(message);
    }
}
// ItemsPropertiesUpdated: the properties changed, (ia{sv}), and removed, (ias), entry by entry.
void TrayItemClient::updateEntries(const QDBusMessage &message) {
    TrayItem *item = model_.find(key_);
    if (!item)
        return;
    const auto updated = message.arguments().at(0).value<QDBusArgument>();
    updated.beginArray();
    while (!updated.atEnd()) {
        int id = 0;
        QVariantMap properties;
        updated.beginStructure();
        updated >> id >> properties;
        updated.endStructure();
        if (auto entry = item->menu.find(id); entry != item->menu.end()) {
            for (auto it = properties.constBegin(); it != properties.constEnd(); ++it)
                entry->second.properties[it.key()] = it.value();
            entry->second.read();
        }
    }
    updated.endArray();
    const auto removed = message.arguments().at(1).value<QDBusArgument>();
    removed.beginArray();
    while (!removed.atEnd()) {
        int id = 0;
        QStringList names;
        removed.beginStructure();
        removed >> id >> names;
        removed.endStructure();
        if (auto entry = item->menu.find(id); entry != item->menu.end()) {
            for (const auto &name : names)
                entry->second.properties.remove(name);
            entry->second.read();
        }
    }
    removed.endArray();
    model_.menuEdited(key_);
}
void TrayItemClient::openMenu(int id) {
    if (menuPath_.isEmpty())
        return;
    // AboutToShow lets the application fill the entries in; it says whether they changed.
    dbus::whenAnswered(callMenu("AboutToShow", {id}), this, [this](QDBusPendingCallWatcher *watch) {
        const QDBusMessage reply = watch->reply();
        if (reply.type() == QDBusMessage::ReplyMessage && reply.signature() == "b" &&
            reply.arguments().at(0).toBool())
            fetchLayout();
    });
    callMenu("Event", {id, QString("opened"), QVariant::fromValue(QDBusVariant(0)), 0u});
}
void TrayItemClient::closeMenu(int id) {
    if (!menuPath_.isEmpty())
        callMenu("Event", {id, QString("closed"), QVariant::fromValue(QDBusVariant(0)), 0u});
}
void TrayItemClient::clickMenu(int id) {
    if (!menuPath_.isEmpty())
        callMenu("Event", {id, QString("clicked"), QVariant::fromValue(QDBusVariant(0)), 0u});
}

TrayHost::TrayHost(TrayModel &model, QObject *parent) : QObject(parent), model_(model) {
    auto forward = [this](void (TrayItemClient::*method)(int, int)) {
        return [this, method](const QString &key, int x, int y) {
            if (auto *item = client(key))
                (item->*method)(x, y);
        };
    };
    connect(&model_, &TrayModel::activateRequested, this, forward(&TrayItemClient::activate));
    connect(&model_, &TrayModel::secondaryActivateRequested, this, forward(&TrayItemClient::secondaryActivate));
    connect(&model_, &TrayModel::contextMenuRequested, this, forward(&TrayItemClient::contextMenu));
    connect(&model_, &TrayModel::scrollRequested, this,
            [this](const QString &key, int delta, const QString &orientation) {
                if (auto *item = client(key))
                    item->scroll(delta, orientation);
            });
    auto forwardMenu = [this](void (TrayItemClient::*method)(int)) {
        return [this, method](const QString &key, int id) {
            if (auto *item = client(key))
                (item->*method)(id);
        };
    };
    connect(&model_, &TrayModel::menuOpenRequested, this, forwardMenu(&TrayItemClient::openMenu));
    connect(&model_, &TrayModel::menuCloseRequested, this, forwardMenu(&TrayItemClient::closeMenu));
    connect(&model_, &TrayModel::menuClickRequested, this, forwardMenu(&TrayItemClient::clickMenu));
}
TrayHost::~TrayHost() {
    items_.clear();
    delete watcher_;
    if (!hostName_.isEmpty())
        bus_.unregisterService(hostName_);
}
TrayItemClient *TrayHost::client(const QString &key) {
    const auto found = items_.find(key);
    return found == items_.end() ? nullptr : found->second.get();
}
bool TrayHost::start(const QDBusConnection &bus) {
    bus_ = bus;
    if (!bus_.isConnected()) {
        error_ = "no session bus: " + bus_.lastError().message();
        return false;
    }
    // A second host in one process (the tests have several) takes a numbered name.
    const QString base = QString("org.kde.StatusNotifierHost-%1").arg(QCoreApplication::applicationPid());
    for (int n = 1; n <= 16 && hostName_.isEmpty(); ++n)
        if (const QString name = n == 1 ? base : base + '-' + QString::number(n); bus_.registerService(name))
            hostName_ = name;
    if (hostName_.isEmpty()) {
        error_ = "cannot own " + base;
        return false;
    }
    owners_ = new QDBusServiceWatcher(this);
    owners_->setConnection(bus_);
    owners_->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(owners_, &QDBusServiceWatcher::serviceUnregistered, this, &TrayHost::serviceGone);
    watcherOwner_ = new QDBusServiceWatcher(watcherService, bus_, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcherOwner_, &QDBusServiceWatcher::serviceOwnerChanged, this, &TrayHost::watcherChanged);
    if (!takeWatcher())
        followWatcher();
    return true;
}
bool TrayHost::takeWatcher() {
    auto *watcher = new TrayWatcher(this);
    if (!watcher->start(bus_)) {
        delete watcher;
        return false;
    }
    watcher_ = watcher;
    if (following_) {
        bus_.disconnect(watcherService, watcherPath, watcherService, "StatusNotifierItemRegistered", this,
                        SLOT(addItem(QString)));
        bus_.disconnect(watcherService, watcherPath, watcherService, "StatusNotifierItemUnregistered", this,
                        SLOT(removeItem(QString)));
        following_ = false;
    }
    connect(watcher_, &TrayWatcher::StatusNotifierItemRegistered, this, &TrayHost::addItem);
    connect(watcher_, &TrayWatcher::StatusNotifierItemUnregistered, this, &TrayHost::removeItem);
    watcher_->addHost(hostName_);
    // Items shown already stay; those of the watcher that went register here again.
    for (const auto &item : watcher_->items())
        addItem(item);
    return true;
}
void TrayHost::followWatcher() {
    if (!following_) {
        bus_.connect(watcherService, watcherPath, watcherService, "StatusNotifierItemRegistered", this,
                     SLOT(addItem(QString)));
        bus_.connect(watcherService, watcherPath, watcherService, "StatusNotifierItemUnregistered", this,
                     SLOT(removeItem(QString)));
        following_ = true;
    }
    auto registration = QDBusMessage::createMethodCall(watcherService, watcherPath, watcherService,
                                                       "RegisterStatusNotifierHost");
    registration.setArguments({hostName_});
    bus_.asyncCall(registration, callTimeout);
    auto get = QDBusMessage::createMethodCall(watcherService, watcherPath, propertiesInterface, "Get");
    get.setArguments({QString(watcherService), QString("RegisteredStatusNotifierItems")});
    dbus::whenAnswered(
        bus_.asyncCall(get, callTimeout), this, [this](QDBusPendingCallWatcher *watch) {
            const QDBusMessage reply = watch->reply();
            if (reply.type() != QDBusMessage::ReplyMessage || reply.signature() != "v")
                return;
            const QVariant items = reply.arguments().at(0).value<QDBusVariant>().variant();
            if (items.metaType().id() == QMetaType::QStringList)
                for (const auto &item : items.toStringList())
                    addItem(item);
        });
}
void TrayHost::watcherChanged(const QString &, const QString &, const QString &newOwner) {
    if (watcher_)
        return; // this host's own
    // The watcher went: serve one. If another program is quicker, its arrival is another change.
    if (newOwner.isEmpty())
        takeWatcher();
    else
        followWatcher();
}
void TrayHost::addItem(const QString &registered) {
    const qsizetype slash = registered.indexOf('/');
    const QString service = slash < 0 ? registered : registered.left(slash);
    const QString path = slash < 0 ? QString("/StatusNotifierItem") : registered.mid(slash);
    const QString key = service + path;
    if (service.isEmpty() || service.size() > 255 || !trayValidPath(path) || items_.count(key) ||
        int(items_.size()) >= maxItems)
        return;
    items_[key] = std::make_unique<TrayItemClient>(model_, bus_, service, path);
    owners_->addWatchedService(service);
}
void TrayHost::removeItem(const QString &registered) {
    const qsizetype slash = registered.indexOf('/');
    items_.erase(slash < 0 ? registered + "/StatusNotifierItem" : registered);
}
void TrayHost::serviceGone(const QString &service) {
    std::erase_if(items_, [&service](const auto &item) { return item.second->service() == service; });
    owners_->removeWatchedService(service);
}
