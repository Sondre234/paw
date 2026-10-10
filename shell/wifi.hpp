// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "row_model.hpp"
#include <QObject>
#include <QStringList>
#include <memory>
#include <vector>

// A Wi-Fi network in range, one row of WifiNetworks.
struct WifiNetwork {
    QString ssid;
    int strength = 0;
    // "open", "owe" (Enhanced Open), "wpa-psk" (WPA or WPA2 Personal, or WPA3 in transition),
    // "sae" (WPA3 Personal), "wep" or "enterprise".
    QString security;
    bool secured = false, known = false, active = false, connecting = false;
    QString key() const { return ssid; }
    bool operator==(const WifiNetwork &) const = default;
};

// The Wi-Fi networks in range, one row each, kept in place (RowModel), so that a row being typed
// in (a password) stays as signals change and the list reorders.
class WifiNetworks : public RowModel<WifiNetwork> {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
  public:
    using Network = WifiNetwork;
    enum Role { SsidRole = Qt::UserRole + 1, StrengthRole, SecurityRole, SecuredRole, KnownRole, ActiveRole, ConnectingRole };
    using RowModel::RowModel;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    // The networks as they are now, in order.
    void update(const std::vector<Network> &networks) {
        if (replace(networks))
            Q_EMIT countChanged();
    }
  Q_SIGNALS:
    void countChanged();
};

// Wi-Fi as Quick Settings and the network widget show it, from NetworkManager: the radio, the
// networks in range (one entry for each name, at its strongest access point), the one connected
// and the connection the machine goes out through. A backend delivers NetworkManager's state
// through update() and carries out the send*() requests; `available` is false while
// NetworkManager does not run, and the widgets then read the kernel's interfaces instead.
class Wifi : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    // Whether NetworkManager has a Wi-Fi device to show.
    Q_PROPERTY(bool hasWifi READ hasWifi NOTIFY changed)
    // The radio: on, and not held off by a switch on the machine (rfkill).
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(bool hardwareEnabled READ hardwareEnabled NOTIFY changed)
    // The networks in range, the one connected first, then the one being connected to, then by
    // signal (0-100); empty while the radio is off.
    Q_PROPERTY(WifiNetworks *networks READ networks CONSTANT)
    // The Wi-Fi network connected to and its signal; "" and 0 for none.
    Q_PROPERTY(QString ssid READ ssid NOTIFY changed)
    Q_PROPERTY(int strength READ strength NOTIFY changed)
    // The network being connected to, from the moment it is asked for; "" for none.
    Q_PROPERTY(QString connecting READ connecting NOTIFY changed)
    // The connection the machine goes out through: "wifi", "ethernet", "other" or "" for none,
    // and its name (the network's, or NetworkManager's name for a wired one).
    Q_PROPERTY(QString primaryType READ primaryType NOTIFY changed)
    Q_PROPERTY(QString primaryName READ primaryName NOTIFY changed)
  public:
    struct AccessPoint {
        QString ssid;
        int strength = 0;
        QString security;
    };
    struct State {
        bool available = false, hasWifi = false, enabled = false, hardwareEnabled = true;
        std::vector<AccessPoint> accessPoints;
        // The networks NetworkManager has a connection for, by name.
        QStringList known;
        QString ssid;
        int strength = 0;
        // The network NetworkManager is connecting to.
        QString activating;
        QString primaryType, primaryName;
    };
    // An access point's security from NetworkManager's flags for it: Flags (802.11 privacy),
    // WpaFlags and RsnFlags (key management).
    static QString security(uint flags, uint wpaFlags, uint rsnFlags);
    explicit Wifi(QObject *parent = nullptr) : QObject(parent), networks_(this) {}
    bool available() const { return state_.available; }
    bool hasWifi() const { return state_.hasWifi; }
    bool enabled() const { return state_.enabled; }
    bool hardwareEnabled() const { return state_.hardwareEnabled; }
    WifiNetworks *networks() { return &networks_; }
    const WifiNetworks *networks() const { return &networks_; }
    QString ssid() const { return state_.ssid; }
    int strength() const { return state_.strength; }
    QString connecting() const;
    QString primaryType() const { return state_.primaryType; }
    QString primaryName() const { return state_.primaryName; }
    void update(State state);
    // A connection the backend tried failed; `password` when for its password, which is then
    // asked for again (passwordRejected).
    void connectionFailed(const QString &ssid, const QString &message, bool password);
    // Whether connecting to `ssid` asks for a password first: secured with a key (WPA, WPA2 or
    // WPA3 Personal) and not known to NetworkManager.
    Q_INVOKABLE bool needsPassword(const QString &ssid) const;
    // Whether the shell can connect to it: known, open, or secured with a key; not a network that
    // needs an enterprise login or WEP.
    Q_INVOKABLE bool canConnect(const QString &ssid) const;
    Q_INVOKABLE void setEnabled(bool enabled);
    // Asks the Wi-Fi devices to look for networks again, as the list opens.
    Q_INVOKABLE void scan();
    // Connects to a network in range: a known one as NetworkManager has it, another with
    // `password` when it needs one.
    Q_INVOKABLE void connectTo(const QString &ssid, const QString &password = {});
    // Disconnects from the Wi-Fi network, and NetworkManager does not connect again by itself.
    Q_INVOKABLE void disconnectNetwork();
  Q_SIGNALS:
    void changed();
    void failed(const QString &message);
    void passwordRejected(const QString &ssid);

  protected:
    // What NetworkManager's backend carries out; without one (the preview's, or a build without
    // Qt's D-Bus module, which finds no NetworkManager) nothing is.
    virtual void sendEnabled(bool /*enabled*/) {}
    virtual void sendScan() {}
    // `security` is the network's ("open", "wpa-psk", ...), for one NetworkManager has no
    // connection for; `password` is "" when it needs none.
    virtual void sendConnect(const QString & /*ssid*/, const QString & /*security*/,
                             const QString & /*password*/) {}
    virtual void sendDisconnect() {}

  private:
    State state_;
    // The network asked for, until NetworkManager connects to it or fails.
    QString requested_;
    WifiNetworks networks_;
    // The strongest access point of each name.
    const AccessPoint *strongest(const QString &ssid) const;
    // Lists the networks again, from the state and the network asked for.
    void list();
};

// The NetworkManager backend on the system bus, or one that never finds NetworkManager when
// paw was built without Qt's D-Bus module.
std::unique_ptr<Wifi> makeWifi();
