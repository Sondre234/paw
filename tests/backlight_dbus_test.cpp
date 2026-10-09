// SPDX-License-Identifier: GPL-3.0-or-later
// Setting the backlight through logind's Session.SetBrightness, against a stand-in for logind on
// a private bus (a dbus-daemon this test starts and kills, named by PAW_LOGIN1_BUS), never
// the real one.
#include "backlight.hpp"
#include <QDBusConnection>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

// logind's session object, as far as SetBrightness goes: it records each call.
class FakeSession : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.login1.Session")
  public:
    QStringList calls;
  public Q_SLOTS:
    void SetBrightness(const QString &subsystem, const QString &name, uint brightness) {
        calls << QString("%1 %2 %3").arg(subsystem, name).arg(brightness);
    }
};

class BacklightDbusTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir_, sys_;
    QProcess bus_;
    QString address_;
    QString serviceName_ = "paw-test-login1-" + QUuid::createUuid().toString(QUuid::Id128);
    FakeSession session_;

    void write(const QString &path, const QByteArray &text) {
        QDir().mkpath(QFileInfo(sys_.filePath(path)).path());
        QFile file(sys_.filePath(path));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(text);
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
                      "<allow own=\"*\"/></policy></busconfig>")
                         .toUtf8());
        config.close();
        bus_.start("dbus-daemon", {"--config-file=" + dir_.filePath("bus.conf"), "--nofork",
                                   "--print-address=1"});
        QVERIFY(bus_.waitForStarted());
        QVERIFY(bus_.waitForReadyRead(5000));
        address_ = QString::fromUtf8(bus_.readLine()).trimmed();
        QVERIFY2(address_.startsWith("unix:"), qPrintable(address_));
        qputenv("PAW_LOGIN1_BUS", address_.toUtf8());
        write("class/backlight/fake/max_brightness", "200\n");
        write("class/backlight/fake/brightness", "100\n");
    }
    void cleanupTestCase() {
        if (bus_.state() != QProcess::NotRunning) {
            bus_.kill(); // this test's own daemon, by its process
            bus_.waitForFinished(3000);
        }
    }
    // Nobody answers for logind yet: the failure is reported, and nothing is written.
    void withoutLogind() {
        Backlight light(sys_.path());
        QSignalSpy failed(&light, &Backlight::failed);
        light.setPercent(30);
        QVERIFY(failed.wait(5000));
        QVERIFY2(failed.at(0).at(0).toString().startsWith("Could not set the brightness: "),
                 qPrintable(failed.at(0).at(0).toString()));
        QFile file(sys_.filePath("class/backlight/fake/brightness"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll().trimmed(), QByteArray("100"));
    }
    // With logind there, the session's object is asked for the level in the backlight's units.
    void throughLogind() {
        auto bus = QDBusConnection::connectToBus(address_, serviceName_);
        QVERIFY(bus.isConnected());
        QVERIFY(bus.registerObject("/org/freedesktop/login1/session/auto", &session_,
                                   QDBusConnection::ExportAllSlots));
        QVERIFY(bus.registerService("org.freedesktop.login1"));
        Backlight light(sys_.path());
        QCOMPARE(light.percent(), 50);
        QSignalSpy failed(&light, &Backlight::failed);
        light.setPercent(30);
        QCOMPARE(light.percent(), 30);
        QTRY_COMPARE(session_.calls, QStringList{"backlight fake 60"});
        light.setPercent(100);
        QTRY_COMPARE(session_.calls.size(), 2);
        QCOMPARE(session_.calls[1], QString("backlight fake 200"));
        QCOMPARE(failed.count(), 0);
    }
};
QTEST_MAIN(BacklightDbusTest)
#include "backlight_dbus_test.moc"
