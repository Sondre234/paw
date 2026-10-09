// SPDX-License-Identifier: GPL-3.0-or-later
// The shell's clipboard history against a real compositor (tests/clipboard_smoke.py starts a
// private headless one): what a program copies through ext-data-control-v1 is kept, a secret and
// what is copied while the session is locked are not, an entry restored is what a program pastes,
// and the limit, pinning, the file and turning it off.
#include "clipboard.hpp"
#include "clipboard_images.hpp"
#include <QBuffer>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>
#include <functional>
#include <memory>
#include <vector>

class ClipboardTest : public QObject {
    Q_OBJECT
    const QString probe = qEnvironmentVariable("PAW_CLIPBOARD_PROBE");
    QTemporaryDir directory;
    // The programs that copied, each handing what it copied to whoever pastes until replaced.
    std::vector<std::unique_ptr<QProcess>> copiers;

    // Copies `formats` (TYPE=VALUE, VALUE @PATH for a file's bytes) as a program would.
    bool copy(const QStringList &formats) {
        auto process = std::make_unique<QProcess>();
        process->start(probe, QStringList{"set"} + formats);
        const bool copied =
            QTest::qWaitFor(
                [&] { return process->canReadLine() || process->state() == QProcess::NotRunning; },
                5000) &&
            process->readLine() == "set\n";
        copiers.push_back(std::move(process));
        return copied;
    }
    // What a program pasting gets: what is copied as `type`, or its types with an empty one.
    QByteArray paste(const QString &type = {}) {
        QProcess process;
        process.start(probe, type.isEmpty() ? QStringList{"types"} : QStringList{"get", type});
        // The history may be what hands it over, so its events go on meanwhile.
        if (!QTest::qWaitFor([&] { return process.state() == QProcess::NotRunning; }, 5000))
            process.kill();
        return process.readAllStandardOutput();
    }
    static QStringList texts(const ClipboardHistory &history) {
        QStringList list;
        for (const auto &entry : history.entries())
            list << entry.toMap()["text"].toString();
        return list;
    }
    static int idOf(const ClipboardHistory &history, const QString &text) {
        for (const auto &entry : history.entries())
            if (entry.toMap()["text"] == text)
                return entry.toMap()["id"].toInt();
        return -1;
    }
    // That `condition` holds for a while, as something that should not happen could happen a
    // little later.
    static bool stays(const std::function<bool()> &condition, int milliseconds = 300) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < milliseconds) {
            if (!condition())
                return false;
            QTest::qWait(10);
        }
        return condition();
    }

  private Q_SLOTS:
    void initTestCase() {
        QVERIFY2(!probe.isEmpty(), "PAW_CLIPBOARD_PROBE names no probe");
        QVERIFY(directory.isValid());
    }
    void cleanup() {
        for (auto &process : copiers) {
            process->terminate();
            process->waitForFinished(2000);
        }
        copiers.clear();
    }

    void followsTheClipboard() {
        ClipboardHistory history;
        ClipboardHistory::Settings settings;
        settings.path = directory.filePath("history");
        history.configure(settings);
        QVERIFY(history.connectDisplay());
        QVERIFY(history.connected());
        // Text, by its usual names, and formatted text beside it.
        QVERIFY(copy({"text/plain;charset=utf-8=first", "text/plain=first"}));
        QVERIFY(QTest::qWaitFor([&] { return texts(history) == QStringList{"first"}; }));
        QVERIFY(copy({"UTF8_STRING=second", "text/html=<b>second</b>"}));
        QVERIFY(
            QTest::qWaitFor([&] { return texts(history) == QStringList({"second", "first"}); }));
        // What a password manager marks as secret is not kept.
        QVERIFY(copy({"text/plain=hunter2", "x-kde-passwordManagerHint=secret"}));
        QVERIFY(copy({"text/plain=third"}));
        QVERIFY(QTest::qWaitFor([&] { return texts(history).value(0) == "third"; }));
        QCOMPARE(texts(history), QStringList({"third", "second", "first"}));
        // Nor what is copied while the session is locked.
        history.setLocked(true);
        QVERIFY(copy({"text/plain=while locked"}));
        QVERIFY(stays([&] { return texts(history).size() == 3; }));
        history.setLocked(false);
        // Nor what holds neither text nor a picture.
        QVERIFY(copy({"application/x-something=data"}));
        QVERIFY(stays([&] { return texts(history).size() == 3; }));
        // A picture, shrunk for the list, with its own size.
        QImage picture(1280, 720, QImage::Format_RGB32);
        picture.fill(Qt::darkCyan);
        QVERIFY(picture.save(directory.filePath("picture.png")));
        QVERIFY(copy({"image/png=@" + directory.filePath("picture.png"), "text/plain= "}));
        QVERIFY(
            QTest::qWaitFor([&] { return history.entries().value(0).toMap()["kind"] == "image"; }));
        const auto image = history.entries().value(0).toMap();
        QCOMPARE(image["width"].toInt(), 1280);
        QCOMPARE(image["height"].toInt(), 720);
        QCOMPARE(image["text"].toString(), QString());
        QVERIFY(image["image"].toString().startsWith("image://clipboard/"));
        QSize served;
        const auto shown = ClipboardImages(history).requestImage(
            image["image"].toString().sliced(QString("image://clipboard/").size()), &served, {});
        QCOMPARE(served, QSize(512, 288));
        QCOMPARE(shown.pixelColor(10, 10), QColor(Qt::darkCyan));

        // Restored, an entry is what is pasted, under every name it was copied by, and the newest;
        // the history does not keep its own copy again.
        QVERIFY(history.restore(idOf(history, "second")));
        QVERIFY(QTest::qWaitFor([&] { return paste("text/plain") == "second"; }));
        QCOMPARE(paste("UTF8_STRING"), QByteArray("second"));
        QCOMPARE(paste("text/html"), QByteArray("<b>second</b>"));
        QVERIFY(paste().split('\n').contains("text/plain;charset=utf-8"));
        QCOMPARE(texts(history).value(0), QString("second"));
        QVERIFY(stays([&] { return texts(history).size() == 4; }));
        // The picture too, as it was copied.
        QVERIFY(history.restore(image["id"].toInt()));
        QVERIFY(QTest::qWaitFor([&] { return paste().split('\n').contains("image/png"); }));
        QImage pasted;
        QVERIFY(pasted.loadFromData(paste("image/png"), "PNG"));
        QCOMPARE(pasted.size(), QSize(1280, 720));
        // Something copied after it is kept again; copied again, an entry moves up.
        QVERIFY(copy({"text/plain=first"}));
        QVERIFY(QTest::qWaitFor([&] { return texts(history).value(0) == "first"; }));
        QCOMPARE(texts(history).size(), 4);

        // Pinned entries come first and stay; the limit and Clear all leave them.
        history.setPinned(idOf(history, "third"), true);
        QCOMPARE(texts(history).value(0), QString("third"));
        QVERIFY(history.entries().value(0).toMap()["pinned"].toBool());
        settings.limit = 2;
        history.configure(settings);
        QCOMPARE(texts(history), QStringList({"third", "first", ""}));
        history.remove(idOf(history, "first"));
        QCOMPARE(texts(history), QStringList({"third", ""}));
        history.clear();
        QCOMPARE(texts(history), QStringList{"third"});

        // Kept across sessions with persist, in a file only the user may read; without, the
        // file goes.
        QVERIFY(copy({"text/plain=kept"}));
        QVERIFY(QTest::qWaitFor([&] { return texts(history).value(1) == "kept"; }));
        settings.persist = true;
        history.configure(settings);
        QVERIFY(QFileInfo::exists(settings.path));
        QCOMPARE(QFileInfo(settings.path).permissions() & (QFile::ReadGroup | QFile::ReadOther),
                 QFileDevice::Permissions());
        {
            ClipboardHistory again;
            again.configure(settings);
            QCOMPARE(texts(again), QStringList({"third", "kept"}));
            QVERIFY(again.entries().value(0).toMap()["pinned"].toBool());
        }
        settings.persist = false;
        history.configure(settings);
        QVERIFY(!QFileInfo::exists(settings.path));
        // Turned off, it forgets everything and keeps nothing more.
        settings.enabled = false;
        history.configure(settings);
        QCOMPARE(texts(history), QStringList());
        QVERIFY(copy({"text/plain=off"}));
        QVERIFY(stays([&] { return texts(history).isEmpty(); }));
    }

    // Without the compositor's data-control, nothing is heard; restoring goes through Qt.
    void withoutTheCompositor() {
        const auto display = qgetenv("WAYLAND_DISPLAY");
        qputenv("WAYLAND_DISPLAY", "paw-no-such-display");
        ClipboardHistory history;
        QVERIFY(!history.connectDisplay());
        qputenv("WAYLAND_DISPLAY", display);
        QVERIFY(history.record({{"text/plain", "offline"}}));
        QVERIFY(history.restore(idOf(history, "offline")));
    }
};

QTEST_MAIN(ClipboardTest)
#include "clipboard_test.moc"
