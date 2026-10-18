// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "launch_history.hpp"
#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <vector>

// What the run launcher (run_launcher.hpp) finds for what is typed, apart from the shell's models:
// its rows; the programs on $PATH and the commands run before, by frecency, for its Run mode; the
// open windows for its Windows mode; and what the text is beside a search (a calculation, a path,
// an address). Its Apps mode ranks the applications as the start menu does
// (StartMenu::searchApps).
namespace run_search {

// A row of the launcher's list.
struct Row {
    // What it is: app, command (one run before), program (on $PATH, or a path to one), window,
    // folder, file, url or calc.
    QString kind;
    QString title, subtitle, icon;
    // What choosing it starts or opens: a desktop id, a command line, a task id, a path, an
    // address, the number a calculation copies.
    QString target;
    // The places in the title of the letters the query matched (fuzzy::matches), to show them.
    QList<int> matches;
    // The focused window, which choosing leaves as it is.
    bool current = false;
    double score = 0;
    QString key() const { return kind + '\n' + target; }
    bool operator==(const Row &) const = default;
};

// How often and how lately a command was run, as zoxide weighs it: how many times, times 4 when
// it last was within the hour, 2 within the day, a half within the week and a quarter after.
double frecency(const LaunchHistory::Entry &entry, const QDateTime &now);

// The programs in `folders` (the directories of $PATH, in its order): the names of the executable
// files in them, each once, sorted. An empty folder, the current one to the shell, is left out.
QStringList programs(const QStringList &folders);

// The Run mode's rows for `query`, at most `limit`: the commands of `history` that match all of
// its words, and the programs (`programs`, sorted) whose names match its first word, each with the
// rest of the query after it, its arguments (with arguments, only the program it names exactly);
// best first, a command's frecency lifting it a little, and a program left out where a command
// is the same. With nothing typed, the commands by frecency, then every program by name. A
// program's icon is `icons`' for its name (an installed application's), else a terminal's.
std::vector<Row> commands(const QString &query, const QList<LaunchHistory::Entry> &history,
                          const QStringList &programs, const QHash<QString, QString> &icons,
                          const QDateTime &now, int limit = 100);

// An open window, as the Windows mode lists it: its task (TaskModel's taskId), title, the name of
// its application, its icon, its workspace (0 when not known) and its state.
struct Window {
    int task = 0;
    QString title, app, icon;
    int workspace = 0;
    bool active = false, urgent = false, minimized = false;
};
// The Windows mode's rows: the windows whose title or application matches `query`, best first, as
// the palette ranks them; with nothing typed every window, those asking for attention first and
// the focused one last, the one least likely wanted.
std::vector<Row> windows(const QString &query, const QList<Window> &windows);

// The rows that go before what a search finds, for what `query` is besides words to search for:
// a calculation's value (calculator::entry); a path that is there (starting with /, ~/ or ~ alone,
// `home` being ~) to open, or to run when it is a program, then up to 6 in its folder whose names
// start with its last part, folders first (hidden ones only for a last part starting with a dot);
// or an address (http://, https:// or www.) to open in the browser.
std::vector<Row> special(const QString &query, const QString &home);

// The command line running `command` through sh in the terminal the terminal action starts
// (`terminal`, its command line), and after it `shell` (the user's), so that the window stays for
// what the command said. Each terminal is told the program to run its own way, as paw showcase
// tells it: after its options (kitty, foot), after `start --` (wezterm), after `--` (GNOME's) or
// after `-e` (the rest).
QStringList inTerminal(const QStringList &terminal, const QString &command, const QString &shell);

// The program `command` runs when it names one that is not among `programs` (sorted), for the
// launcher to say so rather than start a shell that fails: its first word, when that is a plain
// name, not a path, an assignment or anything the shell has itself (cd, exec, if, ...). Empty when
// the program is there, or the command is the shell's to make sense of.
QString missingProgram(const QString &command, const QStringList &programs);

// `text` as Qt's styled text, the letters at `places` bold and in `color`, for a row's title.
QString markup(const QString &text, const QList<int> &places, const QString &color);

} // namespace run_search
