// SPDX-License-Identifier: GPL-3.0-or-later
#include "wifi.hpp"
#include <algorithm>

namespace {
// NetworkManager's NM80211ApFlags and NM80211ApSecurityFlags, as far as they decide how a network
// is joined.
constexpr uint privacy = 0x1;
constexpr uint keyPsk = 0x100, key8021x = 0x200, keySae = 0x400, keyOwe = 0x800, keyOweTransition = 0x1000,
               keySuiteB = 0x2000;

bool sameState(const Wifi::State &a, const Wifi::State &b) {
    auto sameAccessPoints = [](const auto &x, const auto &y) {
        return std::equal(x.begin(), x.end(), y.begin(), y.end(), [](const auto &p, const auto &q) {
            return p.ssid == q.ssid && p.strength == q.strength && p.security == q.security;
        });
    };
    return a.available == b.available && a.hasWifi == b.hasWifi && a.enabled == b.enabled &&
           a.hardwareEnabled == b.hardwareEnabled && sameAccessPoints(a.accessPoints, b.accessPoints) &&
           a.known == b.known && a.ssid == b.ssid && a.strength == b.strength &&
           a.activating == b.activating && a.primaryType == b.primaryType && a.primaryName == b.primaryName;
}
bool withKey(const QString &security) { return security == "wpa-psk" || security == "sae"; }
bool withoutKey(const QString &security) { return security == "open" || security == "owe"; }
} // namespace

QVariant WifiNetworks::data(const QModelIndex &index, int role) const {
    const auto *row = at(index);
    if (!row)
        return {};
    const auto &network = *row;
    switch (role) {
    case Qt::DisplayRole:
    case SsidRole:
        return network.ssid;
    case StrengthRole:
        return network.strength;
    case SecurityRole:
        return network.security;
    case SecuredRole:
        return network.secured;
    case KnownRole:
        return network.known;
    case ActiveRole:
        return network.active;
    case ConnectingRole:
        return network.connecting;
    }
    return {};
}
QHash<int, QByteArray> WifiNetworks::roleNames() const {
    return {{SsidRole, "ssid"},   {StrengthRole, "strength"}, {SecurityRole, "security"},
            {SecuredRole, "secured"}, {KnownRole, "known"}, {ActiveRole, "active"},
            {ConnectingRole, "connecting"}};
}

QString Wifi::security(uint flags, uint wpaFlags, uint rsnFlags) {
    const uint keys = wpaFlags | rsnFlags;
    if (keys & (key8021x | keySuiteB))
        return QStringLiteral("enterprise");
    // WPA3 in transition takes a WPA2 key too.
    if (keys & keyPsk)
        return QStringLiteral("wpa-psk");
    if (keys & keySae)
        return QStringLiteral("sae");
    if (keys & (keyOwe | keyOweTransition))
        return QStringLiteral("owe");
    if (flags & privacy)
        return QStringLiteral("wep");
    return QStringLiteral("open");
}
const Wifi::AccessPoint *Wifi::strongest(const QString &ssid) const {
    const AccessPoint *best = nullptr;
    for (const auto &point : state_.accessPoints)
        if (point.ssid == ssid && (!best || point.strength > best->strength))
            best = &point;
    return best;
}
QString Wifi::connecting() const {
    return !state_.activating.isEmpty() ? state_.activating : requested_;
}
void Wifi::list() {
    if (!state_.available || !state_.enabled) {
        networks_.update({});
        return;
    }
    std::vector<AccessPoint> shown;
    for (const auto &point : state_.accessPoints) {
        // A hidden network has no name to show.
        if (point.ssid.isEmpty())
            continue;
        auto it = std::find_if(shown.begin(), shown.end(),
                               [&point](const AccessPoint &other) { return other.ssid == point.ssid; });
        if (it == shown.end())
            shown.push_back(point);
        else if (point.strength > it->strength)
            *it = point;
    }
    // The one connected stays listed while a scan has lost sight of it.
    if (!state_.ssid.isEmpty() &&
        std::none_of(shown.begin(), shown.end(), [this](const AccessPoint &point) { return point.ssid == state_.ssid; }))
        shown.push_back({state_.ssid, state_.strength, QStringLiteral("wpa-psk")});
    const QString linking = connecting();
    auto rank = [&](const AccessPoint &point) {
        return point.ssid == state_.ssid ? 2 : point.ssid == linking ? 1 : 0;
    };
    std::stable_sort(shown.begin(), shown.end(), [&rank](const AccessPoint &a, const AccessPoint &b) {
        if (rank(a) != rank(b))
            return rank(a) > rank(b);
        if (a.strength != b.strength)
            return a.strength > b.strength;
        return a.ssid.localeAwareCompare(b.ssid) < 0;
    });
    std::vector<WifiNetworks::Network> rows;
    for (const auto &point : shown)
        rows.push_back({point.ssid, point.strength, point.security, !withoutKey(point.security),
                        state_.known.contains(point.ssid), point.ssid == state_.ssid,
                        point.ssid == linking && point.ssid != state_.ssid});
    networks_.update(rows);
}
void Wifi::update(State state) {
    if (!state.available)
        state = State{};
    // Connected to the network asked for, or connecting to it by NetworkManager's account.
    const QString requested = requested_;
    if (!requested_.isEmpty() && (state.ssid == requested_ || state.activating == requested_))
        requested_.clear();
    if (sameState(state, state_) && requested == requested_)
        return;
    state_ = std::move(state);
    list();
    Q_EMIT changed();
}
void Wifi::connectionFailed(const QString &ssid, const QString &message, bool password) {
    if (requested_ == ssid) {
        requested_.clear();
        list();
        Q_EMIT changed();
    }
    Q_EMIT failed(message);
    if (password)
        Q_EMIT passwordRejected(ssid);
}
bool Wifi::needsPassword(const QString &ssid) const {
    const auto *point = strongest(ssid);
    return point && withKey(point->security) && !state_.known.contains(ssid) && ssid != state_.ssid;
}
bool Wifi::canConnect(const QString &ssid) const {
    if (state_.known.contains(ssid))
        return true;
    const auto *point = strongest(ssid);
    return point && (withKey(point->security) || withoutKey(point->security));
}
void Wifi::setEnabled(bool enabled) {
    if (!state_.available || !state_.hasWifi || enabled == state_.enabled)
        return;
    // Shown at once; NetworkManager confirms it.
    state_.enabled = enabled;
    list();
    Q_EMIT changed();
    sendEnabled(enabled);
}
void Wifi::scan() {
    if (state_.available && state_.hasWifi && state_.enabled)
        sendScan();
}
void Wifi::connectTo(const QString &ssid, const QString &password) {
    if (!state_.available || !state_.enabled || ssid.isEmpty() || ssid == state_.ssid || !canConnect(ssid) ||
        (needsPassword(ssid) && password.isEmpty()))
        return;
    requested_ = ssid;
    list();
    Q_EMIT changed();
    const auto *point = strongest(ssid);
    sendConnect(ssid, point ? point->security : QStringLiteral("open"), password);
}
void Wifi::disconnectNetwork() {
    if (state_.available && !state_.ssid.isEmpty())
        sendDisconnect();
}

#if !PAW_NETWORKMANAGER
// Without Qt's D-Bus module there is no NetworkManager to find.
std::unique_ptr<Wifi> makeWifi() { return std::make_unique<Wifi>(); }
#endif
