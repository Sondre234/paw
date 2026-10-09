// SPDX-License-Identifier: GPL-3.0-or-later
// The power mode against a stand-in power-profiles-daemon on a private bus (a dbus-daemon this
// test starts and kills) standing in for the system bus, never the real one: the daemon coming
// and going, under its name and the older one, its changes, and switching profiles, allowed or
// refused.
#include "fake_dbus.hpp"
#include "power_profiles_daemon.hpp"
#include <QDBusMetaType>
#include <QSignalSpy>
#include <QTest>
#include <memory>

namespace {
constexpr auto service = "org.freedesktop.UPower.PowerProfiles";
constexpr auto legacy = "net.hadess.PowerProfiles";

// The daemon on a connection of its own, under `name` (and its object under the path that goes
// with it), offering power-saver and balanced, and performance with `performance`.
class FakeDaemon {
  public:
    FakeDaemon(fakedbus::Bus &bus, const QString &name, const QString &profile = "balanced",
               bool performance = true)
        : name_(name), bus_(bus.connect()),
          object(bus_, name == service ? "/org/freedesktop/UPower/PowerProfiles" : "/net/hadess/PowerProfiles") {
        QList<QVariantMap> profiles{{{"Profile", "power-saver"}, {"Driver", "fake"}},
                                    {{"Profile", "balanced"}, {"Driver", "fake"}}};
        if (performance)
            profiles << QVariantMap{{"Profile", "performance"}, {"Driver", "fake"}};
        object.properties[name] = {{"ActiveProfile", profile},
                                   {"Profiles", QVariant::fromValue(profiles)},
                                   {"PerformanceDegraded", ""},
                                   {"Actions", QStringList()}};
    }
    ~FakeDaemon() {
        bus_.unregisterService(name_);
        QDBusConnection::disconnectFromBus(bus_.name());
    }
    bool start() { return bus_.registerService(name_); }
    void stop() { bus_.unregisterService(name_); }

  private:
    QString name_;
    QDBusConnection bus_;

  public:
    fakedbus::Object object;
};
} // namespace

class PowerModeDbusTest : public QObject {
    Q_OBJECT
    fakedbus::Bus bus_;

  private Q_SLOTS:
    void initTestCase() {
        if (!fakedbus::Bus::available())
            QSKIP("dbus-daemon is not installed");
        qDBusRegisterMetaType<QList<QVariantMap>>();
        QVERIFY(bus_.start());
    }
    // Nothing while no daemon runs; read whole once one comes, and nothing again once it goes.
    void comesAndGoes() {
        PowerProfilesDaemon mode(bus_.connect());
        QSignalSpy changed(&mode, &PowerMode::changed);
        QTest::qWait(50);
        QVERIFY(!mode.available());
        QCOMPARE(changed.count(), 0);
        FakeDaemon daemon(bus_, service);
        QVERIFY(daemon.start());
        QTRY_VERIFY(mode.available());
        QCOMPARE(mode.profile(), QString("balanced"));
        QCOMPARE(mode.profiles(), (QStringList{"power-saver", "balanced", "performance"}));
        QCOMPARE(mode.degraded(), QString());
        daemon.stop();
        QTRY_VERIFY(!mode.available());
        QCOMPARE(mode.profiles(), QStringList());
    }
    // What the daemon announces.
    void follows() {
        FakeDaemon daemon(bus_, service, "power-saver", false);
        QVERIFY(daemon.start());
        PowerProfilesDaemon mode(bus_.connect());
        QTRY_VERIFY(mode.available());
        QCOMPARE(mode.profile(), QString("power-saver"));
        QCOMPARE(mode.profiles(), (QStringList{"power-saver", "balanced"}));
        daemon.object.set(service, "ActiveProfile", "balanced");
        QTRY_COMPARE(mode.profile(), QString("balanced"));
        daemon.object.set(service, "PerformanceDegraded", "lap-detected");
        QTRY_COMPARE(mode.degraded(), QString("lap-detected"));
        QList<QVariantMap> three{{{"Profile", "power-saver"}}, {{"Profile", "balanced"}}, {{"Profile", "performance"}}};
        daemon.object.set(service, "Profiles", QVariant::fromValue(three));
        QTRY_COMPARE(mode.profiles().size(), 3);
    }
    // A switch shows at once and reaches the daemon; one the daemon refuses is undone and said.
    void switches() {
        FakeDaemon daemon(bus_, service);
        QVERIFY(daemon.start());
        PowerProfilesDaemon mode(bus_.connect());
        QTRY_VERIFY(mode.available());
        QSignalSpy failed(&mode, &PowerMode::failed);
        mode.setProfile("performance");
        QCOMPARE(mode.profile(), QString("performance"));
        QTRY_COMPARE(daemon.object.calls, QStringList{"Set org.freedesktop.UPower.PowerProfiles.ActiveProfile performance"});
        // Not one it offers, or the one it runs: nothing to ask.
        mode.setProfile("turbo");
        mode.setProfile("performance");
        daemon.object.setter = [](const QString &, const QString &, const QVariant &) { return false; };
        mode.setProfile("power-saver");
        QCOMPARE(mode.profile(), QString("power-saver"));
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY2(failed.at(0).at(0).toString().startsWith("Could not change the power mode: "),
                 qPrintable(failed.at(0).at(0).toString()));
        QTRY_COMPARE(mode.profile(), QString("performance"));
        QCOMPARE(daemon.object.calls.size(), 2);
    }
    // A daemon before 0.20 answers to its old name only; one answering to both is read by the
    // newer.
    void olderName() {
        FakeDaemon old(bus_, legacy, "performance");
        QVERIFY(old.start());
        PowerProfilesDaemon mode(bus_.connect());
        QTRY_VERIFY(mode.available());
        QCOMPARE(mode.profile(), QString("performance"));
        mode.setProfile("balanced");
        QTRY_COMPARE(old.object.calls, QStringList{"Set net.hadess.PowerProfiles.ActiveProfile balanced"});
        FakeDaemon newer(bus_, service, "power-saver");
        QVERIFY(newer.start());
        QTRY_COMPARE(mode.profile(), QString("power-saver"));
        old.object.set(legacy, "ActiveProfile", "performance");
        newer.stop();
        QTRY_COMPARE(mode.profile(), QString("performance"));
    }
};
QTEST_MAIN(PowerModeDbusTest)
#include "power_mode_dbus_test.moc"
