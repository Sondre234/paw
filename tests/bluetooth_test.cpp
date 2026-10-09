// SPDX-License-Identifier: GPL-3.0-or-later
// Bluetooth's model without a bus: the paired devices and those found, in order and kept in place,
// what the controls ask of BlueZ and when they do not, and the answers to what BlueZ asks while a
// device pairs.
#include "bluetooth.hpp"
#include <QSignalSpy>
#include <QTest>

namespace {
// Records what the model asks of BlueZ.
class FakeBluetooth : public Bluetooth {
  public:
    QStringList requests;

  protected:
    void sendPowered(bool powered) override { requests << QString("powered %1").arg(powered); }
    void sendDiscovery(bool discovering) override { requests << QString("discovery %1").arg(discovering); }
    void sendConnect(const QString &path) override { requests << "connect " + path; }
    void sendDisconnect(const QString &path) override { requests << "disconnect " + path; }
    void sendPair(const QString &path) override { requests << "pair " + path; }
    void sendForget(const QString &path) override { requests << "forget " + path; }
    void sendAnswer(bool accepted, const QString &input) override {
        requests << QString("answer %1 %2").arg(accepted).arg(input).trimmed();
    }
};

BluetoothDevice device(const QString &name, bool paired, bool connected, int rssi = 0, int battery = -1) {
    BluetoothDevice device;
    device.path = "/org/bluez/hci0/" + name;
    device.name = name;
    device.icon = "audio-headphones";
    device.paired = paired;
    device.connected = connected;
    device.named = true;
    device.rssi = rssi;
    device.battery = battery;
    return device;
}
Bluetooth::State around() {
    Bluetooth::State state;
    state.available = state.powered = true;
    auto unnamed = device("Unnamed", false, false, -30);
    unnamed.named = false;
    state.devices = {device("Speaker", true, false), device("Phone", false, false, -60),
                     device("Keyboard", true, true), device("Buds", true, true, 0, 80),
                     device("Watch", false, false, -40), unnamed, device("Far", false, false)};
    return state;
}
QStringList names(const BluetoothDevices *devices) {
    QStringList list;
    for (const auto &device : devices->all())
        list << device.name;
    return list;
}
QString path(const QString &name) { return "/org/bluez/hci0/" + name; }
} // namespace

class BluetoothTest : public QObject {
    Q_OBJECT
  private Q_SLOTS:
    void absent() {
        FakeBluetooth bluetooth;
        QVERIFY(!bluetooth.available());
        bluetooth.setPowered(true);
        bluetooth.lookFor(true);
        bluetooth.connectDevice(path("Speaker"));
        QVERIFY(bluetooth.requests.isEmpty());
        QVERIFY(!bluetooth.looking());
    }
    // Paired ones connected first, then by name; found ones with a name, while looking, by signal.
    void lists() {
        FakeBluetooth bluetooth;
        bluetooth.update(around());
        QCOMPARE(names(bluetooth.paired()), (QStringList{"Buds", "Keyboard", "Speaker"}));
        QCOMPARE(bluetooth.found()->count(), 0);
        QCOMPARE(bluetooth.connectedName(), QString("Buds"));
        QCOMPARE(bluetooth.connectedCount(), 2);
        QCOMPARE(bluetooth.paired()->data(bluetooth.paired()->index(0), BluetoothDevices::BatteryRole).toInt(), 80);
        bluetooth.lookFor(true);
        QVERIFY(bluetooth.looking());
        QCOMPARE(names(bluetooth.found()), (QStringList{"Watch", "Phone", "Far"}));
        QCOMPARE(bluetooth.requests, QStringList{"discovery 1"});
        // Kept in place: a change of charge changes a row, a connection moves it.
        QSignalSpy inserted(bluetooth.paired(), &QAbstractItemModel::rowsInserted);
        QSignalSpy removed(bluetooth.paired(), &QAbstractItemModel::rowsRemoved);
        QSignalSpy moved(bluetooth.paired(), &QAbstractItemModel::rowsMoved);
        auto state = around();
        state.devices[3].battery = 75;
        state.devices[0].connected = true;
        bluetooth.update(state);
        QCOMPARE(names(bluetooth.paired()), (QStringList{"Buds", "Keyboard", "Speaker"}));
        state.devices[2].connected = false;
        bluetooth.update(state);
        QCOMPARE(names(bluetooth.paired()), (QStringList{"Buds", "Speaker", "Keyboard"}));
        QCOMPARE(inserted.count() + removed.count(), 0);
        QCOMPARE(moved.count(), 1);
        bluetooth.lookFor(false);
        QCOMPARE(bluetooth.found()->count(), 0);
        QCOMPARE(bluetooth.requests.last(), QString("discovery 0"));
        // Nothing found while off.
        bluetooth.lookFor(true);
        state.powered = false;
        bluetooth.update(state);
        QCOMPARE(bluetooth.found()->count(), 0);
    }
    // Each asked of BlueZ once, busy until it is done, and only what the device allows.
    void controls() {
        FakeBluetooth bluetooth;
        QSignalSpy failed(&bluetooth, &Bluetooth::failed);
        bluetooth.update(around());
        bluetooth.connectDevice(path("Speaker"));
        bluetooth.connectDevice(path("Speaker"));
        bluetooth.connectDevice(path("Buds"));  // connected already
        bluetooth.connectDevice(path("Phone")); // not paired
        QCOMPARE(bluetooth.requests, QStringList{"connect " + path("Speaker")});
        QVERIFY(bluetooth.paired()->all()[2].busy);
        bluetooth.finished(path("Speaker"), "Could not connect to Speaker: it is out of range");
        QVERIFY(!bluetooth.paired()->all()[2].busy);
        QCOMPARE(failed.count(), 1);
        bluetooth.finished(path("Speaker"), {});
        QCOMPARE(failed.count(), 1);
        bluetooth.requests.clear();
        bluetooth.disconnectDevice(path("Buds"));
        bluetooth.disconnectDevice(path("Speaker"));
        bluetooth.pair(path("Phone"));
        bluetooth.pair(path("Buds"));
        bluetooth.forget(path("Keyboard"));
        bluetooth.forget(path("Phone"));
        QCOMPARE(bluetooth.requests, (QStringList{"disconnect " + path("Buds"), "pair " + path("Phone"),
                                                  "forget " + path("Keyboard")}));
        // Off: the radio only, at once.
        bluetooth.requests.clear();
        bluetooth.setPowered(false);
        QVERIFY(!bluetooth.powered());
        bluetooth.setPowered(false);
        bluetooth.connectDevice(path("Keyboard"));
        bluetooth.pair(path("Watch"));
        QCOMPARE(bluetooth.requests, QStringList{"powered 0"});
        // A device gone is busy no more.
        auto state = around();
        state.devices.erase(state.devices.begin() + 1);
        bluetooth.update(state);
        bluetooth.update(around());
        bluetooth.pair(path("Phone"));
        QCOMPARE(bluetooth.requests.last(), "pair " + path("Phone"));
    }
    // What BlueZ asks: answered yes with what was typed, as far as it is a PIN or a number, or no.
    void requests() {
        FakeBluetooth bluetooth;
        bluetooth.update(around());
        bluetooth.accept();
        bluetooth.reject();
        QVERIFY(bluetooth.requests.isEmpty());
        bluetooth.ask({"confirm", path("Phone"), "Phone", "123456"});
        QCOMPARE(bluetooth.request(), QString("confirm"));
        QCOMPARE(bluetooth.requestName(), QString("Phone"));
        QCOMPARE(bluetooth.requestCode(), QString("123456"));
        bluetooth.accept();
        QCOMPARE(bluetooth.request(), QString());
        bluetooth.ask({"pin", path("Phone"), "Phone", {}});
        bluetooth.accept();
        QCOMPARE(bluetooth.request(), QString("pin"));
        bluetooth.accept("0000");
        bluetooth.ask({"passkey", path("Phone"), "Phone", {}});
        bluetooth.accept("12ab");
        bluetooth.accept("1234567");
        QCOMPARE(bluetooth.request(), QString("passkey"));
        bluetooth.accept("012345");
        bluetooth.ask({"authorize", path("Watch"), "Watch", {}});
        bluetooth.reject();
        QCOMPARE(bluetooth.requests, (QStringList{"answer 1", "answer 1 0000", "answer 1 012345", "answer 0"}));
        // Taken back by BlueZ: nothing to answer.
        bluetooth.ask({"display", path("Watch"), "Watch", "654321"});
        bluetooth.cancelRequest();
        QCOMPARE(bluetooth.request(), QString());
        QCOMPARE(bluetooth.requests.size(), 4);
        // Gone with BlueZ.
        bluetooth.ask({"confirm", path("Phone"), "Phone", "111111"});
        bluetooth.lookFor(true);
        bluetooth.update({});
        QCOMPARE(bluetooth.request(), QString());
        QVERIFY(!bluetooth.looking() && bluetooth.paired()->count() == 0);
    }
};
QTEST_MAIN(BluetoothTest)
#include "bluetooth_test.moc"
