// SPDX-License-Identifier: GPL-3.0-or-later
#include "file_index.hpp"
#include "fuzzy.hpp"
#include "gio_launch.hpp"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHash>
#include <QMimeDatabase>
#include <QSet>
#include <QStandardPaths>
#include <QThread>
#include <QUrl>
#include <QXmlStreamReader>
#include <algorithm>
#include <cmath>
#include <deque>
#include <dirent.h>
#include <fcntl.h>
#include <set>
#include <sys/stat.h>
#include <unistd.h>

namespace {
// Names older than this are read again when a search next asks for them.
constexpr qint64 staleAfter = 5 * 60 * 1000;
// A change seen this soon after the names were read waits for a search after it.
constexpr qint64 restAfter = 10 * 1000;
// Recent files kept, and names scored for a search: those before them in the index (the recent
// files, then the folders nearest the top) come first.
constexpr int recentLimit = 500;
constexpr int scoredLimit = 400;

QString defaultRecent() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) +
           "/recently-used.xbel";
}

// Whether the folder open as `folder` is a tree whose contents are left out: version-controlled,
// or a build or cache tree.
bool leftOut(int folder) {
    struct stat info;
    for (const char *marker :
         {".git", ".hg", ".svn", ".bzr", "_darcs", "CMakeCache.txt", "CACHEDIR.TAG"})
        if (fstatat(folder, marker, &info, AT_SYMLINK_NOFOLLOW) == 0)
            return true;
    return false;
}

bool leftOutByName(const QString &name) { return name == "node_modules" || name == "__pycache__"; }
} // namespace

FileIndex::FileIndex(QObject *parent) : QObject(parent) {}

FileIndex::~FileIndex() {
    if (thread_) {
        thread_->disconnect(this);
        *cancelled_ = true;
        thread_->wait();
        delete thread_;
    }
}

void FileIndex::configure(const Settings &settings) {
    if (settings == settings_)
        return;
    settings_ = settings;
    if (thread_)
        *cancelled_ = true;
    // Read again at the next search.
    stale_ = true;
    age_.invalidate();
    if (!settings_.enabled) {
        found_ = {};
        if (watcher_)
            watcher_->removePaths(watcher_->files() + watcher_->directories());
        Q_EMIT changed();
    }
}

void FileIndex::preview(const Found &found) {
    previewOnly_ = true;
    found_ = found;
    age_.start();
    Q_EMIT changed();
}

QString FileIndex::path(const File &file) const {
    const auto &folder = found_.folders.value(file.folder);
    return folder == "/" ? '/' + file.name : folder + '/' + file.name;
}

QString FileIndex::shown(const QString &path) {
    const auto home = QDir::homePath();
    if (path == home)
        return "~";
    if (home != "/" && path.startsWith(home + '/'))
        return '~' + path.sliced(home.size());
    return path;
}

QStringList FileIndex::defaultRoots() {
    QStringList roots;
    const auto home = QDir::homePath();
    for (auto location : {QStandardPaths::DesktopLocation, QStandardPaths::DocumentsLocation,
                          QStandardPaths::DownloadLocation, QStandardPaths::PicturesLocation,
                          QStandardPaths::MusicLocation, QStandardPaths::MoviesLocation,
                          QStandardPaths::TemplatesLocation, QStandardPaths::PublicShareLocation}) {
        const auto folder = QDir::cleanPath(QStandardPaths::writableLocation(location));
        if (!folder.isEmpty() && folder != home && QFileInfo(folder).isDir() &&
            !roots.contains(folder))
            roots.push_back(folder);
    }
    roots.push_back(home);
    return roots;
}

void FileIndex::refresh() {
    if (previewOnly_ || !settings_.enabled || thread_)
        return;
    const bool never = !age_.isValid();
    if (!never && !(stale_ && age_.elapsed() >= restAfter) && age_.elapsed() < staleAfter)
        return;
    stale_ = false;
    // Where to look is found here, the thread only reading.
    auto settings = settings_;
    if (settings.roots.isEmpty())
        settings.roots = defaultRoots();
    if (settings.recent.isEmpty())
        settings.recent = defaultRecent();
    cancelled_ = std::make_shared<std::atomic<bool>>(false);
    auto result = std::make_shared<Found>();
    thread_ = QThread::create(
        [settings, result, cancelled = cancelled_] { *result = scan(settings, *cancelled); });
    connect(thread_, &QThread::finished, this, [this, result, settings, cancelled = cancelled_] {
        thread_->deleteLater();
        thread_ = nullptr;
        // Settings changed meanwhile: what was read is for the old ones.
        if (*cancelled)
            return;
        found_ = std::move(*result);
        age_.start();
        watch(settings);
        Q_EMIT changed();
    });
    thread_->start(QThread::LowestPriority);
}

// Watches the roots but the home folder, whose own files change all the time (every program's
// settings), and the recent files: a change makes the names out of date.
void FileIndex::watch(const Settings &settings) {
    if (!watcher_) {
        watcher_ = new QFileSystemWatcher(this);
        connect(watcher_, &QFileSystemWatcher::directoryChanged, this, [this] { stale_ = true; });
        connect(watcher_, &QFileSystemWatcher::fileChanged, this, [this] { stale_ = true; });
    }
    const auto watched = watcher_->files() + watcher_->directories();
    if (!watched.isEmpty())
        watcher_->removePaths(watched);
    QStringList paths;
    for (const auto &root : settings.roots)
        if (root != QDir::homePath() || !settings_.roots.isEmpty())
            paths.push_back(root);
    if (QFileInfo::exists(settings.recent))
        paths.push_back(settings.recent);
    if (!paths.isEmpty())
        watcher_->addPaths(paths);
}

std::vector<std::pair<QString, qint64>> FileIndex::readRecent(const QString &path, int limit) {
    std::vector<std::pair<QString, qint64>> recent;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return recent;
    QXmlStreamReader xml(&file);
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement ||
            xml.name() != QLatin1String("bookmark"))
            continue;
        const auto attributes = xml.attributes();
        const QUrl url(attributes.value("href").toString());
        if (!url.isLocalFile())
            continue;
        // The latest of when it was added, changed and visited.
        qint64 when = 0;
        for (const char *name : {"added", "modified", "visited"}) {
            const auto stamp =
                QDateTime::fromString(attributes.value(name).toString(), Qt::ISODateWithMs);
            if (stamp.isValid())
                when = std::max(when, stamp.toMSecsSinceEpoch());
        }
        recent.push_back({QDir::cleanPath(url.toLocalFile()), when});
    }
    std::stable_sort(recent.begin(), recent.end(),
                     [](const auto &a, const auto &b) { return a.second > b.second; });
    if (static_cast<int>(recent.size()) > limit)
        recent.resize(limit);
    return recent;
}

FileIndex::Found FileIndex::scan(const Settings &settings, const std::atomic<bool> &cancelled) {
    Found found;
    QHash<QString, int> folderIndex;
    auto folderOf = [&](const QString &folder) {
        auto it = folderIndex.constFind(folder);
        if (it != folderIndex.cend())
            return *it;
        found.folders.push_back(folder);
        return *folderIndex.insert(folder, static_cast<int>(found.folders.size() - 1));
    };
    auto add = [&](int folder, const QString &name, bool isFolder, qint64 used) {
        found.files.push_back({folder, name, name.toCaseFolded(), isFolder, used});
    };
    // The recent files first, wherever they are, so that the folders do not find them again.
    QSet<QString> recent;
    for (const auto &[path, when] : readRecent(settings.recent, recentLimit)) {
        const QFileInfo info(path);
        if (cancelled || recent.contains(path) || !info.exists())
            continue;
        recent.insert(path);
        add(folderOf(info.absolutePath()), info.fileName(), info.isDir(),
            std::max<qint64>(when, 1));
    }
    // The folders breadth first, every root's first level before any second level, so that the
    // limit leaves out what is deepest.
    struct Pending {
        QString path;
        int depth;
        dev_t device;
        bool root;
    };
    std::deque<Pending> queue;
    for (const auto &root : settings.roots)
        queue.push_back({QDir::cleanPath(root), 0, 0, true});
    std::set<std::pair<dev_t, ino_t>> listed;
    int names = 0;
    while (!queue.empty() && names < settings.limit && !cancelled) {
        const Pending folder = queue.front();
        queue.pop_front();
        // A root may be a link to where it is kept; nothing under one is followed.
        const QByteArray encoded = QFile::encodeName(folder.path);
        const int descriptor = ::open(encoded.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC |
                                                               (folder.root ? 0 : O_NOFOLLOW));
        if (descriptor < 0)
            continue;
        struct stat info;
        if (fstat(descriptor, &info) != 0 || (!folder.root && info.st_dev != folder.device) ||
            !listed.insert({info.st_dev, info.st_ino}).second || leftOut(descriptor)) {
            close(descriptor);
            continue;
        }
        DIR *entries = fdopendir(descriptor);
        if (!entries) {
            close(descriptor);
            continue;
        }
        const int index = folderOf(folder.path);
        const QString prefix = folder.path == "/" ? QString() : folder.path;
        while (const dirent *entry = readdir(entries)) {
            if (entry->d_name[0] == '.' || names >= settings.limit)
                continue; // hidden, or . and ..
            const QString name = QFile::decodeName(entry->d_name);
            const QString path = prefix + '/' + name;
            bool isFolder = entry->d_type == DT_DIR;
            if (entry->d_type == DT_UNKNOWN) {
                struct stat child;
                if (fstatat(dirfd(entries), entry->d_name, &child, AT_SYMLINK_NOFOLLOW) != 0)
                    continue;
                isFolder = S_ISDIR(child.st_mode);
            }
            if (recent.contains(path))
                continue;
            add(index, name, isFolder, 0);
            ++names;
            if (isFolder && folder.depth < settings.depth && !leftOutByName(name))
                queue.push_back({path, folder.depth + 1, info.st_dev, false});
        }
        closedir(entries);
    }
    return found;
}

QVariantList FileIndex::search(const QString &query, int limit) {
    if (!settings_.enabled)
        return {};
    refresh();
    const auto parts = fuzzy::words(query);
    if (parts.isEmpty() || found_.files.empty() || limit <= 0)
        return {};
    // Words with a slash are looked for in the path, the others in the name.
    QStringList inName, inPath;
    for (const auto &part : parts)
        (part.contains('/') ? inPath : inName).push_back(part);
    struct Hit {
        double score;
        const File *file;
    };
    std::vector<Hit> hits;
    for (const auto &file : found_.files) {
        if (!std::all_of(inName.begin(), inName.end(),
                         [&file](const QString &part) { return file.folded.contains(part); }))
            continue;
        if (!inPath.isEmpty()) {
            const auto folded = shown(path(file)).toCaseFolded();
            if (!std::all_of(inPath.begin(), inPath.end(),
                             [&folded](const QString &part) { return folded.contains(part); }))
                continue;
        }
        hits.push_back({0, &file});
        if (static_cast<int>(hits.size()) >= scoredLimit)
            break;
    }
    // Scored as the palette scores a title, a file used lately a little ahead, the more so the
    // more lately.
    const auto now = QDateTime::currentMSecsSinceEpoch();
    for (auto &hit : hits) {
        hit.score = inName.isEmpty() ? 10 : std::max(1.0, fuzzy::scoreWords(inName, hit.file->name));
        if (hit.file->used > 0) {
            const double days = std::max<qint64>(0, now - hit.file->used) / 86400000.0;
            hit.score += 4 + 6 * std::exp(-days / 7);
        }
    }
    std::stable_sort(hits.begin(), hits.end(),
                     [](const Hit &a, const Hit &b) { return a.score > b.score; });
    QMimeDatabase types;
    QVariantList results;
    for (const auto &hit : hits) {
        if (results.size() >= limit)
            break;
        const auto &file = *hit.file;
        const auto full = path(file);
        QString icon = "folder";
        if (!file.isFolder) {
            // By its name alone: nothing is read from the file.
            const auto type = types.mimeTypeForFile(full, QMimeDatabase::MatchExtension);
            icon = type.iconName() + ',' + type.genericIconName() + ",text-x-generic";
        }
        results.push_back(QVariantMap{{"kind", "file"},
                                      {"title", file.name},
                                      {"subtitle", shown(found_.folders.value(file.folder))},
                                      {"icon", icon},
                                      {"target", full},
                                      {"folder", file.isFolder},
                                      {"score", hit.score}});
    }
    return results;
}

QString FileIndex::open(const QString &path, bool folder) {
    const QString target = folder ? QFileInfo(path).absolutePath() : path;
    if (!QFileInfo::exists(target))
        return "it is no longer there";
    return gioOpen(QUrl::fromLocalFile(target).toEncoded());
}
