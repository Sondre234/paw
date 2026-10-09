// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "media.hpp"
#include <QDBusConnection>
#include <QHash>
#include <QSet>

class QDBusMessage;

// The MPRIS players on a session bus: every name under org.mpris.MediaPlayer2. (but
// playerctld's, which only stands in for another), read as it appears and followed through
// PropertiesChanged and Seeked until its owner goes. Signals come from a player's unique name,
// which the names it owns map back to.
class Mpris : public Media {
    Q_OBJECT
  public:
    explicit Mpris(const QDBusConnection &bus, QObject *parent = nullptr);
    // Whether a bus name is a player's.
    static bool playerName(const QString &name);

  protected:
    void sendCommand(const QString &name, const QString &method) override;
    void sendPosition(const QString &name, const QString &trackId, qint64 position) override;
    void queryPosition(const QString &name) override;

  private Q_SLOTS:
    void nameOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);
    void propertiesChanged(const QDBusMessage &message);
    void seeked(const QDBusMessage &message);

  private:
    QDBusConnection bus_;
    // What is known of each player by its bus name, and its owner's unique name.
    struct Known {
        QString owner;
        QVariantMap root, player; // the two interfaces' properties
        bool rootRead = false, playerRead = false;
    };
    QHash<QString, Known> known_;
    void add(const QString &name, const QString &owner);
    void forget(const QString &name);
    // Reads all of one interface of the player, again when it says some changed unannounced.
    void readAll(const QString &name, const QString &interface);
    // Hands the player's properties to the model, once both interfaces are read.
    void publish(const QString &name, qint64 position = -1);
    // The names the owner of a signal holds.
    QStringList namesOf(const QString &owner) const;
};
