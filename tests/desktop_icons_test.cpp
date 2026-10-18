// SPDX-License-Identifier: GPL-3.0-or-later
// The desktop's icons: the launchers, then the desktop folder's folders and files by name; their
// cells on the grid, remembered with whether they are hidden; the folder followed as it changes;
// opening, copying, renaming and trashing files, new folders, and desktop entries, applications'
// and web links', trusted only once executable; files, folders, web links and pictures dropped on
// the desktop or into a folder of it.
#include "desktop_icons.hpp"
#include <QBuffer>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QMimeData>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

namespace {
bool write(const QString &path, const QByteArray &contents = "x\n") {
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(contents) == contents.size();
}
} // namespace

class DesktopIconsTest : public QObject {
    Q_OBJECT
    QTemporaryDir home_;
    QString folder_;
    DesktopIcons::Settings settings_;
    const QList<DesktopIcons::Launcher> launchers_{{"pinned:0", "Terminal", "utilities-terminal"}};

    // A model of the folder, as the shell makes one, with a grid of `columns` by `rows`.
    std::unique_ptr<DesktopIcons> made(int columns = 4, int rows = 3) {
        auto icons = std::make_unique<DesktopIcons>();
        icons->configure(settings_, launchers_);
        icons->setGrid(columns, rows);
        return icons;
    }
    static QVariant value(const DesktopIcons &icons, const QString &key, int role) {
        for (int i = 0; i < icons.rowCount(); ++i)
            if (icons.data(icons.index(i), DesktopIcons::KeyRole) == key)
                return icons.data(icons.index(i), role);
        return {};
    }
    static QPoint cell(const DesktopIcons &icons, const QString &key) {
        const auto column = value(icons, key, DesktopIcons::ColumnRole);
        return column.isValid() ? QPoint(column.toInt(), value(icons, key, DesktopIcons::RowRole).toInt())
                                : QPoint(-9, -9);
    }
    // Whether the icon of `key` is there, its file all in.
    static bool arrived(const DesktopIcons &icons, const QString &key) {
        const auto arriving = value(icons, key, DesktopIcons::ArrivingRole);
        return arriving.isValid() && !arriving.toBool();
    }
    static QList<QUrl> files(const QStringList &paths) {
        QList<QUrl> urls;
        for (const auto &path : paths)
            urls.push_back(QUrl::fromLocalFile(path));
        return urls;
    }
    static QStringList keys(const DesktopIcons &icons) {
        QStringList list;
        for (int i = 0; i < icons.rowCount(); ++i)
            list << icons.data(icons.index(i), DesktopIcons::KeyRole).toString();
        return list;
    }

  private Q_SLOTS:
    void initTestCase() {
        QVERIFY(home_.isValid());
        qputenv("XDG_STATE_HOME", home_.filePath("state").toLocal8Bit());
        qputenv("XDG_DATA_HOME", home_.filePath("data").toLocal8Bit());
        // A home on the temporary folder's file system, whose trash is then the data folder's,
        // which is there as ~/.local/share is.
        qputenv("HOME", home_.path().toLocal8Bit());
        QVERIFY(QDir().mkpath(home_.filePath("data")));
        folder_ = home_.filePath("Desktop");
        settings_.folder = folder_;
        // What opens pawtest: addresses, which notes that it ran; there before GIO first lists the
        // applications.
        QVERIFY(QDir().mkpath(home_.filePath("data/applications")));
        QVERIFY(write(home_.filePath("data/applications/paw-link-test.desktop"),
                      ("[Desktop Entry]\nType=Application\nName=Link test\nExec=touch " +
                       home_.filePath("linked") + "\nMimeType=x-scheme-handler/pawtest;\n").toUtf8()));
        QVERIFY(write(home_.filePath("data/applications/mimeapps.list"),
                      "[Default Applications]\nx-scheme-handler/pawtest=paw-link-test.desktop\n"));
    }
    void init() {
        QDir(folder_).removeRecursively();
        QDir(home_.filePath("state")).removeRecursively();
        QDir(home_.filePath("outside")).removeRecursively();
        QVERIFY(QDir().mkpath(home_.filePath("outside")));
        QVERIFY(QDir().mkpath(folder_ + "/Zeta"));
        for (const auto *name : {"b.txt", "a10.txt", "a2.txt", ".hidden"})
            QVERIFY(write(folder_ + "/" + name));
    }
    void listsLaunchersThenFoldersThenFiles() {
        auto icons = made();
        // Numbers in names in order, case aside, and nothing hidden.
        QCOMPARE(keys(*icons), (QStringList{"launcher:Terminal", "file:Zeta", "file:a2.txt",
                                            "file:a10.txt", "file:b.txt"}));
        QCOMPARE(value(*icons, "launcher:Terminal", DesktopIcons::KindRole).toString(), "launcher");
        QCOMPARE(value(*icons, "launcher:Terminal", DesktopIcons::AppIdRole).toString(), "pinned:0");
        QCOMPARE(value(*icons, "launcher:Terminal", DesktopIcons::IconRole).toString(), "utilities-terminal");
        QCOMPARE(value(*icons, "file:Zeta", DesktopIcons::KindRole).toString(), "folder");
        QCOMPARE(value(*icons, "file:a2.txt", DesktopIcons::KindRole).toString(), "file");
        QCOMPARE(value(*icons, "file:a2.txt", DesktopIcons::PathRole).toString(), folder_ + "/a2.txt");
        QVERIFY(value(*icons, "file:a2.txt", DesktopIcons::IconRole).toString().startsWith("text-plain,"));
        // Down the columns.
        QCOMPARE(cell(*icons, "launcher:Terminal"), QPoint(0, 0));
        QCOMPARE(cell(*icons, "file:Zeta"), QPoint(0, 1));
        QCOMPARE(cell(*icons, "file:a2.txt"), QPoint(0, 2));
        QCOMPARE(cell(*icons, "file:a10.txt"), QPoint(1, 0));
        // No cells before the desktop says how large its grid is.
        DesktopIcons unplaced;
        unplaced.configure(settings_, launchers_);
        QCOMPARE(cell(unplaced, "file:b.txt"), QPoint(-1, -1));
        QVERIFY(unplaced.shown() && unplaced.enabled() && unplaced.iconSize() == 48);
    }
    void showsPicturesAsThemselves() {
        QImage picture(8, 8, QImage::Format_RGB32);
        picture.fill(Qt::magenta);
        QVERIFY(picture.save(folder_ + "/photo.png"));
        auto icons = made();
        QCOMPARE(value(*icons, "file:photo.png", DesktopIcons::PictureRole).toString(),
                 QUrl::fromLocalFile(folder_ + "/photo.png").toString());
        QCOMPARE(value(*icons, "file:b.txt", DesktopIcons::PictureRole).toString(), QString());
    }
    void remembersCellsAndHiding() {
        {
            auto icons = made();
            icons->move({"file:b.txt"}, 2, 1);
            QCOMPARE(cell(*icons, "file:b.txt"), QPoint(3, 2));
            icons->setShown(false);
            QVERIFY(!icons->shown());
        }
        auto again = made();
        QCOMPARE(cell(*again, "file:b.txt"), QPoint(3, 2));
        QVERIFY(!again->shown());
        // The action's verbs.
        QSignalSpy shown(again.get(), &DesktopIcons::shownChanged);
        QVERIFY(again->handle("toggle") && again->shown());
        QVERIFY(again->handle("off") && !again->shown());
        QVERIFY(again->handle("on") && again->shown());
        QVERIFY(!again->handle("flip") && again->shown());
        QCOMPARE(shown.count(), 3);
        // A cell off a smaller grid is kept for when it grows again.
        again->setGrid(2, 3);
        QVERIFY(cell(*again, "file:b.txt").x() < 2);
        again->setGrid(4, 3);
        QCOMPARE(cell(*again, "file:b.txt"), QPoint(3, 2));
        // Tidied, each is laid out anew, and that is remembered.
        again->tidy();
        QCOMPARE(cell(*again, "file:b.txt"), QPoint(1, 1));
        QCOMPARE(cell(*made(), "file:b.txt"), QPoint(1, 1));
    }
    void movesIconsTogether() {
        auto icons = made();
        // a10.txt and a2.txt, from (1,0) and (0,2), dragged together by a10.txt one cell right.
        icons->move({"file:a10.txt", "file:a2.txt"}, 1, 0);
        QCOMPARE(cell(*icons, "file:a10.txt"), QPoint(2, 0));
        QCOMPARE(cell(*icons, "file:a2.txt"), QPoint(1, 2));
        // Dropped onto an icon left where it is, it goes beside it.
        icons->move({"file:a10.txt"}, -2, 0);
        QCOMPARE(cell(*icons, "file:a10.txt"), QPoint(1, 0));
        QCOMPARE(cell(*icons, "launcher:Terminal"), QPoint(0, 0));
        // Where they would land is where they land.
        const auto landing = icons->landing({"file:Zeta", "file:nothing", "file:b.txt"}, 2, 0);
        QCOMPARE(landing.size(), 2);
        QCOMPARE(landing[0].toMap()["column"].toInt(), 2);
        QCOMPARE(landing[0].toMap()["row"].toInt(), 1);
        icons->move({"file:Zeta", "file:nothing", "file:b.txt"}, 2, 0);
        QCOMPARE(cell(*icons, "file:Zeta"), QPoint(2, 1));
        QCOMPARE(cell(*icons, "file:b.txt"), QPoint(landing[1].toMap()["column"].toInt(),
                                                    landing[1].toMap()["row"].toInt()));
        // Unknown keys move nothing.
        icons->move({"file:nothing"}, 1, 1);
        QCOMPARE(icons->count(), 5);
    }
    void followsTheFolder() {
        auto icons = made();
        const auto before = cell(*icons, "file:b.txt");
        QSignalSpy counted(icons.get(), &DesktopIcons::countChanged);
        // A new file sorted before the others takes a free cell; the others stay where they are.
        QVERIFY(write(folder_ + "/a1.txt"));
        QTRY_COMPARE(icons->count(), 6);
        QCOMPARE(cell(*icons, "file:b.txt"), before);
        QCOMPARE(cell(*icons, "file:a1.txt"), QPoint(1, 2));
        QVERIFY(QFile::remove(folder_ + "/b.txt"));
        QTRY_COMPARE(icons->count(), 5);
        QVERIFY(!keys(*icons).contains("file:b.txt"));
        QVERIFY(counted.count() >= 2);
        // A folder that is not there yet shows the launchers, and its files once it is made.
        settings_.folder = home_.filePath("Later");
        icons->configure(settings_, launchers_);
        QCOMPARE(keys(*icons), QStringList{"launcher:Terminal"});
        QVERIFY(QDir().mkpath(settings_.folder));
        QVERIFY(write(settings_.folder + "/note.txt"));
        QTRY_COMPARE(keys(*icons), (QStringList{"launcher:Terminal", "file:note.txt"}));
        settings_.folder = folder_;
    }
    void turnedOffShowsNothing() {
        auto settings = settings_;
        settings.enabled = false;
        DesktopIcons icons;
        icons.configure(settings, launchers_);
        QCOMPARE(icons.count(), 0);
        QVERIFY(!icons.shown());
    }
    void dragsFilesOut() {
        auto icons = made();
        QSignalSpy ended(icons.get(), &DesktopIcons::dragEnded);
        QImage picture(12, 12, QImage::Format_ARGB32_Premultiplied);
        picture.fill(Qt::magenta);
        // A launcher alone is no file to hand over.
        QVERIFY(!icons->drag({"launcher:Terminal", "file:nothing"}, picture, QPointF(6, 6), 2));
        QVERIFY(icons->drag({"launcher:Terminal", "file:a2.txt"}, picture, QPointF(6, 6), 2));
        // Without a display that takes drags it is over at once, nothing taking it.
        QTRY_COMPARE(ended.count(), 1);
        QCOMPARE(ended[0][0].toInt(), int(Qt::IgnoreAction));
    }
    void copiesAndTrashesFiles() {
        auto icons = made();
        icons->copy({"file:a2.txt", "launcher:Terminal", "file:b.txt"});
        const auto *copied = QGuiApplication::clipboard()->mimeData();
        QVERIFY(copied);
        QCOMPARE(copied->urls(), (QList<QUrl>{QUrl::fromLocalFile(folder_ + "/a2.txt"),
                                              QUrl::fromLocalFile(folder_ + "/b.txt")}));
        QCOMPARE(copied->data("x-special/gnome-copied-files"),
                 "copy\n" + QUrl::fromLocalFile(folder_ + "/a2.txt").toEncoded() + "\n" +
                     QUrl::fromLocalFile(folder_ + "/b.txt").toEncoded());
        QSignalSpy failed(icons.get(), &DesktopIcons::failed);
        const int trashed = icons->trash({"file:a2.txt", "launcher:Terminal"});
        QVERIFY2(failed.isEmpty(), qPrintable(failed.value(0).value(0).toString()));
        QCOMPARE(trashed, 1);
        QVERIFY(!QFile::exists(folder_ + "/a2.txt"));
        QVERIFY(!keys(*icons).contains("file:a2.txt"));
        QVERIFY(QFile::exists(home_.filePath("data/Trash/files/a2.txt")));
    }
    void renamesKeepingTheCell() {
        {
            auto icons = made();
            icons->move({"file:b.txt"}, 2, 1);
            QSignalSpy failed(icons.get(), &DesktopIcons::failed);
            QCOMPARE(icons->rename("file:b.txt", "plan.md"), QString("file:plan.md"));
            QVERIFY2(failed.isEmpty(), qPrintable(failed.value(0).value(0).toString()));
            QVERIFY(!QFile::exists(folder_ + "/b.txt") && QFile::exists(folder_ + "/plan.md"));
            // At once, in its row, with what the new name makes of it.
            QVERIFY(!keys(*icons).contains("file:b.txt"));
            QCOMPARE(cell(*icons, "file:plan.md"), QPoint(3, 2));
            QCOMPARE(value(*icons, "file:plan.md", DesktopIcons::NameRole).toString(), "plan.md");
            QCOMPARE(value(*icons, "file:plan.md", DesktopIcons::PathRole).toString(), folder_ + "/plan.md");
            QVERIFY(value(*icons, "file:plan.md", DesktopIcons::IconRole).toString().startsWith("text-markdown,"));
            // A folder too, and its own name again is no change.
            QCOMPARE(icons->rename("file:Zeta", "Alpha"), QString("file:Alpha"));
            QVERIFY(QFileInfo(folder_ + "/Alpha").isDir());
            QCOMPARE(value(*icons, "file:Alpha", DesktopIcons::KindRole).toString(), "folder");
            QCOMPARE(icons->rename("file:Alpha", "Alpha"), QString("file:Alpha"));
            // Read again as the folder changes, it stays where it is.
            icons->rescan();
            QCOMPARE(cell(*icons, "file:plan.md"), QPoint(3, 2));
            QCOMPARE(icons->count(), 5);
        }
        // Remembered under its new name.
        QCOMPARE(cell(*made(), "file:plan.md"), QPoint(3, 2));
    }
    void refusesNamesItCannotGive() {
        auto icons = made();
        QSignalSpy failed(icons.get(), &DesktopIcons::failed);
        auto refused = [&](const QString &name, const QString &why) {
            const int before = int(failed.count());
            if (!icons->rename("file:a2.txt", name).isEmpty() || failed.count() != before + 1)
                return false;
            const auto message = failed.last()[0].toString();
            return message.startsWith("Could not rename a2.txt: ") && message.contains(why);
        };
        QVERIFY(refused("", "empty"));
        QVERIFY(refused("x/y.txt", "/"));
        QVERIFY(refused(".", "not a name"));
        QVERIFY(refused("..", "not a name"));
        QVERIFY(refused("b.txt", "something called b.txt"));
        QVERIFY(refused("Zeta", "something called Zeta"));
        QVERIFY(QFile::exists(folder_ + "/a2.txt") && keys(*icons).contains("file:a2.txt"));
        // A launcher's name is the configuration's, a desktop entry's its Name: neither is
        // renamed, nor is anything not there.
        QVERIFY(write(folder_ + "/thing.desktop", "[Desktop Entry]\nType=Application\nName=Thing\nExec=true\n"));
        icons->rescan();
        const int before = int(failed.count());
        QVERIFY(icons->rename("launcher:Terminal", "Shell").isEmpty());
        QVERIFY(icons->rename("file:thing.desktop", "other.desktop").isEmpty());
        QVERIFY(icons->rename("file:nothing", "else").isEmpty());
        QCOMPARE(failed.count(), before);
        QVERIFY(QFile::exists(folder_ + "/thing.desktop"));
        // One whose file went since says why.
        QVERIFY(QFile::remove(folder_ + "/b.txt"));
        QVERIFY(icons->rename("file:b.txt", "c.txt").isEmpty());
        QCOMPARE(failed.count(), before + 1);
    }
    void makesNewFolders() {
        {
            auto icons = made();
            QSignalSpy failed(icons.get(), &DesktopIcons::failed);
            // In the free cell asked for, at once.
            QCOMPARE(icons->newFolder(3, 1), QString("file:New folder"));
            QVERIFY(QFileInfo(folder_ + "/New folder").isDir());
            QCOMPARE(value(*icons, "file:New folder", DesktopIcons::KindRole).toString(), "folder");
            QCOMPARE(cell(*icons, "file:New folder"), QPoint(3, 1));
            // The next takes the next name, and beside it, its cell being taken; one off the grid
            // comes onto it.
            QCOMPARE(icons->newFolder(3, 1), QString("file:New folder 2"));
            QCOMPARE(cell(*icons, "file:New folder 2"), QPoint(2, 1));
            QCOMPARE(icons->newFolder(9, 9), QString("file:New folder 3"));
            QCOMPARE(cell(*icons, "file:New folder 3"), QPoint(3, 2));
            QVERIFY2(failed.isEmpty(), qPrintable(failed.value(0).value(0).toString()));
            // The others stay where they were.
            QCOMPARE(cell(*icons, "file:b.txt"), QPoint(1, 1));
        }
        QCOMPARE(cell(*made(), "file:New folder 2"), QPoint(2, 1));
        // A desktop folder not there yet is made first.
        settings_.folder = home_.filePath("Fresh");
        auto fresh = made();
        QCOMPARE(fresh->newFolder(0, 0), QString("file:New folder"));
        QVERIFY(QFileInfo(settings_.folder + "/New folder").isDir());
        QCOMPARE(cell(*fresh, "file:New folder"), QPoint(0, 1));
        settings_.folder = folder_;
    }
    void trustsExecutableDesktopEntries() {
        const auto marker = home_.filePath("started");
        const auto entry = folder_ + "/thing.desktop";
        QVERIFY(write(entry, ("[Desktop Entry]\nType=Application\nName=Thing\nIcon=thing-icon\n"
                              "Exec=touch " + marker + "\n").toUtf8()));
        auto icons = made();
        QCOMPARE(value(*icons, "file:thing.desktop", DesktopIcons::KindRole).toString(), "app");
        QCOMPARE(value(*icons, "file:thing.desktop", DesktopIcons::NameRole).toString(), "Thing");
        QCOMPARE(value(*icons, "file:thing.desktop", DesktopIcons::IconRole).toString(), "thing-icon");
        QSignalSpy failed(icons.get(), &DesktopIcons::failed);
        QVERIFY(!icons->open("file:thing.desktop"));
        QCOMPARE(failed.count(), 1);
        QVERIFY(failed[0][0].toString().contains("not trusted"));
        QVERIFY(QFile::setPermissions(entry, QFile::permissions(entry) | QFile::ExeOwner));
        QVERIFY(icons->open("file:thing.desktop"));
        QTRY_VERIFY(QFile::exists(marker));
        // A file gone since it was read says so; a launcher is the controller's.
        QVERIFY(QFile::remove(folder_ + "/b.txt"));
        QVERIFY(!icons->open("file:b.txt"));
        QVERIFY(failed.last()[0].toString().contains("no longer there"));
        QVERIFY(!icons->open("launcher:Terminal"));
    }
    void dropsFilesOnTheDesktop() {
        const auto outside = home_.filePath("outside");
        QVERIFY(write(outside + "/report.txt", "report\n"));
        QVERIFY(QDir().mkpath(outside + "/Album/deep"));
        QVERIFY(write(outside + "/Album/one.txt", "one\n"));
        QVERIFY(write(outside + "/Album/deep/two.txt", "two\n"));
        QVERIFY(QFile::link("one.txt", outside + "/Album/link"));
        QVERIFY(QFile::setPermissions(outside + "/Album/one.txt",
                                      QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        auto icons = made();
        QSignalSpy failed(icons.get(), &DesktopIcons::failed);
        // Where they would land: the first in the free cell under the pointer, the next beside it.
        const auto landing = icons->dropLanding(files({outside + "/report.txt", outside + "/Album"}), 3, 0);
        QCOMPARE(landing.size(), 2);
        QCOMPARE(landing[0].toMap()["column"].toInt(), 3);
        QCOMPARE(landing[1].toMap()["column"].toInt(), 2);
        QCOMPARE(landing[1].toMap()["row"].toInt(), 0);
        // Copied in, a folder with all it holds, each in its cell; the originals stay.
        QCOMPARE(icons->drop(files({outside + "/report.txt", outside + "/Album"}), 3, 0, false), 2);
        QTRY_VERIFY(arrived(*icons, "file:report.txt") && arrived(*icons, "file:Album"));
        QCOMPARE(cell(*icons, "file:report.txt"), QPoint(3, 0));
        QCOMPARE(cell(*icons, "file:Album"), QPoint(2, 0));
        QFile copied(folder_ + "/Album/deep/two.txt");
        QVERIFY(copied.open(QIODevice::ReadOnly) && copied.readAll() == "two\n");
        QCOMPARE(QFileInfo(folder_ + "/Album/link").symLinkTarget(), folder_ + "/Album/one.txt");
        QVERIFY(QFileInfo(folder_ + "/Album/one.txt").isExecutable());
        QVERIFY(QFile::exists(outside + "/report.txt") && QFile::exists(outside + "/Album/one.txt"));
        // Dropped again, nothing is written over: the copies are numbered.
        QCOMPARE(icons->drop(files({outside + "/report.txt", outside + "/Album"}), 3, 2, false), 2);
        QTRY_VERIFY(arrived(*icons, "file:report (2).txt") && arrived(*icons, "file:Album (2)"));
        QCOMPARE(cell(*icons, "file:report (2).txt"), QPoint(3, 2));
        QVERIFY(QFile::exists(folder_ + "/Album (2)/deep/two.txt"));
        QFile kept(folder_ + "/report.txt");
        QVERIFY(kept.open(QIODevice::ReadOnly) && kept.readAll() == "report\n");
        // Moved, as a source proposing it asks, the original goes.
        QVERIFY(write(outside + "/moved.txt"));
        QCOMPARE(icons->drop(files({outside + "/moved.txt"}), 1, 2, true), 1);
        QTRY_VERIFY(arrived(*icons, "file:moved.txt"));
        QVERIFY(!QFile::exists(outside + "/moved.txt"));
        QCOMPARE(cell(*icons, "file:moved.txt"), QPoint(1, 2));
        // One on the desktop already just moves its icon, copying nothing.
        const int before = icons->count();
        QCOMPARE(icons->drop(files({folder_ + "/b.txt"}), 3, 1, false), 1);
        QCOMPARE(cell(*icons, "file:b.txt"), QPoint(3, 1));
        icons->rescan();
        QCOMPARE(icons->count(), before);
        QVERIFY(!QFile::exists(folder_ + "/b (2).txt"));
        QVERIFY2(failed.isEmpty(), qPrintable(failed.value(0).value(0).toString()));
        // Where they landed is remembered.
        QCOMPARE(cell(*made(), "file:report.txt"), QPoint(3, 0));
        // A folder that holds the desktop folder cannot go into it; nor do addresses of nothing.
        QCOMPARE(icons->drop(files({home_.path(), outside + "/nothing"}) << QUrl("trash:///x"), 0, 0, false), 1);
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY(failed[0][0].toString().contains("cannot go into itself"));
    }
    void numbersNamesAsFileManagersDo() {
        const auto outside = home_.filePath("outside");
        for (const auto *name : {"backup.tar.gz", ".profile", "notes.v2.md"}) {
            QVERIFY(write(outside + "/" + name));
            QVERIFY(write(folder_ + "/" + name));
        }
        auto icons = made(6, 4);
        QCOMPARE(icons->drop(files({outside + "/backup.tar.gz", outside + "/.profile",
                                    outside + "/notes.v2.md", outside + "/notes.v2.md"}), 5, 0, false), 4);
        for (const auto *name : {"backup (2).tar.gz", ".profile (2)", "notes.v2 (2).md", "notes.v2 (3).md"})
            QTRY_VERIFY2(QFile::exists(folder_ + "/" + name), name);
    }
    void dropsIntoFolders() {
        const auto outside = home_.filePath("outside");
        QVERIFY(write(outside + "/x.txt"));
        QVERIFY(write(folder_ + "/Zeta/kept.txt"));
        auto icons = made();
        QSignalSpy failed(icons.get(), &DesktopIcons::failed);
        QCOMPARE(icons->dropInto("file:Zeta", files({outside + "/x.txt"}), false), 1);
        QTRY_VERIFY(QFile::exists(folder_ + "/Zeta/x.txt"));
        QCOMPARE(icons->dropInto("file:Zeta", files({outside + "/x.txt"}), false), 1);
        QTRY_VERIFY(QFile::exists(folder_ + "/Zeta/x (2).txt"));
        // A file there already, and the folder itself, stay where they are; nor is a file a folder
        // to drop into.
        QCOMPARE(icons->dropInto("file:Zeta", files({folder_ + "/Zeta/kept.txt", folder_ + "/Zeta"}), false), 0);
        QCOMPARE(icons->dropInto("file:b.txt", files({outside + "/x.txt"}), false), 0);
        // Desktop files moved in leave the desktop; a launcher, or the folder itself, stays.
        QCOMPARE(icons->dropInto("file:Zeta", files({folder_ + "/a2.txt"}), true), 1);
        QTRY_VERIFY(!keys(*icons).contains("file:a2.txt"));
        QVERIFY(QFile::exists(folder_ + "/Zeta/a2.txt"));
        QCOMPARE(icons->moveInto("file:Zeta", {"file:a10.txt", "launcher:Terminal", "file:Zeta"}), 1);
        QTRY_VERIFY(!keys(*icons).contains("file:a10.txt"));
        QVERIFY(QFile::exists(folder_ + "/Zeta/a10.txt") && keys(*icons).contains("launcher:Terminal"));
        QVERIFY2(failed.isEmpty(), qPrintable(failed.value(0).value(0).toString()));
    }
    void makesLinksAndPictures() {
        auto icons = made();
        // Named from the title a browser gives it, else from its host; executable, so that it opens.
        const QString moz = "https://www.example.com/page\nAn example/page\n";
        const QByteArray utf16(reinterpret_cast<const char *>(moz.utf16()), moz.size() * 2);
        QCOMPARE(icons->drop({QUrl("https://www.example.com/page"), QUrl("http://paw.test/x")}, 3, 0,
                             false, utf16), 2);
        QTRY_VERIFY(arrived(*icons, "file:An example-page.desktop") && arrived(*icons, "file:paw.test.desktop"));
        QCOMPARE(value(*icons, "file:An example-page.desktop", DesktopIcons::KindRole).toString(), "link");
        QCOMPARE(value(*icons, "file:An example-page.desktop", DesktopIcons::NameRole).toString(), "An example/page");
        QCOMPARE(value(*icons, "file:paw.test.desktop", DesktopIcons::NameRole).toString(), "paw.test");
        QVERIFY(QFileInfo(folder_ + "/paw.test.desktop").isExecutable());
        QCOMPARE(cell(*icons, "file:An example-page.desktop"), QPoint(3, 0));
        QFile link(folder_ + "/paw.test.desktop");
        QVERIFY(link.open(QIODevice::ReadOnly) && link.readAll().contains("\nURL=http://paw.test/x\n"));
        // A picture is saved under its own name, with its type's extension.
        QImage picture(4, 4, QImage::Format_RGB32);
        picture.fill(Qt::cyan);
        QByteArray png;
        QBuffer buffer(&png);
        QVERIFY(buffer.open(QIODevice::WriteOnly) && picture.save(&buffer, "PNG"));
        QCOMPARE(icons->dropPicture(png, "image/png", QUrl("https://paw.test/cat.jpg?size=2"), 3, 2),
                 QString("file:cat.png"));
        QCOMPARE(icons->dropPicture(png, "image/png", QUrl("https://paw.test/"), 3, 2), QString("file:Picture.png"));
        QCOMPARE(icons->dropPicture(png, "image/png", QUrl("https://paw.test/again/cat.png"), 3, 2),
                 QString("file:cat (2).png"));
        QTRY_VERIFY(arrived(*icons, "file:cat (2).png"));
        QCOMPARE(cell(*icons, "file:cat.png"), QPoint(3, 2));
        QVERIFY(!value(*icons, "file:cat.png", DesktopIcons::PictureRole).toString().isEmpty());
        QVERIFY(QImage(folder_ + "/Picture.png").pixelColor(0, 0) == QColor(Qt::cyan));
    }
    void opensTrustedLinks() {
        const auto entry = folder_ + "/page.desktop";
        QVERIFY(write(entry, "[Desktop Entry]\nType=Link\nName=A page\nURL=pawtest://page\n"));
        QVERIFY(write(folder_ + "/drawn.desktop",
                      "[Desktop Entry]\nType=Link\nName=Drawn\nIcon=drawn-icon\nURL=pawtest://drawn\n"));
        auto icons = made();
        QCOMPARE(value(*icons, "file:page.desktop", DesktopIcons::KindRole).toString(), "link");
        QCOMPARE(value(*icons, "file:page.desktop", DesktopIcons::NameRole).toString(), "A page");
        QVERIFY(value(*icons, "file:page.desktop", DesktopIcons::IconRole).toString().startsWith("text-html,"));
        QCOMPARE(value(*icons, "file:drawn.desktop", DesktopIcons::IconRole).toString(), "drawn-icon");
        // Not yet executable, it is not trusted to open what it points at.
        QSignalSpy failed(icons.get(), &DesktopIcons::failed);
        QVERIFY(!icons->open("file:page.desktop"));
        QCOMPARE(failed.count(), 1);
        QVERIFY(failed[0][0].toString().contains("not trusted to open"));
        QVERIFY(!QFile::exists(home_.filePath("linked")));
        QVERIFY(QFile::setPermissions(entry, QFile::permissions(entry) | QFile::ExeOwner));
        QVERIFY2(icons->open("file:page.desktop"), qPrintable(failed.last()[0].toString()));
        QTRY_VERIFY(QFile::exists(home_.filePath("linked")));
        // Its name is its Name, not its file's: it is not renamed.
        QVERIFY(icons->rename("file:page.desktop", "other.desktop").isEmpty());
        QCOMPARE(failed.count(), 1);
    }
};

QTEST_MAIN(DesktopIconsTest)
#include "desktop_icons_test.moc"
