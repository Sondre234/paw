// SPDX-License-Identifier: GPL-3.0-or-later
// The polkit authentication dialog in its view, on the offscreen platform, with stand-ins for
// polkit's helper: typing and Enter, a wrong password, choosing another user with the keys and
// the pointer, a click beside it, Escape and Cancel, one user named instead of a list, and the
// next request opening with an empty field.
#include "authentication.hpp"
#include "controller.hpp"
#include "view.hpp"
#include <QFile>
#include <QGuiApplication>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QTest>
#include <unistd.h>

namespace {
// polkit's helper: it asks at once and takes "secret", noting what it hears.
class FakeHelper : public AuthConversation {
  public:
    FakeHelper(QStringList &log, const QString &name) : log_(log), name_(name) {}
    void start() override {
        log_ << "start " + name_;
        Q_EMIT request("Password: ", false);
    }
    void respond(const QString &answer) override {
        log_ << "answer " + name_ + " " + answer;
        Q_EMIT completed(answer == "secret");
    }
    void cancel() override { log_ << "cancel " + name_; }

  private:
    QStringList &log_;
    QString name_;
};
QQuickItem *find(QQuickItem *item, const QString &name) {
    if (!item)
        return nullptr;
    if (item->objectName() == name)
        return item;
    for (auto *child : item->childItems())
        if (auto *found = find(child, name))
            return found;
    return nullptr;
}
// QTest::keyClicks is for widgets only.
void type(QWindow *window, const QString &text) {
    for (QChar c : text)
        QTest::keyClick(window, c.toLatin1());
}
QPoint centre(QQuickItem *item) {
    return item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
}
AuthRequest request(const QString &cookie, bool several = true) {
    QList<AuthIdentity> users{{"unix-user:" + QString::number(getuid()), "me", "Me Myself", getuid()}};
    if (several)
        users << AuthIdentity{"unix-user:0", "root", "", 0};
    return {"org.freedesktop.policykit.exec",
            "Authentication is needed to run `/usr/bin/true' as the super user",
            "",
            {{"command_line", "/usr/bin/true --flag"}},
            cookie,
            users};
}
} // namespace

class AuthDialogTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir_;

  private Q_SLOTS:
    void dialog() {
        QFile config(dir_.filePath("init.lua"));
        QVERIFY(config.open(QIODevice::WriteOnly));
        config.write("return { animations = { enabled = false } }");
        config.close();
        ShellController controller(config.fileName().toStdString());
        AuthView view(controller, QGuiApplication::primaryScreen());
        QCOMPARE(view.status(), QQuickView::Ready);
        auto *auth = controller.authentication();
        QStringList log;
        auth->setConversations([&log](const AuthIdentity &identity, const QString &) {
            return new FakeHelper(log, identity.name);
        });
        QStringList outcomes;
        auto note = [&outcomes](const QString &name) {
            return [&outcomes, name](Authentication::Outcome outcome) {
                outcomes << name + (outcome == Authentication::Outcome::Authorized ? " authorized"
                                    : outcome == Authentication::Outcome::Dismissed ? " dismissed"
                                                                                     : " withdrawn");
            };
        };
        auto item = [&](const char *name) { return find(view.rootObject(), name); };
        QVERIFY(!view.isVisible());

        // It opens with the keyboard in the field, the user logged in chosen of the two.
        auth->add(request("a"), note("a"));
        QTRY_VERIFY(view.isVisible());
        auto *field = item("authField");
        QVERIFY(field);
        QTRY_VERIFY(field->hasActiveFocus());
        QCOMPARE(log, QStringList{"start me"});
        QVERIFY(item("authUsers")->isVisible() && !item("authUser")->isVisible());
        QCOMPARE(item("authMessage")->property("text").toString(), auth->message());
        QCOMPARE(item("authCommand")->property("text").toString(), QString("/usr/bin/true --flag"));
        QCOMPARE(field->property("placeholderText").toString(), QString("Password"));

        // A wrong password: said, the field emptied, the dialog still there.
        type(&view, "wrong");
        QCOMPARE(field->property("text").toString(), QString("wrong"));
        QTest::keyClick(&view, Qt::Key_Return);
        QVERIFY(log.contains("answer me wrong"));
        QTRY_COMPARE(item("authStatus")->property("text").toString(),
                     QString("The password was not accepted. Try again."));
        QCOMPARE(field->property("text").toString(), QString());
        QVERIFY(view.isVisible() && outcomes.isEmpty());

        // Down and Up choose another user, and a click on one does.
        log.clear();
        QTest::keyClick(&view, Qt::Key_Down);
        QCOMPARE(auth->identity(), 1);
        QCOMPARE(log, (QStringList{"cancel me", "start root"}));
        QTest::keyClick(&view, Qt::Key_Up);
        QCOMPARE(auth->identity(), 0);
        QTest::mouseClick(&view, Qt::LeftButton, {}, centre(item("authUser1")));
        QCOMPARE(auth->identity(), 1);
        QTRY_VERIFY(field->hasActiveFocus());
        // A click beside the dialog gives nothing up.
        QTest::mouseClick(&view, Qt::LeftButton, {}, QPoint(5, 5));
        QVERIFY(view.isVisible() && outcomes.isEmpty());
        QVERIFY(field->hasActiveFocus());
        type(&view, "secret");
        QTest::keyClick(&view, Qt::Key_Return);
        QCOMPARE(outcomes, QStringList{"a authorized"});
        QVERIFY(log.contains("answer root secret"));
        QTRY_VERIFY(!view.isVisible());

        // One user is named rather than listed; Escape gives it up.
        auth->add(request("b", false), note("b"));
        QTRY_VERIFY(view.isVisible());
        QVERIFY(!item("authUsers")->isVisible() && item("authUser")->isVisible());
        QCOMPARE(item("authUser")->property("text").toString(), QString("As Me Myself (me)"));
        QTRY_VERIFY(field->hasActiveFocus());
        QTest::keyClick(&view, Qt::Key_Escape);
        QCOMPARE(outcomes.last(), QString("b dismissed"));
        QTRY_VERIFY(!view.isVisible());

        // The next of two opens with an empty field; Cancel gives it up.
        auth->add(request("c"), note("c"));
        const int d = auth->add(request("d"), note("d"));
        QTRY_VERIFY(view.isVisible());
        QTRY_VERIFY(field->hasActiveFocus());
        type(&view, "half");
        auth->withdraw(d - 1);
        QCOMPARE(outcomes.last(), QString("c withdrawn"));
        QVERIFY(auth->open());
        QTRY_COMPARE(field->property("text").toString(), QString());
        QTest::mouseClick(&view, Qt::LeftButton, {}, centre(item("authCancel")));
        QCOMPARE(outcomes.last(), QString("d dismissed"));
        QTRY_VERIFY(!view.isVisible());
        // As it went, it still said what it asked.
        QCOMPARE(item("authMessage")->property("text").toString(), auth->message());
    }
};

QTEST_MAIN(AuthDialogTest)
#include "auth_dialog_test.moc"
