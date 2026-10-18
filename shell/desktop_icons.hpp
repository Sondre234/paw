// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QPoint>
#include <QSize>
#include <QStringList>
#include <QTimer>

class QFileInfo;
class QFileSystemWatcher;

// The icons on the desktop (shell.desktop_icons): the configured launchers, then the folders and
// the files of the desktop folder, each in a cell of the grid the main monitor's desktop has room
// for (desktop_grid). An icon keeps the cell it was given or dragged to, remembered across
// sessions in the state folder's desktop-icons with whether they are hidden; a new one takes the
// first free cell down the columns. The folder is watched and read again as it changes, its
// rows changed in place, so that an icon's delegate stays as it moves.
class DesktopIcons : public QAbstractListModel {
    Q_OBJECT
    // Whether the configuration shows icons, and whether they show now: not while the desktop's
    // menu or the desktop_icons action hides them.
    Q_PROPERTY(bool enabled READ enabled NOTIFY configured)
    Q_PROPERTY(bool shown READ shown NOTIFY shownChanged)
    // The icons' size in pixels, and the folder whose files show, an absolute path.
    Q_PROPERTY(int iconSize READ iconSize NOTIFY configured)
    Q_PROPERTY(QString folder READ folder NOTIFY configured)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // The names of the files and folders the last trash() moved, while untrash() can put them
    // back: until it has, or another trash() moves others.
    Q_PROPERTY(QStringList trashed READ trashed NOTIFY trashedChanged)
  public:
    enum Role {
        KeyRole = Qt::UserRole + 1, // "launcher:NAME" or "file:NAME", NAME the file's
        NameRole,
        IconRole,    // for image://icons/: a theme icon's names by commas, or a path
        PictureRole, // a picture file's URL, drawn in place of its icon; "" for others
        PathRole,    // the file's absolute path; "" for a launcher
        KindRole,    // "launcher", "folder", "file" or "app" (a desktop entry)
        AppIdRole,   // a launcher's id, for ShellController::launch
        ColumnRole,
        RowRole,
    };
    struct Launcher {
        QString appId, name, icon;
    };
    struct Settings {
        bool enabled = true;
        QString folder; // absolute
        int size = 48;
    };
    explicit DesktopIcons(QObject *parent = nullptr);
    // From each configuration loaded, with its launchers.
    void configure(const Settings &settings, const QList<Launcher> &launchers);
    // "toggle", "on" or "off", as the compositor's `desktop-icons` line has it; false, changing
    // nothing, for anything else.
    bool handle(const QString &verb);
    bool enabled() const { return settings_.enabled; }
    bool shown() const { return settings_.enabled && !hidden_; }
    int iconSize() const { return settings_.size; }
    QString folder() const { return settings_.folder; }
    int count() const { return int(entries_.size()); }
    // Shows or hides them, remembered across sessions.
    Q_INVOKABLE void setShown(bool shown);
    // The grid the desktop has room for, `columns` by `rows` cells; until it is set no icon has a
    // cell (both -1).
    Q_INVOKABLE void setGrid(int columns, int rows);
    // Moves the icons of `keys`, the one under the pointer first, `columns` and `rows` cells, as
    // dragged together (desktop_grid::drop).
    Q_INVOKABLE void move(const QStringList &keys, int columns, int rows);
    // Where move() would put them: {column, row} for each of `keys` there is, in their order.
    Q_INVOKABLE QVariantList landing(const QStringList &keys, int columns, int rows) const;
    // Lays every icon out anew down the columns: the launchers, the folders, then the files, each
    // by name.
    Q_INVOKABLE void tidy();
    // Opens the file of `key`: a folder in the file manager, a file in its application, a desktop
    // entry's application when the entry is executable, as file managers trust one. False, with
    // `failed`, when it did not start; a launcher is the controller's to start.
    Q_INVOKABLE bool open(const QString &key);
    // The applications that open every file and folder of `keys`, for the icons' Open with:
    // {id (a desktop id), name, icon, group}, none twice. The default application for them all
    // comes first, its group "default"; then those recommended for each one's type
    // ("recommended", in GIO's order); then the others that take them all, as one for a type they
    // are a kind of ("other", by name). None for a launcher or a desktop entry among them.
    Q_INVOKABLE QVariantList openers(const QStringList &keys) const;
    // Opens the files and folders of `keys` in the application `appId`, a desktop id of
    // openers(): all in one go when it takes several, as GIO starts it. False, with `failed`, when
    // it did not start.
    Q_INVOKABLE bool openWith(const QStringList &keys, const QString &appId);
    // Puts the files of `keys` on the clipboard, for a file manager to paste copies of.
    Q_INVOKABLE void copy(const QStringList &keys);
    // Moves the files of `keys` to the trash; how many went. What went can be put back with
    // untrash() until the next trash() that moves something.
    Q_INVOKABLE int trash(const QStringList &keys);
    // Puts back what the last trash() moved, as file managers restore from the trash: each file
    // or folder where it was, under its name, or "name (2)" while something else has that now
    // (never over anything), its .trashinfo gone, and its icon in the cell it left, or the free
    // cell nearest it while that is taken. The keys of those put back, at once; one gone from the
    // trash since is said with `failed`.
    Q_INVOKABLE QStringList untrash();
    QStringList trashed() const;
    // Renames the file or folder of `key` to `name`, in the desktop folder; its icon keeps its cell
    // under its new key, which it returns. "" with `failed` when the name is empty, has a / in it,
    // is . or .., or is another file's there, or the file could not be renamed; "" alone for a
    // launcher or a desktop entry, whose name is not its file's.
    Q_INVOKABLE QString rename(const QString &key, const QString &name);
    // Makes a folder called "New folder" ("New folder 2", ... while that is taken) in the desktop
    // folder, making that first where it is missing, and shows it at once in the cell at `column`
    // and `row` while that is free, else in the free cell nearest it; its key, or "" with
    // `failed`.
    Q_INVOKABLE QString newFolder(int column, int row);
    // Opens the folder in the file manager, making it first where it is missing.
    Q_INVOKABLE bool openFolder();
    // Reads the folder now rather than once its changes settle.
    void rescan();
    // The .trashinfo of the file a trash keeps at `inTrash`, in its files folder: the one of the
    // same name in the info folder beside that, whether the trash is the home one or a mount's own
    // ($topdir/.Trash/$uid, $topdir/.Trash-$uid).
    static QString trashInfo(const QString &inTrash);
    // A name for `name` in `folder` that nothing there has: itself while it is free, else
    // numbered before its extension (a folder's whole name), "notes (2).txt", "Photos (3)".
    static QString freeName(const QString &folder, const QString &name, bool isFolder);
    // Whether cells and hiding are remembered in the state folder; a preview's, on a grid of its
    // own, are not.
    void setRemembering(bool remembering) { remembering_ = remembering; }
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
  Q_SIGNALS:
    void configured();
    void shownChanged();
    void countChanged();
    // A trash() moved something (each time, even the same names), or untrash() put it back.
    void trashedChanged();
    // Something asked of it went wrong, in a sentence for the panel.
    void failed(const QString &message);

  private:
    struct Entry {
        QString key, name, icon, picture, path, kind, appId;
        QPoint cell{-1, -1};
    };
    // A file the last trash() moved: where it was, where the trash keeps it, and the cell its
    // icon left, which the saved cells forget as it goes.
    struct Trashed {
        QString path, inTrash;
        QPoint cell{-1, -1};
    };
    Settings settings_;
    QList<Launcher> launchers_;
    QList<Entry> entries_;
    QStringList order_;              // the keys, launchers first, then folders and files by name
    QHash<QString, QPoint> cells_;   // where each icon was put, as remembered
    QList<Trashed> trashed_;         // what untrash() puts back
    QSize grid_;                     // invalid until the desktop says
    bool hidden_ = false, remembering_ = true;
    QByteArray saved_;               // what the state file holds, so as to write it on a change
    QFileSystemWatcher *watcher_ = nullptr;
    QTimer settle_;                  // the folder's changes settling before it is read
    QList<Entry> read() const;
    // A file's or folder's entry, without a cell.
    static Entry fileEntry(const QFileInfo &file);
    // The paths of the files and folders of `keys`, each once; none when one of them is a
    // launcher, a desktop entry or not there.
    QStringList filePaths(const QStringList &keys) const;
    // The rows of `keys` (none twice), and where they land moved by `offset`.
    QList<QPoint> landed(const QStringList &keys, QPoint offset, QList<int> &moved) const;
    void sync(QList<Entry> entries);
    void relayout();
    void watch();
    void save();
    int indexOf(const QString &key) const;
};
