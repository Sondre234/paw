// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "row_model.hpp"
#include <QObject>
#include <QSet>
#include <memory>
#include <vector>

// A Bluetooth device, one row of BluetoothDevices.
struct BluetoothDevice {
    QString path; // BlueZ's object path, which names it
    QString name;
    // The kind of device, as a freedesktop icon name BlueZ gives: audio-headset, input-mouse, ...
    QString icon;
    bool paired = false, connected = false;
    // Whether BlueZ knows its name rather than only its address; one that does not is not offered
    // to pair with.
    bool named = false;
    // Connecting, disconnecting or pairing, as the shell asked.
    bool busy = false;
    // Its battery's charge, -1 where BlueZ does not say; its signal (dBm), 0 where unknown.
    int battery = -1;
    int rssi = 0;
    QString key() const { return path; }
    bool operator==(const BluetoothDevice &) const = default;
};

// Bluetooth devices, one row each, kept in place (RowModel), so that a row stays as devices
// connect and the list reorders.
class BluetoothDevices : public RowModel<BluetoothDevice> {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
  public:
    enum Role { PathRole = Qt::UserRole + 1, NameRole, IconRole, PairedRole, ConnectedRole, BusyRole, BatteryRole };
    using RowModel::RowModel;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void update(const std::vector<BluetoothDevice> &devices) {
        if (replace(devices))
            Q_EMIT countChanged();
    }
  Q_SIGNALS:
    void countChanged();
};

// Bluetooth as Quick Settings shows it, from BlueZ: the adapter turned on or off, the paired
// devices to connect and disconnect, the devices in range to pair with while the shell looks for
// them, and what BlueZ asks the user as one pairs (through the shell's agent). A backend delivers
// BlueZ's state through update() and its questions through ask(), and carries out the send*()
// requests; `available` is false while BlueZ does not run or has no adapter.
class Bluetooth : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool powered READ powered NOTIFY changed)
    // Whether the shell looks for devices to pair with (lookFor), and whether the adapter does.
    Q_PROPERTY(bool looking READ looking NOTIFY changed)
    Q_PROPERTY(bool discovering READ discovering NOTIFY changed)
    // The paired devices, connected first, then by name; and the others in range with a name,
    // the strongest first, while looking.
    Q_PROPERTY(BluetoothDevices *paired READ paired CONSTANT)
    Q_PROPERTY(BluetoothDevices *found READ found CONSTANT)
    // The first connected device's name and how many are connected.
    Q_PROPERTY(QString connectedName READ connectedName NOTIFY changed)
    Q_PROPERTY(int connectedCount READ connectedCount NOTIFY changed)
    // What BlueZ asks as a device pairs: "confirm" (does the device show `requestCode`?),
    // "authorize" (may it pair?), "pin" (the PIN to type), "passkey" (the number to type),
    // "display" (type `requestCode` on the device), or "" for nothing; and the device's name.
    Q_PROPERTY(QString request READ request NOTIFY changed)
    Q_PROPERTY(QString requestName READ requestName NOTIFY changed)
    Q_PROPERTY(QString requestCode READ requestCode NOTIFY changed)
  public:
    struct State {
        bool available = false, powered = false, discovering = false;
        std::vector<BluetoothDevice> devices;
    };
    struct Request {
        QString kind, path, name, code;
    };
    explicit Bluetooth(QObject *parent = nullptr);
    bool available() const { return state_.available; }
    bool powered() const { return state_.powered; }
    bool looking() const { return looking_; }
    bool discovering() const { return state_.discovering; }
    BluetoothDevices *paired() { return &paired_; }
    BluetoothDevices *found() { return &found_; }
    const BluetoothDevices *paired() const { return &paired_; }
    const BluetoothDevices *found() const { return &found_; }
    QString connectedName() const;
    int connectedCount() const;
    QString request() const { return request_.kind; }
    QString requestName() const { return request_.name; }
    QString requestCode() const { return request_.code; }
    void update(State state);
    // BlueZ asks something of the user, or takes it back.
    void ask(Request request);
    void cancelRequest();
    // What the shell asked of a device is done: `error` says what went wrong, "" for nothing
    // worth saying.
    void finished(const QString &path, const QString &error);
    Q_INVOKABLE void setPowered(bool powered);
    // Looks for devices to pair with, as the list shows them, or stops.
    Q_INVOKABLE void lookFor(bool looking);
    Q_INVOKABLE void connectDevice(const QString &path);
    Q_INVOKABLE void disconnectDevice(const QString &path);
    // Pairs with a device found, then trusts it and connects to it.
    Q_INVOKABLE void pair(const QString &path);
    // Removes a paired device from BlueZ, which forgets the pairing.
    Q_INVOKABLE void forget(const QString &path);
    // Answers what BlueZ asked: yes, with the PIN or the number typed for "pin" and "passkey".
    Q_INVOKABLE void accept(const QString &input = {});
    Q_INVOKABLE void reject();
  Q_SIGNALS:
    void changed();
    void failed(const QString &message);

  protected:
    virtual void sendPowered(bool powered) = 0;
    virtual void sendDiscovery(bool discovering) = 0;
    virtual void sendConnect(const QString &path) = 0;
    virtual void sendDisconnect(const QString &path) = 0;
    virtual void sendPair(const QString &path) = 0;
    virtual void sendForget(const QString &path) = 0;
    // Answers the request: yes with `input`, or no.
    virtual void sendAnswer(bool accepted, const QString &input) = 0;

  private:
    State state_;
    bool looking_ = false;
    QSet<QString> busy_;
    Request request_;
    BluetoothDevices paired_, found_;
    const BluetoothDevice *device(const QString &path) const;
    void list();
};

// The BlueZ backend on the system bus, or one that never finds BlueZ when paw was built
// without Qt's D-Bus module.
std::unique_ptr<Bluetooth> makeBluetooth();
