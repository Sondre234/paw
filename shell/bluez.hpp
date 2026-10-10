// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "bluetooth.hpp"
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QMap>
#include <QTimer>
#include <QVariantMap>


// BlueZ on a system bus: its objects (adapters, devices and their batteries) from its
// ObjectManager, followed through InterfacesAdded, InterfacesRemoved and PropertiesChanged, the
// first adapter standing for Bluetooth; unavailable while BlueZ does not run or has no adapter.
// Pairing goes through an agent of the shell's own (org.bluez.Agent1, KeyboardDisplay), registered
// as the default agent once the user first looks for devices or pairs, so that a shell that is never
// asked takes no part in pairing; its questions wait for an answer from the user, and one that is
// still waiting is cancelled as another comes. A device paired is trusted, then connected.
class BlueZ : public Bluetooth {
    Q_OBJECT
  public:
    explicit BlueZ(const QDBusConnection &bus, QObject *parent = nullptr);
    ~BlueZ() override;
    // The agent's object path.
    static QString agentPath();

  protected:
    void sendPowered(bool powered) override;
    void sendDiscovery(bool discovering) override;
    void sendConnect(const QString &path) override;
    void sendDisconnect(const QString &path) override;
    void sendPair(const QString &path) override;
    void sendForget(const QString &path) override;
    void sendAnswer(bool accepted, const QString &input) override;

  private Q_SLOTS:
    void interfacesAdded(const QDBusMessage &message);
    void interfacesRemoved(const QDBusMessage &message);
    void propertiesChanged(const QDBusMessage &message);

  private:
    class Agent;
    QDBusConnection bus_;
    Agent *agent_ = nullptr;
    // Counts BlueZ's comings and goings, so that an answer from before one is dropped.
    int generation_ = 0;
    bool running_ = false, agentRegistered_ = false;
    // BlueZ's objects by path, and their interfaces' properties.
    QMap<QString, QMap<QString, QVariantMap>> objects_;
    // The agent's question waiting for the user, what it asks ("pin", "passkey", "confirm",
    // "authorize"), and the device a "display" shows a code for.
    QDBusMessage question_;
    QString questionKind_, displayed_;
    // Whether the user said no to the pairing under way, which is then no failure to tell of.
    bool refused_ = false;
    QTimer publish_;
    void start();
    void stop();
    void publish();
    QString adapter() const;
    QString nameOf(const QString &path) const;
    // Registers the agent with BlueZ, once while it runs.
    void ensureAgent();
    // A call of the agent's, from BlueZ; returns whether it knew the method.
    bool agentCall(const QDBusMessage &message);
    // Answers a question still waiting with org.bluez.Error.Canceled.
    void dropQuestion();
    // Calls a method of BlueZ's object `path` and hands `done` the error (an invalid one for
    // none), unless BlueZ went or came again meanwhile.
    void call(const QString &path, const QString &interface, const QString &method, const QVariantList &arguments,
              int timeout, std::function<void(const QDBusError &error)> done);
};
