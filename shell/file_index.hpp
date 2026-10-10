// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QElapsedTimer>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <atomic>
#include <memory>
#include <vector>

class QFileSystemWatcher;
class QThread;

// The files the command palette and the start menu's search find: those used lately, from
// recently-used.xbel, and the names of the files and folders in the home folder and the XDG user
// folders (or the configured ones).
//
// The folders are read on a thread of their own, breadth first, down to `depth` levels and
// `limit` names at most, leaving out hidden files and folders, what is on another file system,
// and what is inside a version-controlled tree (one holding .git, .hg, .svn, .bzr or _darcs), a
// build tree (CMakeCache.txt, or the CACHEDIR.TAG a cache directory has) or node_modules and
// __pycache__, though such a folder is found by its own name. Symbolic links are listed but not
// followed. They are read the first time a search asks for files, so that nothing is read
// before anyone searches, and again when a search asks once the names are older than five
// minutes or something changed in a folder searched from (one of the roots) or in the recent
// files; the search meanwhile finds what was read before, and `changed` says when the new names
// are in.
class FileIndex : public QObject {
    Q_OBJECT
  public:
    struct Settings {
        bool enabled = true;
        // The folders read, absolute; empty: the home folder and the XDG user folders.
        QStringList roots;
        int depth = 4;
        int limit = 20000;
        // The recently used files; empty: $XDG_DATA_HOME/recently-used.xbel.
        QString recent;
        bool operator==(const Settings &) const = default;
    };
    // A file or folder found. Its path is its folder's (an index into the folders) and its name.
    struct File {
        int folder = 0;
        QString name, folded; // the name as it is, and folded to lower case for matching
        bool isFolder = false;
        qint64 used = 0; // when it was last used, in ms since the epoch; 0 if not lately
    };
    struct Found {
        QStringList folders;
        std::vector<File> files;
    };

    explicit FileIndex(QObject *parent = nullptr);
    ~FileIndex() override;
    // Takes new settings; files are read again on the next search when they changed.
    void configure(const Settings &settings);
    // What `query` finds, best first, at most `limit`: {kind: "file", title (its name), subtitle
    // (its folder, ~ for the home folder), icon (its type's, then more generic ones, separated by
    // commas), target (its path), folder (whether it is one), score}. Each word of the query must
    // be in the name, as written in any case; one with a slash in it must be in the path instead
    // (`docs/report`). Starts reading the files when they are out of date.
    QVariantList search(const QString &query, int limit);
    // Whether the files are being read now.
    bool busy() const { return thread_ != nullptr; }
    // How many files and folders were found.
    int size() const { return static_cast<int>(found_.files.size()); }
    // For a preview: these files, never read from disk, and nothing read from now on.
    void preview(const Found &found);

    // The path of a file found.
    QString path(const File &file) const;
    // The folders read by default: the XDG user folders that exist, then the home folder.
    static QStringList defaultRoots();
    // Reads what `settings` say, as the thread does; stops early once `cancelled`.
    static Found scan(const Settings &settings, const std::atomic<bool> &cancelled);
    // The local files recently-used.xbel at `path` lists, the most recently used first, as
    // {path, when (ms since the epoch)}; at most `limit`.
    static std::vector<std::pair<QString, qint64>> readRecent(const QString &path, int limit);
    // Opens `path` in the default application for its type, or with `folder` the folder that
    // holds it in the default file manager, as GIO does. Returns what went wrong, empty when it
    // started.
    static QString open(const QString &path, bool folder);
    // `path` as the search shows it: `~` for the home folder.
    static QString shown(const QString &path);
  Q_SIGNALS:
    // New files were read.
    void changed();

  private:
    Settings settings_;
    Found found_;
    bool stale_ = true, previewOnly_ = false;
    QElapsedTimer age_;
    QThread *thread_ = nullptr;
    std::shared_ptr<std::atomic<bool>> cancelled_;
    QFileSystemWatcher *watcher_ = nullptr;
    // Starts reading the files when they are out of date and none are being read.
    void refresh();
    // Watches what was read with `resolved`, the settings with the default roots filled in.
    void watch(const Settings &resolved);
};
