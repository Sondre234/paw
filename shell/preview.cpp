// SPDX-License-Identifier: GPL-3.0-or-later
#include "preview.hpp"
#include "audio.hpp"
#include "backlight.hpp"
#include "controller.hpp"
#include "image_provider.hpp"
#include "system_status.hpp"
#include "view.hpp"
#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QLinearGradient>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <unistd.h>

namespace {
// A tray icon: a rounded square in `color` with a letter on it.
QImage trayIcon(const QColor &color, const QString &letter) {
    QImage image(44, 44, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(image.rect().adjusted(2, 2, -2, -2), 10, 10);
    QFont font = painter.font();
    font.setPixelSize(26);
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(Qt::white);
    painter.drawText(image.rect(), Qt::AlignCenter, letter);
    return image;
}

TrayMenuEntry menuEntry(QVariantMap properties, std::vector<int> children = {}) {
    TrayMenuEntry entry;
    entry.properties = std::move(properties);
    entry.children = std::move(children);
    entry.read();
    return entry;
}

// The overview's stand-ins, as the compositor would lay them out in `area`: four windows of the
// first workspace (the first selected, the third asking for attention), and the strip of four
// workspaces above them, below the room it leaves for the search box (OVERVIEW_TOP).
QVariantList overviewWindows(const QRect &area) {
    auto window = [&area](int x, int y, int w, int h, const QString &appId, const QString &title,
                          bool urgent = false) {
        return QVariantMap{
            {"x", area.x() + x}, {"y", area.y() + y}, {"w", w},         {"h", h},
            {"appId", appId},    {"title", title},    {"workspace", 1}, {"urgent", urgent}};
    };
    return {window(130, 160, 410, 256, "firefox", "Release notes - Mozilla Firefox"),
            window(562, 160, 410, 256, "foot", "~/dev/paw"),
            window(232, 444, 300, 170, "kitty", "Build finished", true),
            window(562, 444, 300, 170, "foot", "htop")};
}
// Snap Assist's stand-ins in the right half of `area`, as the compositor would lay out two
// windows there, one above the other with the overview's gap of 24 pixels between and around
// them, the first selected.
QRect assistSlot(const QRect &area) {
    return QRect(area.x() + area.width() / 2, area.y(), area.width() - area.width() / 2,
                 area.height());
}
QVariantList assistWindows(const QRect &area) {
    const QRect slot = assistSlot(area);
    const int h = (slot.height() - 3 * 24) / 2, w = std::min(slot.width() - 48, h * 16 / 10);
    auto window = [&](int y, const QString &appId, const QString &title) {
        return QVariantMap{{"x", slot.x() + (slot.width() - w) / 2}, {"y", y}, {"w", w}, {"h", h},
                           {"appId", appId},     {"title", title}, {"workspace", 1}, {"urgent", false}};
    };
    const int top = slot.y() + (slot.height() - 2 * h - 24) / 2;
    return {window(top, "firefox", "Release notes - Mozilla Firefox"),
            window(top + h + 24, "foot", "~/dev/paw")};
}
QVariantList overviewStrip(const QRect &area) {
    const int windows[] = {4, 2, 0, 0};
    QVariantList cells;
    for (int i = 0; i < 4; ++i)
        cells.push_back(QVariantMap{{"x", area.x() + 292 + i * 132},
                                    {"y", area.y() + 48},
                                    {"w", 120},
                                    {"h", 68},
                                    {"workspace", i + 1},
                                    {"windows", windows[i]}});
    return cells;
}

// A stand-in for the picture of stand-in window `id`, as the task model's `windows` provider
// serves the real ones: the window's content without the frame, drawn as a picture of it shows
// it (lines of text as bars), scaled down to fit within 480 by 300 pixels, the most a picture of
// shell.thumbnails takes. A web page for the browser, a shell for the first terminal and a tall
// htop for the second; null for the other windows, which have no picture yet.
QImage windowPicture(int id) {
    if (id < 1 || id > 3)
        return {};
    const QSize window = id == 1 ? QSize(1440, 900) : id == 2 ? QSize(1600, 900) : QSize(700, 1000);
    QImage image(window.scaled(480, 300, Qt::KeepAspectRatio), QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.scale(image.width() / qreal(window.width()), image.height() / qreal(window.height()));
    painter.setPen(Qt::NoPen);
    // A line of words from (x, y): each a bar as wide as given, in its colour.
    auto words = [&painter](qreal x, qreal y, qreal height, std::initializer_list<std::pair<const char *, int>> line) {
        for (auto [color, width] : line) {
            painter.setBrush(QColor(color));
            painter.drawRoundedRect(QRectF(x, y, width, height), height / 3, height / 3);
            x += width + height * 0.6;
        }
    };
    if (id == 1) {
        painter.fillRect(QRect(QPoint(0, 0), window), QColor("#ffffff"));
        painter.fillRect(0, 0, window.width(), 48, QColor("#e3e4ea"));
        painter.fillRect(16, 8, 260, 40, QColor("#ffffff"));
        words(36, 22, 12, {{"#8a8d99", 150}});
        painter.fillRect(0, 48, window.width(), 52, QColor("#f7f7fa"));
        painter.setBrush(QColor("#e6e7ec"));
        painter.drawRoundedRect(QRectF(160, 58, 1120, 32), 16, 16);
        words(184, 68, 12, {{"#5b5e6b", 280}});
        painter.fillRect(0, 100, window.width(), 1, QColor("#d5d7de"));
        words(220, 170, 34, {{"#1f2330", 380}, {"#1f2330", 210}});
        words(220, 230, 14, {{"#7a7e8c", 90}, {"#7a7e8c", 140}});
        QLinearGradient gradient(220, 280, 1220, 560);
        gradient.setColorAt(0, QColor("#5b7cfa"));
        gradient.setColorAt(1, QColor("#b06ad9"));
        painter.setBrush(gradient);
        painter.drawRoundedRect(QRectF(220, 280, 1000, 280), 12, 12);
        for (int line = 0; line < 6; ++line)
            words(220, 600 + line * 40, 14,
                  {{"#a4a8b5", 160 + (line * 70) % 130}, {"#a4a8b5", 220}, {"#a4a8b5", 110 + (line * 50) % 160},
                   {"#a4a8b5", line == 5 ? 90 : 240}});
    } else if (id == 2) {
        painter.fillRect(QRect(QPoint(0, 0), window), QColor("#1e1f29"));
        // Prompts and what they printed.
        const qreal height = 18, step = 40;
        qreal y = 30;
        auto prompt = [&](std::initializer_list<std::pair<const char *, int>> command) {
            words(30, y, height, {{"#7ec699", 110}, {"#79b8ff", 190}});
            words(30 + 110 + 190 + 2 * height * 0.6, y, height, command);
            y += step;
        };
        auto output = [&](std::initializer_list<std::pair<const char *, int>> line) {
            words(30, y, height, line);
            y += step;
        };
        prompt({{"#e6e9f0", 70}, {"#e6e9f0", 160}});
        output({{"#c9ccd6", 380}, {"#c9ccd6", 160}, {"#c9ccd6", 520}});
        output({{"#c9ccd6", 240}, {"#c9ccd6", 610}, {"#c9ccd6", 130}});
        output({{"#7ec699", 90}, {"#c9ccd6", 450}, {"#c9ccd6", 210}});
        prompt({{"#e6e9f0", 90}, {"#e6e9f0", 70}, {"#e6e9f0", 230}});
        for (int line = 0; line < 9; ++line)
            output({{"#6c7086", 120}, {line == 6 ? "#f28b82" : "#c9ccd6", 340 + (line * 130) % 560},
                    {"#c9ccd6", 160 + (line * 90) % 300}});
        output({{"#7ec699", 190}, {"#c9ccd6", 420}});
        prompt({});
        painter.fillRect(QRectF(30 + 110 + 190 + 2 * height * 0.6, y - step, height * 0.7, height * 1.3),
                         QColor("#e6e9f0"));
    } else {
        painter.fillRect(QRect(QPoint(0, 0), window), QColor("#1e1f29"));
        // Meters for the processors and the memory, then the processes, one of them chosen.
        for (int meter = 0; meter < 6; ++meter) {
            const qreal y = 24 + meter * 34;
            words(24, y, 16, {{"#79b8ff", 40}});
            painter.fillRect(QRectF(84, y, 580, 16), QColor("#2b2d3a"));
            const int used = 120 + (meter * 157) % 380;
            painter.fillRect(QRectF(84, y, used, 16), QColor(meter < 4 ? "#7ec699" : "#e5c07b"));
            painter.fillRect(QRectF(84 + used, y, 40 + meter * 9, 16), QColor("#f28b82"));
        }
        painter.fillRect(QRectF(0, 250, window.width(), 30), QColor("#7ec699"));
        words(24, 257, 16, {{"#1e1f29", 50}, {"#1e1f29", 70}, {"#1e1f29", 40}, {"#1e1f29", 60}, {"#1e1f29", 140}});
        for (int row = 0; row < 19; ++row) {
            const qreal y = 292 + row * 36;
            if (row == 2)
                painter.fillRect(QRectF(0, y - 8, window.width(), 32), QColor("#3b5b8c"));
            words(24, y, 16,
                  {{"#c9ccd6", 50}, {"#8a8fa3", 70}, {"#c9ccd6", 40}, {"#c9ccd6", 60},
                   {row % 4 == 0 ? "#7ec699" : "#c9ccd6", 120 + (row * 83) % 260}});
        }
    }
    return image;
}

Notification notification(const QString &app, const QString &icon, const QString &summary,
                          const QString &body, int urgency = Notification::Normal) {
    Notification n;
    n.app = app;
    n.icon = icon;
    n.summary = summary;
    n.body = notificationMarkup(body);
    n.urgency = urgency;
    n.timeout = 0;
    return n;
}
} // namespace

PreviewData::PreviewData(ShellController &controller)
    : QObject(&controller), controller_(controller), audio_(std::make_unique<Audio>()),
      media_(std::make_unique<Media>()), powerMode_(std::make_unique<PowerMode>()),
      wifi_(std::make_unique<Wifi>()), bluetooth_(std::make_unique<Bluetooth>()) {
    // Firefox plays from a child process of its window's, and the music player from the shell of
    // the first terminal, which has no window of its own (the processes are the stand-in tasks').
    audio_->update({"speakers",
                    {{"speakers", "Speakers", 64, false},
                     {"headphones", "USB headphones", 40, false},
                     {"hdmi", "HDMI / DisplayPort (monitor)", 100, false}},
                    {{1, "Firefox", "firefox", 80, false, false, {1105, 1001}},
                     {2, "Music player", "audio-x-generic", 55, false, false, {2210, 2202, 2002}},
                     {3, "Video call", "camera-web", 100, true, false, {3301}}}});
    // A battery that is charging and Wi-Fi that is up, in a sysfs of its own.
    auto put = [this](const QString &path, const QByteArray &text) {
        QDir(sysfs_.path()).mkpath(QFileInfo(sysfs_.filePath(path)).path());
        QFile file(sysfs_.filePath(path));
        if (file.open(QIODevice::WriteOnly))
            file.write(text);
    };
    put("class/power_supply/BAT0/type", "Battery\n");
    put("class/power_supply/BAT0/capacity", "76\n");
    put("class/power_supply/BAT0/status", "Charging\n");
    put("class/net/wlan0/device", "");
    put("class/net/wlan0/wireless", "");
    put("class/net/wlan0/operstate", "up\n");
    status_ = std::make_unique<SystemStatus>(sysfs_.path());
    put("class/backlight/preview/max_brightness", "100\n");
    put("class/backlight/preview/brightness", "70\n");
    backlight_ = std::make_unique<Backlight>(sysfs_.path());

    TrayItem plain;
    plain.key = plain.id = plain.title = "Sync";
    plain.toolTipTitle = "Sync";
    plain.toolTipText = "Up to date";
    plain.icon = {trayIcon(QColor("#2f9e6e"), "S")};
    controller.tray()->add(plain);
    TrayItem withMenu;
    withMenu.key = withMenu.id = withMenu.title = "Updates";
    withMenu.icon = {trayIcon(QColor("#d9822b"), "U")};
    withMenu.menuPath = "/MenuBar";
    withMenu.menu = {{0, menuEntry({}, {1, 2, 3, 4, 5, 6, 7})},
                     {1, menuEntry({{"label", "_Open updates"}})},
                     {2, menuEntry({{"type", "separator"}})},
                     {3, menuEntry({{"label", "Check _automatically"},
                                    {"toggle-type", "checkmark"},
                                    {"toggle-state", 1}})},
                     {4, menuEntry({{"label", "_Schedule"}}, {41, 42})},
                     {41, menuEntry({{"label", "Daily"}, {"toggle-type", "radio"}, {"toggle-state", 1}})},
                     {42, menuEntry({{"label", "Weekly"}, {"toggle-type", "radio"}, {"toggle-state", 0}})},
                     {5, menuEntry({{"label", "Install now"}, {"enabled", false}})},
                     {6, menuEntry({{"type", "separator"}})},
                     {7, menuEntry({{"label", "_Quit"}})}};
    controller.tray()->add(withMenu);

    auto *center = controller.notifications();
    center->setServing(true);
    center->notify(notification("Updates", "system-software-update", "Three updates are ready",
                                "Restart to finish installing them."));
    center->notify(notification("Battery", "battery-caution", "Battery low",
                                "10% left: plug in the charger.", Notification::Critical));
    center->notify(notification("Calendar", "x-office-calendar", "Stand-up in 10 minutes",
                                "Room 3, or join the <b>video call</b>."));
    center->notify(notification("Mail", "mail-unread", "Re: the panel's popups",
                                "Looks good to me. Merge it once the gallery is green, and send "
                                "the screenshots around."));
    // Two more mails, which the history lists under the first, and a message with the sender's
    // picture and buttons for its actions.
    center->notify(notification("Mail", "mail-unread", "Build #418 passed",
                                "All 115 tests passed in 46 seconds."));
    center->notify(notification("Mail", "mail-unread", "Lunch on Friday?",
                                "The usual place at noon, if that suits everyone."));
    Notification message = notification("Chat", "user-available", "Alex",
                                         "Sent a picture: <i>harbour at dusk</i>. Coming tonight?");
    message.image = trayIcon(QColor("#7a5cc4"), "A");
    message.actions = {{"default", "Open"}, {"reply", "Reply"}, {"later", "Remind me later"}};
    center->notify(message);

    controller.power()->setAvailable("lock,suspend,hibernate,reboot,poweroff,logout");
    controller.setNightLight(true, "auto");

    // A music player well into a track, its cover a picture in the preview's own directory, and
    // a browser paused behind it.
    QImage cover(128, 128, QImage::Format_RGB32);
    {
        QPainter painter(&cover);
        QLinearGradient gradient(0, 0, 128, 128);
        gradient.setColorAt(0, QColor("#f2994a"));
        gradient.setColorAt(1, QColor("#7a3fbf"));
        painter.fillRect(cover.rect(), gradient);
        painter.setPen(QPen(QColor(255, 255, 255, 170), 6));
        painter.drawEllipse(QPoint(64, 64), 34, 34);
    }
    const auto coverPath = sysfs_.filePath("cover.png");
    cover.save(coverPath);
    Media::Player music;
    music.name = "org.mpris.MediaPlayer2.music";
    music.identity = "Music";
    music.desktopEntry = "org.gnome.Music";
    music.status = "Playing";
    music.title = "Harbour Lights";
    music.artist = "The Late Ferries";
    music.album = "Night Crossing";
    music.art = QUrl::fromLocalFile(coverPath).toString();
    music.trackId = "/track/1";
    music.length = 214'000'000;
    music.position = 83'000'000;
    music.rate = 0; // stays where it is, for the screenshot
    music.canPlay = music.canPause = music.canGoNext = music.canGoPrevious = music.canSeek = true;
    Media::Player browser = music;
    browser.name = "org.mpris.MediaPlayer2.firefox.instance_1_2";
    browser.identity = "Firefox";
    browser.desktopEntry = "firefox";
    browser.status = "Paused";
    browser.title = "A walk along the coast";
    browser.artist = "";
    browser.art = "";
    media_->setPlayer(browser);
    media_->setPlayer(music);
    powerMode_->update({true, "balanced", {"power-saver", "balanced", "performance"}, ""});
    // Connected to a home network among the neighbours', one open and one that needs a sign-in.
    Wifi::State wifi;
    wifi.available = wifi.hasWifi = wifi.enabled = true;
    wifi.accessPoints = {{"Harbour View", 82, "wpa-psk"}, {"Harbour View 5G", 64, "sae"},
                         {"Café Lumen", 51, "open"},      {"Ferry Office", 45, "enterprise"},
                         {"Lighthouse", 33, "wpa-psk"},   {"Pier 7", 14, "wpa-psk"}};
    wifi.known = {"Harbour View", "Café Lumen"};
    wifi.ssid = "Harbour View";
    wifi.strength = 82;
    wifi.primaryType = "wifi";
    wifi.primaryName = "Harbour View";
    wifi_->update(wifi);
    // Headphones and a keyboard connected, a speaker paired, and two devices in range while it looks
    // for more.
    auto device = [](const QString &id, const QString &name, const QString &icon, bool paired,
                     bool connected, int battery = -1, int rssi = 0) {
        BluetoothDevice device;
        device.path = "/org/bluez/hci0/dev_" + id;
        device.name = name;
        device.icon = icon;
        device.paired = paired;
        device.connected = connected;
        device.named = true;
        device.battery = battery;
        device.rssi = rssi;
        return device;
    };
    bluetooth_->update({true, true, true,
                        {device("buds", "Harbour Buds", "audio-headphones", true, true, 72),
                         device("keys", "MX Keys", "input-keyboard", true, true, 40),
                         device("speaker", "Kitchen speaker", "audio-card", true, false),
                         device("phone", "Pixel 9", "phone", false, false, -1, -48),
                         device("pad", "Controller", "input-gaming", false, false, -1, -70)}});
    bluetooth_->lookFor(true);

    // The start menu as on a desktop in use: two pages of pins, applications launched lately and
    // someone logged in; those of tools/shell_gallery.py that are installed show.
    const auto now = QDateTime::currentDateTimeUtc();
    QStringList pins;
    for (const auto *id : {"firefox", "org.kde.dolphin", "foot", "thunderbird", "code", "libreoffice-writer",
                           "gimp", "spotify", "steam", "org.gnome.Calculator", "obsidian", "systemsettings",
                           "discord", "mpv", "inkscape", "org.kde.kate", "keepassxc", "obs", "blender", "krita"})
        pins << QString(id) + ".desktop";
    controller.startMenu()->preview(pins, {{"code.desktop", 14, now.addSecs(-2 * 60)},
                                           {"firefox.desktop", 40, now.addSecs(-25 * 60)},
                                           {"gimp.desktop", 3, now.addSecs(-3 * 3600)},
                                           {"thunderbird.desktop", 9, now.addDays(-1)},
                                           {"steam.desktop", 2, now.addDays(-3)},
                                           {"org.kde.kate.desktop", 5, now.addDays(-12)}});
    controller.startMenu()->setUser("Robin Lee", QUrl());
    // Files a search finds, in the home folder's user folders, the first used lately; nothing is
    // read from disk.
    const auto home = QDir::homePath();
    const auto lately = now.addSecs(-3600).toMSecsSinceEpoch();
    controller.files()->preview(
        {{home + "/Documents", home + "/Documents/Reports", home + "/Downloads", home + "/Pictures"},
         {{1, "Quarterly report.pdf", "quarterly report.pdf", false, lately},
          {0, "Field notes.md", "field notes.md", false, 0},
          {0, "Reports", "reports", true, 0},
          {2, "firmware-2.4.1.zip", "firmware-2.4.1.zip", false, 0},
          {3, "Fireworks.jpg", "fireworks.jpg", false, 0}}});

    // The stand-in windows' pictures, image://preview-windows/ID.
    controller.engine()->addImageProvider("preview-windows",
                                          new ImageProvider([](const QString &id, QSize) {
                                              return windowPicture(id.section('/', 0, 0).toInt());
                                          }));
    // The windows, as the tests' stand-in model: a ListModel with the roles TaskModel has. The
    // terminals' are stacked, with pictures of two and none yet of the third, and the first
    // playing music. The compositor's numbers for them (windowId) are the switcher's.
    QQmlComponent component(controller.engine());
    component.setData(R"(import QtQml.Models
ListModel {
    ListElement { taskId: 1; title: "Release notes - Mozilla Firefox"; appId: "firefox"; active: true; minimized: false; urgent: false
                  maximized: false; fullscreen: false; output: ""; workspace: 1; sticky: false; floating: false; tiling: true; above: false
                  windowId: 41; pid: 1001; picture: "image://preview-windows/1" }
    ListElement { taskId: 2; title: "~/dev/paw"; appId: "foot"; active: false; minimized: false; urgent: false
                  maximized: false; fullscreen: false; output: ""; workspace: 2; sticky: false; floating: false; tiling: true; above: false
                  windowId: 42; pid: 2002; picture: "image://preview-windows/2" }
    ListElement { taskId: 3; title: "htop"; appId: "foot"; active: false; minimized: false; urgent: false
                  maximized: false; fullscreen: false; output: ""; workspace: 2; sticky: false; floating: true; tiling: true; above: true
                  windowId: 43; pid: 2003; picture: "image://preview-windows/3" }
    ListElement { taskId: 6; title: "man paw"; appId: "foot"; active: false; minimized: false; urgent: false
                  maximized: false; fullscreen: false; output: ""; workspace: 2; sticky: false; floating: false; tiling: true; above: false
                  windowId: 46; pid: 2006; picture: "" }
    ListElement { taskId: 4; title: "Downloads - Dolphin"; appId: "org.kde.dolphin"; active: false; minimized: true; urgent: false
                  maximized: true; fullscreen: false; output: ""; workspace: 3; sticky: false; floating: false; tiling: false; above: false
                  windowId: 44; pid: 4004; picture: "" }
    ListElement { taskId: 5; title: "Build finished"; appId: "kitty"; active: false; minimized: false; urgent: true
                  maximized: false; fullscreen: false; output: ""; workspace: 1; sticky: true; floating: true; tiling: true; above: false
                  windowId: 45; pid: 5005; picture: "" }
})",
                      QUrl());
    tasks_ = component.create();
    if (tasks_) {
        tasks_->setParent(this);
        QQmlEngine::setObjectOwnership(tasks_, QQmlEngine::CppOwnership);
    }
}
PreviewData::~PreviewData() = default;

// What the panel leaves of the output a preview stands for, where a layer surface that keeps clear
// of it (an exclusive zone of 0) goes: the output but the bar's strip, and the menu bar's.
QRect PreviewData::usableArea() const {
    const int panel = controller_.panelExtent();
    const int menuBar = panel_ ? panel_->property("menuBarHeight").toInt() : 0;
    const bool top = controller_.panelSurfaceTop();
    return QRect(QPoint(0, 0), ShellView::previewSize()).adjusted(0, top ? panel : menuBar, 0, top ? 0 : -panel);
}

void PreviewData::fill(QQuickItem *panel) {
    panel_ = panel;
    QQmlEngine::setObjectOwnership(audio_.get(), QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(status_.get(), QQmlEngine::CppOwnership);
    panel->setProperty("audioSource", QVariant::fromValue<QObject *>(audio_.get()));
    panel->setProperty("statusSource", QVariant::fromValue<QObject *>(status_.get()));
    QQmlEngine::setObjectOwnership(backlight_.get(), QQmlEngine::CppOwnership);
    panel->setProperty("backlightSource", QVariant::fromValue<QObject *>(backlight_.get()));
    QQmlEngine::setObjectOwnership(media_.get(), QQmlEngine::CppOwnership);
    panel->setProperty("mediaSource", QVariant::fromValue<QObject *>(media_.get()));
    QQmlEngine::setObjectOwnership(powerMode_.get(), QQmlEngine::CppOwnership);
    panel->setProperty("powerModeSource", QVariant::fromValue<QObject *>(powerMode_.get()));
    QQmlEngine::setObjectOwnership(wifi_.get(), QQmlEngine::CppOwnership);
    panel->setProperty("wifiSource", QVariant::fromValue<QObject *>(wifi_.get()));
    QQmlEngine::setObjectOwnership(bluetooth_.get(), QQmlEngine::CppOwnership);
    panel->setProperty("bluetoothSource", QVariant::fromValue<QObject *>(bluetooth_.get()));
    if (tasks_)
        panel->setProperty("taskSource", QVariant::fromValue(tasks_));
}

bool PreviewData::open(QQuickItem *panel, const QString &name) {
    if (surfaces().contains(name))
        return open(panel, "bar") && panel->window() &&
               showSurface(panel->window()->screen(), name);
    // The Bluetooth devices while a phone pairs, BlueZ asking to confirm its passkey.
    if (name == "quick-settings-pairing") {
        bluetooth_->ask({"confirm", "/org/bluez/hci0/dev_phone", "Pixel 9", "482916"});
        return open(panel, "quick-settings-bluetooth");
    }
    QVariant opened;
    QMetaObject::invokeMethod(panel, "previewPopup", Q_RETURN_ARG(QVariant, opened),
                              Q_ARG(QVariant, name));
    return opened.toBool();
}

QStringList PreviewData::surfaces() {
    return {"osd-volume", "osd-text", "cards",    "power-dialog",
            "palette", "switcher", "overview", "palette-empty", "auth-dialog", "snap-assist",
            "osd-microphone", "display-mode", "palette-calculator", "palette-files", "clipboard",
            "emoji", "display-settings", "display-settings-trial"};
}

namespace {
// The palette's previews and what each searches for: applications, actions and files, nothing
// it finds, a calculation, and files alone.
const QMap<QString, QString> &paletteQueries() {
    static const QMap<QString, QString> queries{{"palette", "fi"},
                                                {"palette-empty", "> nothing like this"},
                                                {"palette-calculator", "2*(3+4)"},
                                                {"palette-files", "/re"}};
    return queries;
}
} // namespace

bool PreviewData::showSurface(QScreen *screen, const QString &name) {
    // What each surface is, as the view in view.cpp that shows it on an output makes it.
    QString file;
    QVariantMap properties;
    auto mode = QQuickView::SizeViewToRootObject;
    // The offscreen platform's screen has no name, which the palette would take for no output.
    const QString output = screen->name().isEmpty() ? QStringLiteral("PREVIEW-1") : screen->name();
    if (name.startsWith("osd-")) {
        file = "Osd.qml";
        properties = {{"outputName", output}};
        // Long enough for a slow screenshot, not cut short by the display fading away.
        auto config = controller_.osd()->config();
        config.timeout = 60000;
        controller_.osd()->configure(config);
        if (name == "osd-volume")
            controller_.osd()->show(output, "Volume", 64, "volume");
        else if (name == "osd-microphone")
            controller_.osd()->show(output, "Microphone muted", -1, "microphone-muted");
        else
            controller_.osd()->show(output, "Do not disturb", -1, "dnd");
    } else if (name == "display-mode") {
        // The popup stepped once from extend, the choice in force, to the next in its order.
        file = "DisplayMode.qml";
        properties = {{"outputName", output}};
        controller_.displayModes()->handle("display-mode " + output +
                                           " external extend internal,duplicate,extend,external");
    } else if (name == "display-settings" || name == "display-settings-trial") {
        // A monitor with HDR to be had on the left, the laptop's panel at a scale of 1.5 right of
        // it, and a projector mirroring the panel; the first chosen, its scale changed but not
        // applied yet, or on trial.
        file = "DisplaySettings.qml";
        properties = {{"screenSize", ShellView::previewSize()}, {"shown", true}};
        auto *settings = controller_.displaySettings();
        settings->setAsk([](const QByteArray &line, std::function<void(const QByteArray &)> done) {
            if (line != "get monitors\n") {
                done(line.startsWith("monitors apply") ? "ok\n15000\n" : "ok\n");
                return;
            }
            done("ok\n"
                 "DP-3\tDell Inc. DELL U2720Q 4KX\t0\tconfig\t1\ton\t-\t0\t0\t2560x1440@143.912\t1\t0\toff\t8\t8\t"
                 "off\tsdr\t-\t1\t2560x1440@143.912*,2560x1440@59.951,1920x1080@143.912,1920x1080@60.000,"
                 "1280x720@60.000\n"
                 "eDP-1\tBOE 0x0BCA \t1\tdefault\t1\ton\t-\t2560\t240\t2880x1800@120.000\t1.5\t0\t-\t8\t8\toff\t"
                 "sdr\tthe monitor does not offer BT.2020 with PQ\t0\t2880x1800@120.000*,2880x1800@60.000\n"
                 "HDMI-A-1\tEpson EB-W06 \t0\twindow\t1\ton\teDP-1\t2560\t240\t1280x800@60.000\t1\t0\t-\t8\t8\t"
                 "off\tsdr\tthe monitor does not offer BT.2020 with PQ\t0\t1280x800@60.000*,1024x768@60.000\n");
        });
        settings->show(output);
        settings->select("DP-3");
        settings->setScale("DP-3", 1.25);
        if (name == "display-settings-trial")
            settings->apply();
    } else if (name == "cards") {
        file = "NotificationCards.qml";
        // Over the stand-ins every preview has: one with a picture, buttons and a timer.
        QImage picture(96, 96, QImage::Format_ARGB32_Premultiplied);
        picture.fill(Qt::transparent);
        {
            QPainter painter(&picture);
            painter.setRenderHint(QPainter::Antialiasing);
            QLinearGradient gradient(0, 0, 96, 96);
            gradient.setColorAt(0, QColor("#f2a65a"));
            gradient.setColorAt(1, QColor("#b8456b"));
            painter.setBrush(gradient);
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(picture.rect());
            QFont font = painter.font();
            font.setPixelSize(44);
            font.setBold(true);
            painter.setFont(font);
            painter.setPen(Qt::white);
            painter.drawText(picture.rect(), Qt::AlignCenter, "AL");
        }
        Notification message = notification("Thunderbird", "", "Ada Lovelace",
                                            "Are we still on for the review at three? I pushed "
                                            "the last fixes this morning.");
        message.desktopEntry = "thunderbird";
        message.image = picture;
        message.actions = {{"default", "Open"}, {"reply", "Reply"}, {"read", "Mark as read"}};
        message.timeout = 60000;
        controller_.notifications()->notify(message);
        // The controller puts the cards on the primary screen.
        properties = {{"outputName", controller_.cardsOutput()}};
    } else if (name == "power-dialog") {
        file = "PowerDialog.qml";
        mode = QQuickView::SizeRootObjectToView;
        // Shown as its view shows it, as the palette, the switcher and the overview are below.
        properties = {{"shown", true}};
        controller_.power()->request("poweroff", output);
    } else if (name == "auth-dialog") {
        file = "AuthDialog.qml";
        mode = QQuickView::SizeRootObjectToView;
        properties = {{"shown", true}};
        // pkexec asking to run GParted, which two users may allow; a stand-in for polkit's helper
        // asks for the password.
        class Helper : public AuthConversation {
          public:
            void start() override { Q_EMIT request("Password: ", false); }
            void respond(const QString &) override {}
            void cancel() override {}
        };
        auto *auth = controller_.authentication();
        auth->setConversations([](const AuthIdentity &, const QString &) { return new Helper; });
        auth->add({"org.freedesktop.policykit.exec",
                   "Authentication is needed to run `/usr/bin/gparted' as the super user",
                   "",
                   {{"command_line", "/usr/bin/gparted /dev/nvme0n1"}},
                   "preview",
                   {{"unix-user:" + QString::number(getuid()), "ada", "Ada Lovelace", getuid()},
                    {"unix-user:0", "root", "", 0}}},
                  {});
    } else if (name == "clipboard") {
        file = "ClipboardPicker.qml";
        properties = {{"screenSize", ShellView::previewSize()}, {"shown", true}};
        // What was copied lately: text, a picture, the second pinned.
        QImage picture(640, 400, QImage::Format_RGB32);
        {
            QPainter painter(&picture);
            QLinearGradient gradient(0, 0, 640, 400);
            gradient.setColorAt(0, QColor("#4a64dc"));
            gradient.setColorAt(1, QColor("#b8456b"));
            painter.fillRect(picture.rect(), gradient);
        }
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        picture.save(&buffer, "PNG");
        controller_.clipboard()->preview(
            {{{"text/plain", "cmake --build build && ctest --test-dir build"}},
             {{"text/plain", "Quarterly report: revenue up 12 %, costs flat. The board meets "
                             "on Thursday to agree the budget for next year."}},
             {{"image/png", png}},
             {{"text/plain", "https://example.org/paw/features"}},
             {{"text/plain", "Robin Lee\n4 Market Street\nOldtown"}}},
            {1});
        controller_.clipboard()->toggle(output);
    } else if (name == "emoji") {
        file = "EmojiPicker.qml";
        properties = {{"screenSize", ShellView::previewSize()}, {"shown", true}};
        // Some picked lately, which open the picker on their tab.
        controller_.emoji()->preview({"👍", "🎉", "😂", "❤️", "🙏", "🚀", "☕"}, 0);
        controller_.emoji()->toggle(output);
    } else if (paletteQueries().contains(name)) {
        file = "Palette.qml";
        properties = {{"screenSize", ShellView::previewSize()}, {"shown", true}};
        controller_.palette()->open(output);
        controller_.palette()->setQuery(paletteQueries().value(name));
    } else if (name == "switcher") {
        file = "Switcher.qml";
        // The taskbar's stand-in windows, most recently focused first, the one before the
        // focused one selected as Alt+Tab selects it, with their pictures where they have them
        // (shell.thumbnails) and the icons standing in for the others'.
        auto window = [&output](const QString &appId, const QString &title, int workspace, int id,
                                bool minimized = false, bool urgent = false) {
            return QVariantMap{{"appId", appId},
                               {"title", title},
                               {"output", output},
                               {"workspace", workspace},
                               {"minimized", minimized},
                               {"urgent", urgent},
                               {"id", id}};
        };
        properties = {{"screenSize", ShellView::previewSize()},
                      {"windows",
                       QVariantList{window("firefox", "Release notes - Mozilla Firefox", 1, 41),
                                    window("foot", "~/dev/paw", 2, 42),
                                    window("kitty", "Build finished", 2, 45, false, true),
                                    window("foot", "htop", 2, 43),
                                    window("org.kde.dolphin", "Downloads - Dolphin", 1, 44, true)}},
                      {"selected", 1},
                      {"taskSource", QVariant::fromValue(tasks_)},
                      {"shown", true}};
    } else if (name == "overview") {
        file = "Overview.qml";
        const QRect area = usableArea();
        properties = {{"screenSize", ShellView::previewSize()},
                      {"windows", overviewWindows(area)},
                      {"strip", overviewStrip(area)},
                      {"area", area},
                      {"selected", 0},
                      {"viewed", 1},
                      {"urgentWorkspaces", QVariantList{1}},
                      {"shown", true}};
    } else if (name == "snap-assist") {
        // The overview in the free half beside a window snapped to the left.
        file = "Overview.qml";
        const QRect area = usableArea();
        properties = {{"screenSize", ShellView::previewSize()},
                      {"windows", assistWindows(area)},
                      {"strip", QVariantList{}},
                      {"area", assistSlot(area)},
                      {"assist", true},
                      {"selected", 0},
                      {"viewed", 0},
                      {"shown", true}};
    } else {
        return false;
    }
    surfaceName_ = name;
    surface_ = std::make_unique<QQuickView>(controller_.engine(), nullptr);
    surface_->setScreen(screen);
    surface_->setTitle("paw preview " + name);
    surface_->setColor(Qt::transparent);
    surface_->setFlags(Qt::FramelessWindowHint);
    surface_->setResizeMode(mode);
    if (mode == QQuickView::SizeRootObjectToView)
        surface_->resize(ShellView::previewSize());
    surface_->setInitialProperties(properties);
    surface_->setSource(QUrl("qrc:/shell/PawShell/" + file));
    if (surface_->status() != QQuickView::Ready)
        return false;
    // As the palette's PickerView does before it shows, and PowerView once it shows.
    if (file == "Palette.qml")
        QMetaObject::invokeMethod(surface_->rootObject(), "reset");
    surface_->show();
    if (name == "power-dialog" || name == "auth-dialog" || name.startsWith("display-settings"))
        QMetaObject::invokeMethod(surface_->rootObject(), "reset");
    return true;
}

QImage PreviewData::withSurface(QImage desktop) const {
    if (!surface_ || !surface_->rootObject())
        return desktop;
    // In the desktop's pixels, which are the preview's scaled by the device pixel ratio.
    const qreal scale = desktop.width() / qreal(ShellView::previewSize().width());
    const QSizeF root = surface_->rootObject()->size();
    QImage image = surface_->grabWindow();
    image.setDevicePixelRatio(1);
    image = image.copy(0, 0, qRound(root.width() * scale), qRound(root.height() * scale));
    // Placed as its layer surface is: by the edges it is anchored to, inside the area the
    // panel leaves when it keeps clear of it (an exclusive zone of 0), over the whole output when
    // it does not (-1).
    const QRect output(QPoint(0, 0), ShellView::previewSize());
    const QRect usable = usableArea();
    const QSize size = root.toSize();
    QPoint at;
    if (surfaceName_.startsWith("osd-")) {
        // OsdView: centred, 48 pixels from the top edge, or ShellController::osdBottom from the
        // bottom.
        at = QPoint((output.width() - size.width()) / 2,
                    controller_.osd()->top() ? 48 : output.height() - controller_.osdBottom() - size.height());
    } else if (surfaceName_.startsWith("palette") || surfaceName_ == "clipboard" ||
               surfaceName_ == "emoji") {
        // PickerView: centred, below the bars by ShellController::paletteDrop.
        at = QPoint(usable.left() + (usable.width() - size.width()) / 2,
                    usable.top() + controller_.paletteDrop(output.height()));
    } else if (surfaceName_ == "display-mode" || surfaceName_.startsWith("display-settings")) {
        // DisplayModeView and DisplaySettingsView: in the middle of the output.
        at = output.center() - QPoint(size.width() / 2, size.height() / 2);
    } else if (surfaceName_ == "switcher") {
        // SwitcherView: centred.
        at = usable.center() - QPoint(size.width() / 2, size.height() / 2);
    } else if (surfaceName_ == "cards") {
        // CardsView: in the configured corner.
        const auto *center = controller_.notifications();
        at = QPoint(center->left() ? usable.left() : usable.right() + 1 - size.width(),
                    center->bottom() ? usable.bottom() + 1 - size.height() : usable.top());
    }
    QPainter painter(&desktop);
    if (surfaceName_ == "overview") {
        // What the compositor draws under the shell's text, in its colours: the dimmed backdrop,
        // the strip's cells with the workspace shown framed, and each window on a card
        // (OVERVIEW_PAD around it), a plain stand-in for its picture, the selected one framed.
        painter.scale(scale, scale);
        painter.fillRect(output, QColor::fromRgbF(0.04f, 0.05f, 0.08f, 0.86f));
        for (const auto &item : overviewStrip(usable)) {
            const auto cell = item.toMap();
            const QRect rect(cell["x"].toInt(), cell["y"].toInt(), cell["w"].toInt(),
                             cell["h"].toInt());
            const bool viewed = cell["workspace"].toInt() == 1;
            painter.fillRect(rect, viewed ? QColor::fromRgbF(0.22f, 0.25f, 0.31f, 0.95f)
                                          : QColor::fromRgbF(0.12f, 0.13f, 0.16f, 0.95f));
            if (viewed) {
                painter.setPen(QPen(QColor::fromRgbF(0.36f, 0.6f, 1.0f), 2));
                painter.drawRect(rect);
            }
        }
        const auto windows = overviewWindows(usable);
        for (int i = 0; i < windows.size(); ++i) {
            const auto window = windows[i].toMap();
            const QRect rect(window["x"].toInt(), window["y"].toInt(), window["w"].toInt(),
                             window["h"].toInt());
            const bool dark = window["appId"] != "firefox";
            painter.fillRect(rect.adjusted(-6, -6, 6, 6),
                             QColor::fromRgbF(0.1f, 0.11f, 0.14f, 0.85f));
            painter.fillRect(rect, dark ? QColor("#1e1f29") : QColor("#eceef3"));
            painter.fillRect(rect.adjusted(0, 0, 0, 14 - rect.height()),
                             dark ? QColor("#2b2d3a") : QColor("#cfd3dd"));
            if (i == 0) {
                painter.setPen(QPen(QColor::fromRgbF(0.36f, 0.6f, 1.0f), 3));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(rect.adjusted(-6, -6, 6, 6));
            }
        }
        painter.resetTransform();
    } else if (surfaceName_ == "snap-assist") {
        // A window snapped to the left half, and what the compositor draws in the right one: its
        // darkened slot and each window on a card, the selected one framed.
        painter.scale(scale, scale);
        const QRect slot = assistSlot(usable);
        const QRect snapped(usable.x(), usable.y(), slot.x() - usable.x(), usable.height());
        painter.fillRect(snapped, QColor("#1e1f29"));
        painter.fillRect(snapped.adjusted(0, 0, 0, 30 - snapped.height()), QColor("#2b2d3a"));
        painter.fillRect(slot, QColor::fromRgbF(0.04f, 0.05f, 0.08f, 0.86f));
        const auto windows = assistWindows(usable);
        for (int i = 0; i < windows.size(); ++i) {
            const auto window = windows[i].toMap();
            const QRect rect(window["x"].toInt(), window["y"].toInt(), window["w"].toInt(),
                             window["h"].toInt());
            const bool dark = window["appId"] != "firefox";
            painter.fillRect(rect.adjusted(-6, -6, 6, 6),
                             QColor::fromRgbF(0.1f, 0.11f, 0.14f, 0.85f));
            painter.fillRect(rect, dark ? QColor("#1e1f29") : QColor("#eceef3"));
            painter.fillRect(rect.adjusted(0, 0, 0, 14 - rect.height()),
                             dark ? QColor("#2b2d3a") : QColor("#cfd3dd"));
            if (i == 0) {
                painter.setPen(QPen(QColor::fromRgbF(0.36f, 0.6f, 1.0f), 3));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(rect.adjusted(-6, -6, 6, 6));
            }
        }
        painter.resetTransform();
    }
    painter.drawImage(QPointF(at) * scale, image);
    return desktop;
}

QImage previewOnDesktop(QImage panel, QImage popover, QImage menuBar, bool panelTop,
                        ShellController &controller) {
    panel.setDevicePixelRatio(1);
    popover.setDevicePixelRatio(1);
    menuBar.setDevicePixelRatio(1);
    // As large as the output a preview stands for, in the bar's pixels.
    const QSize output = ShellView::previewSize() * (panel.width() / qreal(ShellView::previewSize().width()));
    QImage desktop(output, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&desktop);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const QString file = controller.wallpaperFile();
    const QImage wallpaper = file.isEmpty() ? QImage() : QImage(file);
    if (!wallpaper.isNull()) {
        // Cropped to fill, as Desktop.qml shows it.
        const QSize size = wallpaper.size().scaled(desktop.size(), Qt::KeepAspectRatioByExpanding);
        painter.drawImage(QRect(QPoint((desktop.width() - size.width()) / 2,
                                       (desktop.height() - size.height()) / 2),
                                size),
                          wallpaper);
    } else if (controller.style() == "macos") {
        // The wallpaper the macOS style draws while none is set, as Desktop.qml shows it, drawn
        // offscreen for the picture.
        QQuickView drawn(controller.engine(), nullptr);
        drawn.setColor(Qt::transparent);
        drawn.setResizeMode(QQuickView::SizeRootObjectToView);
        drawn.resize(ShellView::previewSize());
        drawn.setSource(QUrl("qrc:/shell/PawShell/DrawnWallpaper.qml"));
        drawn.create();
        QImage picture = drawn.grabWindow();
        picture.setDevicePixelRatio(1);
        painter.drawImage(desktop.rect(), picture);
    } else {
        QLinearGradient gradient(0, 0, 0, desktop.height());
        gradient.setColorAt(0, controller.background().lighter(145));
        gradient.setColorAt(1, controller.background());
        painter.fillRect(desktop.rect(), gradient);
    }
    painter.drawImage(0, panelTop ? 0 : desktop.height() - panel.height(), panel);
    if (!menuBar.isNull())
        painter.drawImage(0, 0, menuBar);
    // Along the bar's edge, as the popups are placed by it, should a compositor have made the
    // window smaller.
    if (!popover.isNull())
        painter.drawImage(0, panelTop ? 0 : desktop.height() - popover.height(), popover);
    return desktop;
}
