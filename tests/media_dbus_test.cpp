// SPDX-License-Identifier: GPL-3.0-or-later
// The MPRIS backend against stand-in players on a private bus (a dbus-daemon this test starts and
// kills), never the real one: finding players already there and those that come, following
// their changes, the order, the controls, the position, and players going.
#include "fake_dbus.hpp"
#include "mpris.hpp"
#include <QDBusObjectPath>
#include <QSignalSpy>
#include <QTest>
#include <memory>

namespace {
constexpr auto rootInterface = "org.mpris.MediaPlayer2";
constexpr auto playerInterface = "org.mpris.MediaPlayer2.Player";

// A player on a connection of its own, as a program would be, owning org.mpris.MediaPlayer2.NAME.
class FakePlayer {
  public:
    FakePlayer(fakedbus::Bus &bus, const QString &name, const QString &status = "Paused",
               const QString &title = "First song")
        : name_("org.mpris.MediaPlayer2." + name), bus_(bus.connect()),
          object(bus_, "/org/mpris/MediaPlayer2") {
        object.properties[rootInterface] = {{"Identity", "Player " + name},
                                            {"DesktopEntry", name},
                                            {"CanRaise", true}};
        object.properties[playerInterface] = {{"PlaybackStatus", status},
                                              {"Metadata", metadata(title, "/track/1")},
                                              {"Rate", 1.0},
                                              {"Position", qlonglong(5'000'000)},
                                              {"CanPlay", true},
                                              {"CanPause", true},
                                              {"CanGoNext", true},
                                              {"CanGoPrevious", true},
                                              {"CanSeek", true},
                                              {"CanControl", true}};
        object.methods = [](const QDBusMessage &call, QDBusConnection &connection) {
            connection.send(call.createReply());
            return true;
        };
    }
    ~FakePlayer() {
        bus_.unregisterService(name_);
        QDBusConnection::disconnectFromBus(bus_.name());
    }
    bool start() { return bus_.registerService(name_); }
    void stop() { bus_.unregisterService(name_); }
    QString name() const { return name_; }
    static QVariantMap metadata(const QString &title, const QString &track) {
        return {{"xesam:title", title},
                {"xesam:artist", QStringList{"Ann", "Bo"}},
                {"xesam:album", "Album"},
                {"mpris:artUrl", "file:///tmp/cover.png"},
                {"mpris:length", qlonglong(180'000'000)},
                {"mpris:trackid", QVariant::fromValue(QDBusObjectPath(track))}};
    }

  private:
    QString name_;
    QDBusConnection bus_;

  public:
    fakedbus::Object object;
};
} // namespace

class MediaDbusTest : public QObject {
    Q_OBJECT
    fakedbus::Bus bus_;
    std::unique_ptr<Mpris> media_;

  private Q_SLOTS:
    void initTestCase() {
        if (!fakedbus::Bus::available())
            QSKIP("dbus-daemon is not installed");
        QVERIFY(bus_.start());
    }
    void cleanup() { media_.reset(); }
    void names() {
        QVERIFY(Mpris::playerName("org.mpris.MediaPlayer2.vlc"));
        QVERIFY(Mpris::playerName("org.mpris.MediaPlayer2.firefox.instance_1_23"));
        QVERIFY(!Mpris::playerName("org.mpris.MediaPlayer2."));
        QVERIFY(!Mpris::playerName("org.mpris.MediaPlayer2.playerctld"));
        QVERIFY(!Mpris::playerName("org.freedesktop.Notifications"));
    }
    // One there already is found and read whole; one that comes later too.
    void found() {
        FakePlayer early(bus_, "early");
        QVERIFY(early.start());
        media_ = std::make_unique<Mpris>(bus_.connect());
        QTRY_VERIFY(media_->available());
        QCOMPARE(media_->player(), early.name());
        QCOMPARE(media_->identity(), QString("Player early"));
        QCOMPARE(media_->desktopEntry(), QString("early"));
        QCOMPARE(media_->title(), QString("First song"));
        QCOMPARE(media_->artist(), QString("Ann, Bo"));
        QCOMPARE(media_->album(), QString("Album"));
        QCOMPARE(media_->art(), QString("file:///tmp/cover.png"));
        QCOMPARE(media_->status(), QString("Paused"));
        QCOMPARE(media_->length(), 180'000.0);
        QVERIFY(media_->canPlayPause() && media_->canGoNext() && media_->canGoPrevious() &&
                media_->canSeek() && media_->canRaise());
        // Its position is read as it is found.
        QTRY_COMPARE(media_->position(), 5'000.0);
        FakePlayer late(bus_, "late", "Playing");
        QVERIFY(late.start());
        QTRY_COMPARE(media_->players().size(), 2);
        // Playing, it goes first and becomes the current player.
        QCOMPARE(media_->player(), late.name());
        QCOMPARE(media_->index(), 0);
        // One that goes is forgotten.
        late.stop();
        QTRY_COMPARE(media_->players().size(), 1);
        QCOMPARE(media_->player(), early.name());
        early.stop();
        QTRY_VERIFY(!media_->available());
    }
    // Changes as the player announces them; a property it only says changed is read again.
    void changes() {
        FakePlayer player(bus_, "changing");
        QVERIFY(player.start());
        media_ = std::make_unique<Mpris>(bus_.connect());
        QTRY_VERIFY(media_->available());
        player.object.set(playerInterface, "PlaybackStatus", "Playing");
        QTRY_VERIFY(media_->playing());
        // A new track: its metadata, and where the player says it is in it.
        player.object.properties[playerInterface]["Position"] = qlonglong(1'000'000);
        player.object.set(playerInterface, "Metadata", FakePlayer::metadata("Second song", "/track/2"));
        QTRY_COMPARE(media_->title(), QString("Second song"));
        QTRY_VERIFY(media_->position() >= 1'000 && media_->position() < 2'000);
        player.object.set(playerInterface, "CanGoNext", false);
        QTRY_VERIFY(!media_->canGoNext());
        player.object.properties[playerInterface]["PlaybackStatus"] = "Paused";
        player.object.emitSignal(fakedbus::propertiesInterface, "PropertiesChanged",
                                 {QString(playerInterface), QVariantMap(), QStringList{"PlaybackStatus"}});
        QTRY_COMPARE(media_->status(), QString("Paused"));
        player.object.set(rootInterface, "Identity", "Renamed");
        QTRY_COMPARE(media_->identity(), QString("Renamed"));
        // A seek the player announces.
        player.object.emitSignal(playerInterface, "Seeked", {qlonglong(90'000'000)});
        QTRY_COMPARE(media_->position(), 90'000.0);
        // Another's signals change nothing of this one's.
        FakePlayer other(bus_, "other", "Paused", "Other song");
        QVERIFY(other.start());
        QTRY_COMPARE(media_->players().size(), 2);
        other.object.emitSignal(playerInterface, "Seeked", {qlonglong(0)});
        other.object.set(playerInterface, "Metadata", FakePlayer::metadata("Other again", "/track/9"));
        QTRY_COMPARE(media_->players().value(1).toMap().value("title").toString(), QString("Other again"));
        QCOMPARE(media_->player(), player.name());
        QCOMPARE(media_->title(), QString("Second song"));
        QCOMPARE(media_->position(), 90'000.0);
    }
    // The controls call the current player's methods.
    void controls() {
        FakePlayer player(bus_, "controlled", "Playing");
        QVERIFY(player.start());
        media_ = std::make_unique<Mpris>(bus_.connect());
        QTRY_VERIFY(media_->available());
        media_->playPause();
        QVERIFY(media_->command("next"));
        QVERIFY(media_->command("previous"));
        QVERIFY(media_->command("stop"));
        media_->raise();
        media_->seek(30'000);
        QCOMPARE(media_->position(), 30'000.0);
        QTRY_COMPARE(player.object.calls,
                     (QStringList{"PlayPause", "Next", "Previous", "Stop", "Raise", "SetPosition /track/1 30000000"}));
    }
    // A name only like a player's, and playerctld's stand-in for another, are not players.
    void notPlayers() {
        FakePlayer proxy(bus_, "playerctld", "Playing");
        QVERIFY(proxy.start());
        auto connection = bus_.connect();
        QVERIFY(connection.registerService("org.mpris.MediaPlayer3.thing"));
        media_ = std::make_unique<Mpris>(bus_.connect());
        FakePlayer real(bus_, "real");
        QVERIFY(real.start());
        QTRY_VERIFY(media_->available());
        QTest::qWait(100);
        QCOMPARE(media_->players().size(), 1);
        QCOMPARE(media_->player(), real.name());
        QDBusConnection::disconnectFromBus(connection.name());
    }
};
QTEST_MAIN(MediaDbusTest)
#include "media_dbus_test.moc"
