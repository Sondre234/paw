// SPDX-License-Identifier: GPL-3.0-or-later
// The files the search finds: the folders read, what is left out, the recent files, the search
// itself, and opening a file or its folder, all in a home folder of the test's own.
#include "file_index.hpp"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

namespace {
bool write(const QString &path, const QByteArray &content = "hello\n") {
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
}
// The names a reading found, as paths under `base` where they are there.
QStringList names(const FileIndex::Found &found, const QString &base) {
    QStringList list;
    for (const auto &file : found.files) {
        auto path = found.folders.value(file.folder) + '/' + file.name;
        if (path.startsWith(base + '/'))
            path = path.sliced(base.size() + 1);
        list << path + (file.isFolder ? "/" : "");
    }
    return list;
}
QString bookmark(const QString &href, const QString &when) {
    return QString("  <bookmark href=\"%1\" added=\"%2\" modified=\"%2\" visited=\"%2\">\n"
                   "    <info/>\n  </bookmark>\n")
        .arg(href, when);
}
} // namespace

class FileIndexTest : public QObject {
    Q_OBJECT
    QTemporaryDir root;
    QString home;

  private Q_SLOTS:
    void initTestCase() {
        QVERIFY(root.isValid());
        home = root.filePath("home");
        // Nothing of the user's: a home, settings and applications of the test's own, set before
        // GLib first reads them.
        qputenv("HOME", home.toLocal8Bit());
        qputenv("XDG_CONFIG_HOME", root.filePath("config").toLocal8Bit());
        qputenv("XDG_CONFIG_DIRS", root.filePath("none").toLocal8Bit());
        qputenv("XDG_DATA_HOME", root.filePath("data").toLocal8Bit());
        qputenv("XDG_DATA_DIRS", root.filePath("none").toLocal8Bit());
        QVERIFY(
            write(root.filePath("config/user-dirs.dirs"),
                  "XDG_DOCUMENTS_DIR=\"$HOME/Documents\"\nXDG_DOWNLOAD_DIR=\"$HOME/Downloads\"\n"
                  "XDG_DESKTOP_DIR=\"$HOME/\"\nXDG_MUSIC_DIR=\"$HOME/Music\"\n"));
        for (const char *name :
             {"Documents/report-2024.pdf", "Documents/Work/plan.odt", "Documents/Work/notes.txt",
              "Documents/Work/alpha/bravo/charlie/delta/too-deep.txt", "Downloads/photo.jpg",
              "Downloads/notes.md", ".secret/key.txt", ".hidden-file", "project/.git/HEAD",
              "project/main.c", "build/CMakeCache.txt", "build/output.o", "cache/CACHEDIR.TAG",
              "cache/blob", "node_modules/pkg/index.js", "top.txt"})
            QVERIFY(write(home + '/' + name));
        QVERIFY(QFile::link(home + "/Documents", home + "/link-to-documents"));
        QVERIFY(QFile::link(home, home + "/Documents/Work/loop"));
        // A file used lately outside the folders read, one inside them, a web page and one that
        // is gone, in the order they were last used.
        QVERIFY(write(root.filePath("elsewhere/notes.md")));
        const auto xbel =
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<xbel version=\"1.0\">\n" +
            bookmark(QUrl::fromLocalFile(root.filePath("elsewhere/notes.md")).toString(),
                     "2026-10-08T09:00:00.000000Z") +
            bookmark("https://example.org/", "2026-10-08T12:00:00Z") +
            bookmark(QUrl::fromLocalFile(home + "/Documents/report-2024.pdf").toString(),
                     "2026-10-07T10:00:00Z") +
            bookmark(QUrl::fromLocalFile(home + "/gone.txt").toString(), "2026-10-08T11:00:00Z") +
            "</xbel>\n";
        QVERIFY(write(root.filePath("data/recently-used.xbel"), xbel.toUtf8()));
    }

    void findsTheUserFolders() {
        // The XDG user folders that exist and are not the home folder itself, then the home
        // folder.
        QCOMPARE(FileIndex::defaultRoots(),
                 (QStringList{home + "/Documents", home + "/Downloads", home}));
        QCOMPARE(FileIndex::shown(home), QString("~"));
        QCOMPARE(FileIndex::shown(home + "/Documents/a b"), QString("~/Documents/a b"));
        QCOMPARE(FileIndex::shown(home + "-other/x"), home + "-other/x");
        QCOMPARE(FileIndex::shown("/etc/fstab"), QString("/etc/fstab"));
    }

    void readsTheRecentFiles() {
        const auto recent = FileIndex::readRecent(root.filePath("data/recently-used.xbel"), 10);
        QCOMPARE(recent.size(), size_t(3)); // not the web page
        QCOMPARE(recent[0].first, home + "/gone.txt");
        QCOMPARE(recent[1].first, root.filePath("elsewhere/notes.md"));
        QCOMPARE(recent[1].second,
                 QDateTime::fromString("2026-10-08T09:00:00Z", Qt::ISODate).toMSecsSinceEpoch());
        QCOMPARE(recent[2].first, home + "/Documents/report-2024.pdf");
        QCOMPARE(FileIndex::readRecent(root.filePath("data/recently-used.xbel"), 1).size(),
                 size_t(1));
        QVERIFY(FileIndex::readRecent(root.filePath("data/missing.xbel"), 10).empty());
        QVERIFY(write(root.filePath("data/broken.xbel"), "<xbel><bookmark href=\"file:///a\""));
        (void)FileIndex::readRecent(root.filePath("data/broken.xbel"), 10); // must not crash
    }

    void readsTheFolders() {
        FileIndex::Settings settings;
        settings.roots = FileIndex::defaultRoots();
        settings.recent = root.filePath("data/recently-used.xbel");
        std::atomic<bool> cancelled = false;
        const auto found = FileIndex::scan(settings, cancelled);
        const auto list = names(found, home);
        // The recent files that are there first, the most recent first, then the folders
        // breadth first.
        QCOMPARE(list.mid(0, 2),
                 (QStringList{root.filePath("elsewhere/notes.md"), "Documents/report-2024.pdf"}));
        QVERIFY(found.files[0].used > 0 && found.files[2].used == 0);
        for (const char *name :
             {"Documents/Work/", "Documents/Work/plan.odt", "Downloads/photo.jpg",
              "Downloads/notes.md", "Documents/Work/alpha/bravo/charlie/delta/", "top.txt",
              "Documents/", "project/", "build/", "cache/", "node_modules/", "link-to-documents",
              "Documents/Work/loop"})
            QVERIFY2(list.contains(name), name);
        // Hidden ones, what is too deep, and what is inside version-controlled, build, cache and
        // package trees are left out, and nothing is found twice.
        for (const char *name :
             {".secret/", ".secret/key.txt", ".hidden-file",
              "Documents/Work/alpha/bravo/charlie/delta/too-deep.txt", "project/main.c",
              "project/.git/", "build/output.o", "cache/blob", "node_modules/pkg/",
              "link-to-documents/report-2024.pdf", "Documents/Work/loop/top.txt"})
            QVERIFY2(!list.contains(name), name);
        QCOMPARE(list.count("Documents/Work/plan.odt"), 1);
        QCOMPARE(list.count("Documents/report-2024.pdf"), 1);
        QCOMPARE(QSet<QString>(list.begin(), list.end()).size(), list.size());
        // Fewer levels, and fewer names.
        settings.depth = 1;
        auto shallow = names(FileIndex::scan(settings, cancelled), home);
        QVERIFY(shallow.contains("Documents/Work/plan.odt"));
        QVERIFY(shallow.contains("Documents/Work/alpha/"));
        QVERIFY(!shallow.contains("Documents/Work/alpha/bravo/"));
        settings.depth = 4;
        settings.limit = 3;
        QCOMPARE(FileIndex::scan(settings, cancelled).files.size(), size_t(2 + 3));
        // Stopped, it reads no folder.
        cancelled = true;
        settings.limit = 20000;
        QCOMPARE(FileIndex::scan(settings, cancelled).files.size(), size_t(0));
    }

    void searches() {
        FileIndex index;
        QSignalSpy changed(&index, &FileIndex::changed);
        FileIndex::Settings settings;
        settings.recent = root.filePath("data/recently-used.xbel");
        index.configure(settings);
        // The first search starts reading and finds nothing yet; the names come in later.
        QCOMPARE(index.search("report", 5), QVariantList());
        QVERIFY(index.busy());
        QVERIFY(changed.wait(10000));
        QVERIFY(!index.busy() && index.size() > 10);
        auto titles = [&index](const QString &query, int limit = 10) {
            QStringList list;
            for (const auto &result : index.search(query, limit))
                list << result.toMap()["title"].toString();
            return list;
        };
        const auto report = index.search("report", 5).value(0).toMap();
        QCOMPARE(report["kind"].toString(), QString("file"));
        QCOMPARE(report["title"].toString(), QString("report-2024.pdf"));
        QCOMPARE(report["subtitle"].toString(), QString("~/Documents"));
        QCOMPARE(report["target"].toString(), home + "/Documents/report-2024.pdf");
        QCOMPARE(report["folder"].toBool(), false);
        const auto pdf = QMimeDatabase().mimeTypeForFile("x.pdf", QMimeDatabase::MatchExtension);
        QCOMPARE(report["icon"].toString(),
                 pdf.iconName() + ',' + pdf.genericIconName() + ",text-x-generic");
        QVERIFY(report["score"].toDouble() > 0);
        const auto work = index.search("work", 5).value(0).toMap();
        QCOMPARE(work["title"].toString(), QString("Work"));
        QCOMPARE(work["folder"].toBool(), true);
        QCOMPARE(work["icon"].toString(), QString("folder"));
        // Every word, in any case, in the name; a word with a slash in the path.
        QCOMPARE(titles("REPORT 2024"), QStringList{"report-2024.pdf"});
        QCOMPARE(titles("report 2023"), QStringList{});
        QCOMPARE(titles("work/plan"), QStringList{"plan.odt"});
        QCOMPARE(titles("~/documents/ notes"), QStringList{"notes.txt"});
        QCOMPARE(titles("main.c"), QStringList{});
        QCOMPARE(titles("key"), QStringList{});
        QCOMPARE(titles(""), QStringList{});
        // A file used lately comes before one that was not, and the limit holds.
        QCOMPARE(titles("notes").mid(0, 1), QStringList{"notes.md"});
        QCOMPARE(index.search("notes", 10).value(0).toMap()["subtitle"].toString(),
                 root.filePath("elsewhere"));
        QCOMPARE(titles("notes", 2).size(), 2);
        QCOMPARE(titles("o", 4).size(), 4);
        // Read again only once out of date: new settings are.
        QVERIFY(!index.busy());
        settings.depth = 1;
        index.configure(settings);
        (void)index.search("plan", 5);
        QVERIFY(index.busy());
        QVERIFY(changed.wait(10000));
        QCOMPARE(titles("bravo"), QStringList{});
        QCOMPARE(titles("plan"), QStringList{"plan.odt"});
        // Off, it finds nothing and reads nothing.
        settings.enabled = false;
        index.configure(settings);
        QCOMPARE(titles("plan"), QStringList{});
        QVERIFY(!index.busy());
    }

    void stops() {
        // Going away while it reads waits for the thread.
        auto index = std::make_unique<FileIndex>();
        FileIndex::Settings settings;
        settings.roots = {home};
        index->configure(settings);
        (void)index->search("x", 1);
        index.reset();
    }

    void previews() {
        FileIndex index;
        index.preview({{"/srv"}, {{0, "Quarterly report.pdf", "quarterly report.pdf", false, 0}}});
        QCOMPARE(index.search("quarterly", 5).value(0).toMap()["target"].toString(),
                 QString("/srv/Quarterly report.pdf"));
        QVERIFY(!index.busy());
    }

    void opens() {
        // Applications of the test's own handle files and folders, each leaving a link to what
        // it was given.
        const auto marks = root.filePath("marks");
        QDir().mkpath(marks);
        QVERIFY(write(root.filePath("data/applications/paw-test-files.desktop"),
                      QString("[Desktop Entry]\nType=Application\nName=Files\n"
                              "MimeType=text/plain;application/octet-stream;\n"
                              "Exec=ln -s %f %1/file\n")
                          .arg(marks)
                          .toUtf8()));
        QVERIFY(write(root.filePath("data/applications/paw-test-folders.desktop"),
                      QString("[Desktop Entry]\nType=Application\nName=Folders\n"
                              "MimeType=inode/directory;\nExec=ln -s %f %1/folder\n")
                          .arg(marks)
                          .toUtf8()));
        QVERIFY(write(root.filePath("config/mimeapps.list"),
                      "[Default Applications]\ntext/plain=paw-test-files.desktop\n"
                      "application/octet-stream=paw-test-files.desktop\n"
                      "inode/directory=paw-test-folders.desktop\n"));
        const auto file = home + "/Documents/Work/notes.txt";
        QCOMPARE(FileIndex::open(file, false), QString());
        QVERIFY(QTest::qWaitFor([&] { return QFileInfo(marks + "/file").isSymLink(); }, 10000));
        QCOMPARE(QFileInfo(marks + "/file").symLinkTarget(), file);
        QCOMPARE(FileIndex::open(file, true), QString());
        QVERIFY(QTest::qWaitFor([&] { return QFileInfo(marks + "/folder").isSymLink(); }, 10000));
        QCOMPARE(QFileInfo(marks + "/folder").symLinkTarget(), home + "/Documents/Work");
        QCOMPARE(FileIndex::open(home + "/gone.txt", false), QString("it is no longer there"));
    }
};

QTEST_GUILESS_MAIN(FileIndexTest)
#include "file_index_test.moc"
