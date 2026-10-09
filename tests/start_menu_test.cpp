// SPDX-License-Identifier: GPL-3.0-or-later
// The start menu's model: its pins, the launch history and the applications from A to Z.
#include "file_index.hpp"
#include "launch_history.hpp"
#include "start_menu.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

namespace {
QString read(const QString &path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}
bool write(const QString &path, const QByteArray &text) {
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
}
QDateTime at(qint64 seconds) { return QDateTime::fromSecsSinceEpoch(seconds, QTimeZone::UTC); }
// An application's record, as the controller gives it.
QVariant app(const QString &id, const QString &name, bool configured = false) {
    return QVariantMap{{"appId", id}, {"name", name}, {"icon", "x"}, {"configured", configured}};
}
QStringList ids(const QVariantList &records) {
    QStringList list;
    for (const auto &record : records)
        list << record.toMap()["appId"].toString();
    return list;
}
const QVariantList someApps{app("pinned:0", "Files", true), app("kate.desktop", "Kate"),
                            app("foot.desktop", "Foot"), app("gimp.desktop", "GIMP"),
                            app("mpv.desktop", "mpv")};
} // namespace

class StartMenuTest : public QObject {
    Q_OBJECT
  private Q_SLOTS:
    void initTestCase() {
        // None of the desktop's applications or default handlers: GIO reads these on first use.
        QVERIFY(home.isValid());
        qputenv("XDG_DATA_HOME", home.filePath("data").toLocal8Bit());
        qputenv("XDG_DATA_DIRS", home.filePath("none").toLocal8Bit());
        qputenv("XDG_CONFIG_HOME", home.filePath("config").toLocal8Bit());
        qputenv("XDG_CONFIG_DIRS", home.filePath("none").toLocal8Bit());
        QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
        // Of the applications there, a web browser by default and a terminal are common ones.
        QVERIFY(write(home.filePath("data/applications/browser.desktop"),
                      "[Desktop Entry]\nType=Application\nName=Browser\nExec=true\n"
                      "MimeType=x-scheme-handler/https;\n"));
        QVERIFY(write(home.filePath("data/applications/term.desktop"),
                      "[Desktop Entry]\nType=Application\nName=Term\nExec=true\n"
                      "Categories=System;TerminalEmulator;\n"));
        QVERIFY(write(home.filePath("data/applications/game.desktop"),
                      "[Desktop Entry]\nType=Application\nName=Game\nExec=true\nCategories=Game;\n"));
        QVERIFY(write(home.filePath("config/mimeapps.list"),
                      "[Default Applications]\nx-scheme-handler/https=browser.desktop\n"));
    }
    void recordsLaunches() {
        QTemporaryDir directory;
        const auto path = directory.filePath("state/paw/launches");
        LaunchHistory history(path);
        QVERIFY(history.entries().isEmpty());
        QVERIFY(history.record("firefox.desktop", at(1000)));
        QVERIFY(history.record("foot.desktop", at(2000)));
        QVERIFY(history.record("firefox.desktop", at(3000)));
        // The most recent first, with how often; saved as it goes, in a folder made for it.
        QCOMPARE(read(path), QString("firefox.desktop\t2\t3000\nfoot.desktop\t1\t2000\n"));
        QCOMPARE(history.find("firefox.desktop")->count, 2);
        QCOMPARE(history.find("firefox.desktop")->last, at(3000));
        QVERIFY(!history.find("kate.desktop"));
        // Read back as it was saved.
        LaunchHistory again(path);
        QCOMPARE(again.entries().size(), 2);
        QCOMPARE(again.entries()[0].id, QString("firefox.desktop"));
        QCOMPARE(again.entries()[1].last, at(2000));
        // Written through a temporary file, nothing else is left in the folder.
        QCOMPARE(QDir(directory.filePath("state/paw")).entryList(QDir::Files),
                 QStringList{"launches"});
    }
    void readsWhatItCan() {
        QTemporaryDir directory;
        const auto path = directory.filePath("launches");
        QVERIFY(write(path, "kate.desktop\t3\t100\nbroken line\nfoot.desktop\tx\t5\n"
                            "gimp.desktop\t1\t900\nkate.desktop\t9\t50\n\t1\t5\nmpv.desktop\t0\t5\n"));
        LaunchHistory history(path);
        // Bad lines and a second line for the same application are left out, and the rest
        // sorted by their last launch.
        QCOMPARE(history.entries().size(), 2);
        QCOMPARE(history.entries()[0].id, QString("gimp.desktop"));
        QCOMPARE(history.entries()[1].id, QString("kate.desktop"));
        QCOMPARE(history.entries()[1].count, 3);
    }
    void keepsTheMostRecent() {
        QTemporaryDir directory;
        const auto path = directory.filePath("launches");
        LaunchHistory history(path);
        for (int i = 0; i < LaunchHistory::limit + 20; ++i)
            QVERIFY(history.record(QString("app%1.desktop").arg(i), at(1000 + i)));
        QCOMPARE(history.entries().size(), LaunchHistory::limit);
        QCOMPARE(history.entries().first().id, QString("app%1.desktop").arg(LaunchHistory::limit + 19));
        QVERIFY(!history.find("app0.desktop"));
        QCOMPARE(read(path).count('\n'), LaunchHistory::limit);
    }
    void saysWhenItCannotSave() {
        QTemporaryDir directory;
        // Its folder is a file.
        QVERIFY(write(directory.filePath("state"), "not a folder"));
        LaunchHistory history(directory.filePath("state/launches"));
        QVERIFY(!history.record("foot.desktop", at(10)));
        QVERIFY(!history.error().isEmpty());
        // What it could not save is still known while the shell runs.
        QCOMPARE(history.find("foot.desktop")->count, 1);
    }
    void seedsPins() {
        const QStringList installed{"a", "b", "c", "d", "e", "f", "g", "h"};
        // The taskbar's pins that are installed, then common applications up to six.
        QCOMPARE(StartMenu::seed({"b", "gone", "a"}, {"c", "a", "d", "x", "e", "f", "g"}, installed),
                 (QStringList{"b", "a", "c", "d", "e", "f"}));
        // Every taskbar pin, however many.
        QCOMPARE(StartMenu::seed(installed, {"z"}, installed), installed);
        QCOMPARE(StartMenu::seed({}, {}, installed), QStringList{});
    }
    void seedsCommonApps() {
        QCOMPARE(StartMenu::commonApps(), (QStringList{"browser.desktop", "term.desktop"}));
        // With no taskbar pins, the start menu begins with those.
        QTemporaryDir state;
        StartMenu menu(state.path());
        menu.setApps({app("game.desktop", "Game"), app("term.desktop", "Term"), app("browser.desktop", "Browser")}, {});
        QCOMPARE(ids(menu.pinned()), (QStringList{"browser.desktop", "term.desktop"}));
        menu.setApps({app("game.desktop", "Game"), app("term.desktop", "Term"), app("browser.desktop", "Browser")},
                     {"game.desktop"});
        QCOMPARE(ids(menu.pinned()), (QStringList{"game.desktop", "browser.desktop", "term.desktop"}));
        QVERIFY(!QFile::exists(state.filePath("start-pinned")));
    }
    void pinsAreItsOwnOnceChanged() {
        QTemporaryDir state;
        const auto file = state.filePath("start-pinned");
        {
            StartMenu menu(state.path());
            // Seeded from the taskbar's pins, and nothing written while they are only the seed.
            menu.setApps(someApps, {"gimp.desktop", "gone.desktop"});
            QCOMPARE(ids(menu.pinned()), QStringList{"gimp.desktop"});
            QVERIFY(!QFile::exists(file));
            menu.setApps(someApps, {"foot.desktop"});
            QCOMPARE(ids(menu.pinned()), QStringList{"foot.desktop"});
            QVERIFY(!QFile::exists(file));
            // Pinned at the end, and the pins are then its own.
            menu.pin("kate.desktop");
            QCOMPARE(read(file), QString("foot.desktop\nkate.desktop\n"));
            menu.setApps(someApps, {"gimp.desktop"});
            QCOMPARE(ids(menu.pinned()), (QStringList{"foot.desktop", "kate.desktop"}));
            // Configured launchers, what is not installed and what is pinned already stay out.
            menu.pin("pinned:0");
            menu.pin("gone.desktop");
            menu.pin("kate.desktop");
            QCOMPARE(read(file), QString("foot.desktop\nkate.desktop\n"));
            menu.pin("gimp.desktop");
            menu.movePin("gimp.desktop", "foot.desktop");
            QCOMPARE(ids(menu.pinned()), (QStringList{"gimp.desktop", "foot.desktop", "kate.desktop"}));
            menu.movePin("gimp.desktop", "kate.desktop");
            QCOMPARE(ids(menu.pinned()), (QStringList{"foot.desktop", "kate.desktop", "gimp.desktop"}));
            menu.unpin("foot.desktop");
            QVERIFY(!menu.isPinned("foot.desktop") && menu.isPinned("kate.desktop"));
            QCOMPARE(read(file), QString("kate.desktop\ngimp.desktop\n"));
        }
        // Read back; an application that is not installed keeps its place but is not shown.
        QVERIFY(write(file, "kate.desktop\nlater.desktop\ngimp.desktop\n"));
        StartMenu again(state.path());
        again.setApps(someApps, {"foot.desktop"});
        QCOMPARE(ids(again.pinned()), (QStringList{"kate.desktop", "gimp.desktop"}));
        again.setApps(QVariantList(someApps) << app("later.desktop", "Later"), {});
        QCOMPARE(ids(again.pinned()), (QStringList{"kate.desktop", "later.desktop", "gimp.desktop"}));
        // Unpinning them all leaves none, rather than the seed again.
        for (const auto *id : {"kate.desktop", "later.desktop", "gimp.desktop"})
            again.unpin(id);
        StartMenu empty(state.path());
        empty.setApps(someApps, {"foot.desktop"});
        QVERIFY(empty.pinned().isEmpty());
    }
    void listsRecentLaunches() {
        QTemporaryDir state;
        StartMenu menu(state.path());
        menu.setApps(someApps, {});
        menu.record("kate.desktop", at(1000));
        menu.record("pinned:0", at(1500));     // a configured launcher
        menu.record("gone.desktop", at(1600)); // not installed
        menu.record("mpv.desktop", at(2000));
        menu.record("kate.desktop", at(3000));
        const auto recent = menu.recent();
        QCOMPARE(ids(recent), (QStringList{"kate.desktop", "mpv.desktop"}));
        QCOMPARE(recent[0].toMap()["launches"].toInt(), 2);
        QCOMPARE(recent[0].toMap()["launched"].toDateTime(), at(3000));
        QCOMPARE(recent[0].toMap()["name"].toString(), QString("Kate"));
        QCOMPARE(read(state.filePath("launches")), QString("kate.desktop\t2\t3000\nmpv.desktop\t1\t2000\n"));
        // Read again by the next session.
        StartMenu next(state.path());
        next.setApps(someApps, {});
        QCOMPARE(ids(next.recent()), (QStringList{"kate.desktop", "mpv.desktop"}));
    }
    void listsAppsByLetter() {
        QCOMPARE(StartMenu::letterOf("firefox"), QString("F"));
        QCOMPARE(StartMenu::letterOf("  Émile"), QString("E"));
        QCOMPARE(StartMenu::letterOf("Ångström"), QString("A"));
        QCOMPARE(StartMenu::letterOf("0 A.D."), QString("#"));
        QCOMPARE(StartMenu::letterOf("[Test]"), QString("#"));
        QCOMPARE(StartMenu::letterOf(""), QString("#"));
        QTemporaryDir state;
        StartMenu menu(state.path());
        menu.setApps({app("z.desktop", "zathura"), app("a.desktop", "Audacity"), app("e.desktop", "Émile"),
                      app("n.desktop", "2048"), app("b.desktop", "blender"), app("a2.desktop", "ark")},
                     {});
        QStringList order, letters;
        for (const auto &record : menu.apps()) {
            order << record.toMap()["name"].toString();
            letters << record.toMap()["letter"].toString();
        }
        QCOMPARE(order, (QStringList{"2048", "ark", "Audacity", "blender", "Émile", "zathura"}));
        QCOMPARE(letters, (QStringList{"#", "A", "A", "B", "E", "Z"}));
    }
    void saysHowLongAgo() {
        QTemporaryDir state;
        StartMenu menu(state.path());
        const QDateTime now(QDate(2026, 10, 6), QTime(15, 0));
        QCOMPARE(menu.ago(now.addSecs(-20), now), QString("Just now"));
        QCOMPARE(menu.ago(now.addSecs(20), now), QString("Just now")); // a clock set back
        QCOMPARE(menu.ago(now.addSecs(-150), now), QString("2 min ago"));
        QCOMPARE(menu.ago(now.addSecs(-3599), now), QString("59 min ago"));
        QCOMPARE(menu.ago(now.addSecs(-3600), now), QString("1 hour ago"));
        QCOMPARE(menu.ago(now.addSecs(-5 * 3600), now), QString("5 hours ago"));
        QCOMPARE(menu.ago(QDateTime(QDate(2026, 10, 5), QTime(23, 30)), now), QString("Yesterday"));
        QCOMPARE(menu.ago(QDateTime(QDate(2026, 10, 2), QTime(9, 0)), now), QString("Friday"));
        QCOMPARE(menu.ago(QDateTime(QDate(2026, 9, 12), QTime(9, 0)), now), QString("12 Sep"));
        QCOMPARE(menu.ago(QDateTime(QDate(2025, 12, 30), QTime(9, 0)), now), QString("30 Dec 2025"));
    }
    void previewsInMemory() {
        QTemporaryDir state;
        StartMenu menu(state.path());
        menu.setApps(someApps, {});
        menu.preview({"mpv.desktop", "foot.desktop"},
                     {{"foot.desktop", 3, at(500)}, {"gimp.desktop", 1, at(900)}});
        QCOMPARE(ids(menu.pinned()), (QStringList{"mpv.desktop", "foot.desktop"}));
        QCOMPARE(ids(menu.recent()), (QStringList{"gimp.desktop", "foot.desktop"}));
        QCOMPARE(menu.recent()[1].toMap()["launches"].toInt(), 3);
        // What a preview does is not saved.
        menu.pin("kate.desktop");
        menu.record("kate.desktop", at(1000));
        QCOMPARE(QDir(state.path()).entryList(QDir::Files), QStringList{});
        menu.setUser("Robin Lee", QUrl());
        QCOMPARE(menu.userName(), QString("Robin Lee"));
    }
    void searches() {
        QTemporaryDir state;
        StartMenu menu(state.path());
        auto described = [](const QString &id, const QString &name, const QString &generic,
                            const QStringList &keywords, const QString &description) {
            auto record = app(id, name).toMap();
            record["genericName"] = generic;
            record["keywords"] = keywords;
            record["description"] = description;
            return QVariant(record);
        };
        menu.setApps({described("firefox.desktop", "Firefox", "Web Browser", {"Internet", "WWW"},
                                "Browse the World Wide Web"),
                      described("foot.desktop", "Foot", "Terminal", {"shell", "prompt", "command"}, ""),
                      described("org.gnome.clocks.desktop", "Clocks", "World clocks", {}, ""),
                      described("kalk.desktop", "Kalk", "Calculator", {}, ""),
                      described("kate.desktop", "Kate", "Text Editor", {}, ""),
                      described("writer.desktop", "LibreOffice Writer", "Word Processor",
                                {"Text", "Letter"}, "Create and edit text in letters and reports"),
                      described("calc.desktop", "Calculator", "Calculator", {},
                                "Perform arithmetic, scientific or financial calculations"),
                      app("pinned:0", "Files", true)},
                     {});
        auto entry = [](const QString &kind, const QString &title, const QString &subtitle) {
            return QVariant(QVariantMap{{"kind", kind}, {"title", title}, {"subtitle", subtitle},
                                        {"target", title}});
        };
        // The palette's entries: its sessions and applications are not searched again.
        const QVariantList others{entry("window", "Release notes - Mozilla Firefox", "Window · firefox"),
                                  entry("window", "htop", "Window · foot"),
                                  entry("session", "Restore session work", "Session · 3 windows"),
                                  entry("workspace", "Workspace 2: web", "Switch workspace"),
                                  entry("action", "Lock screen", "Action · lock"),
                                  entry("action", "Toggle tiling", "Action · toggle_tiling"),
                                  entry("app", "Firefox", "Application")};
        auto found = [&](const QString &query) {
            QStringList list;
            for (const auto &result : menu.search(query, others))
                list << result.toMap()["group"].toString() + ":" + result.toMap()["title"].toString();
            return list;
        };
        QCOMPARE(found(""), QStringList{});
        QCOMPARE(found("  "), QStringList{});
        // The application best, then the window of it.
        QCOMPARE(found("firefox"), (QStringList{"best:Firefox", "windows:Release notes - Mozilla Firefox"}));
        // By generic name, keywords, id and comment.
        QCOMPARE(found("web browser"), QStringList{"best:Firefox"});
        QCOMPARE(found("prompt"), QStringList{"best:Foot"});
        QCOMPARE(found("gnome"), QStringList{"best:Clocks"});
        QCOMPARE(found("world wide"), QStringList{"best:Firefox"});
        QCOMPARE(found("letter"), QStringList{"best:LibreOffice Writer"});
        // What describes an application is searched by its words, not by letters strewn through
        // it ("perform arithmetic, scientific or financial"), and a name with the letters strewn
        // through it ("LibreOffice Writer") is far behind the best match, and left out.
        QCOMPARE(found("fire").join("|"), QString("best:Firefox|windows:Release notes - Mozilla Firefox"));
        // A configured launcher by its name only.
        QCOMPARE(found("files"), QStringList{"best:Files"});
        QCOMPARE(found("pinned"), QStringList{});
        // An action that matches better than any application is the best match.
        QCOMPARE(found("lock screen"), QStringList{"best:Lock screen"});
        QCOMPARE(found("lock").first(), QString("best:Lock screen"));
        QVERIFY(found("lock").contains("apps:Clocks"));
        // Workspaces are among the actions; sessions are not searched.
        QCOMPARE(found("workspace web"), QStringList{"best:Workspace 2: web"});
        QCOMPARE(found("restore session"), QStringList{});
        // The palette's filters: only windows.
        QCOMPARE(found("@"), (QStringList{"best:Release notes - Mozilla Firefox", "windows:htop"}));
        // A calculation's value is the best match, its number what choosing it copies; a lone
        // number is searched for.
        QCOMPARE(found("2*(3+4)"), QStringList{"best:14"});
        const auto calc = menu.search("5 km in mi", others).first().toMap();
        QCOMPARE(calc["kind"].toString(), QString("calc"));
        QCOMPARE(calc["title"].toString(), QString("3.10685596119 mi"));
        QCOMPARE(calc["target"].toString(), QString("3.10685596119"));
        QCOMPARE(found("2").first(), QString("best:Workspace 2: web"));
        // Files come last, unless nothing else matches as well; / finds only them.
        FileIndex files;
        files.preview({{"/srv/docs"},
                       {{0, "Kate notes.txt", "kate notes.txt", false, 0},
                        {0, "Quarterly report.pdf", "quarterly report.pdf", false, 0},
                        {0, "Firefox profile", "firefox profile", true, 0}}});
        menu.setFiles(&files);
        QCOMPARE(found("kate").first(), QString("best:Kate"));
        QCOMPARE(found("kate").last(), QString("files:Kate notes.txt"));
        QCOMPARE(found("quarterly"), QStringList{"best:Quarterly report.pdf"});
        QCOMPARE(found("/fire"), QStringList{"best:Firefox profile"});
        QCOMPARE(found("/kate"), QStringList{"best:Kate notes.txt"});
        QCOMPARE(found("@kate"), QStringList{});
        const auto file = menu.search("quarterly", others).first().toMap();
        QCOMPARE(file["kind"].toString(), QString("file"));
        QCOMPARE(file["target"].toString(), QString("/srv/docs/Quarterly report.pdf"));
        // Searching the web comes last, and is the best match when nothing else is found; not
        // for a prefix's search.
        menu.setWebSearch("https://duckduckgo.com/?q=%s");
        QCOMPARE(found("kate").last(), QString("web:Search the web for “kate”"));
        QCOMPARE(found("zqxw"), QStringList{"best:Search the web for “zqxw”"});
        QCOMPARE(found("@zqxw"), QStringList{});
        QCOMPARE(found("/kate"), QStringList{"best:Kate notes.txt"});
        QCOMPARE(menu.search("zqxw", others).first().toMap()["target"].toString(),
                 QString("https://duckduckgo.com/?q=zqxw"));
        menu.setWebSearch({});
        QCOMPARE(found("zqxw"), QStringList{});
        // An application's result is its record, with what the menu shows of it.
        const auto best = menu.search("kate", others).first().toMap();
        QCOMPARE(best["kind"].toString(), QString("app"));
        QCOMPARE(best["appId"].toString(), QString("kate.desktop"));
        QCOMPARE(best["subtitle"].toString(), QString("Text Editor"));
        QVERIFY(best["score"].toDouble() > 0);
        // Of equal matches, the one launched more often comes first.
        QCOMPARE(found("ka").mid(0, 2), (QStringList{"best:Kalk", "apps:Kate"}));
        menu.record("kate.desktop", at(1000));
        QCOMPARE(found("ka").mid(0, 2), (QStringList{"best:Kate", "apps:Kalk"}));
    }
    void knowsTheUser() {
        QTemporaryDir state;
        StartMenu menu(state.path());
        QVERIFY(!menu.userName().isEmpty());
    }
    void findsTheUsersPicture() {
        // ~/.face, else ~/.face.icon, else none (without D-Bus to ask AccountsService).
        QTemporaryDir fakeHome, state;
        const auto realHome = qgetenv("HOME");
        qputenv("HOME", fakeHome.path().toLocal8Bit());
        QVERIFY(StartMenu(state.path()).userIcon().isEmpty());
        QVERIFY(write(fakeHome.filePath(".face.icon"), "picture"));
        QCOMPARE(StartMenu(state.path()).userIcon(), QUrl::fromLocalFile(fakeHome.filePath(".face.icon")));
        QVERIFY(write(fakeHome.filePath(".face"), "picture"));
        QCOMPARE(StartMenu(state.path()).userIcon(), QUrl::fromLocalFile(fakeHome.filePath(".face")));
        qputenv("HOME", realHome);
    }

  private:
    QTemporaryDir home;
};
QTEST_GUILESS_MAIN(StartMenuTest)
#include "start_menu_test.moc"
