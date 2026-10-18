// SPDX-License-Identifier: GPL-3.0-or-later
// The run launcher's search: the programs on $PATH, the commands run before by frecency, the
// windows, what the text typed is besides a search, and how a command runs in a terminal.
#include "run_search.hpp"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

namespace {
QDateTime at(qint64 seconds) { return QDateTime::fromSecsSinceEpoch(seconds, QTimeZone::UTC); }
const QDateTime now = at(10'000'000);
LaunchHistory::Entry ran(const QString &command, int count, qint64 secondsAgo) {
    return {command, count, now.addSecs(-secondsAgo)};
}
bool write(const QString &path, QFile::Permissions permissions = QFile::ReadOwner | QFile::WriteOwner) {
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("#!/bin/sh\n") > 0 &&
           file.setPermissions(permissions);
}
const auto executable = QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner;
QStringList titles(const std::vector<run_search::Row> &rows) {
    QStringList list;
    for (const auto &row : rows)
        list << row.title;
    return list;
}
QStringList kinds(const std::vector<run_search::Row> &rows) {
    QStringList list;
    for (const auto &row : rows)
        list << row.kind;
    return list;
}
} // namespace

class RunSearchTest : public QObject {
    Q_OBJECT
  private Q_SLOTS:
    void weighsFrecency() {
        // A run within the hour counts four times, within the day twice, within the week half
        // and after that a quarter.
        QCOMPARE(run_search::frecency(ran("a", 3, 60), now), 12.0);
        QCOMPARE(run_search::frecency(ran("a", 3, 7200), now), 6.0);
        QCOMPARE(run_search::frecency(ran("a", 4, 3 * 86400), now), 2.0);
        QCOMPARE(run_search::frecency(ran("a", 4, 30 * 86400), now), 1.0);
    }
    void listsPrograms() {
        QTemporaryDir root;
        const auto first = root.filePath("bin"), second = root.filePath("local/bin");
        QVERIFY(write(first + "/foo", executable));
        QVERIFY(write(first + "/notes.txt"));              // not executable
        QVERIFY(QDir().mkpath(first + "/folder"));          // a folder is no program
        QVERIFY(write(first + "/.hidden", executable));
        QVERIFY(QFile::link(first + "/foo", first + "/linked"));
        QVERIFY(QFile::link(first + "/folder", first + "/to-folder"));
        QVERIFY(write(second + "/foo", executable));        // the first folder's counts once
        QVERIFY(write(second + "/bar", executable));
        QCOMPARE(run_search::programs({first, "", root.filePath("gone"), second}),
                 (QStringList{"bar", "foo", "linked"}));
    }
    void ranksCommands() {
        const QList<LaunchHistory::Entry> history{ran("htop", 1, 60), ran("firefox --private-window", 9, 3 * 86400),
                                                  ran("git status", 30, 40 * 86400)};
        const QStringList programs{"firefox", "foot", "git", "htop", "vim"};
        const QHash<QString, QString> icons{{"firefox", "firefox"}};
        auto found = [&](const QString &query, int limit = 100) {
            return run_search::commands(query, history, programs, icons, now, limit);
        };
        // With nothing typed, the commands by frecency (git's 30 old runs count 7.5, firefox's 9
        // this week 4.5, htop's one this hour 4), then the programs by name, those run with
        // nothing more left out.
        QCOMPARE(titles(found("")), (QStringList{"git status", "firefox --private-window", "htop",
                                                  "firefox", "foot", "git", "vim"}));
        QCOMPARE(kinds(found("")).mid(2, 2), (QStringList{"command", "program"}));
        QCOMPARE(found("")[0].subtitle, QString("30 runs"));
        QCOMPARE(found("")[2].subtitle, QString("1 run"));
        QCOMPARE(titles(found("", 4)).size(), 4);
        // A word matches commands and programs alike, by how well, a command run often a little
        // ahead of as close a match.
        QCOMPARE(titles(found("fi")), (QStringList{"firefox --private-window", "firefox"}));
        QCOMPARE(found("fi")[0].matches, (QList<int>{0, 1}));
        QCOMPARE(titles(found("firefox")), (QStringList{"firefox", "firefox --private-window"}));
        QCOMPARE(titles(found("ht")), QStringList{"htop"});
        QCOMPARE(kinds(found("ht")), QStringList{"command"});
        // Arguments name the program: it alone, with them, unless that was run before.
        QCOMPARE(titles(found("vim notes.txt")), QStringList{"vim notes.txt"});
        QCOMPARE(found("vim notes.txt")[0].target, QString("vim notes.txt"));
        QCOMPARE(found("vim notes.txt")[0].matches, (QList<int>{0, 1, 2}));
        QCOMPARE(titles(found("git status")), QStringList{"git status"});
        QCOMPARE(kinds(found("git status")), QStringList{"command"});
        QCOMPARE(titles(found("gti status")), QStringList{});
        // An installed application's program has its icon; the others a terminal's.
        QCOMPARE(found("firefox")[0].icon, QString("firefox"));
        QVERIFY(found("vim")[0].icon.startsWith("utilities-terminal"));
        QVERIFY(found("xyz").empty());
    }
    void staysFastWithManyPrograms() {
        // Run mode ranks on every keystroke: a desktop's $PATH holds a few thousand programs, and
        // the history up to 200 commands.
        QStringList programs;
        for (int i = 0; i < 4000; ++i)
            programs << QString("program-%1-of-a-package").arg(i, 4, 10, QChar('0'));
        QList<LaunchHistory::Entry> history;
        for (int i = 0; i < 200; ++i)
            history << ran(QString("program-%1 --with some arguments").arg(i), i % 7 + 1, i * 3600);
        QElapsedTimer timer;
        timer.start();
        int found = 0;
        for (const auto *query : {"", "p", "pr", "prog", "prog 12", "program-0042"})
            found += int(run_search::commands(query, history, programs, {}, now).size());
        const auto perSearch = timer.elapsed() / 6.0;
        qInfo() << "a search of 4000 programs and 200 commands:" << perSearch << "ms";
        QVERIFY(found > 0);
        QVERIFY2(perSearch < 50, "Run mode is too slow to type against");
    }
    void ranksWindows() {
        const QList<run_search::Window> open{
            {1, "Release notes - Mozilla Firefox", "Firefox", "firefox", 1, true, false, false},
            {2, "~/dev/paw", "Foot", "foot", 2, false, false, false},
            {3, "", "Kitty", "kitty", 2, false, true, true}};
        // With nothing typed every window, the one asking for attention first and the focused
        // one last; a window without a title goes by its application's name.
        const auto all = run_search::windows("", open);
        QCOMPARE(titles(all), (QStringList{"Kitty", "~/dev/paw", "Release notes - Mozilla Firefox"}));
        QCOMPARE(all[0].subtitle, QString("Kitty · workspace 2 · minimized · needs attention"));
        QCOMPARE(all[2].target, QString("1"));
        QVERIFY(all[2].current && !all[0].current);
        // By title, or by application, which counts less and shows no letters.
        QCOMPARE(titles(run_search::windows("notes", open)), QStringList{"Release notes - Mozilla Firefox"});
        QCOMPARE(run_search::windows("paw", open)[0].matches, (QList<int>{6, 7, 8}));
        QCOMPARE(titles(run_search::windows("foot", open)), QStringList{"~/dev/paw"});
        QVERIFY(run_search::windows("foot", open)[0].matches.isEmpty());
        QVERIFY(run_search::windows("zzz", open).empty());
    }
    void findsWhatTheTextIs() {
        QTemporaryDir home;
        QVERIFY(QDir().mkpath(home.filePath("Documents")));
        QVERIFY(QDir().mkpath(home.filePath("Downloads")));
        QVERIFY(QDir().mkpath(home.filePath(".config")));
        QVERIFY(write(home.filePath("Doc notes.md")));
        QVERIFY(write(home.filePath("bin/tool"), executable));
        auto found = [&](const QString &query) { return run_search::special(query, home.path()); };
        // A calculation, its value first; the palette's = and arithmetic alike.
        QCOMPARE(titles(found("=1+2")), QStringList{"3"});
        QCOMPARE(found("2*(3+4)")[0].target, QString("14"));
        QCOMPARE(found("2*(3+4)")[0].kind, QString("calc"));
        // An address, to open in the browser; a bare www. one over https.
        QCOMPARE(found("https://example.org/a?b=c")[0].target, QString("https://example.org/a?b=c"));
        QCOMPARE(found("www.example.org")[0].target, QString("https://www.example.org"));
        QCOMPARE(found("www.example.org")[0].kind, QString("url"));
        QVERIFY(found("example.org").empty());
        QVERIFY(found("firefox https://example.org").empty());
        // A path: the folder it names, then what in its folder starts with its last part,
        // folders first; the hidden ones only when asked for.
        QCOMPARE(titles(found("~/Do")), (QStringList{"~/Documents/", "~/Downloads/", "~/Doc notes.md"}));
        QCOMPARE(found("~/Do")[0].matches, (QList<int>{2, 3}));
        QCOMPARE(found("~/Do")[0].target, home.filePath("Documents"));
        QCOMPARE(kinds(found("~/Do")), (QStringList{"folder", "folder", "file"}));
        QCOMPARE(titles(found("~/Documents")), QStringList{"~/Documents"});
        QCOMPARE(found("~/Documents")[0].kind, QString("folder"));
        QCOMPARE(titles(found("~/.c")), QStringList{"~/.config/"});
        QCOMPARE(titles(found("~/d")).size(), 3);  // the case of the letters does not matter
        QVERIFY(!titles(found("~")).contains("~/.config/"));
        QCOMPARE(found("~")[0].target, home.path());
        // A program is run rather than opened.
        QCOMPARE(found("~/bin/tool")[0].kind, QString("program"));
        QCOMPARE(found(home.filePath("bin/tool"))[0].target, home.filePath("bin/tool"));
        QVERIFY(found("~/nothing").empty());
        QVERIFY(found("words to search").empty());
    }
    void runsInTerminals() {
        const QStringList tail{"sh", "-c", "htop\nexec \"$0\"", "/bin/zsh"};
        QCOMPARE(run_search::inTerminal({"kitty"}, "htop", "/bin/zsh"), QStringList{"kitty"} + tail);
        QCOMPARE(run_search::inTerminal({"/usr/bin/foot", "-o", "x=1"}, "htop", "/bin/zsh"),
                 (QStringList{"/usr/bin/foot", "-o", "x=1"} + tail));
        QCOMPARE(run_search::inTerminal({"alacritty"}, "htop", "/bin/zsh"), (QStringList{"alacritty", "-e"} + tail));
        QCOMPARE(run_search::inTerminal({"wezterm"}, "htop", "/bin/zsh"),
                 (QStringList{"wezterm", "start", "--"} + tail));
        QCOMPARE(run_search::inTerminal({"gnome-terminal"}, "htop", "/bin/zsh"),
                 (QStringList{"gnome-terminal", "--"} + tail));
        QCOMPARE(run_search::inTerminal({"xterm"}, "htop", "").last(), QString("sh"));
        QVERIFY(run_search::inTerminal({}, "htop", "/bin/zsh").isEmpty());
    }
    void saysWhenAProgramIsMissing() {
        const QStringList programs{"git", "htop"};
        QCOMPARE(run_search::missingProgram("gti status", programs), QString("gti"));
        QCOMPARE(run_search::missingProgram("  nope", programs), QString("nope"));
        QCOMPARE(run_search::missingProgram("git status", programs), QString());
        // What the shell makes sense of itself is its to run.
        for (const auto *command : {"cd /tmp && ls", "FOO=1 git", "./build.sh", "~/bin/x",
                                    "'quoted' thing", "(git status)", "if true; then htop; fi",
                                    "echo hi", "$EDITOR notes", ""})
            QCOMPARE(run_search::missingProgram(command, programs), QString());
    }
    void marksMatchedLetters() {
        QCOMPARE(run_search::markup("Firefox", {0, 1, 5}, "#5e9edb"),
                 QString("<b><font color=\"#5e9edb\">Fi</font></b>ref<b><font color=\"#5e9edb\">o</font></b>x"));
        // The rest is escaped, and places past the end or twice are left out.
        QCOMPARE(run_search::markup("a<b>&c", {1, 1, 40}, "red"),
                 QString("a<b><font color=\"red\">&lt;</font></b>b&gt;&amp;c"));
        QCOMPARE(run_search::markup("plain", {}, "red"), QString("plain"));
    }
};
QTEST_GUILESS_MAIN(RunSearchTest)
#include "run_search_test.moc"
