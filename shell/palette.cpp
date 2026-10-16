// SPDX-License-Identifier: GPL-3.0-or-later
#include "palette.hpp"
#include "calculator.hpp"
#include "controller.hpp"
#include "fuzzy.hpp"
#include "web_search.hpp"
#include <QClipboard>
#include <QGuiApplication>
#include <QHash>

namespace {
struct Action {
    const char *name, *title;
};
// The compositor actions the palette offers, run as `paw msg NAME` (with its argument).
constexpr Action actions[] = {
    {"terminal", "Open a terminal"},
    {"toggle_tiling", "Toggle tiling"},
    {"layout_next", "Next tiling layout"},
    {"layout_dwindle", "Layout: dwindle"},
    {"layout_master", "Layout: master and stack"},
    {"layout_spiral", "Layout: spiral"},
    {"layout_monocle", "Layout: monocle"},
    {"layout_scroll", "Layout: scrolling columns"},
    {"toggle_floating", "Toggle floating"},
    {"fullscreen", "Toggle fullscreen"},
    {"maximize", "Maximize window"},
    {"restore", "Restore window"},
    {"minimize", "Minimize window"},
    {"snap_left", "Snap window left"},
    {"snap_right", "Snap window right"},
    {"snap_top_left", "Snap window to the top-left quarter"},
    {"snap_top_right", "Snap window to the top-right quarter"},
    {"snap_bottom_left", "Snap window to the bottom-left quarter"},
    {"snap_bottom_right", "Snap window to the bottom-right quarter"},
    {"close", "Close window"},
    {"promote", "Promote window to master"},
    {"split_toggle", "Dwindle: toggle the split, side by side or stacked"},
    {"split_swap", "Dwindle: swap the split's two halves"},
    {"pseudo_toggle", "Dwindle: pseudotile the window at its floating size"},
    {"focus_last", "Focus the previous window"},
    {"switcher_app", "Switch between this application's windows"},
    {"focus_urgent", "Focus the window asking for attention"},
    {"toggle_sticky", "Toggle sticky window"},
    {"toggle_above", "Keep window above others, or not"},
    {"move_to_scratchpad", "Hide window in the scratchpad"},
    {"scratchpad_show", "Show the scratchpad"},
    {"group_toggle", "Group: make a tab group, or dissolve it"},
    {"group_next", "Group: next tab"},
    {"group_prev", "Group: previous tab"},
    {"ungroup", "Group: take the window out"},
    {"swallow_toggle", "Swallow the terminal this window came from, or let it out"},
    {"toggle_overview", "Overview of windows"},
    {"peek_toggle", "Peek at the desktop"},
    {"pick_color", "Pick a colour from the screen"},
    {"keep_awake_toggle", "Keep awake, or not"},
    {"workspace_back", "Back to the previous workspace"},
    {"move_to_workspace_next", "Move window to the next workspace"},
    {"move_to_workspace_prev", "Move window to the previous workspace"},
    {"move_to_workspace_empty", "Move window to an empty workspace"},
    {"launcher", "Applications menu"},
    {"shortcuts", "Keyboard shortcuts"},
    {"display_settings", "Display settings"},
    {"desktop_clock", "Show or hide the desktop clock"},
    {"wallpaper_next", "Next wallpaper"},
    {"wallpaper_prev", "Previous wallpaper"},
    {"reload", "Reload configuration"},
    {"shake", "Shake the window"},
    {"gravity", "Let the windows fall"},
    {"party", "Throw a party"},
    {"ambient sakura", "Ambient: sakura petals over the wallpaper"},
    {"ambient snow", "Ambient: soft snow over the wallpaper"},
    {"ambient fireflies", "Ambient: fireflies over the wallpaper"},
    {"ambient off", "Ambient: off"},
    {"cursor_trail sparkles", "Cursor trail: sparkles"},
    {"cursor_trail petals", "Cursor trail: sakura petals"},
    {"cursor_trail hearts", "Cursor trail: hearts"},
    {"cursor_trail off", "Cursor trail: off"},
};
// The actions an empty search lists, the commonest, in this order; the rest are a word away, and
// a lone > lists them all. A new action joins them only when it is as common, so that the list
// stays short and the power actions after them stay in it.
constexpr const char *common[] = {
    "terminal",        "launcher",         "toggle_overview", "toggle_tiling", "layout_next",
    "toggle_floating", "fullscreen",       "close",           "pick_color",    "keep_awake_toggle",
    "shortcuts",       "display_settings", "reload",
};
// How many applications an empty search lists.
constexpr qsizetype homeApps = 8;

QVariantMap entry(const QString &kind, const QString &title, const QString &subtitle,
                  const QString &icon, const QString &target, int number = 0, bool urgent = false) {
    return {{"kind", kind}, {"title", title}, {"subtitle", subtitle}, {"icon", icon},
            {"target", target}, {"number", number}, {"urgent", urgent}};
}

// The files found among the entries ranked, each before the first that matched less well, at most
// `limit` in all.
QVariantList merged(const QVariantList &ranked, const QVariantList &files, int limit = 60) {
    QVariantList list;
    qsizetype i = 0, j = 0;
    while (list.size() < limit && (i < ranked.size() || j < files.size())) {
        const bool file = j < files.size() &&
                          (i == ranked.size() || files[j].toMap().value("score").toDouble() >
                                                     ranked[i].toMap().value("score").toDouble());
        list.push_back(file ? files[j++] : ranked[i++]);
    }
    return list;
}
} // namespace

QString Palette::actionTitle(const QString &name) {
    for (const auto &action : actions)
        if (name == action.name)
            return action.title;
    return {};
}

Palette::Palette(ShellController &controller) : QObject(&controller), controller_(controller) {
    // Files read while it is open join what it shows.
    connect(controller.files(), &FileIndex::changed, this, [this] {
        if (!output_.isEmpty())
            refreshKeepingSelection();
    });
}

void Palette::refreshKeepingSelection() {
    // Someone who has already moved down the list keeps the entry they are on.
    const auto before = selected_ > 0 && selected_ < results_.size() ? results_[selected_].toMap()
                                                                      : QVariantMap{};
    refreshResults();
    for (int i = 0; !before.isEmpty() && i < results_.size(); ++i) {
        const auto item = results_[i].toMap();
        if (item["kind"] == before["kind"] && item["target"] == before["target"]) {
            selected_ = i;
            Q_EMIT selectedChanged();
            break;
        }
    }
}

void Palette::open(const QString &output) {
    if (output.isEmpty())
        return;
    output_ = output;
    query_.clear();
    sessions_.clear();
    collect();
    selected_ = 0;
    refreshResults(); // before the view shows, so it opens at its full size
    Q_EMIT queryChanged();
    Q_EMIT openChanged();
    // Saved sessions come from the compositor; they join the list when it answers.
    controller_.ask("session list\n", [this](const QByteArray &reply) {
        QVariantList sessions;
        const auto lines = reply.split('\n');
        if (lines.isEmpty() || lines[0] != "ok")
            return;
        for (int i = 1; i < lines.size(); ++i) {
            const auto fields = lines[i].split('\t');
            if (fields.size() != 3)
                continue;
            const auto name = QString::fromUtf8(fields[0]);
            const auto count = fields[1].toInt();
            sessions.push_back(QVariantMap{{"name", name}, {"windows", count}});
        }
        sessions_ = sessions;
        if (!output_.isEmpty()) {
            collect();
            refreshKeepingSelection();
        }
    });
}

void Palette::close() {
    if (output_.isEmpty())
        return;
    output_.clear();
    Q_EMIT openChanged();
}

void Palette::collect() {
    entries_ = entries(controller_.tasks());
    for (const auto &app : controller_.apps()) {
        const auto map = app.toMap();
        entries_.push_back(entry("app", map["name"].toString(), "Application",
                                 map["icon"].toString(), map["appId"].toString()));
    }
}

QVariantList Palette::entries(QObject *windows) const {
    QVariantList entries;
    // The windows' roles by name, so that a stand-in model serves as well as TaskModel.
    auto *tasks = qobject_cast<QAbstractItemModel *>(windows);
    const auto roles = tasks ? tasks->roleNames() : QHash<int, QByteArray>();
    auto value = [&](int row, const QByteArray &role) {
        return tasks->data(tasks->index(row, 0), roles.key(role, -1));
    };
    // Windows asking for attention come first, so an empty query offers them at the top.
    QVariantList urgent, others;
    for (int row = 0; tasks && row < tasks->rowCount(); ++row) {
        const auto appId = value(row, "appId").toString();
        const bool asking = value(row, "urgent").toBool();
        const auto detail = appId.isEmpty() ? QString("Window") : "Window · " + appId;
        (asking ? urgent : others)
            .push_back(entry("window", value(row, "title").toString(),
                             asking ? detail + " · needs attention" : detail,
                             controller_.iconFor(appId), QString::number(value(row, "taskId").toInt()),
                             value(row, "active").toBool() ? 1 : 0, asking));
    }
    entries += urgent;
    entries += others;
    for (const auto &item : sessions_) {
        const auto map = item.toMap();
        const auto name = map["name"].toString();
        const auto count = map["windows"].toInt();
        const auto detail = QString("Session · %1 window%2").arg(count).arg(count == 1 ? "" : "s");
        entries.push_back(entry("session", "Restore session " + name, detail, "document-open",
                                "session restore " + name));
        entries.push_back(entry("session", "Restore session " + name + " and launch missing apps",
                                detail, "document-open", "session restore " + name + " launch"));
    }
    for (int number = 1; number <= controller_.workspaceCount(); ++number) {
        const auto names = controller_.workspaceNames();
        const auto label = names.value(number - 1);
        const auto title = label.isEmpty() ? QString("Workspace %1").arg(number)
                                           : QString("Workspace %1: %2").arg(number).arg(label);
        entries.push_back(entry("workspace", title, "Switch workspace", "user-desktop",
                                QString::number(number), number));
        entries.push_back(entry("workspace", "Move window to " + title.toLower(),
                                "Move the focused window", "go-jump",
                                "move_to_workspace " + QString::number(number)));
    }
    for (const auto &name : controller_.profiles())
        entries.push_back(entry("action", "Appearance: " + name,
                                name == controller_.profile() ? "Profile · in use" : "Profile",
                                "preferences-desktop-theme", "profile " + name));
    if (controller_.hasProfileSchedule())
        entries.push_back(entry("action", "Appearance: follow sunrise and sunset",
                                controller_.profileScheduled() ? "Profile schedule · in charge"
                                                               : "Profile schedule",
                                "preferences-desktop-theme", "profile_auto"));
    for (const auto &action : actions)
        entries.push_back(entry("action", action.title, QString("Action · ") + action.name,
                                "system-run", action.name));
    // The power actions that may run; power off, restart and log out ask first, as from the
    // panel.
    static const QMap<QString, QString> powerIcons{
        {"lock", "system-lock-screen"}, {"suspend", "system-suspend"},
        {"hibernate", "system-suspend-hibernate"}, {"reboot", "system-reboot"},
        {"poweroff", "system-shutdown"}, {"logout", "system-log-out"}};
    for (const auto &action : controller_.power()->available()) {
        auto item = entry("action", Power::title(action), "Action · " + action,
                          powerIcons.value(action, "system-run"), action);
        item["power"] = true;
        entries.push_back(item);
    }
    return entries;
}

QVariantList Palette::home() const {
    QVariantList windows, sessions, workspaces, profiles, power;
    QHash<QString, QVariant> apps, actions;
    for (const auto &item : entries_) {
        const auto map = item.toMap();
        const auto kind = map.value("kind").toString();
        const auto target = map.value("target").toString();
        if (kind == "window")
            windows.push_back(item);
        else if (kind == "session" && target.count(' ') == 2) // not "... and launch missing apps"
            sessions.push_back(item);
        else if (kind == "workspace" && map.value("number").toInt() > 0) // not "Move window to"
            workspaces.push_back(item);
        else if (kind == "app")
            apps.insert(target, item);
        else if (map.value("power").toBool())
            power.push_back(item);
        else if (target.startsWith("profile ") || target == "profile_auto")
            profiles.push_back(item);
        else if (kind == "action")
            actions.insert(target, item);
    }
    auto list = windows + sessions;
    // The applications launched lately, then those pinned to the start menu, then by name.
    auto *start = controller_.startMenu();
    QStringList listed;
    for (const auto &records : {start->recent(), start->pinned(), start->apps()})
        for (const auto &record : records) {
            const auto id = record.toMap().value("appId").toString();
            if (listed.size() < homeApps && apps.contains(id) && !listed.contains(id)) {
                listed.push_back(id);
                list.push_back(apps[id]);
            }
        }
    list += workspaces;
    list += profiles;
    for (const char *name : common)
        if (actions.contains(name))
            list.push_back(actions[name]);
    return list + power;
}

void Palette::refreshResults() {
    const auto typed = query_.trimmed();
    // A calculation's value comes first; a leading = asks for nothing else, and a leading / for
    // files alone. A few files otherwise go among the rest by how well they match. A filter's
    // prefix alone lists every entry of its kind, and nothing typed the sections of home().
    const auto calc = calculator::entry(query_);
    if (typed.isEmpty()) {
        results_ = home();
    } else if (typed.startsWith('=')) {
        results_.clear();
    } else if (typed.startsWith('/')) {
        results_ = controller_.files()->search(typed.sliced(1), 60);
    } else if (QString(">@#%").contains(typed[0])) {
        results_ = fuzzy::rank(entries_, query_,
                               typed.size() == 1 ? static_cast<int>(entries_.size()) : 60);
    } else {
        results_ = merged(fuzzy::rank(entries_, query_), controller_.files()->search(typed, 5));
    }
    // Spotlight lists what it found by kind, each under its heading, after the top hit.
    if (controller_.style() == "macos" && !typed.isEmpty() && results_.size() > 2) {
        QVariantList grouped{results_.first()};
        QStringList kinds;
        for (qsizetype i = 1; i < results_.size(); ++i)
            if (const auto kind = results_[i].toMap().value("kind").toString(); !kinds.contains(kind))
                kinds.push_back(kind);
        for (const auto &kind : kinds)
            for (qsizetype i = 1; i < results_.size(); ++i)
                if (results_[i].toMap().value("kind") == kind)
                    grouped.push_back(results_[i]);
        results_ = grouped;
    }
    if (!calc.isEmpty())
        results_.prepend(calc);
    // Searching the web for the words comes after what was found.
    if (const auto web = web_search::entry(controller_.webSearch(), query_); !web.isEmpty())
        results_.push_back(web);
    // Saving the arrangement under the name typed is offered last, once the text can be a name.
    if (fuzzy::validSessionName(typed) && !typed.startsWith('>') && !typed.startsWith('@') &&
        !typed.startsWith('#') && !typed.startsWith('%'))
        results_.push_back(entry("session", "Save session as " + typed,
                                 "Session · keep this arrangement of windows", "document-save",
                                 "session save " + typed));
    selected_ = 0;
    Q_EMIT resultsChanged();
    Q_EMIT selectedChanged();
}

void Palette::setQuery(const QString &query) {
    if (query == query_)
        return;
    query_ = query;
    Q_EMIT queryChanged();
    refreshResults();
}

void Palette::setSelected(int selected) {
    if (results_.isEmpty() || selected < 0 || selected >= results_.size() || selected == selected_)
        return;
    selected_ = selected;
    Q_EMIT selectedChanged();
}

void Palette::move(int delta) {
    if (results_.isEmpty())
        return;
    selected_ = ((selected_ + delta) % results_.size() + results_.size()) % results_.size();
    Q_EMIT selectedChanged();
}

void Palette::activate(int index) {
    if (index < 0)
        index = selected_;
    if (index < 0 || index >= results_.size())
        return;
    const auto item = results_[index].toMap();
    const auto output = output_;
    close(); // hand the keyboard back before the request takes effect
    run(item, output);
}

void Palette::reveal(int index) {
    if (index < 0)
        index = selected_;
    if (index < 0 || index >= results_.size())
        return;
    const auto item = results_[index].toMap();
    if (item["kind"] != "file")
        return activate(index);
    close();
    openFolder(item);
}

void Palette::openFolder(const QVariantMap &item) {
    if (item["kind"] == "file")
        controller_.openFile(item["target"].toString(), true);
}

void Palette::run(const QVariantMap &item, const QString &output) {
    const auto kind = item["kind"].toString();
    const auto target = item["target"].toString();
    if (kind == "window") {
        if (item["number"].toInt() == 0) // the focused one would minimize on a second click
            controller_.tasks()->activate(target.toInt());
    } else if (kind == "calc") {
        QGuiApplication::clipboard()->setText(target);
    } else if (kind == "file") {
        controller_.openFile(target);
    } else if (kind == "web") {
        controller_.openUrl(target);
    } else if (item["power"].toBool()) {
        controller_.power()->request(target, output);
    } else if (kind == "app") {
        controller_.launch(target);
    } else if (kind == "workspace" && target.toInt() > 0) {
        controller_.showWorkspace(output, target.toInt());
    } else if (kind == "workspace" || kind == "action" || kind == "session") {
        controller_.send(target);
    }
}
