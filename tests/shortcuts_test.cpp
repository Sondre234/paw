// SPDX-License-Identifier: GPL-3.0-or-later
// The sheet of keyboard shortcuts: its groups and their order, the rows' titles (a description,
// the palette's title, the sheet's own, a command shortened), the keys' names and the modifiers'
// order in each style, the rows that bindings alike but for their keys become, the binding mode's
// rows first, the search; and the model the shell shows, which follows the configuration.
#include "controller.hpp"
#include "shortcuts.hpp"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <paw/config.hpp>
#include <paw/config_schema.hpp>
#include <xkbcommon/xkbcommon.h>

namespace {
QVariantMap group(const QVariantList &sheet, const QString &name) {
    for (const auto &item : sheet)
        if (item.toMap()["name"] == name)
            return item.toMap();
    return {};
}
QStringList names(const QVariantList &sheet) {
    QStringList list;
    for (const auto &item : sheet)
        list << item.toMap()["name"].toString();
    return list;
}
QStringList titles(const QVariantMap &group) {
    QStringList list;
    for (const auto &row : group["rows"].toList())
        list << row.toMap()["title"].toString();
    return list;
}
QVariantMap row(const QVariantList &sheet, const QString &title) {
    for (const auto &item : sheet)
        for (const auto &row : item.toMap()["rows"].toList())
            if (row.toMap()["title"] == title)
                return row.toMap();
    return {};
}
QStringList strings(const QVariant &value) { return value.toStringList(); }

const char *const bindings = R"(
    layout = { workspace_names = { "", "", "mail" } },
    modes = { resize = {
        { key = "Left", action = "resize_left" }, { key = "Right", action = "resize_right" },
        { key = "Escape", action = "mode", mode = "default" },
        { key = "Return", action = "mode", mode = "default", description = "Done" } } },
    bindings = {
        { mods = { "Super" }, key = "c", action = "close" },
        { mods = { "Super" }, key = "q", action = "terminal" },
        { mods = { "Super" }, key = "e", action = "spawn", command = { "dolphin" },
          description = "  Files,\n the manager " },
        { mods = { "Super", "Shift" }, key = "b", action = "spawn",
          command = { "/usr/bin/firefox", "--private-window" } },
        { mods = { "Super" }, key = "x", action = "spawn",
          command = { "sh", "-c", "notify-send hello && sleep 1 && notify-send goodbye && exit" } },
        { mods = { "Super" }, key = "Left", action = "focus_left" },
        { mods = { "Super" }, key = "Right", action = "focus_right" },
        { mods = { "Super" }, key = "Up", action = "focus_up" },
        { mods = { "Super" }, key = "Down", action = "focus_down" },
        { mods = { "Super", "Ctrl" }, key = "Left", action = "focus_last" },
        { mods = { "Super" }, key = "1", action = "workspace", workspace = 1 },
        { mods = { "Super" }, key = "2", action = "workspace", workspace = 2 },
        { mods = { "Super" }, key = "3", action = "workspace", workspace = 3 },
        { mods = { "Super" }, key = "w", action = "workspace", workspace = 3 },
        { mods = { "Super", "Shift" }, key = "3", action = "move_to_workspace", workspace = 3 },
        { mods = { "Super" }, key = "bracketright", action = "group_next" },
        { mods = { "Super", "Alt" }, key = "r", action = "mode", mode = "resize" },
        { mods = { "Super", "Ctrl", "Alt", "Shift" }, key = "semicolon", action = "emoji_picker" },
        { key = "Print", action = "screenshot", mode = "output" },
        { key = "XF86AudioRaiseVolume", action = "volume_up" },
        { button = "side", app_id = "^(kitty|foot)$", desktop = true, action = "close" },
        { mods = { "Super" }, button = "wheel_up", action = "workspace_next" },
        { switch = "lid", state = "close", action = "suspend" },
        { mods = { "Super" }, key = "slash", action = "shortcuts" },
    },
)";
} // namespace

class ShortcutsTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir_;
    paw::Config config_ = paw::parse_config(std::string("return {") + bindings + "}");

  private Q_SLOTS:
    void initTestCase() {
        // Nothing here may reach a running compositor.
        qunsetenv("PAW_SOCKET");
    }
    void groupsInOrder() {
        const auto sheet = shortcuts::sheet(config_, {}, false);
        QCOMPARE(names(sheet), QStringList({"Windows", "Focus", "Workspaces", "Window groups",
                                            "Launch", "Shell", "Screen", "Media", "Session"}));
        QCOMPARE(shortcuts::rows(sheet), 19);
        for (const auto &item : sheet)
            QVERIFY(!item.toMap()["mode"].toBool());
    }
    void everyActionHasAGroup() {
        // A binding of each of the configuration's actions, titled by its name: none goes under
        // Other, so a new action fails here until shortcuts.cpp gives it a group.
        paw::Config config;
        for (const auto &name : paw::config_action_names()) {
            paw::Binding binding{};
            binding.modifiers = SH_LOGO;
            binding.keysym = XKB_KEY_F1;
            binding.action = paw::parse_action(name);
            binding.description = name;
            config.bindings.push_back(binding);
        }
        const auto sheet = shortcuts::sheet(config, {}, false);
        const auto other = ::titles(group(sheet, "Other"));
        QVERIFY2(other.isEmpty(), qPrintable("no group for " + other.join(", ")));
        QCOMPARE(shortcuts::rows(sheet), int(paw::config_action_names().size()));
        // The newest where they belong.
        QVERIFY(::titles(group(sheet, "Toys")).contains("ambient"));
        QVERIFY(::titles(group(sheet, "Screen")).contains("pick_color"));
    }
    void ambientTitles() {
        // The ambient action by its mode: a kind and off as the palette calls them.
        const auto config = paw::parse_config(
            "return { bindings = {"
            " { mods = { 'Super' }, key = 'a', action = 'ambient' },"
            " { mods = { 'Super' }, key = 'b', action = 'ambient', mode = 'next' },"
            " { mods = { 'Super' }, key = 'c', action = 'ambient', mode = 'snow' },"
            " { mods = { 'Super' }, key = 'd', action = 'ambient', mode = 'off' } } }");
        QCOMPARE(::titles(group(shortcuts::sheet(config, {}, false), "Toys")),
                 QStringList({"Ambient particles, on or off", "Next ambient particles",
                              "Ambient: soft snow over the wallpaper", "Ambient: off"}));
    }
    void titles() {
        const auto sheet = shortcuts::sheet(config_, {}, false);
        // The palette's titles, "Group: next tab" without its prefix under its heading, and the
        // sheet's own for what the palette does not offer.
        QCOMPARE(::titles(group(sheet, "Windows")), QStringList({"Close window", "Close window"}));
        QCOMPARE(::titles(group(sheet, "Window groups")), QStringList{"Next tab"});
        QCOMPARE(::titles(group(sheet, "Shell")),
                 QStringList({"Emoji picker", "Keyboard shortcuts"}));
        QCOMPARE(::titles(group(sheet, "Screen")), QStringList{"Screenshot of the screen"});
        QCOMPARE(::titles(group(sheet, "Session")),
                 QStringList({"Enter the resize mode", "Suspend"}));
        // A description, on one line; a command without one, shortened: the program without its
        // folder, the script of sh -c, cut short.
        QCOMPARE(::titles(group(sheet, "Launch")),
                 QStringList({"Open a terminal", "Files, the manager", "firefox --private-window",
                              "notify-send hello && sleep 1 && notify-send…"}));
        QVERIFY(!row(sheet, "Files, the manager")["command"].toBool());
        QVERIFY(row(sheet, "firefox --private-window")["command"].toBool());
        // Where a button binding acts.
        const auto close = group(sheet, "Windows")["rows"].toList()[1].toMap();
        QCOMPARE(close["detail"].toString(), QString("Over kitty or foot windows and the desktop"));
        QCOMPARE(strings(close["keys"]), QStringList{"Side button"});
        QVERIFY(close["button"].toBool());
        QCOMPARE(strings(row(sheet, "Suspend")["keys"]), QStringList{"Lid closed"});
    }
    void bindingsAlikeJoin() {
        const auto sheet = shortcuts::sheet(config_, {}, false);
        // Super and the arrows, focusing that way; Super + Ctrl + Left is another binding.
        QCOMPARE(::titles(group(sheet, "Focus")),
                 QStringList({"Focus the window that way", "Focus the previous window"}));
        const auto ways = row(sheet, "Focus the window that way");
        QCOMPARE(strings(ways["keys"]), QStringList({"←", "→", "↑", "↓"}));
        QCOMPARE(strings(ways["mods"]), QStringList{"Super"});
        QVERIFY(!ways["range"].toBool());
        QVERIFY(ways["search"].toString().contains("focus the window to the left"));
        // Workspaces 1 to 3 on their digits, but not 3 again on W, nor moving to 3 alone.
        QCOMPARE(::titles(group(sheet, "Workspaces")),
                 QStringList({"Go to workspace 1–3", "Go to workspace 3: mail",
                              "Move window to workspace 3: mail", "Next workspace"}));
        const auto run = row(sheet, "Go to workspace 1–3");
        QCOMPARE(strings(run["keys"]), QStringList({"1", "3"}));
        QVERIFY(run["range"].toBool());
    }
    void eachWayOnce() {
        // Super and the arrows, then Super and H, J, K, L: two rows of the same family.
        const auto config = paw::parse_config(
            "return { bindings = {"
            " { mods = { 'Super' }, key = 'Left', action = 'move_left' },"
            " { mods = { 'Super' }, key = 'Right', action = 'move_right' },"
            " { mods = { 'Super' }, key = 'h', action = 'move_left' },"
            " { mods = { 'Super' }, key = 'l', action = 'move_right' } } }");
        const auto rows = group(shortcuts::sheet(config, {}, false), "Windows")["rows"].toList();
        QCOMPARE(rows.size(), 2);
        QCOMPARE(strings(rows[0].toMap()["keys"]), QStringList({"←", "→"}));
        QCOMPARE(strings(rows[1].toMap()["keys"]), QStringList({"H", "L"}));
        QCOMPARE(rows[1].toMap()["title"].toString(), QString("Move the window that way"));
    }
    void releaseBindings() {
        // A modifier key tapped alone shows as its modifier's cap with "tap"; another key that
        // runs as it comes up says so, and joins no family.
        const auto config = paw::parse_config(
            "return { bindings = {"
            " { key = 'Super_L', release = true, action = 'launcher' },"
            " { mods = { 'Super' }, key = 'Left', action = 'focus_left' },"
            " { mods = { 'Super' }, key = 'Right', action = 'focus_right', release = true } } }");
        const auto sheet = shortcuts::sheet(config, {}, false);
        const auto tap = row(sheet, "Applications menu");
        QCOMPARE(strings(tap["mods"]), QStringList{"Super"});
        QCOMPARE(strings(tap["keys"]), QStringList());
        QCOMPARE(tap["when"].toString(), QString("tap"));
        QVERIFY(tap["search"].toString().contains("super_l"));
        QCOMPARE(::titles(group(sheet, "Focus")),
                 QStringList({"Focus the window to the left", "Focus the window to the right"}));
        const auto released = row(sheet, "Focus the window to the right");
        QCOMPARE(released["when"].toString(), QString("on release"));
        QCOMPARE(strings(released["keys"]), QStringList{"→"});
        QCOMPARE(row(sheet, "Focus the window to the left")["when"].toString(), QString());
        QCOMPARE(shortcuts::rows(shortcuts::filter(sheet, "tap")), 1);
    }
    void keyNames() {
        QCOMPARE(shortcuts::keyName(XKB_KEY_Return, false), QString("Enter"));
        QCOMPARE(shortcuts::keyName(XKB_KEY_Return, true), QString("Return"));
        QCOMPARE(shortcuts::keyName(XKB_KEY_BackSpace, true), QString("Delete"));
        QCOMPARE(shortcuts::keyName(XKB_KEY_q, false), QString("Q"));
        QCOMPARE(shortcuts::keyName(XKB_KEY_semicolon, false), QString(";"));
        QCOMPARE(shortcuts::keyName(XKB_KEY_slash, false), QString("/"));
        QCOMPARE(shortcuts::keyName(XKB_KEY_Left, false), QString("←"));
        QCOMPARE(shortcuts::keyName(XKB_KEY_F11, false), QString("F11"));
        QCOMPARE(shortcuts::keyName(XKB_KEY_KP_1, false), QString("Num 1"));
        QCOMPARE(shortcuts::keyName(XKB_KEY_XF86AudioRaiseVolume, false), QString("Volume up"));
        QCOMPARE(shortcuts::keyName(XKB_KEY_XF86TouchpadToggle, false), QString("Touchpad toggle"));
    }
    void modifiersInTheStylesOrder() {
        const auto emoji = [](const QVariantList &sheet) {
            return strings(row(sheet, "Emoji picker")["mods"]);
        };
        QCOMPARE(emoji(shortcuts::sheet(config_, {}, false)),
                 QStringList({"Super", "Ctrl", "Alt", "Shift"}));
        // ⌃ ⌥ ⇧ ⌘, as a Mac writes them.
        QCOMPARE(emoji(shortcuts::sheet(config_, {}, true)),
                 QStringList({"Ctrl", "Alt", "Shift", "Super"}));
    }
    void modeFirst() {
        // Its bindings first, under its name, then the others; a name no mode has is none.
        const auto sheet = shortcuts::sheet(config_, "resize", false);
        const auto first = sheet.value(0).toMap();
        QCOMPARE(first["name"].toString(), QString("Resize mode"));
        QVERIFY(first["mode"].toBool());
        QCOMPARE(::titles(first),
                 QStringList({"Resize the window that way", "Leave the mode", "Done"}));
        QCOMPARE(strings(first["rows"].toList()[0].toMap()["keys"]), QStringList({"←", "→"}));
        QCOMPARE(strings(first["rows"].toList()[0].toMap()["mods"]), QStringList());
        QCOMPARE(sheet.size(), 10);
        QCOMPARE(names(shortcuts::sheet(config_, "nonesuch", false)).value(0), QString("Windows"));
    }
    void search() {
        const auto sheet = shortcuts::sheet(config_, {}, false);
        QCOMPARE(shortcuts::filter(sheet, ""), sheet);
        QCOMPARE(shortcuts::filter(sheet, "   "), sheet);
        // By the title, the group, a key's name or its keysym's, a modifier by any of its names,
        // the action's name, a command's words; every word must be found.
        QCOMPARE(names(shortcuts::filter(sheet, "WORK")), QStringList{"Workspaces"});
        QCOMPARE(::titles(group(shortcuts::filter(sheet, "print"), "Screen")),
                 QStringList{"Screenshot of the screen"});
        QCOMPARE(shortcuts::rows(shortcuts::filter(sheet, "dolphin")), 1);
        QCOMPARE(shortcuts::rows(shortcuts::filter(sheet, "emoji_picker")), 1);
        QCOMPARE(shortcuts::rows(shortcuts::filter(sheet, "win shift")), 3);
        QCOMPARE(shortcuts::rows(shortcuts::filter(sheet, "option")), 2);
        QCOMPARE(::titles(group(shortcuts::filter(sheet, "mail move"), "Workspaces")),
                 QStringList{"Move window to workspace 3: mail"});
        QVERIFY(shortcuts::filter(sheet, "nothing like this").isEmpty());
    }
    void followsTheConfiguration() {
        QFile file(dir_.filePath("init.lua"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray("return {") + bindings + "}");
        file.close();
        ShellController controller(dir_.filePath("init.lua").toStdString());
        auto *sheet = controller.shortcuts();
        QSignalSpy opened(sheet, &Shortcuts::openChanged);
        QVERIFY(sheet->output().isEmpty());
        sheet->toggle("DP-1");
        QCOMPARE(sheet->output(), QString("DP-1"));
        QCOMPARE(sheet->total(), 19);
        sheet->setQuery("work");
        QCOMPARE(sheet->count(), 4);
        // On another output it moves there, its search empty; again on that one, it closes.
        sheet->toggle("HDMI-A-1");
        QCOMPARE(sheet->output(), QString("HDMI-A-1"));
        QCOMPARE(sheet->query(), QString());
        QCOMPARE(sheet->count(), 19);
        sheet->toggle("HDMI-A-1");
        QVERIFY(sheet->output().isEmpty());
        QCOMPARE(opened.count(), 3);
        // Open, it reads the configuration again as it changes.
        sheet->toggle("DP-1");
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("return { bindings = { { mods = { 'Super' }, key = 'c', action = 'close' } } }");
        file.close();
        controller.reload();
        QTRY_COMPARE(sheet->total(), 1);
        QCOMPARE(sheet->groups().value(0).toMap()["name"].toString(), QString("Windows"));
        sheet->close();
        QVERIFY(sheet->output().isEmpty());
    }
};

QTEST_MAIN(ShortcutsTest)
#include "shortcuts_test.moc"
