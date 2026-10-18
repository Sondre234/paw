// SPDX-License-Identifier: GPL-3.0-or-later
#include "run_search.hpp"
#include "calculator.hpp"
#include "fuzzy.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
// A program run from the launcher with no application of its own to stand for it.
const QString terminalIcon = "utilities-terminal,terminal,application-x-executable";

// How much a command's frecency lifts it among matches, as the start menu lifts an application
// launched often: never more than a close match's lead.
double lift(double frecency) {
    return std::min(4.0, std::log2(1.0 + frecency));
}

// The query's first word and the rest after it, its arguments.
std::pair<QString, QString> split(const QString &typed) {
    static const QRegularExpression space("\\s");
    const qsizetype at = typed.indexOf(space);
    if (at < 0)
        return {typed, {}};
    return {typed.first(at), typed.sliced(at).trimmed()};
}

// The rows' stable order by score, best first.
void rank(std::vector<run_search::Row> &rows) {
    std::stable_sort(rows.begin(), rows.end(),
                     [](const run_search::Row &a, const run_search::Row &b) { return a.score > b.score; });
}
} // namespace

double run_search::frecency(const LaunchHistory::Entry &entry, const QDateTime &now) {
    const qint64 age = entry.last.secsTo(now);
    const double weight = age < 3600 ? 4 : age < 86400 ? 2 : age < 7 * 86400 ? 0.5 : 0.25;
    return entry.count * weight;
}

QStringList run_search::programs(const QStringList &folders) {
    QSet<QString> seen;
    QStringList names;
    for (const auto &folder : folders) {
        if (folder.isEmpty())
            continue;
        DIR *dir = opendir(QFile::encodeName(folder).constData());
        if (!dir)
            continue;
        const int fd = dirfd(dir);
        while (const dirent *entry = readdir(dir)) {
            if (entry->d_name[0] == '.' || entry->d_type == DT_DIR)
                continue;
            // A link, or a file system that does not say, is looked at; a folder is no program.
            struct stat info;
            if (entry->d_type != DT_REG &&
                (fstatat(fd, entry->d_name, &info, 0) != 0 || !S_ISREG(info.st_mode)))
                continue;
            if (faccessat(fd, entry->d_name, X_OK, 0) != 0)
                continue;
            const auto name = QFile::decodeName(entry->d_name);
            if (!seen.contains(name)) {
                seen.insert(name);
                names.push_back(name);
            }
        }
        closedir(dir);
    }
    names.sort();
    return names;
}

std::vector<run_search::Row> run_search::commands(const QString &query,
                                                  const QList<LaunchHistory::Entry> &history,
                                                  const QStringList &programs,
                                                  const QHash<QString, QString> &icons,
                                                  const QDateTime &now, int limit) {
    const auto typed = query.trimmed();
    const auto parts = fuzzy::words(typed);
    const auto [first, arguments] = split(typed);
    auto iconOf = [&icons](const QString &command) {
        return icons.value(split(command).first, terminalIcon);
    };
    std::vector<Row> found;
    QSet<QString> commands;
    for (const auto &entry : history) {
        const double weight = frecency(entry, now);
        double score = weight;
        if (!parts.isEmpty()) {
            const double value = fuzzy::scoreWords(parts, entry.id);
            if (value < 0)
                continue;
            score = value + lift(weight);
        }
        commands.insert(entry.id);
        found.push_back({"command", entry.id,
                         entry.count == 1 ? QString("1 run") : QString("%1 runs").arg(entry.count),
                         iconOf(entry.id), entry.id, fuzzy::matches(parts, entry.id), false, score});
    }
    rank(found);
    // The programs come after the commands with nothing typed; else among them by how well they
    // match. With arguments typed, the program has been named.
    std::vector<Row> named;
    auto add = [&](const QString &name, double score, const QList<int> &matches) {
        const auto command = arguments.isEmpty() ? name : name + ' ' + arguments;
        if (!commands.contains(command))
            named.push_back({"program", command, "Program", icons.value(name, terminalIcon),
                             command, matches, false, score});
    };
    if (parts.isEmpty()) {
        for (const auto &name : programs) {
            if (int(found.size() + named.size()) >= limit)
                break;
            add(name, 0, {});
        }
    } else if (!arguments.isEmpty()) {
        if (std::binary_search(programs.begin(), programs.end(), first)) {
            QList<int> matches;
            for (int i = 0; i < first.size(); ++i)
                matches.push_back(i);
            add(first, fuzzy::scoreWords(parts.mid(0, 1), first), matches);
        }
    } else {
        for (const auto &name : programs)
            if (const double value = fuzzy::scoreWords(parts, name); value >= 0)
                add(name, value, {});
        rank(named);
        // Only the rows shown are told where their letters matched.
        for (size_t i = 0; i < named.size() && int(i) < limit; ++i)
            named[i].matches = fuzzy::matches(parts, named[i].title);
    }
    found.insert(found.end(), named.begin(), named.end());
    if (!parts.isEmpty())
        rank(found);
    if (int(found.size()) > limit)
        found.resize(size_t(limit));
    return found;
}

std::vector<run_search::Row> run_search::windows(const QString &query, const QList<Window> &windows) {
    const auto parts = fuzzy::words(query);
    std::vector<Row> found, urgent, focused;
    for (const auto &window : windows) {
        const auto title = window.title.isEmpty() ? window.app : window.title;
        QStringList details{window.app};
        if (window.workspace > 0)
            details << QString("workspace %1").arg(window.workspace);
        if (window.minimized)
            details << "minimized";
        if (window.urgent)
            details << "needs attention";
        details.removeAll(QString());
        Row row{"window", title, details.join(" · "), window.icon, QString::number(window.task),
                {}, window.active, 0};
        if (!parts.isEmpty()) {
            const double byTitle = fuzzy::scoreWords(parts, title);
            const double byApp = fuzzy::scoreWords(parts, window.app);
            if (byTitle < 0 && byApp < 0)
                continue;
            // As the palette ranks a title over its subtitle, and one asking for attention first.
            row.score = std::max(byTitle, byApp < 0 ? -1 : byApp * 0.6) + (window.urgent ? 3 : 0);
            if (byTitle >= 0)
                row.matches = fuzzy::matches(parts, title);
        }
        (parts.isEmpty() && window.urgent ? urgent : parts.isEmpty() && window.active ? focused : found)
            .push_back(row);
    }
    rank(found);
    urgent.insert(urgent.end(), found.begin(), found.end());
    urgent.insert(urgent.end(), focused.begin(), focused.end());
    return urgent;
}

std::vector<run_search::Row> run_search::special(const QString &query, const QString &home) {
    const auto typed = query.trimmed();
    std::vector<Row> rows;
    if (const auto calc = calculator::entry(typed); !calc.isEmpty()) {
        rows.push_back({"calc", calc["title"].toString(), calc["subtitle"].toString(),
                        calc["icon"].toString(), calc["target"].toString(), {}, false, 0});
        return rows;
    }
    static const QRegularExpression address("^(https?://|www\\.)\\S+$",
                                            QRegularExpression::CaseInsensitiveOption);
    if (address.match(typed).hasMatch()) {
        const bool bare = typed.startsWith("www.", Qt::CaseInsensitive);
        rows.push_back({"url", typed, "Open in the browser",
                        "web-browser,internet-web-browser,applications-internet",
                        bare ? "https://" + typed : typed, {}, false, 0});
        return rows;
    }
    if (!typed.startsWith('/') && !typed.startsWith("~/") && typed != "~")
        return rows;
    // The path as typed stays the title; what it names, with ~ for the home folder, the target.
    auto expand = [&home](const QString &path) {
        return path.startsWith('~') ? home + path.sliced(1) : path;
    };
    static const QMimeDatabase mime;
    auto row = [&](const QString &shown, const QString &path, const QList<int> &matches) {
        const QFileInfo info(path);
        if (info.isDir())
            return Row{"folder", shown, "Open the folder", "folder", path, matches, false, 0};
        if (info.isExecutable())
            return Row{"program", shown, "Run it", "application-x-executable", path, matches,
                       false, 0};
        const auto type = mime.mimeTypeForFile(path, QMimeDatabase::MatchExtension);
        return Row{"file", shown, "Open it with " + type.comment(),
                   type.iconName() + ',' + type.genericIconName(), path, matches, false, 0};
    };
    const auto exact = expand(typed);
    if (QFileInfo::exists(exact))
        rows.push_back(row(typed, QDir::cleanPath(exact), {}));
    // Those in its folder whose names start with the last part typed: a folder typed with its /
    // lists what is in it.
    const qsizetype slash = typed.lastIndexOf('/');
    const auto shownFolder = slash < 0 ? QString("~/") : typed.first(slash + 1);
    const auto start = slash < 0 ? QString() : typed.sliced(slash + 1);
    const auto folder = expand(shownFolder);
    struct Near {
        QString name;
        bool folder;
    };
    std::vector<Near> near;
    if (DIR *dir = opendir(QFile::encodeName(folder).constData())) {
        const int fd = dirfd(dir);
        while (const dirent *entry = readdir(dir)) {
            const auto name = QFile::decodeName(entry->d_name);
            if (name == "." || name == ".." || name == start ||
                (name.startsWith('.') && !start.startsWith('.')) ||
                !name.startsWith(start, Qt::CaseInsensitive))
                continue;
            bool isFolder = entry->d_type == DT_DIR;
            struct stat info;
            if (entry->d_type == DT_LNK || entry->d_type == DT_UNKNOWN)
                isFolder = fstatat(fd, entry->d_name, &info, 0) == 0 && S_ISDIR(info.st_mode);
            near.push_back({name, isFolder});
        }
        closedir(dir);
    }
    std::sort(near.begin(), near.end(), [](const Near &a, const Near &b) {
        if (a.folder != b.folder)
            return a.folder;
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
    QList<int> matches;
    for (qsizetype i = 0; i < start.size(); ++i)
        matches.push_back(int(shownFolder.size() + i));
    for (size_t i = 0; i < near.size() && i < 6; ++i)
        rows.push_back(row(shownFolder + near[i].name + (near[i].folder ? "/" : ""),
                           QDir(folder).filePath(near[i].name), matches));
    return rows;
}

QStringList run_search::inTerminal(const QStringList &terminal, const QString &command,
                                   const QString &shell) {
    if (terminal.isEmpty())
        return {};
    auto line = terminal;
    const auto name = QFileInfo(terminal.front()).fileName();
    if (name == "kitty" || name == "foot" || name == "footclient") {
        // These run what follows their options.
    } else if (name == "wezterm") {
        if (terminal.size() == 1)
            line << "start";
        line << "--";
    } else if (name == "gnome-terminal" || name == "kgx" || name == "ptyxis" ||
               name == "gnome-console") {
        line << "--";
    } else {
        line << "-e";
    }
    // The shell after the command on a line of its own, so that a comment ending the command
    // cannot swallow it.
    return line << "sh" << "-c" << command + "\nexec \"$0\"" << (shell.isEmpty() ? "sh" : shell);
}

QString run_search::missingProgram(const QString &command, const QStringList &programs) {
    static const QSet<QString> shells{
        ".", ":", "[", "alias", "bg", "break", "case", "cd", "command", "continue", "eval", "exec",
        "exit", "export", "false", "fc", "fg", "for", "getopts", "hash", "if", "jobs", "kill",
        "local", "pwd", "read", "readonly", "return", "set", "shift", "source", "test", "time",
        "times", "trap", "true", "type", "ulimit", "umask", "unalias", "unset", "until", "wait",
        "while", "echo", "printf"};
    static const QRegularExpression plain("^[A-Za-z0-9_+.,@-]+$");
    const auto first = split(command.trimmed()).first;
    if (first.isEmpty() || !plain.match(first).hasMatch() || shells.contains(first) ||
        std::binary_search(programs.begin(), programs.end(), first))
        return {};
    return first;
}

QString run_search::markup(const QString &text, const QList<int> &places, const QString &color) {
    auto escaped = [](QStringView part) {
        return part.toString().toHtmlEscaped();
    };
    QString result;
    qsizetype done = 0;
    for (qsizetype i = 0; i < places.size();) {
        const int start = places[i];
        if (start < done || start >= text.size()) {
            ++i;
            continue;
        }
        // A run of matched letters goes in one tag.
        int end = start + 1;
        while (++i < places.size() && places[i] == end && end < text.size())
            ++end;
        result += escaped(QStringView(text).sliced(done, start - done));
        result += "<b><font color=\"" + color + "\">" +
                  escaped(QStringView(text).sliced(start, end - start)) + "</font></b>";
        done = end;
    }
    return result + escaped(QStringView(text).sliced(done));
}
