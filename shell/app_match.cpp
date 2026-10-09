// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_match.hpp"
#include <QFileInfo>
#include <QHash>
#include <QProcess>
#include <QRegularExpression>
#include <QStringList>
#include <algorithm>
#include <functional>

namespace app_match {

QString trimmed(const QString &name) {
    static const QStringList extensions{".desktop", ".exe", ".appimage", ".sh", ".py"};
    static const QStringList suffixes{
        "-wrapped", "-bin",     "-desktop", "-stable",      "-beta",     "-nightly", "-git",
        "-browser", "-wayland", "-x11",     "-url-handler", "-unstable", "-canary",  "-insiders",
        "-x86_64",  "_x86_64",  ".x86_64",  "-amd64",       "-linux"};
    static const QRegularExpression version(R"([-_ ]v?\d+(\.\d+)*$)");
    auto text = name.trimmed().toLower();
    while (text.startsWith('.'))
        text.remove(0, 1);
    for (bool changed = true; changed;) {
        changed = false;
        for (const auto &list : {extensions, suffixes})
            for (const auto &suffix : list)
                if (text.size() > suffix.size() && text.endsWith(suffix)) {
                    text.chop(suffix.size());
                    changed = true;
                }
        if (const auto match = version.match(text); match.hasMatch() && match.capturedStart() > 0) {
            text.truncate(match.capturedStart());
            changed = true;
        }
    }
    return text;
}

QString key(const QString &name) {
    auto text = trimmed(name);
    text.removeIf([](QChar c) { return !c.isLetterOrNumber(); });
    return text;
}

QString program(const QString &exec) {
    static const QStringList wrappers{"env",           "exec",        "nice",     "ionice",
                                      "prime-run",     "gamemoderun", "mangohud", "optirun",
                                      "primusrun",     "pkexec",      "sudo",     "firejail",
                                      "flatpak-spawn", "systemd-run", "uwsm-app"};
    static const QRegularExpression assignment(R"(^[A-Za-z_][A-Za-z0-9_]*=)");
    const auto words = QProcess::splitCommand(exec);
    for (qsizetype i = 0; i < words.size(); ++i) {
        const auto &word = words[i];
        const auto file = QFileInfo(word).fileName();
        // A variable, or a wrapper's option or its value (nice -n 10).
        if (assignment.match(word).hasMatch() || word.startsWith('-') ||
            std::all_of(word.begin(), word.end(), [](QChar c) { return c.isDigit(); }))
            continue;
        if (wrappers.contains(file))
            continue;
        if ((file == "sh" || file == "bash" || file == "zsh" || file == "dash") &&
            i + 2 < words.size() && words[i + 1] == "-c")
            return program(words[i + 2]);
        if (file == "flatpak") {
            QString application;
            for (qsizetype j = i + 1; j < words.size(); ++j) {
                if (words[j].startsWith("--command="))
                    return QFileInfo(words[j].mid(10)).fileName();
                if (!words[j].startsWith('-') && !words[j].startsWith('@') &&
                    !words[j].startsWith('%') && words[j] != "run" && application.isEmpty())
                    application = words[j];
            }
            return application;
        }
        return file;
    }
    return {};
}

Index::Index(const QList<Entry> &entries) {
    static const QRegularExpression steamGame(R"(steam://rungameid/(\d+))");
    for (const auto &entry : entries) {
        Keys keys;
        keys.id = entry.id;
        keys.base = entry.id.endsWith(".desktop") ? entry.id.chopped(8) : entry.id;
        // A reverse-DNS name's parts: org.gnome.Calculator's Calculator and gnomecalculator.
        const auto parts = keys.base.split('.');
        if (parts.size() >= 2)
            keys.last = parts.last();
        if (parts.size() >= 3) {
            keys.lastKey = key(keys.last);
            keys.lastTwoKey = key(parts[parts.size() - 2] + parts.last());
        }
        keys.wmClass = entry.wmClass;
        keys.steamGame = steamGame.match(entry.exec).captured(1);
        keys.baseKey = key(keys.base);
        keys.wmClassKey = key(entry.wmClass);
        keys.programKey = key(program(entry.exec));
        keys.nameKey = key(entry.name);
        entries_.push_back(std::move(keys));
    }
}

QString Index::find(const QString &appId) const {
    if (auto id = findOne(appId); !id.isEmpty())
        return id;
    for (const auto &alias : aliases(appId))
        if (auto id = findOne(alias); !id.isEmpty())
            return id;
    return {};
}

QString Index::findOne(const QString &appId) const {
    if (appId.isEmpty())
        return {};
    static const QRegularExpression steamApp(R"(^steam_app_(\d+)$)",
                                             QRegularExpression::CaseInsensitiveOption);
    const auto steamGame = steamApp.match(appId).captured(1);
    const auto appKey = key(appId);
    // The last part of a reverse-DNS app id, unless it says nothing of the application.
    static const QStringList vague{"desktop",  "app", "application", "client", "main",
                                   "launcher", "gui", "qt",          "gtk",    "electron"};
    const auto parts = appId.split('.');
    auto appLastKey = parts.size() >= 3 ? key(parts.last()) : QString();
    if (appLastKey.size() < 3 || vague.contains(appLastKey))
        appLastKey.clear();
    auto same = [](const QString &a, const QString &b) { return !a.isEmpty() && a == b; };
    const std::function<bool(const Keys &)> matches[] = {
        [&](const Keys &e) { return e.base == appId; },
        [&](const Keys &e) { return e.base.compare(appId, Qt::CaseInsensitive) == 0; },
        [&](const Keys &e) {
            return !e.wmClass.isEmpty() && e.wmClass.compare(appId, Qt::CaseInsensitive) == 0;
        },
        [&](const Keys &e) { return same(steamGame, e.steamGame); },
        [&](const Keys &e) {
            return !e.last.isEmpty() && e.last.compare(appId, Qt::CaseInsensitive) == 0;
        },
        [&](const Keys &e) { return same(appKey, e.baseKey) || same(appKey, e.wmClassKey); },
        [&](const Keys &e) { return same(appKey, e.programKey); },
        [&](const Keys &e) { return same(appKey, e.lastTwoKey) || same(appKey, e.lastKey); },
        [&](const Keys &e) {
            return same(appLastKey, e.baseKey) || same(appLastKey, e.lastKey) ||
                   same(appLastKey, e.programKey);
        },
        [&](const Keys &e) { return appKey.size() >= 3 && same(appKey, e.nameKey); },
    };
    for (const auto &match : matches)
        for (const auto &entry : entries_)
            if (match(entry))
                return entry.id;
    return {};
}

QStringList aliases(const QString &appId) {
    static const QHash<QString, QStringList> known{
        {"steamwebhelper", {"steam"}},
        {"soffice", {"libreoffice-startcenter", "libreoffice"}},
        {"libreoffice", {"libreoffice-startcenter"}},
        {"gnome-terminal-server", {"org.gnome.Terminal", "gnome-terminal"}},
        {"virtualbox manager", {"virtualbox"}},
        {"virtualbox machine", {"virtualbox"}},
        {"virtualboxvm", {"virtualbox"}},
        {"jetbrains-studio", {"android-studio"}},
    };
    return known.value(appId.toLower());
}

QStringList iconGuesses(const QString &appId) {
    QStringList guesses;
    // A path is no icon name, and may be a file the application wrote.
    if (appId.isEmpty() || appId.contains('/'))
        return guesses;
    auto add = [&guesses](const QString &name) {
        if (!name.isEmpty() && !guesses.contains(name))
            guesses << name;
    };
    // Steam installs a game's icon under this name as it makes its shortcut.
    static const QRegularExpression steamApp(R"(^steam_app_(\d+)$)",
                                             QRegularExpression::CaseInsensitiveOption);
    if (const auto game = steamApp.match(appId).captured(1); !game.isEmpty())
        add("steam_icon_" + game);
    add(appId);
    add(appId.toLower());
    if (const auto parts = appId.split('.'); parts.size() >= 3) {
        add(parts.last());
        add(parts.last().toLower());
    }
    add(trimmed(appId));
    for (const auto &alias : aliases(appId))
        add(alias);
    return guesses;
}

} // namespace app_match
