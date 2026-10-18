// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QAbstractListModel>
#include <QHash>
#include <QList>
#include <QPoint>
#include <QSize>
#include <QStringList>
#include <QTimer>

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
    // Lays every icon out anew down the columns: the launchers, the folders, then the files, each
    // by name.
    Q_INVOKABLE void tidy();
    // Opens the file of `key`: a folder in the file manager, a file in its application, a desktop
    // entry's application when the entry is executable, as file managers trust one. False, with
    // `failed`, when it did not start; a launcher is the controller's to start.
    Q_INVOKABLE bool open(const QString &key);
    // Puts the files of `keys` on the clipboard, for a file manager to paste copies of.
    Q_INVOKABLE void copy(const QStringList &keys);
    // Moves the files of `keys` to the trash; how many went.
    Q_INVOKABLE int trash(const QStringList &keys);
    // Opens the folder in the file manager, making it first where it is missing.
    Q_INVOKABLE bool openFolder();
    // Reads the folder now rather than once its changes settle.
    void rescan();
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
    Settings settings_;
    QList<Launcher> launchers_;
    QList<Entry> entries_;
    QStringList order_;              // the keys, launchers first, then folders and files by name
    QHash<QString, QPoint> cells_;   // where each icon was put, as remembered
    QSize grid_;                     // invalid until the desktop says
    bool hidden_ = false;
    QByteArray saved_;               // what the state file holds, so as to write it on a change
    QFileSystemWatcher *watcher_ = nullptr;
    QTimer settle_;                  // the folder's changes settling before it is read
    QList<Entry> read() const;
    void sync(QList<Entry> entries);
    void relayout();
    void watch();
    void save();
    int indexOf(const QString &key) const;
};
