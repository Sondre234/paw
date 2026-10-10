// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio.hpp"
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTest>
#include <unistd.h>

namespace {
// A procfs of made-up processes: `stat` files with the parent each names.
class Proc {
  public:
    QTemporaryDir dir;
    void stat(int pid, const QByteArray &text) {
        QDir().mkpath(dir.filePath(QString::number(pid)));
        QFile file(dir.filePath(QString::number(pid) + "/stat"));
        QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "cannot write fake procfs");
        file.write(text);
    }
    void process(int pid, const QByteArray &name, int parent) {
        stat(pid, QByteArray::number(pid) + " (" + name + ") S " + QByteArray::number(parent) +
                      " " + QByteArray::number(pid) + " 0 0 -1 4194560 120 0 0 0 1 2 0 0 20 0 1\n");
    }
};

// A sound server that notes what is asked of it, as "mute 4 1".
class TestAudio : public Audio {
  public:
    QStringList requests;

  protected:
    void sendStreamMute(uint32_t id, bool muted) override {
        requests << QString("mute %1 %2").arg(id).arg(muted);
    }
};

// The taskbar's windows, as far as matching them goes: a pid role.
class Windows : public QStandardItemModel {
  public:
    static constexpr int PidRole = Qt::UserRole + 1;
    Windows() { setItemRoleNames({{PidRole, "pid"}}); }
    void add(int pid) {
        auto *item = new QStandardItem;
        item->setData(pid, PidRole);
        appendRow(item);
    }
    void remove(int pid) {
        for (int row = rowCount() - 1; row >= 0; --row)
            if (item(row)->data(PidRole).toInt() == pid)
                removeRow(row);
    }
};

AudioStreams::Stream stream(uint32_t id, QList<int> processes, int volume = 50, bool muted = false,
                            bool corked = false) {
    return {id, QString("Stream %1").arg(id), "audio-x-generic", volume, muted, corked, processes};
}
} // namespace

class WindowSoundTest : public QObject {
    Q_OBJECT
  private Q_SLOTS:
    // A process and its parents, nearest first, up to but not including pid 1.
    void ancestry() {
        Proc proc;
        proc.process(300, "player", 200);
        proc.process(200, "zsh", 100);
        proc.process(100, "foot", 1);
        QCOMPARE(processAncestry(300, proc.dir.path()), (QList<int>{300, 200, 100}));
        QCOMPARE(processAncestry(100, proc.dir.path()), (QList<int>{100}));
    }
    // A name holding spaces and parentheses, as the kernel writes it unquoted.
    void oddNames() {
        Proc proc;
        proc.process(310, "Web Content", 300);
        proc.process(300, "a) S 1 (b", 200);
        proc.process(200, ") ) (", 1);
        QCOMPARE(processAncestry(310, proc.dir.path()), (QList<int>{310, 300, 200}));
    }
    // What cannot be read ends the walk, keeping the processes named so far: a process that has
    // gone, or one in another namespace, still matches a window of its own.
    void unreadable() {
        Proc proc;
        proc.process(500, "child", 450); // 450 has gone
        proc.stat(600, "garbage");
        proc.stat(610, "610 (truncated)");
        QCOMPARE(processAncestry(400, proc.dir.path()), (QList<int>{400}));
        QCOMPARE(processAncestry(500, proc.dir.path()), (QList<int>{500, 450}));
        QCOMPARE(processAncestry(600, proc.dir.path()), (QList<int>{600}));
        QCOMPARE(processAncestry(610, proc.dir.path()), (QList<int>{610}));
        QCOMPARE(processAncestry(400, "/nonexistent/proc"), (QList<int>{400}));
    }
    void nothingForInit() {
        Proc proc;
        proc.process(1, "init", 0);
        QVERIFY(processAncestry(1, proc.dir.path()).isEmpty());
        QVERIFY(processAncestry(0, proc.dir.path()).isEmpty());
        QVERIFY(processAncestry(-4, proc.dir.path()).isEmpty());
    }
    // A loop of parents, or a long chain, ends at the depth asked for.
    void limits() {
        Proc proc;
        proc.process(700, "a", 701);
        proc.process(701, "b", 700);
        QCOMPARE(processAncestry(700, proc.dir.path()), (QList<int>{700, 701}));
        for (int pid = 800; pid < 900; ++pid)
            proc.process(pid, "deep", pid + 1);
        QCOMPARE(processAncestry(800, proc.dir.path(), 5), (QList<int>{800, 801, 802, 803, 804}));
        QCOMPARE(processAncestry(800, proc.dir.path()).size(), 64);
    }
    // The real procfs: a child of this test, then this test.
    void realProcesses() {
        QProcess child;
        child.start("cat", {});
        QVERIFY(child.waitForStarted());
        const int pid = int(child.processId());
        const auto processes = processAncestry(pid);
        child.closeWriteChannel();
        QVERIFY(child.waitForFinished());
        QVERIFY(processes.size() >= 2);
        QCOMPARE(processes[0], pid);
        QCOMPARE(processes[1], int(getpid()));
    }
    // A stream is the nearest window-owning process's of its process and that one's parents.
    void owner() {
        const QList<int> processes{1020, 1000, 200, 100};
        QCOMPARE(soundOwner(processes, {100, 1000}), 1000);
        QCOMPARE(soundOwner(processes, {100}), 100);
        QCOMPARE(soundOwner(processes, {1020, 1000}), 1020);
        QCOMPARE(soundOwner(processes, {7}), 0);
        QCOMPARE(soundOwner({}, {100}), 0);
    }

    // A browser (process 1000, two windows) playing from a child process, and a terminal (100)
    // that started a player without a window and, from its shell, a second browser (2000).
    void windowSound() {
        TestAudio audio;
        Windows windows;
        for (int pid : {100, 1000, 1000, 2000, 3000})
            windows.add(pid);
        audio.update(
            {"speakers",
             {{"speakers", "Speakers", 50, false}},
             {stream(1, {1010, 1000}, 60), stream(2, {300, 200, 100}, 40),
              stream(3, {1020, 1000}, 90, false, true), stream(4, {2010, 2000, 200, 100})}});
        WindowSound browser, terminal, other, unknown;
        for (auto *sound : {&browser, &terminal, &other, &unknown}) {
            sound->setAudio(&audio);
            sound->setWindows(&windows);
        }
        browser.setPid(1000);
        terminal.setPid(100);
        other.setPid(3000);
        // The browser plays at its loudest playing stream's volume, the paused one aside; the
        // terminal plays its player, and not the second browser started from it.
        QVERIFY(browser.playing() && !browser.muted());
        QCOMPARE(browser.volume(), 60);
        QVERIFY(terminal.playing());
        QCOMPARE(terminal.volume(), 40);
        QVERIFY(!other.playing() && !other.muted());
        QVERIFY(!unknown.playing() && !unknown.muted());

        // Muting a window mutes all its process's streams, the paused one too; then it shows
        // muted, and unmuting unmutes them all.
        QSignalSpy changed(&browser, &WindowSound::changed);
        browser.toggleMute();
        QCOMPARE(audio.requests, (QStringList{"mute 1 1", "mute 3 1"}));
        QVERIFY(!browser.playing() && browser.muted());
        QVERIFY(terminal.playing());
        QCOMPARE(changed.size(), 1);
        audio.requests.clear();
        browser.toggleMute();
        QCOMPARE(audio.requests, (QStringList{"mute 1 0", "mute 3 0"}));
        QVERIFY(browser.playing() && !browser.muted());
        audio.requests.clear();

        // Paused, it plays nothing; muted and paused, it stays muted, to be unmuted.
        audio.update({"speakers",
                      {{"speakers", "Speakers", 50, false}},
                      {stream(1, {1010, 1000}, 60, false, true),
                       stream(2, {300, 200, 100}, 40, true, true)}});
        QVERIFY(!browser.playing() && !browser.muted());
        QVERIFY(!terminal.playing() && terminal.muted());
        browser.toggleMute(); // nothing to mute
        QVERIFY(audio.requests.isEmpty());

        // A window whose process closes leaves its streams to the next process up with one.
        audio.update({"speakers",
                      {{"speakers", "Speakers", 50, false}},
                      {stream(4, {2010, 2000, 200, 100}, 70)}});
        QVERIFY(!terminal.playing());
        windows.remove(2000);
        QVERIFY(terminal.playing());
        QCOMPARE(terminal.volume(), 70);
        windows.add(2000);
        QVERIFY(!terminal.playing());

        // A window learns its process late, and a sound server goes away.
        unknown.setPid(2000);
        QVERIFY(unknown.playing());
        audio.setUnavailable();
        QVERIFY(!unknown.playing() && !unknown.muted());
        unknown.toggleMute();
        QVERIFY(audio.requests.isEmpty());
    }
};

QTEST_GUILESS_MAIN(WindowSoundTest)
#include "window_sound_test.moc"
