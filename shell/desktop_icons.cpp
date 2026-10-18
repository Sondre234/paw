// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop_icons.hpp"
#include "desktop_grid.hpp"
#include "gio_launch.hpp"
#include "icons.hpp"
#include "state_files.hpp"
#include <QClipboard>
#include <QCollator>
#include <QDir>
#include <QDrag>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QImageReader>
#include <QMimeData>
#include <QMimeDatabase>
#include <QPixmap>
#include <QSet>
#include <QUrl>
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <gio/gdesktopappinfo.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
// Where the icons' cells are remembered: a line `hidden` while they are hidden, and a line for
// each icon, its column, its row and its key, by tabs.
QString statePath() { return stateDir() + "/desktop-icons"; }
// The most files of the folder shown, the first by name.
constexpr int maxFiles = 512;
// A picture larger than this shows its type's icon rather than itself.
constexpr qint64 maxPicture = 32 * 1024 * 1024;

// `name` as the `n`th of its name in a folder, as file managers number a copy: " (n)" before its
// extension, a file's after its last dot ("notes (2).txt", "backup (2).tar.gz"), a folder's at the
// end ("Photos (2)"); the first is `name` itself.
QString numbered(const QString &name, int n, bool folder) {
    if (n <= 1)
        return name;
    qsizetype dot = folder ? -1 : name.lastIndexOf('.');
    if (dot > 0 && name.left(dot).endsWith(".tar"))
        dot -= 4;
    if (dot <= 0) // none, or a hidden file's
        dot = name.size();
    return name.left(dot) + " (" + QString::number(n) + ")" + name.mid(dot);
}

// Why the last system call failed, in a sentence.
QString systemError() { return QString::fromLocal8Bit(std::strerror(errno)); }

// Copies `from` to `to`, which it makes: a file with its permissions and modification time, a
// folder with all it holds, a symbolic link as a link; anything else (a pipe, a socket, a device)
// is not copied. It stops once `stop` is set. An error, or ""; `taken` is set when `to` was there
// already, `madeIt` once it has made `to`.
QString copyTree(const QByteArray &from, const QByteArray &to, const std::atomic_bool &stop,
                 bool *taken = nullptr, bool *madeIt = nullptr) {
    struct stat info;
    if (stop)
        return "the shell is quitting";
    if (lstat(from.constData(), &info) != 0)
        return systemError();
    auto made = [&](int result) {
        if (taken)
            *taken = result != 0 && errno == EEXIST;
        if (madeIt)
            *madeIt = result == 0;
        return result == 0;
    };
    if (S_ISLNK(info.st_mode)) {
        std::vector<char> target(size_t(std::max<off_t>(info.st_size, PATH_MAX)) + 1);
        const ssize_t length = readlink(from.constData(), target.data(), target.size() - 1);
        if (length < 0)
            return systemError();
        target[size_t(length)] = '\0';
        return made(symlink(target.data(), to.constData())) ? QString() : systemError();
    }
    if (S_ISDIR(info.st_mode)) {
        if (!made(mkdir(to.constData(), 0700)))
            return systemError();
        DIR *folder = opendir(from.constData());
        if (!folder)
            return systemError();
        QString error;
        while (error.isEmpty()) {
            errno = 0;
            const dirent *child = readdir(folder);
            if (!child) {
                if (errno != 0)
                    error = systemError();
                break;
            }
            const QByteArray name(child->d_name);
            if (name != "." && name != "..")
                error = copyTree(from + '/' + name, to + '/' + name, stop);
        }
        closedir(folder);
        const struct timespec times[2] = {{0, UTIME_OMIT}, info.st_mtim};
        if (error.isEmpty() && (chmod(to.constData(), info.st_mode & 07777) != 0 ||
                                utimensat(AT_FDCWD, to.constData(), times, 0) != 0))
            error = systemError();
        return error;
    }
    if (!S_ISREG(info.st_mode))
        return "it is not a file or a folder";
    const int in = open(from.constData(), O_RDONLY | O_CLOEXEC);
    if (in < 0)
        return systemError();
    const int out = open(to.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (!made(out < 0 ? -1 : 0)) {
        const auto error = systemError();
        close(in);
        return error;
    }
    QString error;
    std::vector<char> buffer(1 << 20);
    while (error.isEmpty()) {
        if (stop) {
            error = "the shell is quitting";
            break;
        }
        const ssize_t count = read(in, buffer.data(), buffer.size());
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0) {
            if (count < 0)
                error = systemError();
            break;
        }
        for (ssize_t written = 0; written < count && error.isEmpty();) {
            const ssize_t step = write(out, buffer.data() + written, size_t(count - written));
            if (step >= 0)
                written += step;
            else if (errno != EINTR)
                error = systemError();
        }
    }
    const struct timespec times[2] = {{0, UTIME_OMIT}, info.st_mtim};
    if (error.isEmpty() && (fchmod(out, info.st_mode & 07777) != 0 || futimens(out, times) != 0))
        error = systemError();
    if (close(out) != 0 && error.isEmpty())
        error = systemError();
    close(in);
    return error;
}

// Removes `path`, a folder with all it holds, a link but not what it points to.
void removeTree(const QString &path) {
    const QFileInfo file(path);
    if (file.isDir() && !file.isSymLink())
        QDir(path).removeRecursively();
    else
        QFile::remove(path);
}

// A desktop entry of `Type=Link`: its name, icon and address, all empty for any other file.
struct LinkEntry {
    QString name, icon, url;
};
LinkEntry readLink(const QString &path) {
    LinkEntry link;
    GKeyFile *keys = g_key_file_new();
    auto value = [keys](const char *key, bool localized) {
        gchar *text =
            localized ? g_key_file_get_locale_string(keys, "Desktop Entry", key, nullptr, nullptr)
                      : g_key_file_get_string(keys, "Desktop Entry", key, nullptr);
        const auto string = QString::fromUtf8(text ? text : "");
        g_free(text);
        return string;
    };
    if (g_key_file_load_from_file(keys, path.toUtf8().constData(), G_KEY_FILE_NONE, nullptr) &&
        value("Type", false) == "Link")
        link = {value("Name", true), value("Icon", false), value("URL", false)};
    g_key_file_free(keys);
    return link;
}

// The titles a browser gives the addresses it drags, by address, from its text/x-moz-url: lines of
// an address and its title, in UTF-16 (Firefox's and Chromium's) or UTF-8.
QHash<QString, QString> linkTitles(const QByteArray &mozUrl) {
    QString text = mozUrl.contains('\0')
                       ? QString::fromUtf16(reinterpret_cast<const char16_t *>(mozUrl.constData()),
                                            mozUrl.size() / 2)
                       : QString::fromUtf8(mozUrl);
    text.remove(QChar(0xfeff)).remove(QChar('\0')).remove('\r');
    const auto lines = text.split('\n');
    QHash<QString, QString> titles;
    for (qsizetype i = 0; i + 1 < lines.size(); i += 2)
        titles.insert(QUrl(lines[i].trimmed()).toString(), lines[i + 1].trimmed());
    return titles;
}
} // namespace

DesktopIcons::DesktopIcons(QObject *parent) : QAbstractListModel(parent) {
    settle_.setSingleShot(true);
    settle_.setInterval(150);
    transfers_.setMaxThreadCount(1);
    connect(&settle_, &QTimer::timeout, this, &DesktopIcons::rescan);
    QFile file(statePath());
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        saved_ = file.readAll();
        for (const auto &line : QString::fromUtf8(saved_).split('\n', Qt::SkipEmptyParts)) {
            if (line == "hidden") {
                hidden_ = true;
                continue;
            }
            const auto fields = line.split('\t');
            bool column = false, row = false;
            const QPoint cell(fields.value(0).toInt(&column), fields.value(1).toInt(&row));
            if (fields.size() == 3 && !fields[2].isEmpty() && column && row)
                cells_.insert(fields[2], cell);
        }
    }
}

DesktopIcons::~DesktopIcons() {
    stopping_ = true;
    transfers_.waitForDone();
}

void DesktopIcons::configure(const Settings &settings, const QList<Launcher> &launchers) {
    const bool wasShown = shown();
    settings_ = settings;
    launchers_ = launchers;
    watch();
    rescan();
    Q_EMIT configured();
    if (shown() != wasShown)
        Q_EMIT shownChanged();
}

bool DesktopIcons::handle(const QString &verb) {
    if (verb == "toggle")
        setShown(hidden_);
    else if (verb == "on" || verb == "off")
        setShown(verb == "on");
    else
        return false;
    return true;
}

void DesktopIcons::setShown(bool shown) {
    if (hidden_ == !shown)
        return;
    hidden_ = !shown;
    save();
    Q_EMIT shownChanged();
}

void DesktopIcons::setGrid(int columns, int rows) {
    const QSize grid(columns, rows);
    if (grid == grid_ || columns <= 0 || rows <= 0)
        return;
    grid_ = grid;
    relayout();
}

int DesktopIcons::indexOf(const QString &key) const {
    for (qsizetype i = 0; i < entries_.size(); ++i)
        if (entries_[i].key == key)
            return int(i);
    return -1;
}

QList<QPoint> DesktopIcons::landed(const QStringList &keys, QPoint offset, QList<int> &moved) const {
    QList<QPoint> from, others;
    for (const auto &key : keys)
        if (const int i = indexOf(key); i >= 0 && !moved.contains(i)) {
            moved.push_back(i);
            from.push_back(entries_[i].cell);
        }
    for (qsizetype i = 0; i < entries_.size(); ++i)
        if (!moved.contains(int(i)))
            others.push_back(entries_[i].cell);
    return desktop_grid::drop(from, offset, others, grid_);
}

QVariantList DesktopIcons::landing(const QStringList &keys, int columns, int rows) const {
    QVariantList cells;
    if (!grid_.isValid())
        return cells;
    QList<int> moved;
    for (const auto &cell : landed(keys, QPoint(columns, rows), moved))
        cells.push_back(QVariantMap{{"column", cell.x()}, {"row", cell.y()}});
    return cells;
}

void DesktopIcons::move(const QStringList &keys, int columns, int rows) {
    if (!grid_.isValid())
        return;
    QList<int> moved;
    const auto landed = this->landed(keys, QPoint(columns, rows), moved);
    for (qsizetype i = 0; i < moved.size(); ++i) {
        auto &entry = entries_[moved[i]];
        cells_[entry.key] = landed[i];
        if (entry.cell != landed[i]) {
            entry.cell = landed[i];
            Q_EMIT dataChanged(index(moved[i]), index(moved[i]), {ColumnRole, RowRole});
        }
    }
    save();
}

void DesktopIcons::tidy() {
    cells_.clear();
    relayout();
    save();
}

bool DesktopIcons::open(const QString &key) {
    const int i = indexOf(key);
    if (i < 0 || entries_[i].kind == "launcher")
        return false;
    const auto &entry = entries_[i];
    const QFileInfo file(entry.path);
    QString error;
    if (!file.exists() && !file.isSymLink()) {
        error = "it is no longer there";
    } else if ((entry.kind == "app" || entry.kind == "link") && !file.isExecutable()) {
        // As file managers do, an entry not marked executable is not trusted to run its command,
        // nor to open its address: anything saved to the desktop could otherwise pass itself off
        // as a document.
        const QString verb = entry.kind == "app" ? "start" : "open";
        Q_EMIT failed(entry.name + " is not trusted to " + verb + ": make " + file.fileName() +
                      " executable to " + verb + " it from the desktop");
        return false;
    } else if (entry.kind == "link") {
        const auto url = readLink(entry.path).url;
        error = url.isEmpty() ? QString("its desktop entry cannot be read") : gioOpen(url.toUtf8());
    } else if (entry.kind == "app") {
        GDesktopAppInfo *info = g_desktop_app_info_new_from_filename(entry.path.toUtf8().constData());
        if (!info) {
            error = "its desktop entry cannot be read";
        } else {
            error = gioLaunch([info](GAppLaunchContext *context, GError **gerror) {
                return g_app_info_launch(G_APP_INFO(info), nullptr, context, gerror);
            });
            g_object_unref(info);
        }
    } else {
        error = gioOpen(QUrl::fromLocalFile(entry.path).toEncoded());
    }
    if (error.isEmpty())
        return true;
    Q_EMIT failed("Could not open " + entry.name + ": " + error);
    return false;
}

QMimeData *DesktopIcons::fileData(const QStringList &keys) const {
    QList<QUrl> urls;
    QStringList paths;
    for (const auto &key : keys)
        if (const int i = indexOf(key); i >= 0 && !entries_[i].path.isEmpty()) {
            urls.push_back(QUrl::fromLocalFile(entries_[i].path));
            paths.push_back(entries_[i].path);
        }
    if (urls.isEmpty())
        return nullptr;
    auto *data = new QMimeData;
    data->setUrls(urls);
    // What GTK's file managers paste files from.
    QByteArray copied = "copy";
    for (const auto &url : urls)
        copied += '\n' + url.toEncoded();
    data->setData("x-special/gnome-copied-files", copied);
    data->setText(paths.join('\n'));
    return data;
}

void DesktopIcons::copy(const QStringList &keys) {
    if (auto *data = fileData(keys))
        QGuiApplication::clipboard()->setMimeData(data);
}

bool DesktopIcons::drag(const QStringList &keys, const QImage &picture, QPointF hotSpot,
                        qreal scale) {
    auto *data = fileData(keys);
    if (!data)
        return false;
    auto *drag = new QDrag(this);
    drag->setMimeData(data);
    QImage image = picture;
    image.setDevicePixelRatio(scale > 0 ? scale : 1);
    drag->setPixmap(QPixmap::fromImage(image));
    drag->setHotSpot(hotSpot.toPoint());
    // exec() runs an event loop of its own until the drag is over, and Qt deletes the drag.
    QTimer::singleShot(0, this, [this, drag] {
        Q_EMIT dragEnded(
            drag->exec(Qt::CopyAction | Qt::MoveAction | Qt::LinkAction, Qt::CopyAction));
    });
    return true;
}

int DesktopIcons::trash(const QStringList &keys) {
    int moved = 0;
    QStringList refused;
    QString why;
    for (const auto &key : keys)
        if (const int i = indexOf(key); i >= 0 && !entries_[i].path.isEmpty()) {
            QFile file(entries_[i].path);
            if (file.moveToTrash()) {
                ++moved;
            } else {
                refused.push_back(entries_[i].name);
                why = file.errorString();
            }
        }
    if (!refused.isEmpty())
        Q_EMIT failed("Could not move " + refused.join(", ") + " to the Trash: " + why);
    if (moved > 0)
        rescan();
    return moved;
}

QString DesktopIcons::rename(const QString &key, const QString &name) {
    const int i = indexOf(key);
    if (i < 0 || (entries_[i].kind != "file" && entries_[i].kind != "folder"))
        return {};
    const QString old = entries_[i].name;
    if (name == old)
        return key;
    const QFileInfo target(QFileInfo(entries_[i].path).dir().filePath(name));
    QString why;
    if (name.isEmpty())
        why = "a name cannot be empty";
    else if (name.contains('/'))
        why = "a name cannot have a / in it";
    else if (name == "." || name == "..")
        why = name + " is not a name a file can have";
    // Only its letters' case changed, it may be the same file there (QFile tells).
    else if ((target.exists() || target.isSymLink()) && name.compare(old, Qt::CaseInsensitive) != 0)
        why = "there is something called " + name + " already";
    QFile file(entries_[i].path);
    if (why.isEmpty() && !file.rename(target.filePath()))
        why = file.errorString();
    if (!why.isEmpty()) {
        Q_EMIT failed("Could not rename " + old + ": " + why);
        return {};
    }
    // It stays where it is, under its new key, without waiting for the folder to be read again.
    auto entry = fileEntry(QFileInfo(target.filePath()));
    entry.cell = entries_[i].cell;
    if (cells_.contains(key))
        cells_.insert(entry.key, cells_.take(key));
    if (const auto at = order_.indexOf(key); at >= 0)
        order_[at] = entry.key;
    entries_[i] = entry;
    Q_EMIT dataChanged(index(i), index(i));
    save();
    return entry.key;
}

QString DesktopIcons::newFolder(int column, int row) {
    const QDir folder(settings_.folder);
    auto had = [&folder](const QString &name) {
        const QFileInfo file(folder.filePath(name));
        return file.exists() || file.isSymLink();
    };
    QString name = "New folder";
    for (int n = 2; had(name); ++n)
        name = "New folder " + QString::number(n);
    if (settings_.folder.isEmpty() || !QDir().mkpath(settings_.folder) || !folder.mkdir(name)) {
        Q_EMIT failed("Could not make a folder in the desktop folder " + settings_.folder);
        return {};
    }
    const QString key = "file:" + name;
    if (grid_.isValid()) {
        QList<QPoint> taken;
        for (const auto &entry : entries_)
            taken.push_back(entry.cell);
        cells_.insert(key, desktop_grid::nearestFree(grid_, QPoint(column, row), taken));
    }
    watch();
    rescan();
    save();
    return key;
}

bool DesktopIcons::openFolder() {
    if (!QDir().mkpath(settings_.folder)) {
        Q_EMIT failed("Could not make the desktop folder " + settings_.folder);
        return false;
    }
    watch();
    const auto error = gioOpen(QUrl::fromLocalFile(settings_.folder).toEncoded());
    if (error.isEmpty())
        return true;
    Q_EMIT failed("Could not open the desktop folder: " + error);
    return false;
}

QList<DesktopIcons::Transfer> DesktopIcons::transfers(const QList<QUrl> &urls,
                                                      const QString &folder, bool move,
                                                      const QByteArray &mozUrl) const {
    QList<Transfer> list;
    const auto into = QFileInfo(folder).canonicalFilePath();
    const auto titles = mozUrl.isEmpty() ? QHash<QString, QString>() : linkTitles(mozUrl);
    for (const auto &url : urls) {
        if (url.isLocalFile()) {
            const QFileInfo file(url.toLocalFile());
            if ((!file.exists() && !file.isSymLink()) || file.fileName().isEmpty())
                continue;
            // A folder dropped on itself goes nowhere.
            if (!into.isEmpty() && file.canonicalFilePath() == into && !file.isSymLink())
                continue;
            Transfer transfer{file.absoluteFilePath(), {}, folder, file.fileName()};
            transfer.move = move;
            transfer.here =
                !into.isEmpty() && QFileInfo(file.absolutePath()).canonicalFilePath() == into;
            transfer.directory = file.isDir() && !file.isSymLink();
            list.push_back(transfer);
        } else if (url.scheme() == "http" || url.scheme() == "https") {
            // A link file, named from the page's title, or its host.
            QString title = titles.value(url.toString()).simplified();
            QString host = url.host();
            if (host.startsWith("www."))
                host = host.mid(4);
            if (title.isEmpty())
                title = host.isEmpty() ? "Link" : host;
            QString name = title.left(80).replace('/', '-').trimmed();
            while (name.startsWith('.'))
                name.remove(0, 1);
            if (name.isEmpty())
                name = "Link";
            const QByteArray entry = "[Desktop Entry]\nVersion=1.0\nType=Link\nName=" +
                                     QString(title).replace('\\', "\\\\").toUtf8() +
                                     "\nURL=" + url.toEncoded() + "\nIcon=text-html\n";
            Transfer transfer{{}, entry, folder, name + ".desktop"};
            transfer.executable = true;
            list.push_back(transfer);
        }
    }
    return list;
}

QList<QPoint> DesktopIcons::dropCells(int count, QPoint cell, const QStringList &moving) const {
    QList<QPoint> taken;
    for (const auto &entry : entries_)
        if (!moving.contains(entry.key))
            taken.push_back(entry.cell);
    // The cells of files still on their way.
    for (const auto &key : arriving_)
        if (indexOf(key) < 0 && cells_.contains(key))
            taken.push_back(cells_.value(key));
    return desktop_grid::flow(grid_, cell, count, taken);
}

QVariantList DesktopIcons::dropLanding(const QList<QUrl> &urls, int column, int row) const {
    QVariantList cells;
    if (!grid_.isValid() || !settings_.enabled || settings_.folder.isEmpty())
        return cells;
    const auto list = transfers(urls, settings_.folder, false, {});
    QStringList moving;
    for (const auto &transfer : list)
        if (transfer.here)
            moving.push_back("file:" + transfer.name);
    for (const auto &cell : dropCells(int(list.size()), QPoint(column, row), moving))
        cells.push_back(QVariantMap{{"column", cell.x()}, {"row", cell.y()}});
    return cells;
}

int DesktopIcons::drop(const QList<QUrl> &urls, int column, int row, bool move,
                       const QByteArray &mozUrl) {
    if (!grid_.isValid() || !settings_.enabled || settings_.folder.isEmpty())
        return 0;
    const auto list = transfers(urls, settings_.folder, move, mozUrl);
    QStringList moving;
    for (const auto &transfer : list)
        if (transfer.here)
            moving.push_back("file:" + transfer.name);
    const auto cells = dropCells(int(list.size()), QPoint(column, row), moving);
    QList<Transfer> carried;
    QList<QPoint> landed;
    for (qsizetype i = 0; i < list.size(); ++i) {
        if (!list[i].here) {
            carried.push_back(list[i]);
            landed.push_back(cells[i]);
            continue;
        }
        // On the desktop already: its icon moves.
        const auto key = "file:" + list[i].name;
        cells_[key] = cells[i];
        if (const int at = indexOf(key); at >= 0 && entries_[at].cell != cells[i]) {
            entries_[at].cell = cells[i];
            Q_EMIT dataChanged(index(at), index(at), {ColumnRole, RowRole});
        }
    }
    start(carried, landed);
    save();
    return int(list.size());
}

int DesktopIcons::dropInto(const QString &key, const QList<QUrl> &urls, bool move,
                           const QByteArray &mozUrl) {
    const int i = indexOf(key);
    if (i < 0 || entries_[i].kind != "folder")
        return 0;
    auto list = transfers(urls, entries_[i].path, move, mozUrl);
    list.removeIf([](const Transfer &transfer) { return transfer.here; });
    start(list, {});
    return int(list.size());
}

int DesktopIcons::moveInto(const QString &key, const QStringList &keys) {
    QList<QUrl> urls;
    for (const auto &moved : keys)
        if (const int i = indexOf(moved); i >= 0 && !entries_[i].path.isEmpty() && moved != key)
            urls.push_back(QUrl::fromLocalFile(entries_[i].path));
    return dropInto(key, urls, true);
}

QString DesktopIcons::dropPicture(const QByteArray &data, const QString &type, const QUrl &from,
                                  int column, int row) {
    if (data.isEmpty() || !grid_.isValid() || !settings_.enabled || settings_.folder.isEmpty())
        return {};
    // Its own name where it has one of the type's extensions, else its name with the type's.
    const auto mime = QMimeDatabase().mimeTypeForName(type);
    const QFileInfo file(from.fileName());
    QString name = file.fileName();
    while (name.startsWith('.'))
        name.remove(0, 1);
    if (name.isEmpty() || !mime.suffixes().contains(file.suffix().toLower())) {
        const auto base = file.completeBaseName();
        name = (base.isEmpty() || base.startsWith('.') ? QString("Picture") : base) +
               (mime.preferredSuffix().isEmpty() ? QString() : "." + mime.preferredSuffix());
    }
    return start({{{}, data, settings_.folder, name}}, dropCells(1, QPoint(column, row), {}))
        .value(0);
}

QString DesktopIcons::Transfer::named(int n) const { return numbered(name, n, directory); }

QStringList DesktopIcons::start(QList<Transfer> list, const QList<QPoint> &cells) {
    QStringList keys;
    if (list.isEmpty())
        return keys;
    // The desktop folder's, each under a name free there, which its icon takes at once.
    for (auto &transfer : list) {
        if (transfer.into != settings_.folder)
            continue;
        auto free = [&](const QString &name) {
            const QFileInfo file(settings_.folder + "/" + name);
            return !file.exists() && !file.isSymLink() && !arriving_.contains("file:" + name);
        };
        while (!free(transfer.named(transfer.number)))
            ++transfer.number;
        const auto key = "file:" + transfer.named(transfer.number);
        arriving_.insert(key);
        if (keys.size() < cells.size())
            cells_.insert(key, cells[keys.size()]);
        keys.push_back(key);
    }
    if (!QDir().mkpath(settings_.folder))
        Q_EMIT failed("Could not make the desktop folder " + settings_.folder);
    watch();
    for (const auto &transfer : list)
        transfers_.start([this, transfer] {
            QString error;
            const auto name = carry(transfer, stopping_, error);
            QMetaObject::invokeMethod(
                this, [this, transfer, name, error] { arrived(transfer, name, error); },
                Qt::QueuedConnection);
        });
    return keys;
}

QString DesktopIcons::carry(const Transfer &transfer, const std::atomic_bool &stop,
                            QString &error) {
    const QFileInfo from(transfer.from);
    const auto into = QFileInfo(transfer.into).canonicalFilePath();
    if (transfer.directory &&
        (into == from.canonicalFilePath() || into.startsWith(from.canonicalFilePath() + "/"))) {
        error = "a folder cannot go into itself";
        return {};
    }
    const auto source = QFile::encodeName(transfer.from);
    // Its name, or the next free one like it: a name taken meanwhile is never written over.
    for (int n = transfer.number; n < transfer.number + 10000; ++n) {
        const auto name = transfer.named(n);
        const auto to = QFile::encodeName(transfer.into + "/" + name);
        bool taken = false, made = false;
        if (transfer.from.isEmpty()) {
            const int file = ::open(to.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                                    transfer.executable ? 0755 : 0644);
            taken = file < 0 && errno == EEXIST;
            error = file < 0 ? systemError() : QString();
            for (qsizetype written = 0;
                 file >= 0 && error.isEmpty() && written < transfer.data.size();) {
                const ssize_t step = ::write(file, transfer.data.constData() + written,
                                             size_t(transfer.data.size() - written));
                if (step >= 0)
                    written += step;
                else if (errno != EINTR)
                    error = systemError();
            }
            if (file >= 0 && ::close(file) != 0 && error.isEmpty())
                error = systemError();
            made = file >= 0;
        } else if (transfer.move && renameat2(AT_FDCWD, source.constData(), AT_FDCWD,
                                              to.constData(), RENAME_NOREPLACE) == 0) {
            error.clear();
        } else if (transfer.move && errno != EXDEV && errno != EINVAL && errno != ENOSYS) {
            taken = errno == EEXIST;
            error = systemError();
        } else {
            // A copy, and a move across file systems, the original going once all is there.
            error = copyTree(source, to, stop, &taken, &made);
            if (error.isEmpty() && transfer.move)
                removeTree(transfer.from);
        }
        if (error.isEmpty())
            return name;
        if (made)
            removeTree(QFile::decodeName(to));
        if (!taken)
            return {};
    }
    error = "every name like it is taken";
    return {};
}

void DesktopIcons::arrived(const Transfer &transfer, const QString &name, const QString &error) {
    const bool desktop = transfer.into == settings_.folder;
    const auto planned = "file:" + transfer.named(transfer.number);
    if (desktop) {
        arriving_.remove(planned);
        if (name.isEmpty())
            cells_.remove(planned);
        else if ("file:" + name != planned && cells_.contains(planned))
            cells_.insert("file:" + name, cells_.take(planned));
    }
    if (!error.isEmpty()) {
        const auto what =
            transfer.from.isEmpty() ? transfer.name : QFileInfo(transfer.from).fileName();
        const auto where =
            desktop ? QString("to the desktop") : "into " + QFileInfo(transfer.into).fileName();
        Q_EMIT failed(QString(transfer.move ? "Could not move " : "Could not copy ") + what + " " +
                      where + ": " + error);
    }
    if (const int i = indexOf(planned); desktop && i >= 0)
        Q_EMIT dataChanged(index(i), index(i), {ArrivingRole});
    rescan();
    save();
}

QList<DesktopIcons::Entry> DesktopIcons::read() const {
    QList<Entry> entries;
    if (!settings_.enabled)
        return entries;
    QSet<QString> keys;
    for (const auto &launcher : launchers_) {
        // Two launchers of one name are told apart by their places among the launchers.
        auto key = "launcher:" + launcher.name;
        for (int n = 2; keys.contains(key); ++n)
            key = "launcher:" + launcher.name + "#" + QString::number(n);
        keys.insert(key);
        entries.push_back({key, launcher.name, launcher.icon, {}, {}, "launcher", launcher.appId});
    }
    if (settings_.folder.isEmpty())
        return entries;
    auto files = QDir(settings_.folder)
                     .entryInfoList(QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot);
    QCollator names;
    names.setNumericMode(true);
    names.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(files.begin(), files.end(), [&names](const QFileInfo &a, const QFileInfo &b) {
        if (a.isDir() != b.isDir())
            return a.isDir();
        return names.compare(a.fileName(), b.fileName()) < 0;
    });
    if (files.size() > maxFiles)
        files.resize(maxFiles);
    for (const auto &file : files)
        entries.push_back(fileEntry(file));
    return entries;
}

DesktopIcons::Entry DesktopIcons::fileEntry(const QFileInfo &file) {
    static const auto pictures = QImageReader::supportedMimeTypes();
    Entry entry{"file:" + file.fileName(), file.fileName(), {}, {}, file.absoluteFilePath(),
                "file", {}};
    if (file.isDir()) {
        entry.kind = "folder";
        entry.icon = "folder";
    } else if (file.suffix() == "desktop") {
        if (GDesktopAppInfo *info =
                g_desktop_app_info_new_from_filename(entry.path.toUtf8().constData())) {
            entry.kind = "app";
            entry.name = QString::fromUtf8(g_app_info_get_display_name(G_APP_INFO(info)));
            entry.icon = gioIconName(g_app_info_get_icon(G_APP_INFO(info)), "application-x-executable");
            g_object_unref(info);
        } else if (const auto link = readLink(entry.path); !link.url.isEmpty()) {
            entry.kind = "link";
            entry.name = link.name.isEmpty() ? file.completeBaseName() : link.name;
            entry.icon =
                link.icon.isEmpty() ? "text-html,applications-internet,text-x-generic" : link.icon;
        }
    }
    if (entry.kind == "file") {
        const auto type = QMimeDatabase().mimeTypeForFile(file);
        entry.icon = type.iconName() + ',' + type.genericIconName() + ",text-x-generic";
        if (pictures.contains(type.name().toLatin1()) && file.size() <= maxPicture)
            entry.picture = QUrl::fromLocalFile(entry.path).toString();
    }
    return entry;
}

void DesktopIcons::rescan() {
    settle_.stop();
    sync(read());
}

void DesktopIcons::sync(QList<Entry> entries) {
    const int before = count();
    QSet<QString> keys;
    order_.clear();
    for (const auto &entry : entries) {
        keys.insert(entry.key);
        order_.push_back(entry.key);
    }
    for (int i = count() - 1; i >= 0; --i)
        if (!keys.contains(entries_[i].key)) {
            beginRemoveRows({}, i, i);
            entries_.removeAt(i);
            endRemoveRows();
        }
    QList<Entry> added;
    for (auto &entry : entries) {
        const int i = indexOf(entry.key);
        if (i < 0) {
            added.push_back(entry);
            continue;
        }
        auto &kept = entries_[i];
        if (kept.name != entry.name || kept.icon != entry.icon || kept.picture != entry.picture ||
            kept.path != entry.path || kept.kind != entry.kind || kept.appId != entry.appId) {
            entry.cell = kept.cell;
            kept = entry;
            Q_EMIT dataChanged(index(i), index(i));
        }
    }
    if (!added.isEmpty()) {
        beginInsertRows({}, count(), count() + int(added.size()) - 1);
        entries_ += added;
        endInsertRows();
    }
    relayout();
    if (count() != before)
        Q_EMIT countChanged();
}

void DesktopIcons::relayout() {
    if (!grid_.isValid())
        return;
    const auto cells = desktop_grid::arrange(order_, cells_, grid_);
    bool placed = false;
    for (qsizetype n = 0; n < order_.size(); ++n) {
        const int i = indexOf(order_[n]);
        if (i < 0)
            continue;
        // A new icon keeps the cell it is given; one remembered off a grid that has shrunk keeps
        // that cell for when the grid grows again.
        if (!cells_.contains(order_[n]) && desktop_grid::contains(grid_, cells[n])) {
            cells_.insert(order_[n], cells[n]);
            placed = true;
        }
        if (entries_[i].cell != cells[n]) {
            entries_[i].cell = cells[n];
            Q_EMIT dataChanged(index(i), index(i), {ColumnRole, RowRole});
        }
    }
    if (placed)
        save();
}

void DesktopIcons::watch() {
    if (!settings_.enabled || settings_.folder.isEmpty()) {
        delete watcher_;
        watcher_ = nullptr;
        return;
    }
    if (!watcher_) {
        watcher_ = new QFileSystemWatcher(this);
        connect(watcher_, &QFileSystemWatcher::directoryChanged, this, [this] {
            watch();
            settle_.start();
        });
    }
    // The folder, or the nearest above it until it is made.
    QString watched = settings_.folder;
    while (!QFileInfo(watched).isDir() && QFileInfo(watched).path() != watched)
        watched = QFileInfo(watched).path();
    if (watcher_->directories() != QStringList{watched}) {
        if (!watcher_->directories().isEmpty())
            watcher_->removePaths(watcher_->directories());
        watcher_->addPath(watched);
    }
}

void DesktopIcons::save() {
    QByteArray contents;
    if (hidden_)
        contents += "hidden\n";
    // The cells of the icons there are; while there are none (the folder gone for now, as an
    // unmounted one is), those remembered stay. A file whose name has a tab or a line break in it
    // is not remembered, and takes a free cell each time.
    const QStringList keys = entries_.isEmpty() ? cells_.keys() : order_;
    for (const auto &key : keys)
        if (const auto it = cells_.constFind(key);
            it != cells_.cend() && !key.contains('\t') && !key.contains('\n'))
            contents += QString("%1\t%2\t%3\n").arg(it->x()).arg(it->y()).arg(key).toUtf8();
    if (contents == saved_ || !remembering_)
        return;
    if (const auto error = saveFile(statePath(), contents); !error.isEmpty()) {
        Q_EMIT failed("Could not remember the desktop's icons: " + error);
        return;
    }
    saved_ = contents;
}

int DesktopIcons::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : count();
}

QVariant DesktopIcons::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= count())
        return {};
    const auto &entry = entries_[index.row()];
    switch (role) {
    case KeyRole:
        return entry.key;
    case Qt::DisplayRole:
    case NameRole:
        return entry.name;
    case IconRole:
        return entry.icon;
    case PictureRole:
        return entry.picture;
    case PathRole:
        return entry.path;
    case KindRole:
        return entry.kind;
    case AppIdRole:
        return entry.appId;
    case ColumnRole:
        return entry.cell.x();
    case RowRole:
        return entry.cell.y();
    case ArrivingRole:
        return arriving_.contains(entry.key);
    default:
        return {};
    }
}

QHash<int, QByteArray> DesktopIcons::roleNames() const {
    return {{KeyRole, "key"},       {NameRole, "name"}, {IconRole, "icon"},
            {PictureRole, "picture"}, {PathRole, "path"}, {KindRole, "kind"},
            {AppIdRole, "appId"},   {ColumnRole, "column"}, {RowRole, "row"},
            {ArrivingRole, "arriving"}};
}
