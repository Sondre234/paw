// SPDX-License-Identifier: GPL-3.0-or-later
#include "bluetooth.hpp"
#include <algorithm>

QVariant BluetoothDevices::data(const QModelIndex &index, int role) const {
    const auto *device = at(index);
    if (!device)
        return {};
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return device->name;
    case PathRole:
        return device->path;
    case IconRole:
        return device->icon;
    case PairedRole:
        return device->paired;
    case ConnectedRole:
        return device->connected;
    case BusyRole:
        return device->busy;
    case BatteryRole:
        return device->battery;
    }
    return {};
}
QHash<int, QByteArray> BluetoothDevices::roleNames() const {
    return {{PathRole, "path"},         {NameRole, "name"}, {IconRole, "icon"},       {PairedRole, "paired"},
            {ConnectedRole, "connected"}, {BusyRole, "busy"}, {BatteryRole, "battery"}};
}

Bluetooth::Bluetooth(QObject *parent) : QObject(parent), paired_(this), found_(this) {}

const BluetoothDevice *Bluetooth::device(const QString &path) const {
    auto it = std::find_if(state_.devices.begin(), state_.devices.end(),
                           [&path](const BluetoothDevice &device) { return device.path == path; });
    return it == state_.devices.end() ? nullptr : &*it;
}
QString Bluetooth::connectedName() const {
    for (const auto &device : paired_.all())
        if (device.connected)
            return device.name;
    return {};
}
int Bluetooth::connectedCount() const {
    return int(std::count_if(paired_.all().begin(), paired_.all().end(),
                             [](const BluetoothDevice &device) { return device.connected; }));
}
// The paired devices, connected first, then by name; the others with a name while looking, the
// strongest first (an unknown signal last).
void Bluetooth::list() {
    std::vector<BluetoothDevice> paired, found;
    for (auto device : state_.devices) {
        device.busy = busy_.contains(device.path);
        if (device.paired)
            paired.push_back(device);
        else if (looking_ && state_.powered && device.named)
            found.push_back(device);
    }
    auto byName = [](const BluetoothDevice &a, const BluetoothDevice &b) {
        return a.name.localeAwareCompare(b.name) < 0;
    };
    std::stable_sort(paired.begin(), paired.end(), [&byName](const BluetoothDevice &a, const BluetoothDevice &b) {
        return a.connected != b.connected ? a.connected : byName(a, b);
    });
    std::stable_sort(found.begin(), found.end(), [&byName](const BluetoothDevice &a, const BluetoothDevice &b) {
        const int x = a.rssi ? a.rssi : -1000, y = b.rssi ? b.rssi : -1000;
        return x != y ? x > y : byName(a, b);
    });
    paired_.update(paired);
    found_.update(found);
}
void Bluetooth::update(State state) {
    if (!state.available) {
        state = State{};
        looking_ = false;
        request_ = {};
    }
    state_ = std::move(state);
    // A device gone is busy no more.
    for (auto it = busy_.begin(); it != busy_.end();)
        it = device(*it) ? std::next(it) : busy_.erase(it);
    list();
    Q_EMIT changed();
}
void Bluetooth::ask(Request request) {
    request_ = std::move(request);
    Q_EMIT changed();
}
void Bluetooth::cancelRequest() {
    if (request_.kind.isEmpty())
        return;
    request_ = {};
    Q_EMIT changed();
}
void Bluetooth::finished(const QString &path, const QString &error) {
    busy_.remove(path);
    list();
    Q_EMIT changed();
    if (!error.isEmpty())
        Q_EMIT failed(error);
}
void Bluetooth::setPowered(bool powered) {
    if (!state_.available || powered == state_.powered)
        return;
    // Shown at once; BlueZ confirms it.
    state_.powered = powered;
    list();
    Q_EMIT changed();
    sendPowered(powered);
}
void Bluetooth::lookFor(bool looking) {
    if (looking == looking_ || (looking && !state_.available))
        return;
    looking_ = looking;
    list();
    Q_EMIT changed();
    if (state_.available)
        sendDiscovery(looking);
}
void Bluetooth::connectDevice(const QString &path) {
    const auto *found = device(path);
    if (!found || !found->paired || found->connected || busy_.contains(path) || !state_.powered)
        return;
    busy_.insert(path);
    list();
    Q_EMIT changed();
    sendConnect(path);
}
void Bluetooth::disconnectDevice(const QString &path) {
    const auto *found = device(path);
    if (!found || !found->connected || busy_.contains(path))
        return;
    busy_.insert(path);
    list();
    Q_EMIT changed();
    sendDisconnect(path);
}
void Bluetooth::pair(const QString &path) {
    const auto *found = device(path);
    if (!found || found->paired || busy_.contains(path) || !state_.powered)
        return;
    busy_.insert(path);
    list();
    Q_EMIT changed();
    sendPair(path);
}
void Bluetooth::forget(const QString &path) {
    const auto *found = device(path);
    if (found && found->paired)
        sendForget(path);
}
void Bluetooth::accept(const QString &input) {
    const QString kind = request_.kind;
    if (kind.isEmpty() || (kind == "pin" && input.isEmpty()))
        return;
    if (kind == "passkey") {
        bool ok = false;
        if (input.toUInt(&ok) > 999999 || !ok)
            return;
    }
    request_ = {};
    Q_EMIT changed();
    sendAnswer(true, input);
}
void Bluetooth::reject() {
    if (request_.kind.isEmpty())
        return;
    request_ = {};
    Q_EMIT changed();
    sendAnswer(false, {});
}

#if !PAW_BLUETOOTH
// Without Qt's D-Bus module there is no BlueZ to find.
std::unique_ptr<Bluetooth> makeBluetooth() { return std::make_unique<Bluetooth>(); }
#endif
