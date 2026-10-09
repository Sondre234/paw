// SPDX-License-Identifier: GPL-3.0-or-later
// The media players' model without a bus: which player is current, the order they are listed in,
// what the controls and the media keys ask of which player, and the position between reads.
#include "media.hpp"
#include <QSignalSpy>
#include <QTest>

namespace {
// Records what the model asks of the players.
class FakeMedia : public Media {
  public:
    QStringList requests;

  protected:
    void sendCommand(const QString &name, const QString &method) override {
        requests << method + " " + name;
    }
    void sendPosition(const QString &name, const QString &trackId, qint64 position) override {
        requests << QString("SetPosition %1 %2 %3").arg(name, trackId).arg(position);
    }
    void queryPosition(const QString &name) override { requests << "Position " + name; }
};

Media::Player player(const QString &name, const QString &status, const QString &title = "A song") {
    Media::Player player;
    player.name = "org.mpris.MediaPlayer2." + name;
    player.identity = name;
    player.status = status;
    player.title = title;
    player.trackId = "/track/1";
    player.length = 200'000'000;
    player.canPlay = player.canPause = player.canGoNext = player.canGoPrevious = player.canSeek = true;
    return player;
}
QStringList names(const Media &media) {
    QStringList list;
    for (const auto &entry : media.players())
        list << entry.toMap().value("identity").toString();
    return list;
}
} // namespace

class MediaTest : public QObject {
    Q_OBJECT
  private Q_SLOTS:
    void empty() {
        FakeMedia media;
        QVERIFY(!media.available());
        QCOMPARE(media.player(), QString());
        QCOMPARE(media.index(), -1);
        QCOMPARE(media.status(), QString("Stopped"));
        media.playPause();
        media.next();
        QVERIFY(media.command("stop"));
        QVERIFY(media.requests.isEmpty());
    }
    // A player stopped with nothing loaded is not shown; one with a track, or paused, is.
    void shownPlayers() {
        FakeMedia media;
        QSignalSpy changed(&media, &Media::changed);
        media.setPlayer(player("idle", "Stopped", ""));
        QVERIFY(!media.available());
        QCOMPARE(changed.count(), 1);
        media.setPlayer(player("idle", "Stopped", "Loaded"));
        QVERIFY(media.available());
        QCOMPARE(media.identity(), QString("idle"));
        QCOMPARE(media.title(), QString("Loaded"));
        media.removePlayer("org.mpris.MediaPlayer2.idle");
        QVERIFY(!media.available());
        media.removePlayer("org.mpris.MediaPlayer2.nobody");
    }
    // Playing first, the one that started last ahead; then the paused, by when they last
    // played; then those never seen playing, newest first.
    void order() {
        FakeMedia media;
        media.setPlayer(player("a", "Stopped"));
        media.setPlayer(player("b", "Stopped"));
        QCOMPARE(names(media), (QStringList{"b", "a"}));
        media.setPlayer(player("a", "Playing"));
        QCOMPARE(names(media), (QStringList{"a", "b"}));
        media.setPlayer(player("c", "Paused"));
        QCOMPARE(names(media), (QStringList{"a", "c", "b"}));
        media.setPlayer(player("b", "Playing"));
        QCOMPARE(names(media), (QStringList{"b", "a", "c"}));
        QCOMPARE(media.identity(), QString("b"));
        // Pausing puts it behind those playing, ahead of those that stopped playing before.
        media.setPlayer(player("b", "Paused"));
        QCOMPARE(names(media), (QStringList{"a", "b", "c"}));
        media.setPlayer(player("a", "Paused"));
        QCOMPARE(names(media), (QStringList{"a", "b", "c"}));
        media.setPlayer(player("c", "Playing"));
        media.setPlayer(player("c", "Stopped"));
        QCOMPARE(names(media), (QStringList{"c", "a", "b"}));
        QCOMPARE(media.index(), 0);
    }
    // A player picked stays current until one starts playing.
    void selection() {
        FakeMedia media;
        media.setPlayer(player("a", "Playing"));
        media.setPlayer(player("b", "Paused"));
        QCOMPARE(media.identity(), QString("a"));
        media.select("org.mpris.MediaPlayer2.b");
        QCOMPARE(media.identity(), QString("b"));
        QCOMPARE(media.index(), 1);
        media.select("org.mpris.MediaPlayer2.nobody");
        QCOMPARE(media.identity(), QString("b"));
        media.selectNext(1);
        QCOMPARE(media.identity(), QString("a"));
        media.selectNext(-1);
        QCOMPARE(media.identity(), QString("b"));
        // Changes of the one picked keep it current; another starting to play takes over.
        media.setPlayer(player("a", "Playing", "Another song"));
        QCOMPARE(media.identity(), QString("b"));
        media.setPlayer(player("c", "Playing"));
        QCOMPARE(media.identity(), QString("c"));
        media.select("org.mpris.MediaPlayer2.a");
        media.removePlayer("org.mpris.MediaPlayer2.a");
        QCOMPARE(media.identity(), QString("c"));
    }
    // The controls and the media keys act on the current player, as far as it allows.
    void commands() {
        FakeMedia media;
        media.setPlayer(player("a", "Playing"));
        auto limited = player("b", "Paused");
        limited.canGoNext = limited.canSeek = false;
        media.setPlayer(limited);
        media.playPause();
        QVERIFY(media.command("next"));
        QVERIFY(media.command("previous"));
        QVERIFY(media.command("play-pause"));
        QVERIFY(media.command("stop"));
        QVERIFY(!media.command("rewind"));
        const QString a = "org.mpris.MediaPlayer2.a";
        QCOMPARE(media.requests, (QStringList{"PlayPause " + a, "Next " + a, "Previous " + a,
                                              "PlayPause " + a, "Stop " + a}));
        media.requests.clear();
        media.select("org.mpris.MediaPlayer2.b");
        QVERIFY(!media.canGoNext());
        QVERIFY(!media.canSeek());
        media.next();
        media.seek(1000);
        media.raise();
        QVERIFY(media.requests.isEmpty());
        // A player that takes no control is only shown.
        auto shown = player("b", "Paused");
        shown.canControl = false;
        media.setPlayer(shown);
        media.playPause();
        media.stop();
        QVERIFY(!media.canPlayPause());
        QVERIFY(media.requests.isEmpty());
        shown.canControl = true;
        shown.canRaise = true;
        media.setPlayer(shown);
        media.raise();
        media.refreshPosition();
        QCOMPARE(media.requests, (QStringList{"Raise org.mpris.MediaPlayer2.b",
                                              "Position org.mpris.MediaPlayer2.b"}));
    }
    // Between reads the position moves on while the player plays, and stops where it got to
    // when it pauses; a new track starts at 0; a seek shows at once.
    void position() {
        FakeMedia media;
        QSignalSpy moved(&media, &Media::positionChanged);
        auto playing = player("a", "Playing");
        playing.position = 10'000'000;
        media.setPlayer(playing);
        QCOMPARE(media.length(), 200'000.0);
        QTest::qWait(120);
        QVERIFY2(media.position() >= 10'100 && media.position() < 10'600, qPrintable(QString::number(media.position())));
        auto paused = player("a", "Paused");
        media.setPlayer(paused);
        const double stoppedAt = media.position();
        QVERIFY(stoppedAt >= 10'100);
        QTest::qWait(60);
        QCOMPARE(media.position(), stoppedAt);
        media.setPosition("org.mpris.MediaPlayer2.a", 50'000'000);
        QCOMPARE(media.position(), 50'000.0);
        auto next = player("a", "Paused", "Next song");
        next.trackId = "/track/2";
        media.setPlayer(next);
        QCOMPARE(media.position(), 0.0);
        moved.clear();
        media.seek(30'000);
        QCOMPARE(media.requests, QStringList{"SetPosition org.mpris.MediaPlayer2.a /track/2 30000000"});
        QCOMPARE(media.position(), 30'000.0);
        QCOMPARE(moved.count(), 1);
        // Never past the track's end, nor before its start.
        media.seek(500'000);
        QCOMPARE(media.position(), 200'000.0);
        media.seek(-5);
        QCOMPARE(media.position(), 0.0);
    }
};
QTEST_MAIN(MediaTest)
#include "media_test.moc"
