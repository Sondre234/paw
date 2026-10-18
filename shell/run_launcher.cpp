// SPDX-License-Identifier: GPL-3.0-or-later
#include "run_launcher.hpp"
#include "controller.hpp"
#include "fuzzy.hpp"
#include "state_files.hpp"
#include <QClipboard>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QRegularExpression>
#include <algorithm>

namespace {
// How many rows it lists at most: typing finds the rest.
constexpr int limit = 100;

// `path` as sh reads it: as it is when nothing in it means something to sh, else quoted.
QString quoted(const QString &path) {
    static const QRegularExpression plain("^[A-Za-z0-9_@%+=:,./-]+$");
    if (plain.match(path).hasMatch())
        return path;
    return "'" + QString(path).replace("'", "'\\''") + "'";
}
} // namespace

QVariant RunResults::data(const QModelIndex &index, int role) const {
    const auto *found = at(index);
    if (!found)
        return {};
    switch (role) {
    case Kind: return found->kind;
    case Title: return found->title;
    case Subtitle: return found->subtitle;
    case Icon: return found->icon;
    case Target: return found->target;
    case Matches: {
        QVariantList places;
        for (int place : found->matches)
            places.push_back(place);
        return places;
    }
    case Current: return found->current;
    default: return {};
    }
}

QHash<int, QByteArray> RunResults::roleNames() const {
    return {{Kind, "kind"},       {Title, "title"},   {Subtitle, "subtitle"}, {Icon, "icon"},
            {Target, "target"},   {Matches, "matches"}, {Current, "current"}};
}

RunLauncher::RunLauncher(ShellController &controller, QString stateDir)
    : QObject(&controller), controller_(controller),
      history_((stateDir.isEmpty() ? ::stateDir() : stateDir) + "/run-history") {
    connect(&controller, &ShellController::appsChanged, this, [this] {
        programIcons_.clear();
        if (!output_.isEmpty())
            refresh();
    });
}

const QStringList &RunLauncher::programs() {
    if (previewOnly_ || programsChecked_)
        return programs_;
    programsChecked_ = true;
    const auto folders = qEnvironmentVariable("PATH").split(':');
    QList<QDateTime> times;
    for (const auto &folder : folders)
        times.push_back(QFileInfo(folder).lastModified());
    if (folders != folders_ || times != folderTimes_) {
        programs_ = run_search::programs(folders);
        folders_ = folders;
        folderTimes_ = times;
    }
    return programs_;
}

const QHash<QString, QString> &RunLauncher::programIcons() {
    if (!programIcons_.isEmpty())
        return programIcons_;
    // By the program each runs, then, for one whose program says nothing (a wrapper's, a
    // flatpak's), by its desktop id's last part (org.kde.dolphin's dolphin).
    const auto apps = controller_.apps();
    for (const auto &app : apps) {
        const auto record = app.toMap();
        const auto program = record["program"].toString();
        if (!program.isEmpty() && !programIcons_.contains(program))
            programIcons_.insert(program, record["icon"].toString());
    }
    for (const auto &app : apps) {
        const auto record = app.toMap();
        auto id = record["appId"].toString();
        if (record["configured"].toBool())
            continue;
        if (id.endsWith(".desktop"))
            id.chop(8);
        id = id.section('.', -1).toLower();
        if (!id.isEmpty() && !programIcons_.contains(id))
            programIcons_.insert(id, record["icon"].toString());
    }
    return programIcons_;
}

std::vector<run_search::Row> RunLauncher::apps() const {
    auto *start = controller_.startMenu();
    const auto parts = fuzzy::words(query_);
    auto found = start->searchApps(query_);
    // With nothing typed, those launched most and lately first, by the frecency their launches
    // have as a command's would; the others after them by name.
    if (parts.isEmpty()) {
        const auto now = QDateTime::currentDateTimeUtc();
        QHash<QString, double> weights;
        for (const auto &record : start->recent()) {
            const auto app = record.toMap();
            weights.insert(app["appId"].toString(),
                           run_search::frecency({app["appId"].toString(), app["launches"].toInt(),
                                                 app["launched"].toDateTime()},
                                                now));
        }
        std::stable_sort(found.begin(), found.end(), [&weights](const QVariant &a, const QVariant &b) {
            return weights.value(a.toMap()["appId"].toString()) > weights.value(b.toMap()["appId"].toString());
        });
    }
    std::vector<run_search::Row> rows;
    for (const auto &item : found) {
        if (int(rows.size()) == limit)
            break;
        const auto app = item.toMap();
        const auto name = app["name"].toString();
        rows.push_back({"app", name, app["subtitle"].toString(), app["icon"].toString(),
                        app["appId"].toString(), fuzzy::matches(parts, name), false,
                        app["score"].toDouble()});
    }
    return rows;
}

QAbstractItemModel *RunLauncher::windowSource() const {
    return windows_ ? windows_.data() : controller_.tasks();
}

QList<run_search::Window> RunLauncher::windows() const {
    QList<run_search::Window> list;
    auto *tasks = windowSource();
    // By the roles' names, as the palette reads them.
    const auto roles = tasks->roleNames();
    auto value = [&](int row, const QByteArray &role) {
        return tasks->data(tasks->index(row, 0), roles.key(role, -1));
    };
    for (int row = 0; row < tasks->rowCount(); ++row) {
        const auto appId = value(row, "appId").toString();
        const auto name = controller_.appRecord(controller_.appFor(appId)).toMap()["name"].toString();
        const int task = value(row, "taskId").toInt();
        list.push_back({task, value(row, "title").toString(), name.isEmpty() ? appId : name,
                        controller_.iconFor(appId), value(row, "workspace").toInt(), task == focused_,
                        value(row, "urgent").toBool(), value(row, "minimized").toBool()});
    }
    return list;
}

void RunLauncher::refresh() {
    std::vector<run_search::Row> rows;
    if (mode_ != Windows)
        rows = run_search::special(query_, QDir::homePath());
    std::vector<run_search::Row> found;
    if (mode_ == Apps)
        found = apps();
    else if (mode_ == Run) {
        const auto now = QDateTime::currentDateTimeUtc();
        found = run_search::commands(query_, history_.entries(), programs(), programIcons(), now,
                                     limit);
        // A command says when it last ran, as the start menu says it of an application.
        for (auto &row : found)
            if (const auto *entry = history_.find(row.target); entry && row.kind == "command")
                row.subtitle += " · " + controller_.startMenu()->ago(entry->last, now);
    } else
        found = run_search::windows(query_, windows());
    rows.insert(rows.end(), found.begin(), found.end());
    if (int(rows.size()) > limit)
        rows.resize(limit);
    results_.set(rows);
    selected_ = 0;
    Q_EMIT selectedChanged();
}

void RunLauncher::setQuery(const QString &query) {
    if (query == query_)
        return;
    query_ = query;
    setProblem({});
    Q_EMIT queryChanged();
    if (!output_.isEmpty())
        refresh();
}

void RunLauncher::setMode(int mode) {
    if (mode < Apps || mode > Windows || mode == mode_)
        return;
    mode_ = mode;
    setProblem({});
    Q_EMIT modeChanged();
    if (!output_.isEmpty())
        refresh();
}

void RunLauncher::setSelected(int selected) {
    if (selected < 0 || selected >= results_.count() || selected == selected_)
        return;
    selected_ = selected;
    Q_EMIT selectedChanged();
}

void RunLauncher::setProblem(const QString &problem) {
    if (problem == problem_)
        return;
    problem_ = problem;
    Q_EMIT problemChanged();
}

void RunLauncher::toggle(const QString &output) {
    if (output.isEmpty())
        return;
    if (output == output_) {
        close();
        return;
    }
    output_ = output;
    query_.clear();
    mode_ = Apps;
    problem_.clear();
    programsChecked_ = false;
    // The window focused now, before the launcher takes the keyboard from it.
    focused_ = -1;
    auto *tasks = windowSource();
    const auto roles = tasks->roleNames();
    for (int row = 0; row < tasks->rowCount(); ++row)
        if (tasks->data(tasks->index(row, 0), roles.key("active", -1)).toBool())
            focused_ = tasks->data(tasks->index(row, 0), roles.key("taskId", -1)).toInt();
    refresh(); // before the view shows, so that it opens at its full size
    Q_EMIT queryChanged();
    Q_EMIT modeChanged();
    Q_EMIT problemChanged();
    Q_EMIT openChanged();
}

void RunLauncher::close() {
    if (output_.isEmpty())
        return;
    output_.clear();
    Q_EMIT openChanged();
}

void RunLauncher::move(int delta) {
    const int count = results_.count();
    if (count == 0)
        return;
    selected_ = ((selected_ + delta) % count + count) % count;
    Q_EMIT selectedChanged();
}

void RunLauncher::cycle(int step) {
    setMode(((mode_ + step) % 3 + 3) % 3);
}

bool RunLauncher::activate(int index) {
    const auto *row = results_.row(index < 0 ? selected_ : index);
    if (!row)
        return mode_ != Windows && runTyped();
    const auto kind = row->kind, target = row->target;
    if (kind == "command" || kind == "program")
        return runCommand(kind == "program" && target.startsWith('/') ? quoted(target) : target, false);
    const bool current = row->current;
    close(); // the keyboard goes back before what was chosen takes it
    if (kind == "app")
        controller_.launch(target);
    else if (kind == "window" && !current) { // the focused one would minimize
        auto *source = windowSource();
        if (source == controller_.tasks())
            controller_.tasks()->activate(target.toInt());
        else if (source->metaObject()->indexOfMethod("activate(QVariant)") >= 0)
            QMetaObject::invokeMethod(source, "activate", Q_ARG(QVariant, target.toInt()));
    }
    else if (kind == "folder" || kind == "file")
        controller_.openFile(target);
    else if (kind == "url")
        controller_.openUrl(target);
    else if (kind == "calc")
        QGuiApplication::clipboard()->setText(target);
    return true;
}

bool RunLauncher::runTyped() {
    return runCommand(query_, false);
}

bool RunLauncher::runInTerminal(int index) {
    const auto *row = results_.row(index < 0 ? selected_ : index);
    if (row && (row->kind == "command" || row->kind == "program"))
        return runCommand(row->target.startsWith('/') ? quoted(row->target) : row->target, true);
    return runCommand(query_, true);
}

bool RunLauncher::runCommand(const QString &command, bool terminal) {
    const auto line = command.trimmed();
    if (line.isEmpty())
        return false;
    if (const auto missing = run_search::missingProgram(line, programs()); !missing.isEmpty()) {
        setProblem("There is no program called “" + missing + "”");
        return false;
    }
    close();
    const auto name = line.section(' ', 0, 0);
    if (terminal) {
        // The terminal the terminal action starts, as the compositor has it.
        controller_.ask("get terminal\n", [this, line, name](const QByteArray &reply) {
            const auto lines = reply.split('\n');
            const auto words = lines.size() > 1 && lines[0] == "ok"
                                   ? QString::fromUtf8(lines[1]).split('\t', Qt::SkipEmptyParts)
                                   : QStringList();
            if (words.isEmpty()) {
                Q_EMIT failed("No terminal to run " + name +
                              " in: set terminal in the configuration, or install one");
                return;
            }
            controller_.startProgram(
                run_search::inTerminal(words, line, qEnvironmentVariable("SHELL", "/bin/sh")), name);
        });
    } else {
        controller_.startProgram({"/bin/sh", "-c", line}, name);
    }
    // A line its file cannot hold is run, not remembered.
    if (!line.contains('\n') && !line.contains('\t') && !history_.record(line))
        Q_EMIT failed("Could not save the commands run: " + history_.error());
    return true;
}

QString RunLauncher::markup(const QString &text, const QVariantList &places,
                            const QString &color) const {
    QList<int> list;
    for (const auto &place : places)
        list.push_back(place.toInt());
    return run_search::markup(text, list, color);
}

void RunLauncher::preview(const QList<LaunchHistory::Entry> &history, const QStringList &programs) {
    previewOnly_ = true;
    history_ = LaunchHistory();
    auto ordered = history;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto &a, const auto &b) { return a.last < b.last; });
    for (const auto &entry : ordered)
        for (int i = 0; i < entry.count; ++i)
            history_.record(entry.id, entry.last);
    programs_ = programs;
    programs_.sort();
}
