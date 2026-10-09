// SPDX-License-Identifier: GPL-3.0-or-later
// Wi-Fi's model without a bus: how a network is secured from NetworkManager's flags, the list of
// networks (one per name, in order), when a password is asked for, and what the controls ask of
// NetworkManager.
#include "wifi.hpp"
#include <QSignalSpy>
#include <QTest>

namespace {
// Records what the model asks of NetworkManager.
class FakeWifi : public Wifi {
  public:
    QStringList requests;

  protected:
    void sendEnabled(bool enabled) override { requests << QString("enabled %1").arg(enabled); }
    void sendScan() override { requests << "scan"; }
    void sendConnect(const QString &ssid, const QString &security, const QString &password) override {
        requests << QString("connect %1 %2 %3").arg(ssid, security, password).trimmed();
    }
    void sendDisconnect() override { requests << "disconnect"; }
};

Wifi::State inRange() {
    Wifi::State state;
    state.available = state.hasWifi = state.enabled = true;
    state.accessPoints = {{"Cafe", 40, "open"},     {"Home", 30, "wpa-psk"}, {"Office", 60, "sae"},
                          {"Home", 80, "wpa-psk"},  {"", 90, "wpa-psk"},     {"Corp", 70, "enterprise"},
                          {"Old", 20, "wep"},       {"Lobby", 40, "owe"}};
    state.known = {"Home"};
    return state;
}
QStringList names(const Wifi &wifi) {
    QStringList list;
    for (const auto &network : wifi.networks()->all())
        list << network.ssid;
    return list;
}
// A row of the list, by its roles' names.
QVariantMap network(const Wifi &wifi, const QString &ssid) {
    const auto *model = wifi.networks();
    for (int row = 0; row < model->count(); ++row) {
        const auto index = model->index(row);
        if (model->data(index, WifiNetworks::SsidRole) != ssid)
            continue;
        QVariantMap roles;
        for (auto [role, name] : model->roleNames().asKeyValueRange())
            roles[QString::fromUtf8(name)] = model->data(index, role);
        return roles;
    }
    return {};
}
} // namespace

class WifiTest : public QObject {
    Q_OBJECT
  private Q_SLOTS:
    // From NetworkManager's 802.11 flags: key management decides, then privacy alone (WEP).
    void security() {
        QCOMPARE(Wifi::security(0, 0, 0), QString("open"));
        QCOMPARE(Wifi::security(0x1, 0, 0), QString("wep"));
        QCOMPARE(Wifi::security(0x1, 0x100 | 0x8, 0), QString("wpa-psk"));      // WPA Personal
        QCOMPARE(Wifi::security(0x1, 0, 0x100 | 0x8), QString("wpa-psk"));      // WPA2 Personal
        QCOMPARE(Wifi::security(0x1, 0, 0x100 | 0x400), QString("wpa-psk"));    // WPA2/WPA3
        QCOMPARE(Wifi::security(0x1, 0, 0x400), QString("sae"));                // WPA3 Personal
        QCOMPARE(Wifi::security(0x1, 0, 0x200), QString("enterprise"));
        QCOMPARE(Wifi::security(0x1, 0, 0x2000), QString("enterprise"));
        QCOMPARE(Wifi::security(0, 0, 0x800), QString("owe"));                  // Enhanced Open
    }
    void absent() {
        FakeWifi wifi;
        QVERIFY(!wifi.available() && !wifi.hasWifi());
        QVERIFY(wifi.networks()->count() == 0);
        wifi.setEnabled(true);
        wifi.scan();
        wifi.connectTo("Home", "secret");
        wifi.disconnectNetwork();
        QVERIFY(wifi.requests.isEmpty());
        // A state without NetworkManager is none at all.
        auto gone = inRange();
        gone.available = false;
        wifi.update(gone);
        QVERIFY(!wifi.hasWifi() && wifi.networks()->count() == 0);
    }
    // One entry for each name at its strongest, hidden networks left out, by signal.
    void list() {
        FakeWifi wifi;
        QSignalSpy changed(&wifi, &Wifi::changed);
        wifi.update(inRange());
        QCOMPARE(changed.count(), 1);
        wifi.update(inRange());
        QCOMPARE(changed.count(), 1);
        QCOMPARE(names(wifi), (QStringList{"Home", "Corp", "Office", "Cafe", "Lobby", "Old"}));
        const auto home = network(wifi, "Home");
        QCOMPARE(home.value("strength").toInt(), 80);
        QVERIFY(home.value("secured").toBool() && home.value("known").toBool() && !home.value("active").toBool());
        QVERIFY(!network(wifi, "Cafe").value("secured").toBool());
        QVERIFY(!network(wifi, "Lobby").value("secured").toBool());
        QVERIFY(network(wifi, "Old").value("secured").toBool());
        // Connected first, wherever its signal is.
        auto connected = inRange();
        connected.ssid = "Cafe";
        connected.strength = 40;
        connected.primaryType = "wifi";
        connected.primaryName = "Cafe";
        wifi.update(connected);
        QCOMPARE(names(wifi).first(), QString("Cafe"));
        QVERIFY(network(wifi, "Cafe").value("active").toBool());
        QCOMPARE(wifi.ssid(), QString("Cafe"));
        QCOMPARE(wifi.strength(), 40);
        // Still listed while a scan has lost sight of it.
        connected.accessPoints.clear();
        wifi.update(connected);
        QCOMPARE(names(wifi), QStringList{"Cafe"});
        // Nothing while the radio is off.
        connected.enabled = false;
        wifi.update(connected);
        QVERIFY(wifi.networks()->count() == 0);
    }
    // Rows change in place and move as the order changes, rather than being made again, so that a
    // row being typed in stays.
    void inPlace() {
        FakeWifi wifi;
        wifi.update(inRange());
        auto *model = wifi.networks();
        QSignalSpy inserted(model, &QAbstractItemModel::rowsInserted);
        QSignalSpy removed(model, &QAbstractItemModel::rowsRemoved);
        QSignalSpy moved(model, &QAbstractItemModel::rowsMoved);
        QSignalSpy changed(model, &QAbstractItemModel::dataChanged);
        auto stronger = inRange();
        stronger.accessPoints.push_back({"Cafe", 95, "open"});
        wifi.update(stronger);
        QCOMPARE(names(wifi), (QStringList{"Cafe", "Home", "Corp", "Office", "Lobby", "Old"}));
        QCOMPARE(inserted.count() + removed.count(), 0);
        QCOMPARE(moved.count(), 1);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(network(wifi, "Cafe").value("strength").toInt(), 95);
        auto other = stronger;
        std::erase_if(other.accessPoints, [](const Wifi::AccessPoint &point) { return point.ssid == "Old"; });
        other.accessPoints.push_back({"Park", 10, "open"});
        wifi.update(other);
        QCOMPARE(names(wifi), (QStringList{"Cafe", "Home", "Corp", "Office", "Lobby", "Park"}));
        QCOMPARE(removed.count(), 1);
        QCOMPARE(inserted.count(), 1);
        QCOMPARE(model->count(), 6);
    }
    // A password only for a network secured with a key that NetworkManager does not know.
    void passwords() {
        FakeWifi wifi;
        wifi.update(inRange());
        QVERIFY(!wifi.needsPassword("Home"));
        QVERIFY(wifi.needsPassword("Office"));
        QVERIFY(!wifi.needsPassword("Cafe"));
        QVERIFY(!wifi.needsPassword("Lobby"));
        QVERIFY(!wifi.needsPassword("Nowhere"));
        for (const char *ssid : {"Home", "Office", "Cafe", "Lobby"})
            QVERIFY2(wifi.canConnect(ssid), ssid);
        QVERIFY(!wifi.canConnect("Corp"));
        QVERIFY(!wifi.canConnect("Old"));
        QVERIFY(!wifi.canConnect("Nowhere"));
    }
    // Connecting shows at once, until NetworkManager connects or fails; a refused password is
    // asked for again.
    void connecting() {
        FakeWifi wifi;
        QSignalSpy failed(&wifi, &Wifi::failed);
        QSignalSpy rejected(&wifi, &Wifi::passwordRejected);
        wifi.update(inRange());
        wifi.connectTo("Office");
        wifi.connectTo("Corp");
        wifi.connectTo("Old", "secret");
        QVERIFY(wifi.requests.isEmpty());
        wifi.connectTo("Office", "secret");
        QCOMPARE(wifi.requests, QStringList{"connect Office sae secret"});
        QCOMPARE(wifi.connecting(), QString("Office"));
        QCOMPARE(names(wifi).first(), QString("Office"));
        QVERIFY(network(wifi, "Office").value("connecting").toBool());
        wifi.connectionFailed("Office", "Could not connect to Office: wrong password", true);
        QCOMPARE(wifi.connecting(), QString());
        QCOMPARE(failed.count(), 1);
        QCOMPARE(rejected.count(), 1);
        QCOMPARE(rejected.at(0).at(0).toString(), QString("Office"));
        wifi.requests.clear();
        wifi.connectTo("Home");
        wifi.connectTo("Cafe");
        QCOMPARE(wifi.requests, (QStringList{"connect Home wpa-psk", "connect Cafe open"}));
        // NetworkManager's own account takes over, then the connection.
        auto activating = inRange();
        activating.activating = "Cafe";
        wifi.update(activating);
        QCOMPARE(wifi.connecting(), QString("Cafe"));
        auto connected = inRange();
        connected.ssid = "Cafe";
        wifi.update(connected);
        QCOMPARE(wifi.connecting(), QString());
        QVERIFY(!network(wifi, "Cafe").value("connecting").toBool());
        // Already there: nothing to ask.
        wifi.requests.clear();
        wifi.connectTo("Cafe");
        QVERIFY(wifi.requests.isEmpty());
    }
    void radioAndDisconnect() {
        FakeWifi wifi;
        auto state = inRange();
        wifi.update(state);
        wifi.disconnectNetwork(); // connected to nothing
        wifi.setEnabled(true);    // on already
        wifi.scan();
        wifi.setEnabled(false);
        QVERIFY(!wifi.enabled());
        wifi.scan();
        wifi.connectTo("Cafe");
        QCOMPARE(wifi.requests, (QStringList{"scan", "enabled 0"}));
        wifi.requests.clear();
        state.ssid = "Home";
        wifi.update(state);
        wifi.disconnectNetwork();
        QCOMPARE(wifi.requests, QStringList{"disconnect"});
    }
};
QTEST_MAIN(WifiTest)
#include "wifi_test.moc"
