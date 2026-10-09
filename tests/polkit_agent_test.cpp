// SPDX-License-Identifier: GPL-3.0-or-later
// The polkit authentication agent against a stand-in for polkitd (tests/fake_polkitd.c) on a
// private bus that this test starts as the system bus, never the real one: GLib's callbacks on
// Qt's event loop, registering for the session, requests reaching the dialog's model with their
// users and details and answered as the user does, withdrawn by polkit, several at once, going
// away with one open, and staying out of the way of another agent. polkit's helper, which would
// ask PAM for a real password, is replaced by stand-ins.
#include "authentication.hpp"
#include "polkit_agent.hpp"
#include <QAbstractEventDispatcher>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <algorithm>
#include <gio/gio.h>
#include <memory>
#include <unistd.h>

// polkit's helper as the test drives it: it asks for the password at once and accepts "secret".
class FakeConversation : public AuthConversation {
    Q_OBJECT
  public:
    FakeConversation(QStringList &log, const AuthIdentity &identity) : log_(log), name_(identity.name) {}
    void start() override {
        log_ << "start " + name_;
        Q_EMIT request("Password: ", false);
    }
    void respond(const QString &answer) override { Q_EMIT completed(answer == "secret"); }
    void cancel() override { log_ << "cancel " + name_; }

  private:
    QStringList &log_;
    QString name_;
};

class PolkitAgentTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir_;
    QProcess bus_;
    QString address_;
    std::unique_ptr<QProcess> polkitd_;
    QStringList said_; // what the stand-in for polkitd printed
    QStringList conversations_;

    // Starts the stand-in for polkitd, ended by the next start or at the end.
    void startPolkitd(const QStringList &options = {}) {
        stopPolkitd();
        said_.clear();
        polkitd_ = std::make_unique<QProcess>();
        polkitd_->setProcessChannelMode(QProcess::ForwardedErrorChannel);
        connect(polkitd_.get(), &QProcess::readyReadStandardOutput, this, [this] {
            while (polkitd_->canReadLine())
                said_ << QString::fromUtf8(polkitd_->readLine()).trimmed();
        });
        polkitd_->start(FAKE_POLKITD, QStringList{address_} + options);
        QVERIFY(polkitd_->waitForStarted());
        QTRY_VERIFY_WITH_TIMEOUT(said_.contains("ready"), 10000);
    }
    void stopPolkitd() {
        if (!polkitd_)
            return;
        polkitd_->closeWriteChannel(); // it quits at the end of its input
        if (!polkitd_->waitForFinished(5000))
            polkitd_->kill();
        polkitd_.reset();
    }
    void tell(const QString &command) {
        polkitd_->write((command + '\n').toUtf8());
        polkitd_->waitForBytesWritten(1000);
    }
    bool heard(const QString &line, int timeout = 10000) {
        return QTest::qWaitFor([&] { return said_.contains(line); }, timeout);
    }
    // A model whose conversations are stand-ins.
    void prepare(Authentication &auth) {
        conversations_.clear();
        auth.setConversations([this](const AuthIdentity &identity, const QString &) {
            return new FakeConversation(conversations_, identity);
        });
    }
    static QString me() { return QString::number(getuid()); }

  private Q_SLOTS:
    void initTestCase() {
        if (QStandardPaths::findExecutable("dbus-daemon").isEmpty())
            QSKIP("dbus-daemon is not installed");
        // No service directories: nothing is started for a name nobody owns.
        QFile config(dir_.filePath("bus.conf"));
        QVERIFY(config.open(QIODevice::WriteOnly));
        config.write(("<busconfig><type>system</type><listen>unix:dir=" + dir_.path() +
                      "</listen><auth>EXTERNAL</auth><policy context=\"default\">"
                      "<allow send_destination=\"*\" eavesdrop=\"true\"/><allow eavesdrop=\"true\"/>"
                      "<allow own=\"*\"/></policy></busconfig>")
                         .toUtf8());
        config.close();
        bus_.start("dbus-daemon", {"--config-file=" + dir_.filePath("bus.conf"), "--nofork",
                                   "--print-address=1"});
        QVERIFY(bus_.waitForStarted());
        QVERIFY(bus_.waitForReadyRead(5000));
        address_ = QString::fromUtf8(bus_.readLine()).trimmed();
        QVERIFY2(address_.startsWith("unix:"), qPrintable(address_));
        // Before anything of GLib's opens the system bus, which it then keeps.
        qputenv("DBUS_SYSTEM_BUS_ADDRESS", address_.toUtf8());
        if (qEnvironmentVariableIsEmpty("XDG_SESSION_ID"))
            qputenv("XDG_SESSION_ID", "test-session");
    }
    void cleanupTestCase() {
        stopPolkitd();
        if (bus_.state() != QProcess::NotRunning) {
            bus_.kill(); // this test's own daemon, by its process
            bus_.waitForFinished(3000);
        }
    }
    // GLib's sources run on Qt's event loop: what the agent's GObjects need.
    void glibOnQtsLoop() {
        QVERIFY2(QAbstractEventDispatcher::instance()->inherits("QEventDispatcherGlib"),
                 QAbstractEventDispatcher::instance()->metaObject()->className());
        bool ran = false;
        g_idle_add_once([](gpointer data) { *static_cast<bool *>(data) = true; }, &ran);
        QTRY_VERIFY_WITH_TIMEOUT(ran, 2000);
    }
    void withoutPolkit() {
        Authentication auth;
        PolkitAgent agent(auth);
        QVERIFY(!agent.start());
        QVERIFY(!agent.taken());
        QVERIFY(!agent.error().isEmpty());
    }
    void answersRequests() {
        startPolkitd();
        Authentication auth;
        auto agent = std::make_unique<PolkitAgent>(auth);
        prepare(auth);
        QVERIFY2(agent->start(), qPrintable(agent->error()));
        QVERIFY(QTest::qWaitFor([&] {
            return std::any_of(said_.begin(), said_.end(), [](const QString &line) {
                return line.startsWith("registered unix-session ") &&
                       line.endsWith(" /org/paw/PolicyKit1/AuthenticationAgent");
            });
        }, 10000));
        // A request names its users; a group's members come as users, a group is left out.
        tell("begin org.freedesktop.policykit.exec cookie-1 user:0 group:10 user:" + me());
        QTRY_VERIFY_WITH_TIMEOUT(auth.open(), 10000);
        QCOMPARE(auth.actionId(), QString("org.freedesktop.policykit.exec"));
        QCOMPARE(auth.message(), QString("Authentication is needed to run `/usr/bin/true' as the super user"));
        QCOMPARE(auth.iconName(), QString("dialog-password"));
        QCOMPARE(auth.command(), QString("/usr/bin/true --test"));
        QCOMPARE(auth.identities().size(), 2);
        QCOMPARE(auth.identities()[0].toMap()["name"].toString(), QString("root"));
        QCOMPARE(auth.identity(), 1); // the user logged in
        QCOMPARE(auth.prompt(), QString("Password"));
        // A wrong answer is the model's to retry; polkit hears nothing of it.
        auth.submit("wrong");
        QVERIFY(auth.open() && !auth.error().isEmpty());
        QVERIFY(!said_.join('\n').contains("reply cookie-1"));
        auth.submit("secret");
        QVERIFY(heard("reply cookie-1 ok"));
        QVERIFY(!auth.open());

        // Cancel: polkit hears that the user dismissed it.
        tell("begin org.example.one cookie-2 user:" + me());
        QTRY_VERIFY_WITH_TIMEOUT(auth.open(), 10000);
        auth.cancel();
        QVERIFY(heard("reply cookie-2 error org.freedesktop.PolicyKit1.Error.Cancelled"));

        // polkit withdraws one: the dialog closes.
        tell("begin org.example.two cookie-3 user:" + me());
        QTRY_VERIFY_WITH_TIMEOUT(auth.open(), 10000);
        tell("cancel cookie-3");
        QTRY_VERIFY_WITH_TIMEOUT(!auth.open(), 10000);
        QVERIFY(heard("cancelled cookie-3"));
        QVERIFY(QTest::qWaitFor([&] {
            return said_.filter(QRegularExpression("^reply cookie-3 error ")).size() == 1;
        }, 10000));

        // Two at once: one after the other.
        tell("begin org.example.three cookie-4 user:" + me());
        tell("begin org.example.four cookie-5 user:" + me());
        QTRY_VERIFY_WITH_TIMEOUT(auth.count() == 2, 10000);
        QCOMPARE(auth.actionId(), QString("org.example.three"));
        auth.submit("secret");
        QVERIFY(heard("reply cookie-4 ok"));
        QCOMPARE(auth.actionId(), QString("org.example.four"));
        // The agent goes with the second open: polkit hears it withdrawn, and the agent go.
        agent.reset();
        QVERIFY(!auth.open());
        QVERIFY(heard("unregistered /org/paw/PolicyKit1/AuthenticationAgent"));
        QVERIFY(QTest::qWaitFor([&] {
            return said_.filter(QRegularExpression("^reply cookie-5 error ")).size() == 1;
        }, 10000));
    }
    void anotherAgentServes() {
        startPolkitd({"--taken"});
        Authentication auth;
        PolkitAgent agent(auth);
        QVERIFY(!agent.start());
        QVERIFY(agent.taken());
        QVERIFY2(agent.error().contains("already exists"), qPrintable(agent.error()));
    }
};

QTEST_GUILESS_MAIN(PolkitAgentTest)
#include "polkit_agent_test.moc"
