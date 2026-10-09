// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QVariantList>
#include <memory>
#include <vector>

// The media players as Quick Settings shows them (MPRIS): the player playing most lately first,
// the current one's track and the controls it offers. A backend delivers the players through
// setPlayer() and removePlayer() and carries out the send*() requests. Players stopped with
// nothing loaded are left out, so `available` says whether there is anything to control.
class Media : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    // The players shown, the one playing most lately first, as {name, identity, desktopEntry,
    // status, title, artist}.
    Q_PROPERTY(QVariantList players READ players NOTIFY changed)
    // The current player's bus name: the one picked with select(), else the first. The media keys
    // and the card act on it.
    Q_PROPERTY(QString player READ player NOTIFY changed)
    Q_PROPERTY(int index READ index NOTIFY changed)
    // Its name ("Spotify"), and its desktop entry's id, for its icon.
    Q_PROPERTY(QString identity READ identity NOTIFY changed)
    Q_PROPERTY(QString desktopEntry READ desktopEntry NOTIFY changed)
    // The track: its title, artists, album and cover's URL (file:, http: or data:; "" for none).
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString artist READ artist NOTIFY changed)
    Q_PROPERTY(QString album READ album NOTIFY changed)
    Q_PROPERTY(QString art READ art NOTIFY changed)
    // "Playing", "Paused" or "Stopped".
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(bool canPlayPause READ canPlayPause NOTIFY changed)
    Q_PROPERTY(bool canGoNext READ canGoNext NOTIFY changed)
    Q_PROPERTY(bool canGoPrevious READ canGoPrevious NOTIFY changed)
    Q_PROPERTY(bool canSeek READ canSeek NOTIFY changed)
    Q_PROPERTY(bool canRaise READ canRaise NOTIFY changed)
    // The track's length in milliseconds, 0 when the player does not say.
    Q_PROPERTY(double length READ length NOTIFY changed)
  public:
    struct Player {
        QString name; // its bus name, org.mpris.MediaPlayer2.*
        QString identity, desktopEntry;
        QString status = QStringLiteral("Stopped");
        QString title, artist, album, art, trackId;
        qint64 length = 0;   // microseconds
        qint64 position = -1; // microseconds, when last read; -1 when not read
        double rate = 1;
        bool canPlay = false, canPause = false, canGoNext = false, canGoPrevious = false,
             canSeek = false, canControl = true, canRaise = false;
    };
    explicit Media(QObject *parent = nullptr);
    bool available() const { return !shown_.empty(); }
    QVariantList players() const;
    QString player() const;
    int index() const;
    QString identity() const { return field(&Player::identity); }
    QString desktopEntry() const { return field(&Player::desktopEntry); }
    QString title() const { return field(&Player::title); }
    QString artist() const { return field(&Player::artist); }
    QString album() const { return field(&Player::album); }
    QString art() const { return field(&Player::art); }
    QString status() const;
    bool playing() const { return status() == "Playing"; }
    bool canPlayPause() const;
    bool canGoNext() const;
    bool canGoPrevious() const;
    bool canSeek() const;
    bool canRaise() const;
    double length() const;
    // The current player as known, or null without one.
    const Player *current() const;
    // A player appeared or changed: all of it, as the backend now knows it.
    void setPlayer(Player player);
    void removePlayer(const QString &name);
    // What a player says its position is now (microseconds), as read or after a seek.
    void setPosition(const QString &name, qint64 position);
    // The current player's position in milliseconds now: the one last read, moved on by the time
    // since at its rate while it plays, within the track.
    Q_INVOKABLE double position() const;
    // Asks the current player where it is; position() follows once it answers.
    Q_INVOKABLE void refreshPosition();
    // Makes another player the current one, until a player starts playing.
    Q_INVOKABLE void select(const QString &name);
    // Steps to the next (1) or previous (-1) player shown, wrapping.
    Q_INVOKABLE void selectNext(int step);
    Q_INVOKABLE void playPause();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void stop();
    // Brings the player's window forward, where it can.
    Q_INVOKABLE void raise();
    // Moves the current track to `milliseconds` from its start.
    Q_INVOKABLE void seek(double milliseconds);
    // A media key from the compositor ("media VERB"): play-pause, next, previous or stop. Returns
    // whether it knew the verb.
    bool command(const QString &verb);
  Q_SIGNALS:
    void changed();
    // The current player's position changed other than by playing on: read, or sought.
    void positionChanged();

  protected:
    // Calls a method of org.mpris.MediaPlayer2.Player (PlayPause, Next, Previous, Stop), or Raise
    // of org.mpris.MediaPlayer2, on the player `name`.
    virtual void sendCommand(const QString &name, const QString &method) = 0;
    // SetPosition(trackId, position) on the player.
    virtual void sendPosition(const QString &name, const QString &trackId, qint64 position) = 0;
    // Reads the player's Position, and calls setPosition() with it.
    virtual void queryPosition(const QString &name) = 0;

  private:
    struct Entry {
        Player player;
        QElapsedTimer since; // since `position` was read
        // When it last started or stopped playing, or appeared: larger is later.
        quint64 order = 0;
        bool played = false;
    };
    std::vector<Entry> players_;
    // Those shown, the one playing most lately first.
    std::vector<const Entry *> shown_;
    QString chosen_;
    quint64 clock_ = 0;
    Entry *find(const QString &name);
    static qint64 now(const Entry &entry);
    void sort();
    QString field(QString Player::*member) const;
};

// The MPRIS backend on the session bus, or one that never has players when paw was built
// without Qt's D-Bus module or the session has no bus.
std::unique_ptr<Media> makeMedia();
