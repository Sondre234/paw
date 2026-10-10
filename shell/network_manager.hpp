// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "wifi.hpp"
#include <QDBusConnection>
#include <QHash>
#include <QTimer>
#include <QVariantMap>
#include <optional>

class QDBusMessage;

// NetworkManager on a system bus: its devices, the access points the Wi-Fi ones see, the
// connections active and those it knows, each read as it appears and followed through
// PropertiesChanged until it goes; unavailable while NetworkManager does not run. A known network
// is connected to with ActivateConnection, another with AddAndActivateConnection (WPA and WPA2
// Personal, WPA3 Personal, open and Enhanced Open); a connection that fails is said, and one added
// for it removed again so that a wrong password is not kept.
class NetworkManager : public Wifi {
    Q_OBJECT
  public:
    explicit NetworkManager(const QDBusConnection &bus, QObject *parent = nullptr);

  protected:
    void sendEnabled(bool enabled) override;
    void sendScan() override;
    void sendConnect(const QString &ssid, const QString &security, const QString &password) override;
    void sendDisconnect() override;

  private Q_SLOTS:
    void propertiesChanged(const QDBusMessage &message);
    void connectionsChanged();

  private:
    QDBusConnection bus_;
    // Counts NetworkManager's comings and goings, so that an answer from before one is dropped.
    int generation_ = 0;
    bool running_ = false, managerRead_ = false;
    // What NetworkManager said of its objects, by path: the manager, each device (and its Wi-Fi
    // interface), each access point, each active connection; and the networks of the connections
    // it knows.
    QVariantMap manager_;
    struct Device {
        QVariantMap device, wireless;
    };
    QHash<QString, Device> devices_;
    QHash<QString, QVariantMap> accessPoints_, actives_;
    struct Connection {
        QString type, ssid;
    };
    QHash<QString, Connection> connections_;
    // A connection asked for, until it is active or fails: the network, its active connection, the
    // connection added for it ("" for a known one) and whether a password went with it.
    struct Pending {
        QString ssid, active, added;
        bool password = false;
    };
    std::optional<Pending> pending_;
    QTimer publish_;
    void start();
    void stop();
    // Reads all of one interface of an object, and hands it to `done` unless NetworkManager went
    // or came again meanwhile; an error hands it nothing.
    void getAll(const QString &path, const QString &interface, std::function<void(const QVariantMap &)> done);
    void readDevice(const QString &path);
    void readAccessPoint(const QString &path);
    void readActive(const QString &path);
    void readConnection(const QString &path);
    // Reads the objects a list property names that are new, and forgets those it no longer does.
    void syncDevices();
    void syncAccessPoints();
    void syncActives();
    // Whether the connection asked for is active now, or failed.
    void checkPending();
    // The connection asked for failed: NetworkManager's `error`, or "" when it only went.
    void fail(const QString &error = {});
    // The Wi-Fi devices, those seeing `ssid` first.
    QStringList wifiDevices(const QString &ssid = {}) const;
    QString ssidOf(const QString &accessPoint) const;
    void publish();
};
