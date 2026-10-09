// SPDX-License-Identifier: GPL-3.0-or-later
// The notification daemon on a private session bus: a dbus-daemon this test starts and kills,
// never the real one.
#include "notification_service.hpp"
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDir>
#include <QProcess>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

namespace {
constexpr auto service = "org.freedesktop.Notifications";
constexpr auto path = "/org/freedesktop/Notifications";
} // namespace

// The client side: receives the daemon's signals.
class Client : public QObject {
    Q_OBJECT
  public:
    QList<QPair<uint, uint>> closed;
    QList<QPair<uint, QString>> actions;
  public Q_SLOTS:
    void NotificationClosed(uint id, uint reason) { closed.append({id, reason}); }
    void ActionInvoked(uint id, const QString &key) { actions.append({id, key}); }
};

class NotificationsDbusTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir_;
    QProcess bus_;
    QString address_;
    QString serverName_ = "paw-test-server-" + QUuid::createUuid().toString(QUuid::Id128);
    QString clientName_ = "paw-test-client-" + QUuid::createUuid().toString(QUuid::Id128);
    NotificationCenter center_;
    NotificationService *server_ = nullptr;
    Client client_;

    QDBusConnection clientBus() { return QDBusConnection::connectToBus(address_, clientName_); }
    // A call that lets this thread's event loop run, since the daemon lives in it too.
    QDBusMessage call(const QString &method, const QVariantList &arguments) {
        auto message = QDBusMessage::createMethodCall(service, path, service, method);
        message.setArguments(arguments);
        return clientBus().call(message, QDBus::BlockWithGui, 5000);
    }
    uint notify(const QString &summary, const QString &body = {}, const QStringList &actions = {},
                const QVariantMap &hints = {}, int timeout = -1, uint replaces = 0,
                const QString &app = "probe", const QString &icon = {}) {
        const auto reply = call("Notify", {app, replaces, icon, summary, body, actions, hints, timeout});
        if (reply.type() != QDBusMessage::ReplyMessage) {
            qWarning() << reply.errorMessage();
            return 0;
        }
        return reply.arguments().at(0).toUInt();
    }

  private Q_SLOTS:
    void initTestCase() {
        if (QStandardPaths::findExecutable("dbus-daemon").isEmpty())
            QSKIP("dbus-daemon is not installed");
        // No service directories: a bus name nobody owns stays unowned, whatever is installed.
        QFile config(dir_.filePath("bus.conf"));
        QVERIFY(config.open(QIODevice::WriteOnly));
        config.write(("<busconfig><type>session</type><listen>unix:dir=" + dir_.path() +
                      "</listen><auth>EXTERNAL</auth><policy context=\"default\">"
                      "<allow send_destination=\"*\" eavesdrop=\"true\"/><allow eavesdrop=\"true\"/>"
                      "<allow own=\"*\"/></policy></busconfig>").toUtf8());
        config.close();
        bus_.start("dbus-daemon", {"--config-file=" + dir_.filePath("bus.conf"), "--nofork",
                                   "--print-address=1"});
        QVERIFY(bus_.waitForStarted());
        QVERIFY(bus_.waitForReadyRead(5000));
        address_ = QString::fromUtf8(bus_.readLine()).trimmed();
        QVERIFY2(address_.startsWith("unix:"), qPrintable(address_));
        paw::NotificationsConfig config_;
        center_.configure(config_);
        server_ = new NotificationService(center_, this);
        QDBusConnection serverBus = QDBusConnection::connectToBus(address_, serverName_);
        QVERIFY(serverBus.isConnected());
        QVERIFY2(server_->start(serverBus), qPrintable(server_->error()));
        QVERIFY(center_.serving());
        auto bus = clientBus();
        QVERIFY(bus.connect(service, path, service, "NotificationClosed", &client_,
                            SLOT(NotificationClosed(uint, uint))));
        QVERIFY(bus.connect(service, path, service, "ActionInvoked", &client_,
                            SLOT(ActionInvoked(uint, QString))));
    }
    void cleanupTestCase() {
        delete server_;
        server_ = nullptr;
        if (bus_.state() != QProcess::NotRunning) {
            bus_.kill(); // this test's own daemon, by its process
            bus_.waitForFinished(3000);
        }
    }
    void serverInformation() {
        QDBusMessage reply = call("GetServerInformation", {});
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QCOMPARE(reply.arguments().size(), 4);
        QCOMPARE(reply.arguments()[0].toString(), QString("paw"));
        QCOMPARE(reply.arguments()[3].toString(), QString("1.2"));
    }
    void capabilities() {
        const auto reply = call("GetCapabilities", {});
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        const auto capabilities = reply.arguments().at(0).toStringList();
        for (const char *name : {"actions", "body", "body-markup", "body-hyperlinks", "icon-static", "persistence"})
            QVERIFY2(capabilities.contains(name), name);
        QVERIFY(!capabilities.contains("sound"));
    }
    void notifyShowsACard() {
        const uint id = notify("Hello", "a <b>bold</b> <script>x</script> & more", {"default", "Open", "reply", "Reply"},
                               {{"urgency", QVariant::fromValue<uchar>(2)}, {"value", 40}, {"category", "im.received"},
                                {"desktop-entry", "org.example.App"}},
                               0, 0, "Example", "mail-unread");
        QVERIFY(id > 0);
        const auto *n = center_.cards()->find(id);
        QVERIFY(n);
        QCOMPARE(n->app, QString("Example"));
        QCOMPARE(n->summary, QString("Hello"));
        QCOMPARE(n->body, QString("a <b>bold</b> x &amp; more"));
        QCOMPARE(n->icon, QString("mail-unread"));
        QCOMPARE(n->urgency, int(Notification::Critical));
        QCOMPARE(n->progress, 40);
        QCOMPARE(n->timeout, 0);
        QCOMPARE(n->category, QString("im.received"));
        QCOMPARE(n->desktopEntry, QString("org.example.App"));
        QCOMPARE(n->actions.size(), size_t(2));
        QCOMPARE(n->actions[1].second, QString("Reply"));
        center_.dismiss(id);
    }
    void ids() {
        const uint a = notify("one", {}, {}, {}, 0);
        const uint b = notify("two", {}, {}, {}, 0);
        QVERIFY(a > 0 && b > a);
        const int cards = center_.cards()->count();
        QCOMPARE(notify("one, updated", {}, {}, {}, 0, a), a);
        QCOMPARE(center_.cards()->count(), cards);
        QCOMPARE(center_.cards()->find(a)->summary, QString("one, updated"));
        center_.dismiss(a);
        center_.dismiss(b);
    }
    void closeNotification() {
        const uint id = notify("to close", {}, {}, {}, 0);
        client_.closed.clear();
        call("CloseNotification", {id});
        QTRY_COMPARE(client_.closed.size(), 1);
        QCOMPARE(client_.closed[0].first, id);
        QCOMPARE(client_.closed[0].second, uint(NotificationCenter::Closed));
        QVERIFY(!center_.cards()->find(id));
        // Closing what does not exist is not an error and sends nothing.
        auto reply = call("CloseNotification", {uint(987654)});
        QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
        QCoreApplication::processEvents(); // a signal would have come before the reply
        QCOMPARE(client_.closed.size(), 1);
    }
    void actionInvoked() {
        const uint id = notify("act", {}, {"default", "Open", "yes", "Yes"}, {}, 0);
        client_.actions.clear();
        client_.closed.clear();
        center_.activate(id);
        QTRY_COMPARE(client_.actions.size(), 1);
        QCOMPARE(client_.actions[0].first, id);
        QCOMPARE(client_.actions[0].second, QString("default"));
        QTRY_COMPARE(client_.closed.size(), 1);
        QCOMPARE(client_.closed[0].second, uint(NotificationCenter::Dismissed));
        const uint second = notify("act2", {}, {"yes", "Yes"}, {}, 0);
        client_.actions.clear();
        center_.invoke(second, "yes");
        QTRY_COMPARE(client_.actions.size(), 1);
        QCOMPARE(client_.actions[0].second, QString("yes"));
    }
    void expiry() {
        client_.closed.clear();
        const uint id = notify("brief", {}, {}, {}, 60);
        QTRY_COMPARE_WITH_TIMEOUT(client_.closed.size(), 1, 3000);
        QCOMPARE(client_.closed[0].first, id);
        QCOMPARE(client_.closed[0].second, uint(NotificationCenter::Expired));
    }
    void stackTagHint() {
        const uint a = notify("volume 10", {}, {}, {{"x-canonical-private-synchronous", "volume"}}, 0);
        const uint b = notify("volume 20", {}, {}, {{"x-canonical-private-synchronous", "volume"}}, 0);
        QCOMPARE(a, b);
        QCOMPARE(center_.cards()->find(a)->summary, QString("volume 20"));
        center_.dismiss(a);
    }
    void imageData() {
        QByteArray pixels(2 * 6, '\x40'); // 2 rows of 2 RGB pixels, stride 6
        QDBusArgument argument;
        argument.beginStructure();
        argument << 2 << 2 << 6 << false << 8 << 3 << pixels;
        argument.endStructure();
        const uint id = notify("pic", {}, {}, {{"image-data", QVariant::fromValue(argument)}}, 0);
        QVERIFY(center_.cards()->find(id));
        const QImage image = center_.cards()->find(id)->image;
        QCOMPARE(image.size(), QSize(2, 2));
        QCOMPARE(image.pixelColor(1, 1), QColor(0x40, 0x40, 0x40));
        center_.dismiss(id);
        // A large picture is scaled down to what a card can use.
        QDBusArgument large;
        large.beginStructure();
        large << 400 << 200 << 1200 << false << 8 << 3 << QByteArray(1200 * 200, '\x20');
        large.endStructure();
        const uint big = notify("large", {}, {}, {{"image-data", QVariant::fromValue(large)}}, 0);
        QCOMPARE(center_.cards()->find(big)->image.size(), QSize(128, 64));
        center_.dismiss(big);
        // A picture whose data is too short is ignored, not read past its end.
        QDBusArgument bad;
        bad.beginStructure();
        bad << 100 << 100 << 300 << false << 8 << 3 << pixels;
        bad.endStructure();
        const uint id2 = notify("bad", {}, {}, {{"image-data", QVariant::fromValue(bad)}}, 0);
        QVERIFY(center_.cards()->find(id2)->image.isNull());
        center_.dismiss(id2);
    }
    void hintParsing() {
        auto n = NotificationsAdaptor::parse("app", "file:///usr/share/icons/x.png", "s", "b", {"a", "A", "orphan"},
                                             {{"urgency", 7}, {"value", 250}, {"resident", true},
                                              {"transient", true}, {"image-path", "/tmp/y.png"}}, 5000);
        QCOMPARE(n.urgency, 2); // out-of-range urgency clamps
        QCOMPARE(n.progress, 100);
        QVERIFY(n.resident && n.transient);
        QCOMPARE(n.actions.size(), size_t(1)); // a key without a label is dropped
        QCOMPARE(n.icon, QString("/tmp/y.png")); // image-path wins over the icon parameter
        QCOMPARE(n.timeout, 5000);
        auto fileIcon = NotificationsAdaptor::parse("app", "file:///usr/share/icons/x.png", "s", "", {}, {}, -7);
        QCOMPARE(fileIcon.icon, QString("/usr/share/icons/x.png"));
        QCOMPARE(fileIcon.timeout, -1);
        QCOMPARE(fileIcon.urgency, int(Notification::Normal));
        QCOMPARE(fileIcon.progress, -1);
        // Absurd sizes are cut rather than trusted.
        auto huge = NotificationsAdaptor::parse(QString(1000, 'a'), {}, QString(5000, 'b'), QString(50000, 'c'), {}, {}, 0);
        QVERIFY(huge.app.size() <= 200 && huge.summary.size() <= 500 && huge.body.size() <= 8000);
    }
    // Random, often wrong, notifications and closes: the daemon must neither crash nor break its
    // own limits, and answers every call.
    void fuzz() {
        QRandomGenerator random(20260929);
        auto text = [&](int most) {
            QString result;
            const int length = random.bounded(most);
            for (int i = 0; i < length; ++i) {
                static const QString alphabet = "ab <>&;\"'/=\n\t\u00e5\u4e16{}#x";
                result += alphabet[random.bounded(alphabet.size())];
            }
            return result;
        };
        auto value = [&]() -> QVariant {
            switch (random.bounded(8)) {
            case 0: return int(random.bounded(400)) - 100;
            case 1: return text(20);
            case 2: return random.bounded(2) == 0;
            case 3: return QVariant::fromValue<uchar>(uchar(random.bounded(256)));
            case 4: return double(random.bounded(1000)) / 7;
            case 5: return QStringList{"x", "y"};
            case 6: {
                QDBusArgument argument;
                argument.beginStructure();
                argument << int(random.bounded(6)) << int(random.bounded(6)) << int(random.bounded(20))
                         << (random.bounded(2) == 0) << int(random.bounded(10)) << int(random.bounded(6))
                         << QByteArray(random.bounded(80), 'p');
                argument.endStructure();
                return QVariant::fromValue(argument);
            }
            default: return QVariant::fromValue(uint(random.bounded(1000)));
            }
        };
        static const char *const keys[] = {"urgency", "value", "image-data", "image_data", "icon_data", "image-path",
                                           "resident", "transient", "category", "desktop-entry",
                                           "x-canonical-private-synchronous", "synchronous", "x-dunst-stack-tag", "junk"};
        for (int i = 0; i < 1500; ++i) {
            QVariantMap hints;
            for (int h = random.bounded(5); h > 0; --h)
                hints[keys[random.bounded(int(std::size(keys)))]] = value();
            QStringList actions;
            for (int a = random.bounded(7); a > 0; --a)
                actions << text(8);
            const uint replaces = random.bounded(3) == 0 ? uint(random.bounded(60)) : 0;
            const uint id = notify(text(30), text(200), actions, hints, int(random.bounded(200)) - 50,
                                   replaces, text(10), random.bounded(4) == 0 ? "/nonexistent/x.png" : text(6));
            QVERIFY2(id > 0, qPrintable(QString::number(i)));
            QVERIFY(center_.cards()->count() <= 4);
            QVERIFY(center_.history()->count() <= 100);
            if (random.bounded(5) == 0)
                call("CloseNotification", {uint(random.bounded(80))});
            if (random.bounded(9) == 0 && center_.cards()->count() > 0)
                center_.dismiss(center_.cards()->items().front().id);
        }
        // Ids are unique across what is kept.
        QSet<uint> seen;
        for (const auto &n : center_.history()->items()) {
            QVERIFY(!seen.contains(n.id));
            seen.insert(n.id);
        }
        // Still answering.
        QCOMPARE(call("GetServerInformation", {}).type(), QDBusMessage::ReplyMessage);
        center_.clearHistory();
        while (center_.cards()->count() > 0)
            center_.dismiss(center_.cards()->items().front().id);
    }
    void secondDaemonIsRefused() {
        NotificationCenter other;
        NotificationService second(other);
        QVERIFY(!second.start(QDBusConnection::connectToBus(address_, "paw-test-second-" + clientName_)));
        QVERIFY(second.error().contains("another notification daemon"));
        QVERIFY(!other.serving());
        // The first keeps answering.
        QVERIFY(notify("still here", {}, {}, {}, 0) > 0);
    }
    // Last, since it takes down the service the others use.
    void releasesTheName() {
        NotificationCenter other;
        NotificationService transient(other);
        // Once the owner goes, the name can be had.
        delete server_;
        server_ = nullptr;
        QVERIFY2(transient.start(QDBusConnection::connectToBus(address_, "paw-test-third-" + clientName_)),
                 qPrintable(transient.error()));
        QVERIFY(other.serving());
    }
};
QTEST_MAIN(NotificationsDbusTest)
#include "notifications_dbus_test.moc"
