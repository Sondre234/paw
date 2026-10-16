// SPDX-License-Identifier: GPL-3.0-or-later
// The wallpaper slideshow's order (shell.wallpaper_cycle): a folder's pictures by name or
// shuffled, each shown once before any comes again, stepping back through what was shown, the
// monitors each taking the next one; and its timer, which waits while paused.
#include "wallpaper_cycle.hpp"
#include <QFile>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {
paw::WallpaperCycleConfig order(bool shuffle, int interval = 0) {
    paw::WallpaperCycleConfig config;
    config.shuffle = shuffle;
    config.interval = interval;
    return config;
}
} // namespace

class WallpaperCycleTest : public QObject {
    Q_OBJECT
    QTemporaryDir directory_;
    QString folder_;
    // The folder's pictures by name, as the cycle finds them.
    QStringList pictures_;
    QString at(const QString &name) const { return folder_ + "/" + name; }

  private Q_SLOTS:
    void initTestCase() {
        QVERIFY(directory_.isValid());
        folder_ = directory_.path() + "/sky";
        QVERIFY(QDir().mkpath(folder_ + "/more"));
        // Only the pictures directly in it count, numbers in order, whatever case they are in.
        for (const auto *name : {"10.png", "2.jpg", "aurora.JPG", "kasumi.webp", "Kumo.jpeg",
                                 "mochi.png", "nami.bmp", "notes.txt", "more/deep.png"}) {
            QFile file(folder_ + "/" + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        pictures_ = {at("2.jpg"),       at("10.png"),    at("aurora.JPG"), at("kasumi.webp"),
                     at("Kumo.jpeg"),   at("mochi.png"), at("nami.bmp")};
    }
    void findsThePicturesByName() {
        QCOMPARE(WallpaperCycle::pictures(folder_), pictures_);
        QCOMPARE(WallpaperCycle::pictures(folder_ + "/"), pictures_);
        QVERIFY(WallpaperCycle::pictures(directory_.path() + "/none").isEmpty());
        QVERIFY(WallpaperCycle::pictures("").isEmpty());
    }
    void stepsByName() {
        WallpaperCycle cycle;
        cycle.configure(order(false), folder_);
        QCOMPARE(cycle.step({at("kasumi.webp")}, 1), QStringList{at("Kumo.jpeg")});
        QCOMPARE(cycle.step({at("Kumo.jpeg")}, 1), QStringList{at("mochi.png")});
        QCOMPARE(cycle.step({at("mochi.png")}, -1), QStringList{at("Kumo.jpeg")});
        QCOMPARE(cycle.step({at("Kumo.jpeg")}, -1), QStringList{at("kasumi.webp")});
        // Round past either end.
        QCOMPARE(cycle.step({at("nami.bmp")}, 1), QStringList{at("2.jpg")});
        QCOMPARE(cycle.step({at("2.jpg")}, -1), QStringList{at("nami.bmp")});
        QCOMPARE(cycle.step({at("nami.bmp")}, -1), QStringList{at("mochi.png")});
        // A picture picked meanwhile goes on from there; one from elsewhere from the start.
        QCOMPARE(cycle.step({at("aurora.JPG")}, 1), QStringList{at("kasumi.webp")});
        QCOMPARE(cycle.step({"/elsewhere/sky.png"}, 1), QStringList{at("2.jpg")});
        QCOMPARE(cycle.step({"/elsewhere/sky.png"}, -1), QStringList{at("nami.bmp")});
        QCOMPARE(cycle.step({""}, 1), QStringList{at("2.jpg")});
        // The same path written another way is the same picture.
        QCOMPARE(cycle.step({folder_ + "/./more/../mochi.png"}, 1), QStringList{at("nami.bmp")});
    }
    void shufflesEachOnceBeforeAnyAgain() {
        for (unsigned seed = 1; seed <= 20; ++seed) {
            WallpaperCycle cycle;
            cycle.seed(seed);
            cycle.configure(order(true), folder_);
            // The one shown counts: the six steps after it show the other six, and each seven
            // after those all seven again.
            QString last = at("mochi.png");
            QSet<QString> seen{last};
            for (int round = 0; round < 3; ++round) {
                if (round > 0)
                    seen.clear();
                for (int i = 0; i < (round == 0 ? 6 : 7); ++i) {
                    const auto next = cycle.step({last}, 1);
                    QCOMPARE(next.size(), 1);
                    QVERIFY2(!seen.contains(next[0]),
                             qPrintable(QString("seed %1, round %2: %3 again").arg(seed).arg(round).arg(next[0])));
                    QVERIFY2(next[0] != last, "the same picture twice in a row");
                    seen.insert(next[0]);
                    last = next[0];
                }
                QCOMPARE(seen.size(), pictures_.size());
            }
        }
    }
    void stepsBackThroughWhatWasShown() {
        WallpaperCycle cycle;
        cycle.seed(7);
        cycle.configure(order(true), folder_);
        QStringList shown{at("2.jpg")};
        for (int i = 0; i < 10; ++i)
            shown.push_back(cycle.step({shown.last()}, 1).value(0));
        // Back the way it came, past the start of the second deck and of the first.
        for (int i = 9; i >= 0; --i)
            QCOMPARE(cycle.step({shown[i + 1]}, -1), QStringList{shown[i]});
        const auto before = cycle.step({shown[0]}, -1);
        QCOMPARE(before.size(), 1);
        QVERIFY(before[0] != shown[0]);
        // And forward again over the same pictures.
        QCOMPARE(cycle.step(before, 1), QStringList{shown[0]});
        QCOMPARE(cycle.step({shown[0]}, 1), QStringList{shown[1]});
    }
    void givesEachMonitorTheNext() {
        // By name, two monitors showing the same picture take the two after it.
        WallpaperCycle named;
        named.configure(order(false), folder_);
        QCOMPARE(named.step({at("2.jpg"), at("2.jpg")}, 1),
                 (QStringList{at("10.png"), at("aurora.JPG")}));
        QCOMPARE(named.step({at("10.png"), at("aurora.JPG")}, 1),
                 (QStringList{at("kasumi.webp"), at("Kumo.jpeg")}));
        QCOMPARE(named.step({at("kasumi.webp"), at("Kumo.jpeg")}, -1),
                 (QStringList{at("10.png"), at("aurora.JPG")}));
        // Shuffled, three monitors never show one picture twice at once, nor any again before
        // the deck is through.
        for (unsigned seed = 1; seed <= 20; ++seed) {
            WallpaperCycle cycle;
            cycle.seed(seed);
            cycle.configure(order(true), folder_);
            QStringList shown{at("mochi.png"), at("mochi.png"), at("mochi.png")};
            QSet<QString> seen{at("mochi.png")};
            for (int i = 0; i < 2; ++i) {
                shown = cycle.step(shown, 1);
                QCOMPARE(shown.size(), 3);
                for (const auto &picture : shown) {
                    QVERIFY2(!seen.contains(picture), qPrintable(QString("seed %1: %2 again").arg(seed).arg(picture)));
                    seen.insert(picture);
                }
            }
            QCOMPARE(seen.size(), pictures_.size());
            // Into the next deck: still three apart from those just shown.
            const auto next = cycle.step(shown, 1);
            QCOMPARE(QSet<QString>(next.begin(), next.end()).size(), 3);
            for (const auto &picture : next)
                QVERIFY2(!shown.contains(picture), "a picture shown at the end of a deck came at the start of the next");
        }
    }
    void followsTheFolder() {
        QTemporaryDir directory;
        const auto folder = directory.path();
        WallpaperCycle cycle;
        cycle.configure(order(false), folder);
        QVERIFY(cycle.step({""}, 1).isEmpty());
        for (const auto *name : {"a.png", "c.png"}) {
            QFile file(folder + "/" + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        QCOMPARE(cycle.step({folder + "/a.png"}, 1), QStringList{folder + "/c.png"});
        QFile b(folder + "/b.png");
        QVERIFY(b.open(QIODevice::WriteOnly));
        QCOMPARE(cycle.step({folder + "/a.png"}, 1), QStringList{folder + "/b.png"});
        // Another folder, and a single picture, which follows itself.
        QTemporaryDir other;
        QFile only(other.path() + "/only.png");
        QVERIFY(only.open(QIODevice::WriteOnly));
        cycle.configure(order(true), other.path());
        QCOMPARE(cycle.step({folder + "/a.png"}, 1), QStringList{other.path() + "/only.png"});
        QCOMPARE(cycle.step({other.path() + "/only.png"}, 1), QStringList{other.path() + "/only.png"});
    }
    void stepsEveryInterval() {
        WallpaperCycle cycle;
        QSignalSpy due(&cycle, &WallpaperCycle::due);
        cycle.setMinute(40);
        cycle.configure(order(true), folder_);
        QVERIFY(!cycle.running());
        cycle.configure(order(true, 2), folder_); // 80 ms
        QVERIFY(cycle.running());
        QVERIFY(due.wait(1000));
        QVERIFY(cycle.running());
        QVERIFY(due.wait(1000));
        // A reload that keeps the interval keeps the time; paused, none comes.
        cycle.configure(order(false, 2), folder_);
        QVERIFY(cycle.running());
        cycle.setPaused(true);
        QVERIFY(!cycle.running());
        const auto count = due.count();
        QTest::qWait(200);
        QCOMPARE(due.count(), count);
        cycle.setPaused(false);
        QVERIFY(cycle.running());
        QVERIFY(due.wait(1000));
        // A step starts the time over; 0 stops it.
        cycle.step({at("2.jpg")}, 1);
        QVERIFY(cycle.running());
        cycle.configure(order(false, 0), folder_);
        QVERIFY(!cycle.running());
        // Paused, a new interval waits too.
        cycle.setPaused(true);
        cycle.configure(order(false, 1), folder_);
        QVERIFY(!cycle.running());
        cycle.setPaused(false);
        QVERIFY(cycle.running());
    }
};

QTEST_GUILESS_MAIN(WallpaperCycleTest)
#include "wallpaper_cycle_test.moc"
