// SPDX-License-Identifier: GPL-3.0-or-later
// The authentication dialog's model, with stand-ins for polkit's helper: which user is chosen,
// answers given before and after the helper asks, wrong answers, choosing another user, Cancel,
// requests waiting in turn and polkit withdrawing them.
#include "authentication.hpp"
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <unistd.h>

// polkit's helper as a test drives it: it records what it is told, and says what the test makes
// it say.
class FakeConversation : public AuthConversation {
    Q_OBJECT
  public:
    FakeConversation(const AuthIdentity &identity, const QString &cookie)
        : identity(identity.name), cookie(cookie) {}
    QString identity, cookie;
    QStringList answers;
    bool started = false, cancelled = false;
    void start() override { started = true; }
    void respond(const QString &answer) override { answers << answer; }
    void cancel() override { cancelled = true; }
    void ask(const QString &prompt = "Password: ", bool echo = false) { Q_EMIT request(prompt, echo); }
    void say(const QString &text) { Q_EMIT error(text); }
    void tell(const QString &text) { Q_EMIT info(text); }
    void end(bool authorized) { Q_EMIT completed(authorized); }
};

namespace {
AuthIdentity user(const QString &name, uint uid, const QString &fullName = {}) {
    return {"unix-user:" + QString::number(uid), name, fullName, uid};
}
QString myName() {
    return "me";
}
AuthRequest request(const QString &cookie, QList<AuthIdentity> identities = {}) {
    if (identities.isEmpty())
        identities = {user("root", 0), user(myName(), getuid(), "Me Myself"), user("ada", 64000)};
    return {"org.freedesktop.policykit.exec",
            "Authentication is needed to run `/usr/bin/true' as the super user",
            "dialog-password",
            {{"command_line", "/usr/bin/true --flag"}, {"program", "/usr/bin/true"}},
            cookie,
            identities};
}
} // namespace

class AuthenticationTest : public QObject {
    Q_OBJECT
    QList<QPointer<FakeConversation>> made_;
    QString output_ = "DP-1";
    // A model whose conversations are stand-ins the test keeps.
    void prepare(Authentication &auth) {
        made_.clear();
        auth.setConversations([this](const AuthIdentity &identity, const QString &cookie) {
            auto *conversation = new FakeConversation(identity, cookie);
            made_ << conversation;
            return conversation;
        });
        auth.setOutputSource([this] { return output_; });
    }
    FakeConversation *last() { return made_.isEmpty() ? nullptr : made_.last().data(); }

  private Q_SLOTS:
    void answersRightAfterAWrongOne() {
        Authentication auth;
        prepare(auth);
        QList<Authentication::Outcome> outcomes;
        QSignalSpy rejected(&auth, &Authentication::rejected);
        QVERIFY(!auth.open());
        auth.add(request("c1"), [&](Authentication::Outcome outcome) { outcomes << outcome; });
        QVERIFY(auth.open());
        QCOMPARE(auth.serial(), 1);
        QCOMPARE(auth.output(), QString("DP-1"));
        QCOMPARE(auth.actionId(), QString("org.freedesktop.policykit.exec"));
        QVERIFY(auth.message().startsWith("Authentication is needed"));
        QCOMPARE(auth.iconName(), QString("dialog-password"));
        QCOMPARE(auth.command(), QString("/usr/bin/true --flag"));
        // The user logged in answers, of the three.
        QCOMPARE(auth.identity(), 1);
        const auto identities = auth.identities();
        QCOMPARE(identities.size(), 3);
        QCOMPARE(identities[0].toMap()["label"].toString(), QString("root"));
        QCOMPARE(identities[1].toMap()["label"].toString(), QString("Me Myself (me)"));
        QCOMPARE(made_.size(), 1);
        QVERIFY(last()->started);
        QCOMPARE(last()->identity, myName());
        QCOMPARE(last()->cookie, QString("c1"));
        // The helper asks; its prompt loses its colon.
        last()->ask("Password: ");
        QCOMPARE(auth.prompt(), QString("Password"));
        QVERIFY(!auth.echo() && !auth.checking());
        auth.submit("wrong");
        QCOMPARE(last()->answers, QStringList{"wrong"});
        QVERIFY(auth.checking());
        // Nothing more is taken while it checks.
        auth.submit("again");
        QCOMPARE(last()->answers, QStringList{"wrong"});
        last()->end(false);
        QCOMPARE(rejected.count(), 1);
        QVERIFY(!auth.checking());
        QCOMPARE(auth.error(), QString("The password was not accepted. Try again."));
        QVERIFY(auth.open() && outcomes.isEmpty());
        // A new conversation, as the same user; an answer typed before it asks waits for it.
        QCOMPARE(made_.size(), 2);
        QVERIFY(made_[1]->started && made_[1]->identity == myName());
        auth.submit("right");
        QVERIFY(auth.checking() && auth.error().isEmpty());
        QVERIFY(made_[1]->answers.isEmpty());
        made_[1]->ask();
        QCOMPARE(made_[1]->answers, QStringList{"right"});
        QVERIFY(auth.checking());
        made_[1]->end(true);
        QCOMPARE(outcomes, QList{Authentication::Outcome::Authorized});
        QVERIFY(!auth.open());
        // What it showed stays readable while the dialog fades out.
        QVERIFY(auth.message().startsWith("Authentication is needed"));
        auth.submit("late");
        auth.cancel();
        QCOMPARE(outcomes.size(), 1);
    }
    void helperSpeaks() {
        Authentication auth;
        prepare(auth);
        auth.add(request("c"), {});
        last()->tell("Touch your key");
        QCOMPARE(auth.info(), QString("Touch your key"));
        last()->say("Your account has expired");
        QCOMPARE(auth.error(), QString("Your account has expired"));
        // A prompt to answer in the clear, and a second question after the first.
        last()->ask("Name:", true);
        QVERIFY(auth.echo());
        QCOMPARE(auth.prompt(), QString("Name"));
        auth.submit("ada");
        QVERIFY(auth.error().isEmpty() && auth.info().isEmpty());
        last()->ask("Verification code: ");
        QVERIFY(!auth.checking() && !auth.echo());
        QCOMPARE(auth.prompt(), QString("Verification code"));
        auth.submit("123456");
        QCOMPARE(last()->answers, (QStringList{"ada", "123456"}));
        last()->ask("");
        QCOMPARE(auth.prompt(), QString("Password"));
    }
    void anotherUser() {
        Authentication auth;
        prepare(auth);
        QList<Authentication::Outcome> outcomes;
        auth.add(request("c"), [&](Authentication::Outcome outcome) { outcomes << outcome; });
        last()->ask();
        auth.submit("typed");
        auth.setIdentity(2);
        QCOMPARE(auth.identity(), 2);
        QVERIFY(made_[0]->cancelled);
        QCOMPARE(made_.size(), 2);
        QCOMPARE(made_[1]->identity, QString("ada"));
        QVERIFY(!auth.checking());
        // Out of range, or the same, changes nothing.
        auth.setIdentity(3);
        auth.setIdentity(-1);
        auth.setIdentity(2);
        QCOMPARE(made_.size(), 2);
        made_[1]->ask();
        auth.submit("ada's");
        made_[1]->end(true);
        QCOMPARE(outcomes, QList{Authentication::Outcome::Authorized});
    }
    void choosesRootElseTheFirst() {
        Authentication auth;
        prepare(auth);
        auth.add(request("a", {user("x", 64001), user("root", 0)}), {});
        QCOMPARE(auth.identity(), 1);
        auth.cancel();
        auth.add(request("b", {user("x", 64001), user("y", 64002)}), {});
        QCOMPARE(auth.identity(), 0);
        QCOMPARE(last()->identity, QString("x"));
        auth.cancel();
        // Nobody may answer: it says so, and Cancel still closes it.
        QList<Authentication::Outcome> outcomes;
        auth.add({"a", "m", "", {}, "c", {}}, [&](Authentication::Outcome outcome) { outcomes << outcome; });
        QVERIFY(auth.open() && auth.identity() == -1 && !auth.error().isEmpty());
        auth.cancel();
        QCOMPARE(outcomes, QList{Authentication::Outcome::Dismissed});
    }
    void cancelled() {
        Authentication auth;
        prepare(auth);
        QList<Authentication::Outcome> outcomes;
        auth.add(request("c"), [&](Authentication::Outcome outcome) { outcomes << outcome; });
        QPointer<FakeConversation> conversation = last();
        conversation->ask();
        auth.cancel();
        QCOMPARE(outcomes, QList{Authentication::Outcome::Dismissed});
        QVERIFY(!auth.open());
        QVERIFY(conversation && conversation->cancelled);
        // What the helper says after that is not heard.
        conversation->end(true);
        QCOMPARE(outcomes.size(), 1);
        QTRY_VERIFY(!conversation); // deleted later
    }
    void inTurn() {
        Authentication auth;
        prepare(auth);
        QStringList done;
        auto note = [&done](const QString &name) {
            return [&done, name](Authentication::Outcome outcome) {
                done << name + (outcome == Authentication::Outcome::Authorized ? " authorized"
                                : outcome == Authentication::Outcome::Dismissed ? " dismissed"
                                                                                 : " withdrawn");
            };
        };
        QSignalSpy changed(&auth, &Authentication::changed);
        const int first = auth.add(request("one"), note("one"));
        const int second = auth.add(request("two"), note("two"));
        const int third = auth.add(request("three"), note("three"));
        QCOMPARE(auth.count(), 3);
        QCOMPARE(made_.size(), 1);
        QCOMPARE(last()->cookie, QString("one"));
        // A waiting one withdrawn leaves the open one be.
        auth.withdraw(second);
        QCOMPARE(done, QStringList{"two withdrawn"});
        QCOMPARE(made_.size(), 1);
        QCOMPARE(auth.serial(), 1);
        // The open one done, the next opens, on the output overlays belong on now.
        output_ = "HDMI-A-1";
        last()->ask();
        auth.submit("x");
        last()->end(true);
        QCOMPARE(done, (QStringList{"two withdrawn", "one authorized"}));
        QVERIFY(auth.open());
        QCOMPARE(auth.serial(), 2);
        QCOMPARE(auth.output(), QString("HDMI-A-1"));
        QCOMPARE(made_.size(), 2);
        QCOMPARE(last()->cookie, QString("three"));
        auth.withdraw(first); // already done: nothing
        auth.withdraw(third);
        QCOMPARE(done, (QStringList{"two withdrawn", "one authorized", "three withdrawn"}));
        QVERIFY(!auth.open() && made_[1]->cancelled);
        QVERIFY(changed.count() > 0);
        // Every one at once, as the agent goes.
        auth.add(request("four"), note("four"));
        auth.add(request("five"), note("five"));
        auth.withdrawAll();
        QCOMPARE(done.mid(3), (QStringList{"five withdrawn", "four withdrawn"}));
        QVERIFY(!auth.open() && auth.count() == 0);
        QCOMPARE(made_.size(), 3); // five never opened
    }
    void withoutHelper() {
        Authentication auth;
        auth.setConversations([](const AuthIdentity &, const QString &) { return nullptr; });
        auth.add(request("c"), {});
        QVERIFY(auth.open());
        QVERIFY(!auth.error().isEmpty());
        auth.submit("x");
        auth.cancel();
        QVERIFY(!auth.open());
    }
};

QTEST_GUILESS_MAIN(AuthenticationTest)
#include "authentication_test.moc"
