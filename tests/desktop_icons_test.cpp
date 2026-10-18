// SPDX-License-Identifier: GPL-3.0-or-later
// The desktop's icons: the launchers, then the desktop folder's folders and files by name; their
// cells on the grid, remembered with whether they are hidden; the folder followed as it changes;
// opening, copying, renaming and trashing files, new folders, and desktop entries trusted only once
// executable.
#include "desktop_icons.hpp"
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
    }
    void init() {
        QDir(folder_).removeRecursively();
        QDir(home_.filePath("state")).removeRecursively();
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
};

QTEST_MAIN(DesktopIconsTest)
#include "desktop_icons_test.moc"
