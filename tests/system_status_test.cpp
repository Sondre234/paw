// SPDX-License-Identifier: GPL-3.0-or-later
#include "system_status.hpp"
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <cstdlib>

namespace {
// A fake sysfs tree the tests rewrite between refreshes.
class Sysfs {
  public:
    QTemporaryDir dir;
    void write(const QString &path, const QString &text = {}) {
        QDir().mkpath(QFileInfo(dir.filePath(path)).absolutePath());
        QFile file(dir.filePath(path));
        QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "cannot write fake sysfs");
        file.write(text.toUtf8());
    }
    void battery(const QString &name, int percent, const QString &status,
                 const QString &type = "Battery") {
        write("class/power_supply/" + name + "/type", type + "\n");
        write("class/power_supply/" + name + "/capacity", QString::number(percent) + "\n");
        write("class/power_supply/" + name + "/status", status + "\n");
    }
    void interface(const QString &name, const QString &state, bool wireless = false,
                   bool physical = true) {
        write("class/net/" + name + "/operstate", state + "\n");
        if (physical)
            write("class/net/" + name + "/device");
        if (wireless)
            write("class/net/" + name + "/wireless");
    }
};
template <size_t N> QByteArray message(const char (&text)[N]) { return QByteArray(text, int(N) - 1); }
} // namespace

class SystemStatusTest : public QObject {
    Q_OBJECT
  private Q_SLOTS:
    void emptyTree() {
        Sysfs sys;
        SystemStatus status(sys.dir.path());
        QVERIFY(!status.batteryPresent());
        QCOMPARE(status.networkState(), QString("none"));
        QVERIFY(status.batteryText().isEmpty() && status.networkText().isEmpty());
    }
    void missingRoot() {
        SystemStatus status("/nonexistent/sysfs");
        QVERIFY(!status.batteryPresent());
        QCOMPARE(status.networkState(), QString("none"));
    }
    void battery() {
        Sysfs sys;
        sys.battery("AC", 0, "Unknown", "Mains");
        sys.battery("BAT0", 64, "Discharging");
        SystemStatus status(sys.dir.path());
        QVERIFY(status.batteryPresent());
        QCOMPARE(status.batteryPercent(), 64);
        QCOMPARE(status.batteryState(), QString("discharging"));
        QCOMPARE(status.batteryText(), QString("Battery 64%, on battery"));
        QSignalSpy changed(&status, &SystemStatus::changed);
        status.refresh();
        QCOMPARE(changed.count(), 0); // nothing differs
        sys.battery("BAT0", 65, "Charging");
        status.refresh();
        QCOMPARE(changed.count(), 1);
        QCOMPARE(status.batteryState(), QString("charging"));
        sys.battery("BAT0", 100, "Not charging");
        status.refresh();
        QCOMPARE(status.batteryState(), QString("full"));
    }
    void peripheralIsNotTheBattery() {
        Sysfs sys;
        sys.battery("hid-mouse", 40, "Discharging");
        sys.write("class/power_supply/hid-mouse/scope", "Device\n");
        SystemStatus status(sys.dir.path());
        QVERIFY(!status.batteryPresent());
    }
    void network() {
        Sysfs sys;
        sys.interface("lo", "unknown", false, false);
        sys.interface("docker0", "up", false, false);
        sys.interface("wlan0", "up", true);
        SystemStatus status(sys.dir.path());
        QCOMPARE(status.networkState(), QString("wifi"));
        QCOMPARE(status.networkInterface(), QString("wlan0"));
        sys.interface("eth0", "up");
        status.refresh();
        QCOMPARE(status.networkState(), QString("ethernet")); // wired wins
        QCOMPARE(status.networkText(), QString("Wired connected (eth0)"));
        sys.interface("eth0", "down");
        sys.interface("wlan0", "down", true);
        status.refresh();
        QCOMPARE(status.networkState(), QString("disconnected"));
    }
    void pollsWithoutKernelMessages() {
        Sysfs sys;
        SystemStatus status(sys.dir.path());
        QVERIFY(!status.watching());
        QCOMPARE(status.pollInterval(), 5000);
    }
    void relevantUevents() {
        QVERIFY(SystemStatus::relevantUevent(message("change@/devices/x/BAT0\0ACTION=change\0SUBSYSTEM=power_supply\0")));
        QVERIFY(SystemStatus::relevantUevent(message("add@/x\0SUBSYSTEM=net\0INTERFACE=eth0\0")));
        QVERIFY(!SystemStatus::relevantUevent(message("add@/x\0SUBSYSTEM=usb\0")));
        QVERIFY(!SystemStatus::relevantUevent(message("SUBSYSTEM=network\0")));
        QVERIFY(!SystemStatus::relevantUevent({}));
    }
    // With the kernel's messages there is no fast poll, and none at all without a battery.
    void watchingStopsPolling() {
        Sysfs sys;
        SystemStatus none(sys.dir.path(), nullptr, true);
        if (!none.watching())
            QSKIP("netlink sockets are not available here");
        QCOMPARE(none.pollInterval(), 0);
        sys.battery("BAT0", 50, "Discharging");
        none.refresh();
        QCOMPARE(none.pollInterval(), 120000);
        SystemStatus with(sys.dir.path(), nullptr, true);
        QCOMPARE(with.pollInterval(), 120000);
    }
    // A link appearing prompts a read at once, with no timer involved. The test runs itself in a
    // network namespace of its own (unshare -rn), so it can create an interface without touching
    // the real ones.
    void linkChangePromptsRead() {
        if (!qEnvironmentVariableIsSet("PAW_TEST_NETNS")) {
            // Portage's sandbox treats writing /proc/self/uid_map as a violation.
            if (qEnvironmentVariable("SANDBOX_ON") == "1")
                QSKIP("Portage's sandbox does not allow user namespaces");
            if (QProcess::execute("unshare", {"-rn", "true"}) != 0)
                QSKIP("cannot make a network namespace here");
            QProcess child;
            child.setProcessEnvironment([] {
                auto env = QProcessEnvironment::systemEnvironment();
                env.insert("PAW_TEST_NETNS", "1");
                return env;
            }());
            child.start("unshare", {"-rn", QCoreApplication::applicationFilePath(), "linkChangePromptsRead"});
            if (!child.waitForStarted())
                QSKIP("unshare is not available here");
            if (!child.waitForFinished(20000)) {
                child.kill();
                child.waitForFinished();
                QFAIL("the run in a network namespace hung");
            }
            if (child.exitCode() == 77)
                QSKIP("cannot make a network namespace or an interface here");
            QVERIFY2(child.exitCode() == 0, child.readAllStandardOutput().constData());
            return;
        }
        if (std::system("ip link add name pawtest0 type dummy >/dev/null 2>&1") != 0)
            std::exit(77);
        std::system("ip link del pawtest0 >/dev/null 2>&1");
        Sysfs sys;
        sys.interface("eth0", "down");
        SystemStatus status(sys.dir.path(), nullptr, true);
        QVERIFY(status.watching());
        QCOMPARE(status.networkState(), QString("disconnected"));
        QSignalSpy changed(&status, &SystemStatus::changed);
        sys.interface("eth0", "up");
        QVERIFY(std::system("ip link add name pawtest0 type dummy >/dev/null 2>&1") == 0);
        QVERIFY(changed.wait(3000));
        QCOMPARE(status.networkState(), QString("ethernet"));
    }
};
QTEST_GUILESS_MAIN(SystemStatusTest)
#include "system_status_test.moc"
