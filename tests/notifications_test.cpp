// SPDX-License-Identifier: GPL-3.0-or-later
#include "notifications.hpp"
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>

namespace {
Notification make(const QString &summary, int timeout = -1, int urgency = Notification::Normal) {
    Notification n;
    n.app = "test";
    n.summary = summary;
    n.timeout = timeout;
    n.urgency = urgency;
    return n;
}
paw::NotificationsConfig config() {
    paw::NotificationsConfig c;
    c.timeout = 5000;
    return c;
}
} // namespace

class NotificationsTest : public QObject {
    Q_OBJECT
  private Q_SLOTS:
    void markup_data() {
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("output");
        QTest::newRow("plain") << "hello" << "hello";
        QTest::newRow("bold") << "a <b>bold</b> b" << "a <b>bold</b> b";
        QTest::newRow("case") << "<B>x</B><I>y</I><U>z</U>" << "<b>x</b><i>y</i><u>z</u>";
        QTest::newRow("unclosed") << "<b>x <i>y" << "<b>x <i>y</i></b>";
        QTest::newRow("stray closer") << "x</b>y" << "xy";
        QTest::newRow("closer closes inner") << "<b>x<i>y</b>z" << "<b>x<i>y</i></b>z";
        QTest::newRow("unknown tags dropped") << "<span color='red'>x</span><script>y</script>" << "xy";
        QTest::newRow("lone angle") << "1 < 2 > 0" << "1 &lt; 2 &gt; 0";
        QTest::newRow("tag-like without end") << "a <b" << "a &lt;b";
        QTest::newRow("ampersand") << "Tom & Jerry" << "Tom &amp; Jerry";
        QTest::newRow("entities") << "&lt;b&gt; &amp; &quot;q&quot; &apos;a&apos; &#65;&#x42;"
                                  << "&lt;b&gt; &amp; \"q\" 'a' AB";
        QTest::newRow("escaped tag is text") << "&lt;b&gt;x" << "&lt;b&gt;x";
        QTest::newRow("newline") << "a\nb\r\nc" << "a<br/>b<br/>c";
        QTest::newRow("br") << "a<br>b<br/>c" << "a<br/>b<br/>c";
        QTest::newRow("link") << "<a href=\"https://example.org/?a=1&b=2\">site</a>"
                              << "<a href=\"https://example.org/?a=1&amp;b=2\">site</a>";
        QTest::newRow("single quoted link") << "<a href='mailto:x@example.org'>m</a>"
                                            << "<a href=\"mailto:x@example.org\">m</a>";
        QTest::newRow("script link dropped") << "<a href=\"javascript:alert(1)\">x</a>y" << "xy";
        QTest::newRow("file link dropped") << "<a href=\"file:///etc/passwd\">x</a>" << "x";
        QTest::newRow("link without href") << "<a>x</a>" << "x";
        QTest::newRow("image alt") << "<img src=\"/x.png\" alt=\"logo\"/>after" << "logoafter";
        QTest::newRow("image without alt") << "<img src=\"/x.png\"/>after" << "after";
        QTest::newRow("angle bracket inside a quoted attribute")
            << "<a href=\"https://e.org/?q=>x\">y</a>"
            << "<a href=\"https://e.org/?q=&gt;x\">y</a>";
        QTest::newRow("empty") << "" << "";
    }
    void markup() {
        QFETCH(QString, input);
        QFETCH(QString, output);
        QCOMPARE(notificationMarkup(input), output);
    }
    void newNotification() {
        NotificationCenter center;
        center.configure(config());
        QSignalSpy received(&center, &NotificationCenter::received);
        const uint a = center.notify(make("one"));
        const uint b = center.notify(make("two"));
        QVERIFY(a != 0 && b > a);
        QCOMPARE(received.count(), 2);
        QCOMPARE(center.cards()->count(), 2);
        QCOMPARE(center.history()->count(), 2);
        QCOMPARE(center.unread(), 2);
        // Newest first.
        QCOMPARE(center.cards()->index(0).data(NotificationModel::SummaryRole).toString(), QString("two"));
        QVERIFY(center.timerRunning(a));
    }
    void replaces() {
        NotificationCenter center;
        center.configure(config());
        const uint a = center.notify(make("one"));
        const uint b = center.notify(make("two"));
        QCOMPARE(center.notify(make("one again"), a), a);
        QCOMPARE(center.cards()->count(), 2);
        QCOMPARE(center.history()->count(), 2);
        QCOMPARE(center.cards()->find(a)->summary, QString("one again"));
        // An id nobody holds gets a new notification.
        const uint c = center.notify(make("three"), 9999);
        QVERIFY(c != 9999 && c != a && c != b);
        QCOMPARE(center.cards()->count(), 3);
        // Replacing a card that already left brings it back.
        center.dismiss(a);
        QCOMPARE(center.cards()->count(), 2);
        QCOMPARE(center.notify(make("back"), a), a);
        QCOMPARE(center.cards()->count(), 3);
    }
    void stackTag() {
        NotificationCenter center;
        center.configure(config());
        auto tagged = make("volume 10");
        tagged.tag = "volume";
        const uint a = center.notify(tagged);
        tagged.summary = "volume 20";
        QCOMPARE(center.notify(tagged), a);
        QCOMPARE(center.history()->count(), 1);
        QCOMPARE(center.cards()->find(a)->summary, QString("volume 20"));
        auto other = tagged;
        other.app = "other";
        QVERIFY(center.notify(other) != a);
        QCOMPARE(center.history()->count(), 2);
    }
    void expires() {
        NotificationCenter center;
        center.configure(config());
        QSignalSpy closed(&center, &NotificationCenter::closed);
        const uint a = center.notify(make("brief", 40));
        QVERIFY(closed.wait(2000));
        QCOMPARE(closed.at(0).at(0).toUInt(), a);
        QCOMPARE(closed.at(0).at(1).toUInt(), uint(NotificationCenter::Expired));
        QCOMPARE(center.cards()->count(), 0);
        QCOMPARE(center.history()->count(), 1);
        // Not seen by the user, so still unread.
        QCOMPARE(center.unread(), 1);
        QVERIFY(!center.timerRunning(a));
    }
    void lifetimes() {
        NotificationCenter center;
        auto c = config();
        center.configure(c);
        QCOMPARE(center.lifetime(make("x")), 5000);
        QCOMPARE(center.lifetime(make("x", 1234)), 1234);
        QCOMPARE(center.lifetime(make("x", 0)), 0);
        QCOMPARE(center.lifetime(make("x", -1, Notification::Low)), 5000);
        // Critical notifications stay until dismissed, whatever they ask for.
        QCOMPARE(center.lifetime(make("x", -1, Notification::Critical)), 0);
        QCOMPARE(center.lifetime(make("x", 100, Notification::Critical)), 0);
        // A card's, for its countdown; none for a card that is gone.
        const uint brief = center.notify(make("brief", 1234));
        QCOMPARE(center.cardLifetime(brief), 1234);
        QCOMPARE(center.cardLifetime(center.notify(make("alarm", 100, Notification::Critical))), 0);
        center.dismiss(brief);
        QCOMPARE(center.cardLifetime(brief), 0);
        c.timeout = 0;
        center.configure(c);
        QCOMPARE(center.lifetime(make("x")), 0);
        QCOMPARE(center.lifetime(make("x", 700)), 700);
        const uint id = center.notify(make("stays"));
        QVERIFY(!center.timerRunning(id));
        QCOMPARE(center.cardLifetime(id), 0);
    }
    void hoverPausesTimer() {
        NotificationCenter center;
        center.configure(config());
        QSignalSpy closed(&center, &NotificationCenter::closed);
        const uint a = center.notify(make("hovered", 150));
        center.hold(a, true);
        QVERIFY(!center.timerRunning(a));
        QTest::qWait(350);
        QCOMPARE(closed.count(), 0);
        QCOMPARE(center.cards()->count(), 1);
        center.hold(a, false);
        QVERIFY(center.timerRunning(a));
        QVERIFY(closed.wait(2000));
        QCOMPARE(center.cards()->count(), 0);
    }
    void holdKeepsRemainingTime() {
        NotificationCenter center;
        center.configure(config());
        QSignalSpy closed(&center, &NotificationCenter::closed);
        const uint a = center.notify(make("x", 400));
        QTest::qWait(250);
        center.hold(a, true);
        QTest::qWait(300);
        center.hold(a, false);
        // Roughly 150 ms were left, not the full 400.
        QElapsedTimer waited;
        waited.start();
        QVERIFY(closed.wait(2000));
        QVERIFY2(waited.elapsed() < 330, qPrintable(QString::number(waited.elapsed())));
    }
    void replaceWhileHeldStaysPaused() {
        NotificationCenter center;
        center.configure(config());
        QSignalSpy closed(&center, &NotificationCenter::closed);
        const uint a = center.notify(make("x", 100));
        center.hold(a, true);
        center.notify(make("y", 100), a);
        QTest::qWait(300);
        QCOMPARE(closed.count(), 0);
        center.hold(a, false);
        QVERIFY(closed.wait(2000));
    }
    void maxVisible() {
        NotificationCenter center;
        auto c = config();
        c.max_visible = 2;
        center.configure(c);
        QSignalSpy closed(&center, &NotificationCenter::closed);
        const uint a = center.notify(make("one"));
        center.notify(make("two"));
        center.notify(make("three"));
        QCOMPARE(center.cards()->count(), 2);
        QVERIFY(!center.cards()->find(a));
        QCOMPARE(center.history()->count(), 3);
        QCOMPARE(closed.count(), 1);
        QCOMPARE(closed.at(0).at(0).toUInt(), a);
        QCOMPARE(closed.at(0).at(1).toUInt(), uint(NotificationCenter::Expired));
        c.max_visible = 1;
        center.configure(c);
        QCOMPARE(center.cards()->count(), 1);
        QCOMPARE(center.cards()->items().front().summary, QString("three"));
    }
    void doNotDisturb() {
        NotificationCenter center;
        auto c = config();
        c.dnd = true;
        center.configure(c);
        QVERIFY(center.dnd());
        center.notify(make("quiet"));
        QCOMPARE(center.cards()->count(), 0);
        QCOMPARE(center.history()->count(), 1);
        QCOMPARE(center.unread(), 1);
        // Critical ones still get through.
        center.notify(make("fire", -1, Notification::Critical));
        QCOMPARE(center.cards()->count(), 1);
        QSignalSpy changed(&center, &NotificationCenter::dndChanged);
        center.toggleDnd();
        QVERIFY(!center.dnd());
        QCOMPARE(changed.count(), 1);
        center.setDnd(false);
        QCOMPARE(changed.count(), 1);
        center.notify(make("loud"));
        QCOMPARE(center.cards()->count(), 2);
        // A reload keeps what was toggled.
        center.configure(c);
        QVERIFY(!center.dnd());
    }
    void disabled() {
        NotificationCenter center;
        auto c = config();
        c.enabled = false;
        center.configure(c);
        QVERIFY(!center.enabled());
        center.notify(make("x"));
        QCOMPARE(center.cards()->count(), 0);
    }
    void dismiss() {
        NotificationCenter center;
        center.configure(config());
        QSignalSpy closed(&center, &NotificationCenter::closed);
        const uint a = center.notify(make("one"));
        center.notify(make("two"));
        center.dismiss(a);
        QCOMPARE(closed.count(), 1);
        QCOMPARE(closed.at(0).at(0).toUInt(), a);
        QCOMPARE(closed.at(0).at(1).toUInt(), uint(NotificationCenter::Dismissed));
        QCOMPARE(center.cards()->count(), 1);
        // The history keeps it, read.
        QCOMPARE(center.history()->count(), 2);
        QCOMPARE(center.unread(), 1);
        QVERIFY(!center.timerRunning(a));
        // A second dismissal is harmless.
        center.dismiss(a);
        QCOMPARE(closed.count(), 1);
        center.dismiss(4242);
        QCOMPARE(closed.count(), 1);
    }
    void defaultAction() {
        NotificationCenter center;
        center.configure(config());
        QSignalSpy invoked(&center, &NotificationCenter::actionInvoked);
        QSignalSpy closed(&center, &NotificationCenter::closed);
        auto n = make("has default");
        n.actions = {{"default", "Open"}, {"reply", "Reply"}};
        const uint a = center.notify(n);
        QVERIFY(center.cards()->index(0).data(NotificationModel::HasDefaultRole).toBool());
        // "default" is not offered as a button.
        const auto buttons = center.cards()->index(0).data(NotificationModel::ActionsRole).toList();
        QCOMPARE(buttons.size(), 1);
        QCOMPARE(buttons[0].toMap()["key"].toString(), QString("reply"));
        center.activate(a);
        QCOMPARE(invoked.count(), 1);
        QCOMPARE(invoked.at(0).at(0).toUInt(), a);
        QCOMPARE(invoked.at(0).at(1).toString(), QString("default"));
        QCOMPARE(closed.count(), 1);
        QCOMPARE(closed.at(0).at(1).toUInt(), uint(NotificationCenter::Dismissed));
        QCOMPARE(center.cards()->count(), 0);
        QCOMPARE(center.unread(), 0);
    }
    void clickWithoutDefaultDismisses() {
        NotificationCenter center;
        center.configure(config());
        QSignalSpy invoked(&center, &NotificationCenter::actionInvoked);
        const uint a = center.notify(make("plain"));
        center.activate(a);
        QCOMPARE(invoked.count(), 0);
        QCOMPARE(center.cards()->count(), 0);
    }
    void namedAction() {
        NotificationCenter center;
        center.configure(config());
        QSignalSpy invoked(&center, &NotificationCenter::actionInvoked);
        auto n = make("x");
        n.actions = {{"yes", "Yes"}};
        const uint a = center.notify(n);
        center.invoke(a, "yes");
        QCOMPARE(invoked.at(0).at(1).toString(), QString("yes"));
        QCOMPARE(center.cards()->count(), 0);
    }
    void residentSurvivesAction() {
        NotificationCenter center;
        center.configure(config());
        QSignalSpy closed(&center, &NotificationCenter::closed);
        auto n = make("x");
        n.actions = {{"a", "A"}};
        n.resident = true;
        const uint id = center.notify(n);
        center.invoke(id, "a");
        QCOMPARE(closed.count(), 0);
        QCOMPARE(center.cards()->count(), 1);
    }
    void closeFromApplication() {
        NotificationCenter center;
        center.configure(config());
        QSignalSpy closed(&center, &NotificationCenter::closed);
        const uint a = center.notify(make("one"));
        QVERIFY(center.closeFromApplication(a));
        QCOMPARE(closed.at(0).at(1).toUInt(), uint(NotificationCenter::Closed));
        QCOMPARE(center.cards()->count(), 0);
        QCOMPARE(center.history()->count(), 0);
        QCOMPARE(center.unread(), 0);
        QVERIFY(!center.closeFromApplication(a));
        QCOMPARE(closed.count(), 1);
        // Already expired, still in the history: removed without a second signal.
        const uint b = center.notify(make("two", 30));
        QVERIFY(closed.wait(2000));
        QCOMPARE(closed.count(), 2);
        QVERIFY(center.closeFromApplication(b));
        QCOMPARE(closed.count(), 2);
        QCOMPARE(center.history()->count(), 0);
    }
    void historyAndUnread() {
        NotificationCenter center;
        auto c = config();
        c.history = 3;
        center.configure(c);
        QSignalSpy unread(&center, &NotificationCenter::unreadChanged);
        for (int i = 0; i < 5; ++i)
            center.notify(make(QString::number(i)));
        QCOMPARE(center.history()->count(), 3);
        QCOMPARE(center.history()->items().front().summary, QString("4"));
        QCOMPARE(center.unread(), 3);
        QVERIFY(unread.count() > 0);
        center.markAllRead();
        QCOMPARE(center.unread(), 0);
        center.notify(make("new"));
        QCOMPARE(center.unread(), 1);
        center.removeFromHistory(center.history()->items().front().id);
        QCOMPARE(center.unread(), 0);
        QCOMPARE(center.history()->count(), 2);
        // Removing a notification that still has a card closes the card too.
        const uint live = center.notify(make("live"));
        center.removeFromHistory(live);
        QVERIFY(!center.cards()->find(live));
        center.clearHistory();
        QCOMPARE(center.history()->count(), 0);
        QCOMPARE(center.unread(), 0);
    }
    // The history by application: the one with the newest notification first, each newest first,
    // an application known by its desktop entry before its name; a change is announced once.
    void historyGroups() {
        NotificationCenter center;
        center.configure(config());
        auto from = [](const QString &app, const QString &entry, const QString &summary) {
            Notification n = make(summary);
            n.app = app;
            n.desktopEntry = entry;
            return n;
        };
        QSignalSpy changed(center.history(), &NotificationModel::groupsChanged);
        center.notify(from("Mail", "", "first mail"));
        center.notify(from("Chat", "org.example.chat", "hello"));
        center.notify(from("Mail", "", "second mail"));
        center.notify(from("Chat (beta)", "org.example.chat", "again"));
        QVERIFY(changed.wait());
        QCOMPARE(changed.count(), 1);
        const auto groups = center.history()->groups();
        QCOMPARE(groups.size(), 2);
        const auto chat = groups[0].toMap(), mail = groups[1].toMap();
        QCOMPARE(chat["key"].toString(), QString("org.example.chat"));
        QCOMPARE(chat["app"].toString(), QString("Chat (beta)"));
        QCOMPARE(mail["key"].toString(), QString("Mail"));
        const auto chats = chat["notifications"].toList(), mails = mail["notifications"].toList();
        QCOMPARE(chats.size(), 2);
        QCOMPARE(chats[0].toMap()["summary"].toString(), QString("again"));
        QCOMPARE(chats[1].toMap()["summary"].toString(), QString("hello"));
        QCOMPARE(mails[0].toMap()["summary"].toString(), QString("second mail"));
        QVERIFY(mails[0].toMap()["notificationId"].toUInt() > 0);
        QVERIFY(!mails[0].toMap()["read"].toBool());
        center.markAllRead();
        QVERIFY(changed.wait());
        QCOMPARE(changed.count(), 2);
        QVERIFY(center.history()->groups()[1].toMap()["notifications"].toList()[0].toMap()["read"].toBool());
    }
    void noHistory() {
        NotificationCenter center;
        auto c = config();
        c.history = 0;
        center.configure(c);
        center.notify(make("x"));
        QCOMPARE(center.cards()->count(), 1);
        QCOMPARE(center.history()->count(), 0);
        QCOMPARE(center.unread(), 0);
    }
    void transientLeavesNoHistory() {
        NotificationCenter center;
        center.configure(config());
        auto n = make("x");
        n.transient = true;
        center.notify(n);
        QCOMPARE(center.cards()->count(), 1);
        QCOMPARE(center.history()->count(), 0);
    }
    void servingFlag() {
        NotificationCenter center;
        QSignalSpy spy(&center, &NotificationCenter::servingChanged);
        QVERIFY(!center.serving());
        center.setServing(true);
        center.setServing(true);
        QCOMPARE(spy.count(), 1);
    }
    void layoutProperties() {
        NotificationCenter center;
        auto c = config();
        c.position = paw::Corner::BottomLeft;
        c.width = 420;
        center.configure(c);
        QVERIFY(center.bottom() && center.left());
        QCOMPARE(center.cardWidth(), 420);
    }
    void cardCountSignal() {
        NotificationCenter center;
        center.configure(config());
        QSignalSpy count(center.cards(), &NotificationModel::countChanged);
        const uint a = center.notify(make("x"));
        center.dismiss(a);
        QCOMPARE(count.count(), 2);
    }
};
QTEST_MAIN(NotificationsTest)
#include "notifications_test.moc"
