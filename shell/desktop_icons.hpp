// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QPoint>
#include <QSet>
#include <QSize>
#include <QStringList>
#include <QThreadPool>
#include <QTimer>
#include <QUrl>
#include <atomic>

class QFileInfo;
class QFileSystemWatcher;

// The icons on the desktop (shell.desktop_icons): the configured launchers, then the folders and
// the files of the desktop folder, each in a cell of the grid the main monitor's desktop has room
// for (desktop_grid). An icon keeps the cell it was given or dragged to, remembered across
// sessions in the state folder's desktop-icons with whether they are hidden; a new one takes the
// first free cell down the columns. The folder is watched and read again as it changes, its
// rows changed in place, so that an icon's delegate stays as it moves. Files dropped on the
// desktop are copied or moved into the folder on a thread of its own, one drop after another,
// each icon showing in the cell it was dropped on as its file comes.
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
  public:
    enum Role {
        KeyRole = Qt::UserRole + 1, // "launcher:NAME" or "file:NAME", NAME the file's
        NameRole,
        IconRole,    // for image://icons/: a theme icon's names by commas, or a path
        PictureRole, // a picture file's URL, drawn in place of its icon; "" for others
        PathRole,    // the file's absolute path; "" for a launcher
        KindRole,    // "launcher", "folder", "file", "app" (a desktop entry) or "link" (a web link)
        AppIdRole,   // a launcher's id, for ShellController::launch
        ColumnRole,
        RowRole,
        ArrivingRole, // whether its file is being copied or moved in, dropped on the desktop
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
    // Waits for the copy under way, which stops at the next of its files.
    ~DesktopIcons() override;
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
    // entry's application, or a link's address in the browser, when the entry is executable, as
    // file managers trust one. False, with `failed`, when it did not start; a launcher is the
    // controller's to start.
    Q_INVOKABLE bool open(const QString &key);
    // Puts the files of `keys` on the clipboard, for a file manager to paste copies of.
    Q_INVOKABLE void copy(const QStringList &keys);
    // Moves the files of `keys` to the trash; how many went.
    Q_INVOKABLE int trash(const QStringList &keys);
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
    // Where a drop of `urls` on the cell at `column` and `row` puts them: {column, row} for each
    // it takes (a file or folder, or a web address), in their order, the first in that cell while
    // it is free and each after it in the free cell nearest it then. One already in the desktop
    // folder is moved from its own cell, which is free to it.
    Q_INVOKABLE QVariantList dropLanding(const QList<QUrl> &urls, int column, int row) const;
    // Drops `urls` on the cell at `column` and `row`, each where dropLanding() says: a file or
    // folder already in the desktop folder just moves there; one from elsewhere is copied in, a
    // folder with all it holds, or moved with `move`, as a file manager takes a drag whose source
    // proposes moving it; a web address becomes a link file (a desktop entry of `Type=Link`, made
    // executable so that the desktop opens it) named from its title in `mozUrl`, a browser's
    // text/x-moz-url, or its host. A name taken there gets " (2)", " (3)", ...: nothing is ever
    // overwritten. How many it takes; what could not be copied or moved, `failed` says.
    Q_INVOKABLE int drop(const QList<QUrl> &urls, int column, int row, bool move,
                         const QByteArray &mozUrl = {});
    // Drops `urls` into the folder of `key`, a folder in the desktop folder, as drop() does into
    // the desktop folder; one there already, or the folder itself, is left where it is.
    Q_INVOKABLE int dropInto(const QString &key, const QList<QUrl> &urls, bool move,
                             const QByteArray &mozUrl = {});
    // Saves `data`, a picture of the MIME type `type` dragged from a browser, in the desktop
    // folder in the cell at `column` and `row`, named from `from`, its address, with the type's
    // extension ("Picture" without one); its key, or "" when there is nothing to save.
    Q_INVOKABLE QString dropPicture(const QByteArray &data, const QString &type, const QUrl &from,
                                    int column, int row);
    // Reads the folder now rather than once its changes settle.
    void rescan();
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
    // Something asked of it went wrong, in a sentence for the panel.
    void failed(const QString &message);

  private:
    struct Entry {
        QString key, name, icon, picture, path, kind, appId;
        QPoint cell{-1, -1};
    };
    // A file dropped on the desktop or a folder of it, as the copying thread takes it: copied or
    // moved from `from` (a folder with all it holds with `directory`), or written from `data` (a
    // link file, a picture), into the folder `into` as `name`, numbered from `number` on while that
    // is taken (" (2)", " (3)", ...); `here` when it is in `into` already, where its icon just
    // moves.
    struct Transfer {
        QString from;
        QByteArray data;
        QString into, name;
        int number = 1;
        bool move = false, executable = false, here = false, directory = false;
        QString named(int n) const;
    };
    Settings settings_;
    QList<Launcher> launchers_;
    QList<Entry> entries_;
    QStringList order_;              // the keys, launchers first, then folders and files by name
    QHash<QString, QPoint> cells_;   // where each icon was put, as remembered
    QSize grid_;                     // invalid until the desktop says
    bool hidden_ = false, remembering_ = true;
    QByteArray saved_;               // what the state file holds, so as to write it on a change
    QFileSystemWatcher *watcher_ = nullptr;
    QTimer settle_;                  // the folder's changes settling before it is read
    QSet<QString> arriving_;         // the keys of the files being copied or moved in
    QThreadPool transfers_;          // the drops' copies and moves, one after another
    std::atomic_bool stopping_{false};
    QList<Entry> read() const;
    // A file's or folder's entry, without a cell.
    static Entry fileEntry(const QFileInfo &file);
    // The rows of `keys` (none twice), and where they land moved by `offset`.
    QList<QPoint> landed(const QStringList &keys, QPoint offset, QList<int> &moved) const;
    void sync(QList<Entry> entries);
    void relayout();
    void watch();
    void save();
    int indexOf(const QString &key) const;
    // The cells a drop of `count` new icons at `cell` takes, the icons of `moving` (keys) free to
    // move.
    QList<QPoint> dropCells(int count, QPoint cell, const QStringList &moving) const;
    // What drop() and dropInto() make of `urls` dropped into `folder`, in their order: a transfer
    // for each they take.
    QList<Transfer> transfers(const QList<QUrl> &urls, const QString &folder, bool move,
                              const QByteArray &mozUrl) const;
    // Carries `transfer` out, on the copying thread, under its name or the first free one like it:
    // the name it took, or "" with `error`.
    static QString carry(const Transfer &transfer, const std::atomic_bool &stop, QString &error);
    // Copies or moves `list` in on the copying thread, those into the desktop folder under names
    // free there, showing in the cells of `cells`, by their order, as they come; their keys.
    QStringList start(QList<Transfer> list, const QList<QPoint> &cells);
    // `transfer` is over, its file called `name` (a file may have taken the name it was to have
    // since), or with `error`, why it could not be.
    void arrived(const Transfer &transfer, const QString &name, const QString &error);
};
