// SPDX-License-Identifier: GPL-3.0-or-later
#include "shortcuts.hpp"
#include "controller.hpp"
#include "palette.hpp"
#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <linux/input-event-codes.h>
#include <paw/config.hpp>
#include <paw/config_schema.hpp>
#include <xkbcommon/xkbcommon.h>

namespace {
// The groups, in the order the sheet shows them.
enum Group {
    Windows,
    Focus,
    Workspaces,
    Tiling,
    Scrolling,
    Groups,
    Launch,
    Shell,
    Screen,
    Media,
    Session,
    Toys,
    Other,
};
const char *const groupNames[] = {"Windows", "Focus",  "Workspaces", "Tiling",  "Scrolling",
                                  "Window groups", "Launch", "Shell", "Screen", "Media",
                                  "Session", "Toys", "Other"};

struct Action {
    const char *name;
    Group group;
    // Its title where the command palette has none.
    const char *title;
};
// Each action's group and title; one that is not here (a newer action) goes under Other, called
// what the palette calls it, else by its name.
constexpr Action actions[] = {
    {"spawn", Launch, "Run a program"},
    {"terminal", Launch, "Open a terminal"},
    {"launcher", Launch, "Applications menu"},

    {"close", Windows, "Close window"},
    {"maximize", Windows, "Maximize window"},
    {"restore", Windows, "Restore window"},
    {"minimize", Windows, "Minimize window"},
    {"fullscreen", Windows, "Toggle fullscreen"},
    {"toggle_floating", Windows, "Toggle floating"},
    {"tile", Windows, "Tile the window again"},
    {"toggle_sticky", Windows, "Toggle sticky window"},
    {"toggle_above", Windows, "Keep window above others, or not"},
    {"swallow_toggle", Windows, "Swallow the terminal, or let it out"},
    {"move_to_scratchpad", Windows, "Hide window in the scratchpad"},
    {"scratchpad_show", Windows, "Show the scratchpad"},
    {"snap_left", Windows, "Snap window left"},
    {"snap_right", Windows, "Snap window right"},
    {"snap_top_left", Windows, "Snap window to the top-left quarter"},
    {"snap_top_right", Windows, "Snap window to the top-right quarter"},
    {"snap_bottom_left", Windows, "Snap window to the bottom-left quarter"},
    {"snap_bottom_right", Windows, "Snap window to the bottom-right quarter"},
    {"snap_cycle_left", Windows, "Snap window left, step by step"},
    {"snap_cycle_right", Windows, "Snap window right, step by step"},
    {"snap_cycle_up", Windows, "Maximize window, step by step"},
    {"snap_cycle_down", Windows, "Minimize window, step by step"},
    {"move_left", Windows, "Move window left"},
    {"move_right", Windows, "Move window right"},
    {"move_up", Windows, "Move window up"},
    {"move_down", Windows, "Move window down"},
    {"resize_left", Windows, "Resize toward the left"},
    {"resize_right", Windows, "Resize toward the right"},
    {"resize_up", Windows, "Resize upward"},
    {"resize_down", Windows, "Resize downward"},

    {"focus_left", Focus, "Focus the window to the left"},
    {"focus_right", Focus, "Focus the window to the right"},
    {"focus_up", Focus, "Focus the window above"},
    {"focus_down", Focus, "Focus the window below"},
    {"focus_last", Focus, "Focus the previous window"},
    {"focus_urgent", Focus, "Focus the window asking for attention"},
    {"cycle", Focus, "Raise the next window"},
    {"switcher", Focus, "Switch windows"},
    {"switcher_prev", Focus, "Switch windows, backward"},
    {"switcher_app", Focus, "Switch between this application's windows"},
    {"switcher_app_prev", Focus, "Switch this application's windows, backward"},
    {"switcher_confirm", Focus, "Pick the switcher's window"},
    {"switcher_cancel", Focus, "Leave the switcher"},
    {"toggle_overview", Focus, "Overview of windows"},
    {"overview_confirm", Focus, "Pick the overview's window"},
    {"overview_cancel", Focus, "Leave the overview"},
    {"peek", Focus, "Peek at the desktop while held"},
    {"peek_toggle", Focus, "Peek at the desktop"},

    {"workspace", Workspaces, "Go to a workspace"},
    {"move_to_workspace", Workspaces, "Move window to a workspace"},
    {"workspace_next", Workspaces, "Next workspace"},
    {"workspace_prev", Workspaces, "Previous workspace"},
    {"workspace_back", Workspaces, "Back to the previous workspace"},
    {"move_to_workspace_next", Workspaces, "Move window to the next workspace"},
    {"move_to_workspace_prev", Workspaces, "Move window to the previous workspace"},
    {"move_to_workspace_empty", Workspaces, "Move window to an empty workspace"},
    {"move_workspace_to_output", Workspaces, "Move the workspace to another monitor"},
    {"swap_workspaces", Workspaces, "Swap workspaces with another monitor"},

    {"toggle_tiling", Tiling, "Toggle tiling"},
    {"layout_next", Tiling, "Next tiling layout"},
    {"layout_prev", Tiling, "Previous tiling layout"},
    {"layout_dwindle", Tiling, "Layout: dwindle"},
    {"layout_master", Tiling, "Layout: master and stack"},
    {"layout_spiral", Tiling, "Layout: spiral"},
    {"layout_monocle", Tiling, "Layout: monocle"},
    {"layout_scroll", Tiling, "Layout: scrolling columns"},
    {"promote", Tiling, "Promote window to master"},
    {"focus_next", Tiling, "Focus the next tile"},
    {"focus_prev", Tiling, "Focus the previous tile"},
    {"swap_next", Tiling, "Swap with the next tile"},
    {"swap_prev", Tiling, "Swap with the previous tile"},
    {"master_grow", Tiling, "Grow the master area"},
    {"master_shrink", Tiling, "Shrink the master area"},
    {"master_more", Tiling, "One more window in the master area"},
    {"master_less", Tiling, "One fewer window in the master area"},
    {"split_toggle", Tiling, "Dwindle: toggle the split, side by side or stacked"},
    {"split_swap", Tiling, "Dwindle: swap the split's two halves"},
    {"pseudo_toggle", Tiling, "Dwindle: pseudotile the window at its floating size"},

    {"scroll_left", Scrolling, "Focus the column to the left"},
    {"scroll_right", Scrolling, "Focus the column to the right"},
    {"column_widen", Scrolling, "Widen the column"},
    {"column_narrow", Scrolling, "Narrow the column"},
    {"column_cycle_width", Scrolling, "Step the column through its widths"},
    {"consume_left", Scrolling, "Stack the window into the column to the left"},
    {"consume_right", Scrolling, "Stack the window into the column to the right"},
    {"expel", Scrolling, "Take the window out into a column of its own"},
    {"center_column", Scrolling, "Center the column"},

    {"group_toggle", Groups, "Make a tab group, or dissolve it"},
    {"group_next", Groups, "Next tab"},
    {"group_prev", Groups, "Previous tab"},
    {"ungroup", Groups, "Take the window out"},
    {"group_merge_left", Groups, "Join the group to the left"},
    {"group_merge_right", Groups, "Join the group to the right"},
    {"group_merge_up", Groups, "Join the group above"},
    {"group_merge_down", Groups, "Join the group below"},

    {"palette", Shell, "Command palette"},
    {"shortcuts", Shell, "Keyboard shortcuts"},
    {"taskbar_focus", Shell, "The taskbar from the keyboard"},
    {"notification_history", Shell, "Notifications"},
    {"dnd_toggle", Shell, "Do not disturb"},
    {"dnd_on", Shell, "Do not disturb on"},
    {"dnd_off", Shell, "Do not disturb off"},
    {"clipboard_history", Shell, "Clipboard history"},
    {"emoji_picker", Shell, "Emoji picker"},
    {"display_settings", Shell, "Display settings"},
    {"desktop_clock", Shell, "Show or hide the desktop clock"},

    {"screenshot", Screen, "Screenshot"},
    {"zoom_in", Screen, "Zoom in"},
    {"zoom_out", Screen, "Zoom out"},
    {"zoom_reset", Screen, "Zoom back to normal"},
    {"night_light_toggle", Screen, "Night light"},
    {"night_light_on", Screen, "Night light on"},
    {"night_light_off", Screen, "Night light off"},
    {"night_light_auto", Screen, "Night light on its schedule"},
    {"display_off", Screen, "Turn the monitors off"},
    {"display_on", Screen, "Turn the monitors on"},
    {"display_toggle", Screen, "Turn the monitors off or on"},
    {"display_mode", Screen, "Display mode"},
    {"profile_auto", Screen, "Follow sunrise and sunset"},

    {"volume_up", Media, "Volume up"},
    {"volume_down", Media, "Volume down"},
    {"volume_mute", Media, "Mute the sound"},
    {"mic_mute", Media, "Mute the microphone"},
    {"brightness_up", Media, "Brightness up"},
    {"brightness_down", Media, "Brightness down"},
    {"media_play_pause", Media, "Play or pause"},
    {"media_next", Media, "Next track"},
    {"media_previous", Media, "Previous track"},
    {"media_stop", Media, "Stop playing"},

    {"lock", Session, "Lock the screen"},
    {"power_menu", Session, "Power menu"},
    {"suspend", Session, "Suspend"},
    {"hibernate", Session, "Hibernate"},
    {"reboot", Session, "Restart"},
    {"poweroff", Session, "Power off"},
    {"logout", Session, "Log out"},
    {"keep_awake_toggle", Session, "Keep awake, or not"},
    {"keep_awake_on", Session, "Keep awake"},
    {"keep_awake_off", Session, "Let the screen sleep again"},
    {"quit", Session, "Quit paw"},
    {"reload", Session, "Reload configuration"},
    {"mode", Session, "Binding mode"},
    {"switch_layout", Session, "Next keyboard layout"},
    {"toggle_shortcuts_inhibit", Session, "Let the window have the shortcuts, or take them back"},

    {"shake", Toys, "Shake the window"},
    {"gravity", Toys, "Let the windows fall"},
    {"party", Toys, "Throw a party"},
    {"cat", Toys, "The desktop cat"},
    {"cursor_trail", Toys, "Cursor trail"},
    {"xray", Toys, "Exploded view of the layers"},
    {"heatmap", Toys, "Pointer heatmap"},
};

// What the families of actions that differ only in their way are called together, when bindings
// alike but for their keys show as one row.
struct Family {
    const char *prefix, *title;
};
constexpr Family families[] = {
    {"focus_", "Focus the window that way"},
    {"move_", "Move the window that way"},
    {"snap_cycle_", "Snap the window that way, step by step"},
    {"resize_", "Resize the window that way"},
    {"group_merge_", "Join the group that way"},
    {"scroll_", "Focus the column that way"},
    {"consume_", "Stack the window into the column that way"},
};

// The name of each action, by its value.
const QHash<int, QString> &actionNames() {
    static const QHash<int, QString> names = [] {
        QHash<int, QString> map;
        for (const auto &name : paw::config_action_names())
            map.insert(paw::parse_action(name), QString::fromStdString(name));
        return map;
    }();
    return names;
}

const Action *find(const QString &name) {
    for (const auto &action : actions)
        if (name == action.name)
            return &action;
    return nullptr;
}

// "left", "right", "up" or "down" when `name` ends with one after `_`, else "".
QString way(const QString &name) {
    for (const char *end : {"_left", "_right", "_up", "_down"})
        if (name.endsWith(end))
            return QString(end).sliced(1);
    return {};
}

QString capitalized(QString text) {
    if (!text.isEmpty())
        text[0] = text[0].toUpper();
    return text;
}

// A program and its arguments as a line short enough for a row: the program without its folder,
// and of `sh -c SCRIPT`, the script.
QString shortCommand(const paw::Command &command) {
    QStringList words;
    for (const auto &word : command)
        words << QString::fromStdString(word);
    if (words.isEmpty())
        return {};
    words[0] = words[0].section('/', -1);
    static const QStringList shells{"sh", "bash", "dash", "zsh", "fish", "ksh"};
    if (words.size() >= 3 && shells.contains(words[0]) && words[1] == "-c")
        words = words.mid(2, 1);
    auto line = words.join(' ').simplified();
    constexpr qsizetype most = 44;
    if (line.size() > most)
        line = line.first(most - 1).trimmed() + "…";
    return line;
}

// Where a button binding acts: over the windows its app_id matches, the desktop, or both. A
// pattern that only lists app IDs (^kitty$, ^(kitty|foot)$) names them; any other is quoted.
QString buttonPlace(const paw::Binding &binding) {
    if (binding.app_id.empty())
        return binding.desktop ? "Over the desktop" : "";
    auto pattern = QString::fromStdString(binding.app_id);
    QString names;
    static const QRegularExpression plain(R"(^\^?\(?([A-Za-z0-9._-]+(?:\|[A-Za-z0-9._-]+)*)\)?\$?$)");
    if (const auto match = plain.match(pattern); match.hasMatch()) {
        auto list = match.captured(1).split('|');
        for (auto &name : list)
            name.remove('\\');
        const auto last = list.takeLast();
        names = list.isEmpty() ? last : list.join(", ") + " or " + last;
    } else {
        names = "“" + pattern + "”";
    }
    return "Over " + names + " windows" + (binding.desktop ? " and the desktop" : "");
}

// What a binding of action `name` does, by its argument.
QString title(const paw::Binding &binding, const QString &name, const paw::Config &config) {
    switch (binding.action) {
    case SH_WORKSPACE:
    case SH_MOVE_TO_WORKSPACE: {
        // "3", or "3: mail" for a named one.
        auto label = QString::number(binding.workspace);
        const auto index = static_cast<size_t>(binding.workspace) - 1;
        if (index < config.workspace_names.size() && !config.workspace_names[index].empty())
            label += ": " + QString::fromStdString(config.workspace_names[index]);
        return (binding.action == SH_WORKSPACE ? "Go to workspace " : "Move window to workspace ") +
               label;
    }
    case SH_SCREENSHOT:
        return binding.screenshot == SH_SCREENSHOT_OUTPUT   ? "Screenshot of the screen"
               : binding.screenshot == SH_SCREENSHOT_WINDOW ? "Screenshot of the window"
                                                            : "Screenshot of a region";
    case SH_MODE:
        if (binding.mode > 0 && static_cast<size_t>(binding.mode) <= config.modes.size())
            return "Enter the " +
                   QString::fromStdString(config.modes[static_cast<size_t>(binding.mode) - 1].name) +
                   " mode";
        return "Leave the mode";
    case SH_DISPLAY_MODE:
        switch (binding.mode) {
        case SH_DISPLAY_MODE_EXTEND: return "Extend the desktop across the monitors";
        case SH_DISPLAY_MODE_DUPLICATE: return "Duplicate the screen";
        case SH_DISPLAY_MODE_INTERNAL: return "Built-in screen only";
        case SH_DISPLAY_MODE_EXTERNAL: return "Other monitors only";
        default: return "Choose a display mode";
        }
    case SH_DISPLAY_OFF:
    case SH_DISPLAY_ON:
    case SH_DISPLAY_TOGGLE:
        if (!binding.output.empty()) {
            const auto output = QString::fromStdString(binding.output);
            return binding.action == SH_DISPLAY_OFF  ? "Turn " + output + " off"
                   : binding.action == SH_DISPLAY_ON ? "Turn " + output + " on"
                                                     : "Turn " + output + " off or on";
        }
        break;
    case SH_MOVE_WORKSPACE_TO_OUTPUT:
    case SH_SWAP_WORKSPACES: {
        const auto output = QString::fromStdString(binding.output);
        const auto where = output == "left" || output == "right" || output == "next" ||
                                   output == "prev"
                               ? (output == "prev" ? QString("the previous") : "the " + output) +
                                     " monitor"
                               : output;
        return (binding.action == SH_SWAP_WORKSPACES ? "Swap workspaces with "
                                                     : "Move the workspace to ") +
               where;
    }
    case SH_SWITCH_LAYOUT:
        return binding.layout == 0    ? "Next keyboard layout"
               : binding.layout == -1 ? "Previous keyboard layout"
                                      : QString("Keyboard layout %1").arg(binding.layout);
    case SH_CAT:
        return binding.mode == SH_CAT_ON    ? "Let the desktop cat out"
               : binding.mode == SH_CAT_OFF ? "Send the desktop cat away"
                                            : "The desktop cat, out or away";
    case SH_CURSOR_TRAIL: {
        // A kind is SH_CURSOR_TRAIL_OFF plus its sh_trail_kind, none being off.
        const int kind = binding.mode - int{SH_CURSOR_TRAIL_OFF};
        return binding.mode == SH_CURSOR_TRAIL_NEXT ? "Next cursor trail"
               : kind == int{SH_TRAIL_SPARKLES}     ? "Sparkles behind the pointer"
               : kind == int{SH_TRAIL_PETALS}       ? "Sakura petals behind the pointer"
               : kind == int{SH_TRAIL_HEARTS}       ? "Hearts behind the pointer"
               : kind == int{SH_TRAIL_NONE}         ? "Cursor trail off"
                                                    : "Cursor trail, on or off";
    }
    case SH_HEATMAP:
        return binding.mode == SH_HEATMAP_MOTION   ? "Heatmap of where the pointer rested"
               : binding.mode == SH_HEATMAP_CLICKS ? "Heatmap of the clicks"
               : binding.mode == SH_HEATMAP_RESET  ? "Forget the heatmap"
                                                   : "Pointer heatmap";
    default:
        break;
    }
    if (const auto palette = Palette::actionTitle(name); !palette.isEmpty()) {
        // Under its group's heading, the palette's "Group: next tab" is "Next tab".
        return palette.startsWith("Group: ") ? capitalized(palette.sliced(7)) : palette;
    }
    if (const auto *action = find(name))
        return action->title;
    return capitalized(QString(name).replace('_', ' '));
}

// The words a search finds a modifier by, besides its name.
QString modifierWords(const QString &modifier) {
    if (modifier == "Super")
        return "super win windows logo meta cmd command ⌘ ⊞";
    if (modifier == "Ctrl")
        return "ctrl control ⌃";
    if (modifier == "Alt")
        return "alt option ⌥";
    return "shift ⇧";
}

// A row of the sheet while it is made, before it is a map for QML.
struct Row {
    Group group = Other;
    QString title, detail;
    QStringList mods, keys;
    bool range = false, button = false, command = false;
    // "tap" for a modifier key's binding that runs as it comes up alone (Super_L, release),
    // "on release" for another key's that runs as it comes up.
    QString when;
    // Rows of one family (focus_left and focus_right; workspace 1 and 2) with the same modifiers
    // become one; `number` is a workspace's, `digit` its key's, for a run of them.
    QString family;
    int first = 0, number = 0, digit = -1;
    bool joined = false;
    QStringList actions; // the actions of the rows it is made of
    QStringList words; // more to find it by: the keys' names, the action's name, the command
};

// The modifier a modifier key is (SH_LOGO for Super_L), 0 for another key.
uint32_t modifierKey(uint32_t keysym) {
    switch (keysym) {
    case XKB_KEY_Super_L:
    case XKB_KEY_Super_R:
    case XKB_KEY_Meta_L:
    case XKB_KEY_Meta_R: return SH_LOGO;
    case XKB_KEY_Control_L:
    case XKB_KEY_Control_R: return SH_CTRL;
    case XKB_KEY_Alt_L:
    case XKB_KEY_Alt_R: return SH_ALT;
    case XKB_KEY_Shift_L:
    case XKB_KEY_Shift_R: return SH_SHIFT;
    default: return 0;
    }
}

QString buttonName(uint32_t button) {
    switch (button) {
    case BTN_LEFT: return "Left click";
    case BTN_RIGHT: return "Right click";
    case BTN_MIDDLE: return "Middle click";
    case BTN_SIDE: return "Side button";
    case BTN_EXTRA: return "Extra button";
    case BTN_FORWARD: return "Forward button";
    case BTN_BACK: return "Back button";
    case SH_WHEEL_UP: return "Wheel up";
    case SH_WHEEL_DOWN: return "Wheel down";
    default: return QString("Button %1").arg(button);
    }
}

Row row(const paw::Binding &binding, const paw::Config &config, bool macos) {
    Row row;
    const auto name = actionNames().value(binding.action);
    const auto *action = find(name);
    row.group = action ? action->group : Other;
    if (!binding.description.empty()) {
        row.title = QString::fromStdString(binding.description).simplified();
    } else if (binding.action == SH_SPAWN) {
        row.title = shortCommand(binding.command);
        row.command = true;
    } else {
        row.title = title(binding, name, config);
    }
    if (binding.action == SH_SPAWN)
        for (const auto &word : binding.command)
            row.words << QString::fromStdString(word);
    row.words << name << QString(name).replace('_', ' ');
    row.actions << name;
    // A modifier key bound itself (Super_L tapped for the launcher) shows as its modifier's cap.
    uint32_t held = binding.modifiers;
    const uint32_t own = binding.button || binding.switch_type >= 0 ? 0 : modifierKey(binding.keysym);
    held |= own;
    // As the taskbar style writes them, Super + Ctrl + Alt + Shift, or a Mac, ⌃ ⌥ ⇧ ⌘.
    using Modifier = std::pair<uint32_t, const char *>;
    static constexpr Modifier taskbar[] = {
        {SH_LOGO, "Super"}, {SH_CTRL, "Ctrl"}, {SH_ALT, "Alt"}, {SH_SHIFT, "Shift"}};
    static constexpr Modifier mac[] = {
        {SH_CTRL, "Ctrl"}, {SH_ALT, "Alt"}, {SH_SHIFT, "Shift"}, {SH_LOGO, "Super"}};
    for (const auto &[bit, modifier] : macos ? mac : taskbar)
        if (held & bit)
            row.mods << modifier;
    if (binding.switch_type >= 0) {
        row.button = true;
        row.keys << (binding.switch_type == SH_SWITCH_LID
                         ? binding.switch_on ? "Lid closed" : "Lid opened"
                     : binding.switch_on ? "Tablet mode on"
                                         : "Tablet mode off");
    } else if (binding.button) {
        row.button = true;
        row.keys << buttonName(binding.button);
        row.detail = buttonPlace(binding);
    } else {
        if (!own)
            row.keys << shortcuts::keyName(binding.keysym, macos);
        if (binding.release)
            row.when = own ? "tap" : "on release";
        char raw[64];
        if (xkb_keysym_get_name(binding.keysym, raw, sizeof(raw)) > 0)
            row.words << raw;
        // Bindings that differ only in their key: the ways of one family, and workspaces whose
        // numbers and keys' digits run on together; not those that run as their key comes up.
        const bool plain = binding.description.empty() && !binding.release;
        if (const auto ways = way(name); !ways.isEmpty() && plain) {
            for (const auto &family : families)
                if (name.startsWith(family.prefix) && name.sliced(qstrlen(family.prefix)) == ways)
                    row.family = family.prefix;
        } else if ((binding.action == SH_WORKSPACE || binding.action == SH_MOVE_TO_WORKSPACE) &&
                   binding.keysym >= XKB_KEY_0 && binding.keysym <= XKB_KEY_9 && plain) {
            row.family = name;
            row.first = row.number = binding.workspace;
            row.digit = static_cast<int>(binding.keysym - XKB_KEY_0);
        }
    }
    return row;
}

// Whether `next` carries on the run of rows `last` ends, which then become one.
bool joins(const Row &last, const Row &next) {
    if (next.family.isEmpty() || next.family != last.family || next.mods != last.mods)
        return false;
    // Each way once: Super and the arrows, then Super and H, J, K and L, are two rows.
    if (next.number == 0)
        return !last.actions.contains(next.actions.first());
    // Workspace 10 is on 0, after 9.
    return next.number == last.number + 1 && next.digit == (last.digit + 1) % 10 && last.digit != 0;
}

void join(Row &last, const Row &next) {
    if (!last.joined)
        last.words << last.title;
    last.joined = true;
    last.words << next.title << next.words;
    last.actions << next.actions;
    if (next.number == 0) {
        last.keys << next.keys;
        for (const auto &family : families)
            if (last.family == family.prefix)
                last.title = family.title;
    } else {
        // The first workspace's key and the last's.
        last.keys = QStringList{last.keys.first(), next.keys.first()};
        last.range = true;
        last.number = next.number;
        last.digit = next.digit;
        last.title = QString(last.family == "workspace" ? "Go to workspace %1–%2"
                                                        : "Move window to workspace %1–%2")
                         .arg(last.first)
                         .arg(last.number);
    }
}

QVariantMap map(const Row &row, const QString &group) {
    QStringList search{row.title, row.detail, group};
    for (const auto &modifier : row.mods)
        search << modifier << modifierWords(modifier);
    search << row.keys << row.when << row.words;
    return {{"title", row.title},     {"detail", row.detail},   {"mods", row.mods},
            {"keys", row.keys},       {"range", row.range},     {"button", row.button},
            {"command", row.command}, {"when", row.when},       {"search", search.join(' ').toLower()}};
}

// The bindings as rows, one family's run as one.
std::vector<Row> rowsOf(const std::vector<paw::Binding> &bindings, const paw::Config &config,
                        bool macos) {
    std::vector<Row> list;
    for (const auto &binding : bindings) {
        if (binding.action == SH_NONE)
            continue;
        auto next = row(binding, config, macos);
        if (!list.empty() && list.back().group == next.group && joins(list.back(), next))
            join(list.back(), next);
        else
            list.push_back(std::move(next));
    }
    return list;
}
} // namespace

QString shortcuts::keyName(uint32_t keysym, bool macos) {
    struct Name {
        uint32_t keysym;
        const char *taskbar, *macos;
    };
    static constexpr Name names[] = {
        {XKB_KEY_Return, "Enter", "Return"},
        {XKB_KEY_KP_Enter, "Enter", "Enter"},
        {XKB_KEY_Escape, "Esc", "Esc"},
        {XKB_KEY_space, "Space", "Space"},
        {XKB_KEY_Tab, "Tab", "Tab"},
        {XKB_KEY_ISO_Left_Tab, "Tab", "Tab"},
        {XKB_KEY_BackSpace, "Backspace", "Delete"},
        {XKB_KEY_Delete, "Del", "⌦"},
        {XKB_KEY_Insert, "Ins", "Insert"},
        {XKB_KEY_Home, "Home", "Home"},
        {XKB_KEY_End, "End", "End"},
        {XKB_KEY_Prior, "PgUp", "Page Up"},
        {XKB_KEY_Next, "PgDn", "Page Down"},
        {XKB_KEY_Left, "←", "←"},
        {XKB_KEY_Right, "→", "→"},
        {XKB_KEY_Up, "↑", "↑"},
        {XKB_KEY_Down, "↓", "↓"},
        {XKB_KEY_Print, "PrtSc", "Print"},
        {XKB_KEY_Pause, "Pause", "Pause"},
        {XKB_KEY_Scroll_Lock, "ScrLk", "Scroll Lock"},
        {XKB_KEY_Caps_Lock, "Caps Lock", "Caps Lock"},
        {XKB_KEY_Menu, "Menu", "Menu"},
        {XKB_KEY_XF86AudioRaiseVolume, "Volume up", "Volume up"},
        {XKB_KEY_XF86AudioLowerVolume, "Volume down", "Volume down"},
        {XKB_KEY_XF86AudioMute, "Mute", "Mute"},
        {XKB_KEY_XF86AudioMicMute, "Mic mute", "Mic mute"},
        {XKB_KEY_XF86MonBrightnessUp, "Brightness up", "Brightness up"},
        {XKB_KEY_XF86MonBrightnessDown, "Brightness down", "Brightness down"},
        {XKB_KEY_XF86AudioPlay, "Play", "Play"},
        {XKB_KEY_XF86AudioPause, "Pause", "Pause"},
        {XKB_KEY_XF86AudioNext, "Next track", "Next track"},
        {XKB_KEY_XF86AudioPrev, "Previous track", "Previous track"},
        {XKB_KEY_XF86AudioStop, "Stop", "Stop"},
        {XKB_KEY_XF86Display, "Display", "Display"},
        {XKB_KEY_XF86PowerOff, "Power", "Power"},
        {XKB_KEY_XF86WWW, "Browser", "Browser"},
    };
    for (const auto &name : names)
        if (name.keysym == keysym)
            return QString::fromUtf8(macos ? name.macos : name.taskbar);
    char raw[64];
    const QString xkb = xkb_keysym_get_name(keysym, raw, sizeof(raw)) > 0 ? raw : "";
    // A key that types something shows it, a letter in capitals; the keypad's with "Num".
    if (const auto character = xkb_keysym_to_utf32(keysym); character > 0x20 && character != 0x7f) {
        const char32_t text[] = {character};
        const auto shown = QString::fromUcs4(text, 1).toUpper();
        return xkb.startsWith("KP_") ? "Num " + shown : shown;
    }
    // XF86TouchpadToggle: Touchpad toggle.
    if (xkb.startsWith("XF86") && xkb.size() > 4) {
        QString words;
        for (const auto c : xkb.sliced(4)) {
            if (c.isUpper() && !words.isEmpty())
                words += ' ';
            words += words.isEmpty() ? c : c.toLower();
        }
        return words;
    }
    return xkb.isEmpty() ? QString("Key %1").arg(keysym) : xkb;
}

QVariantList shortcuts::sheet(const paw::Config &config, const QString &mode, bool macos) {
    QVariantList groups;
    auto add = [&groups](const QString &name, bool inMode, const std::vector<Row> &rows,
                         auto wanted) {
        QVariantList list;
        for (const auto &row : rows)
            if (wanted(row))
                list.push_back(map(row, name));
        if (!list.isEmpty())
            groups.push_back(QVariantMap{{"name", name}, {"mode", inMode}, {"rows", list}});
    };
    const int number = mode.isEmpty() ? 0 : config.mode_number(mode.toStdString());
    if (number > 0) {
        const auto &bindings = config.modes[static_cast<size_t>(number) - 1].bindings;
        add(capitalized(mode) + " mode", true, rowsOf(bindings, config, macos),
            [](const Row &) { return true; });
    }
    const auto list = rowsOf(config.bindings, config, macos);
    for (int group = Windows; group <= Other; ++group)
        add(groupNames[group], false, list,
            [group](const Row &row) { return row.group == group; });
    return groups;
}

QVariantList shortcuts::filter(const QVariantList &sheet, const QString &query) {
    const auto words = query.toLower().split(' ', Qt::SkipEmptyParts);
    if (words.isEmpty())
        return sheet;
    QVariantList groups;
    for (const auto &item : sheet) {
        auto group = item.toMap();
        QVariantList kept;
        for (const auto &row : group["rows"].toList()) {
            const auto search = row.toMap()["search"].toString();
            if (std::all_of(words.begin(), words.end(),
                            [&](const QString &word) { return search.contains(word); }))
                kept.push_back(row);
        }
        if (!kept.isEmpty()) {
            group["rows"] = kept;
            groups.push_back(group);
        }
    }
    return groups;
}

int shortcuts::rows(const QVariantList &sheet) {
    int count = 0;
    for (const auto &group : sheet)
        count += static_cast<int>(group.toMap()["rows"].toList().size());
    return count;
}

Shortcuts::Shortcuts(ShellController &controller) : QObject(&controller), controller_(controller) {
    // Open, it follows the configuration and the binding mode in use.
    connect(&controller, &ShellController::configChanged, this, [this] {
        if (!output_.isEmpty())
            refresh();
    });
    connect(&controller, &ShellController::bindingModeChanged, this, [this] {
        if (!output_.isEmpty())
            refresh();
    });
}

void Shortcuts::toggle(const QString &output) {
    if (output.isEmpty())
        return;
    if (output == output_) {
        close();
        return;
    }
    output_ = output;
    query_.clear();
    refresh(); // before the view shows, so it opens at its full size
    Q_EMIT queryChanged();
    Q_EMIT openChanged();
}

void Shortcuts::close() {
    if (output_.isEmpty())
        return;
    output_.clear();
    Q_EMIT openChanged();
}

void Shortcuts::setQuery(const QString &query) {
    if (query == query_)
        return;
    query_ = query;
    Q_EMIT queryChanged();
    search();
}

void Shortcuts::preview(const paw::Config &config, const QString &mode) {
    mode_ = mode;
    sheet_ = shortcuts::sheet(config, mode, controller_.style() == "macos");
    standIn_ = true;
    search();
}

void Shortcuts::refresh() {
    if (!standIn_) {
        mode_ = controller_.bindingMode();
        sheet_ = shortcuts::sheet(controller_.config(), mode_, controller_.style() == "macos");
    }
    search();
}

void Shortcuts::search() {
    groups_ = shortcuts::filter(sheet_, query_);
    Q_EMIT groupsChanged();
}
