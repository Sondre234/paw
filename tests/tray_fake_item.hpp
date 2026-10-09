// SPDX-License-Identifier: GPL-3.0-or-later
// A StatusNotifierItem and its com.canonical.dbusmenu menu for the tray tests, answered message by
// message rather than through Qt's adaptors: a test can leave out GetAll or Activate, send
// mistyped properties and malformed layouts, and see every call the host makes. tray_probe runs
// one as a program; tray_dbus_test runs several in its own process.
#pragma once
#include <QColor>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QStringList>
#include <QVariantMap>
#include <functional>
#include <map>

namespace faketray {
inline constexpr auto itemInterface = "org.kde.StatusNotifierItem";
inline constexpr auto menuInterface = "com.canonical.dbusmenu";
inline constexpr auto propertiesInterface = "org.freedesktop.DBus.Properties";
inline constexpr auto watcherService = "org.kde.StatusNotifierWatcher";
inline constexpr auto watcherPath = "/StatusNotifierWatcher";

// (iiay): an icon's width, height and ARGB32 pixels in network byte order.
struct Pixmap {
    int width = 0, height = 0;
    QByteArray data;
};
// (sa(iiay)ss): a tooltip's icon name, icon, title and text.
struct ToolTip {
    QString icon;
    QList<Pixmap> pixmaps;
    QString title, text;
};
// (ia{sv}av): a menu entry, its properties and its children, each a variant holding a Layout.
struct Layout {
    int id = 0;
    QVariantMap properties;
    QList<QDBusVariant> children;
};
// (ia{sv}) and (ias): the arguments of ItemsPropertiesUpdated.
struct Properties {
    int id = 0;
    QVariantMap properties;
};
struct Removed {
    int id = 0;
    QStringList names;
};
} // namespace faketray
Q_DECLARE_METATYPE(faketray::Pixmap)
Q_DECLARE_METATYPE(faketray::ToolTip)
Q_DECLARE_METATYPE(faketray::Layout)
Q_DECLARE_METATYPE(faketray::Properties)
Q_DECLARE_METATYPE(faketray::Removed)

namespace faketray {
inline QDBusArgument &operator<<(QDBusArgument &argument, const Pixmap &pixmap) {
    argument.beginStructure();
    argument << pixmap.width << pixmap.height << pixmap.data;
    argument.endStructure();
    return argument;
}
inline const QDBusArgument &operator>>(const QDBusArgument &argument, Pixmap &pixmap) {
    argument.beginStructure();
    argument >> pixmap.width >> pixmap.height >> pixmap.data;
    argument.endStructure();
    return argument;
}
inline QDBusArgument &operator<<(QDBusArgument &argument, const ToolTip &tip) {
    argument.beginStructure();
    argument << tip.icon << tip.pixmaps << tip.title << tip.text;
    argument.endStructure();
    return argument;
}
inline const QDBusArgument &operator>>(const QDBusArgument &argument, ToolTip &tip) {
    argument.beginStructure();
    argument >> tip.icon >> tip.pixmaps >> tip.title >> tip.text;
    argument.endStructure();
    return argument;
}
inline QDBusArgument &operator<<(QDBusArgument &argument, const Layout &layout) {
    argument.beginStructure();
    argument << layout.id << layout.properties;
    argument.beginArray(QMetaType::fromType<QDBusVariant>());
    for (const auto &child : layout.children)
        argument << child;
    argument.endArray();
    argument.endStructure();
    return argument;
}
inline const QDBusArgument &operator>>(const QDBusArgument &argument, Layout &layout) {
    argument.beginStructure();
    argument >> layout.id >> layout.properties;
    argument.beginArray();
    while (!argument.atEnd()) {
        QDBusVariant child;
        argument >> child;
        layout.children << child;
    }
    argument.endArray();
    argument.endStructure();
    return argument;
}
inline QDBusArgument &operator<<(QDBusArgument &argument, const Properties &entry) {
    argument.beginStructure();
    argument << entry.id << entry.properties;
    argument.endStructure();
    return argument;
}
inline const QDBusArgument &operator>>(const QDBusArgument &argument, Properties &entry) {
    argument.beginStructure();
    argument >> entry.id >> entry.properties;
    argument.endStructure();
    return argument;
}
inline QDBusArgument &operator<<(QDBusArgument &argument, const Removed &entry) {
    argument.beginStructure();
    argument << entry.id << entry.names;
    argument.endStructure();
    return argument;
}
inline const QDBusArgument &operator>>(const QDBusArgument &argument, Removed &entry) {
    argument.beginStructure();
    argument >> entry.id >> entry.names;
    argument.endStructure();
    return argument;
}

// Makes the types above known to QtDBus; call once before using them.
inline void registerTypes() {
    qDBusRegisterMetaType<Pixmap>();
    qDBusRegisterMetaType<QList<Pixmap>>();
    qDBusRegisterMetaType<ToolTip>();
    qDBusRegisterMetaType<Layout>();
    qDBusRegisterMetaType<Properties>();
    qDBusRegisterMetaType<QList<Properties>>();
    qDBusRegisterMetaType<Removed>();
    qDBusRegisterMetaType<QList<Removed>>();
}

// A square of one colour, `size` pixels a side.
inline Pixmap square(const QColor &color, int size) {
    Pixmap pixmap{size, size, {}};
    pixmap.data.reserve(size * size * 4);
    for (int i = 0; i < size * size; ++i)
        pixmap.data.append(char(color.alpha())).append(char(color.red()))
            .append(char(color.green())).append(char(color.blue()));
    return pixmap;
}
// An IconPixmap value: a square of `color` at each size.
inline QVariant pixmaps(const QColor &color, const QList<int> &sizes) {
    QList<Pixmap> list;
    for (int size : sizes)
        list << square(color, size);
    return QVariant::fromValue(list);
}
inline QVariant toolTip(const QString &title, const QString &text) {
    return QVariant::fromValue(ToolTip{{}, {}, title, text});
}

// Answers what the message asked with `reply` and says it was handled.
inline bool answer(const QDBusConnection &connection, const QDBusMessage &reply) {
    connection.send(reply);
    return true;
}

// The item: org.kde.StatusNotifierItem and its properties.
class Item : public QDBusVirtualObject {
  public:
    Item(const QDBusConnection &bus, const QString &path) : bus_(bus), path_(path) {}
    // What Get and GetAll answer.
    QVariantMap properties;
    // false: GetAll fails, as with some implementations; Get still works.
    bool getAll = true;
    // false: there is no Activate method, as with Ayatana's items.
    bool activate = true;
    // "activate X Y", "secondary X Y", "context X Y", "scroll DELTA ORIENTATION", in order.
    QStringList calls;
    std::function<void(const QString &)> called;
    QString path() const { return path_; }
    // Sends one of the item's signals ("NewTitle", "NewStatus", ...).
    void send(const QString &signal, const QVariantList &arguments = {}) const {
        auto message = QDBusMessage::createSignal(path_, itemInterface, signal);
        message.setArguments(arguments);
        bus_.send(message);
    }
    // Sets a property, or removes it given an invalid value, and sends `signal`.
    void change(const QString &name, const QVariant &value, const QString &signal,
                const QVariantList &arguments = {}) {
        if (value.isValid())
            properties[name] = value;
        else
            properties.remove(name);
        send(signal, arguments);
    }
    QString introspect(const QString &) const override {
        return "<interface name=\"org.kde.StatusNotifierItem\"/>";
    }
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override {
        const auto arguments = message.arguments();
        const QString member = message.member();
        if (message.interface() == propertiesInterface) {
            if (member == "GetAll" && getAll)
                return answer(connection, message.createReply(QVariant::fromValue(properties)));
            if (member == "Get" && arguments.size() == 2 && properties.contains(arguments[1].toString()))
                return answer(connection, message.createReply(QVariant::fromValue(
                                              QDBusVariant(properties[arguments[1].toString()]))));
            return answer(connection, message.createErrorReply(QDBusError::UnknownProperty, "no such property"));
        }
        auto log = [&](const QString &line) {
            calls << line;
            if (called)
                called(line);
            return answer(connection, message.createReply());
        };
        const QString signature = message.signature();
        if (member == "Activate" && signature == "ii" && activate)
            return log(QString("activate %1 %2").arg(arguments[0].toInt()).arg(arguments[1].toInt()));
        if (member == "SecondaryActivate" && signature == "ii")
            return log(QString("secondary %1 %2").arg(arguments[0].toInt()).arg(arguments[1].toInt()));
        if (member == "ContextMenu" && signature == "ii")
            return log(QString("context %1 %2").arg(arguments[0].toInt()).arg(arguments[1].toInt()));
        if (member == "Scroll" && signature == "is")
            return log(QString("scroll %1 %2").arg(arguments[0].toInt()).arg(arguments[1].toString()));
        return answer(connection, message.createErrorReply(QDBusError::UnknownMethod, "no " + member + " here"));
    }

  private:
    QDBusConnection bus_;
    QString path_;
};

// The item's menu: com.canonical.dbusmenu over a tree of entries.
class Menu : public QDBusVirtualObject {
  public:
    struct Entry {
        QVariantMap properties;
        QList<int> children;
    };
    Menu(const QDBusConnection &bus, const QString &path) : bus_(bus), path_(path) {}
    // The tree, by id; 0 is the root.
    std::map<int, Entry> entries = sample();
    uint revision = 1;
    // What AboutToShow answers: whether the host should fetch the layout again.
    bool needUpdate = false;
    // "layout PARENT", "abouttoshow ID", "event ID TYPE", in order.
    QStringList calls;
    std::function<void(const QString &)> called;
    // Answers GetLayout with these arguments instead of the tree when set (malformed layouts).
    std::function<QVariantList()> layoutReply;
    QString path() const { return path_; }

    // Every kind of entry the panel draws: a separator, a submenu with a check box and two radio
    // buttons, a disabled and a hidden entry, mnemonics and an icon.
    static std::map<int, Entry> sample() {
        return {{0, {{{"children-display", "submenu"}}, {1, 2, 3, 4, 5, 6}}},
                {1, {{{"label", "_Open probe"}}, {}}},
                {2, {{{"type", "separator"}}, {}}},
                {3, {{{"label", "_Options"}, {"children-display", "submenu"}}, {31, 32, 33}}},
                {31, {{{"label", "Show __hidden"}, {"toggle-type", "checkmark"}, {"toggle-state", 1}}, {}}},
                {32, {{{"label", "Radio _A"}, {"toggle-type", "radio"}, {"toggle-state", 1}}, {}}},
                {33, {{{"label", "Radio _B"}, {"toggle-type", "radio"}, {"toggle-state", 0}}, {}}},
                {4, {{{"label", "Disabled"}, {"enabled", false}}, {}}},
                {5, {{{"label", "Hidden"}, {"visible", false}}, {}}},
                {6, {{{"label", "_Quit"}, {"icon-name", "application-exit"}}, {}}}};
    }
    Layout layout(int id, int depth = 8) const {
        Layout result{id, {}, {}};
        const auto found = entries.find(id);
        if (found == entries.end())
            return result;
        result.properties = found->second.properties;
        if (depth > 0)
            for (int child : found->second.children)
                result.children << QDBusVariant(QVariant::fromValue(layout(child, depth - 1)));
        return result;
    }
    // Tells the host the tree changed.
    void layoutUpdated(int parent = 0) {
        auto message = QDBusMessage::createSignal(path_, menuInterface, "LayoutUpdated");
        message.setArguments({QVariant::fromValue(++revision), parent});
        bus_.send(message);
    }
    // Changes and removes entries' properties, here and with ItemsPropertiesUpdated.
    void propertiesUpdated(const QList<Properties> &updated, const QList<Removed> &removed) {
        for (const auto &entry : updated)
            for (auto it = entry.properties.begin(); it != entry.properties.end(); ++it)
                entries[entry.id].properties[it.key()] = it.value();
        for (const auto &entry : removed)
            for (const auto &name : entry.names)
                entries[entry.id].properties.remove(name);
        auto message = QDBusMessage::createSignal(path_, menuInterface, "ItemsPropertiesUpdated");
        message.setArguments({QVariant::fromValue(updated), QVariant::fromValue(removed)});
        bus_.send(message);
    }
    QString introspect(const QString &) const override {
        return "<interface name=\"com.canonical.dbusmenu\"/>";
    }
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override {
        const auto arguments = message.arguments();
        const QString member = message.member(), signature = message.signature();
        auto log = [&](const QString &line) {
            calls << line;
            if (called)
                called(line);
        };
        if (message.interface() == propertiesInterface) {
            const QVariantMap properties{{"Version", 3u}, {"Status", "normal"}, {"TextDirection", "ltr"}};
            if (member == "GetAll")
                return answer(connection, message.createReply(QVariant::fromValue(properties)));
            if (member == "Get" && arguments.size() == 2 && properties.contains(arguments[1].toString()))
                return answer(connection, message.createReply(QVariant::fromValue(
                                              QDBusVariant(properties[arguments[1].toString()]))));
            return answer(connection, message.createErrorReply(QDBusError::UnknownProperty, "no such property"));
        }
        if (member == "GetLayout" && signature == "iias") {
            log(QString("layout %1").arg(arguments[0].toInt()));
            auto reply = message.createReply();
            if (layoutReply)
                reply.setArguments(layoutReply());
            else
                reply.setArguments({QVariant::fromValue(revision),
                                    QVariant::fromValue(layout(arguments[0].toInt(),
                                                               arguments[1].toInt() < 0 ? 8 : arguments[1].toInt()))});
            return answer(connection, reply);
        }
        if (member == "AboutToShow" && signature == "i") {
            log(QString("abouttoshow %1").arg(arguments[0].toInt()));
            return answer(connection, message.createReply(QVariant(needUpdate)));
        }
        if (member == "Event" && signature == "isvu") {
            log(QString("event %1 %2").arg(arguments[0].toInt()).arg(arguments[1].toString()));
            return answer(connection, message.createReply());
        }
        return answer(connection, message.createErrorReply(QDBusError::UnknownMethod, "no " + member + " here"));
    }

  private:
    QDBusConnection bus_;
    QString path_;
};
} // namespace faketray
