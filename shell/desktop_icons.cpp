// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop_icons.hpp"
#include "desktop_grid.hpp"
#include "gio_launch.hpp"
#include "icons.hpp"
#include "state_files.hpp"
#include <QClipboard>
#include <QCollator>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QGuiApplication>
#include <QImageReader>
#include <QMimeData>
#include <QMimeDatabase>
#include <QSet>
#include <QUrl>
#include <algorithm>
#include <gio/gdesktopappinfo.h>
#include <utility>

namespace {
// Where the icons' cells are remembered: a line `hidden` while they are hidden, and a line for
// each icon, its column, its row and its key, by tabs.
QString statePath() { return stateDir() + "/desktop-icons"; }
// The most files of the folder shown, the first by name.
constexpr int maxFiles = 512;
// A picture larger than this shows its type's icon rather than itself.
constexpr qint64 maxPicture = 32 * 1024 * 1024;
} // namespace

DesktopIcons::DesktopIcons(QObject *parent) : QAbstractListModel(parent) {
    settle_.setSingleShot(true);
    settle_.setInterval(150);
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
    } else if (entry.kind == "app") {
        // As file managers do, an entry not marked executable is not trusted to run its command:
        // anything saved to the desktop could otherwise pass itself off as a document.
        if (!file.isExecutable()) {
            Q_EMIT failed(entry.name + " is not trusted to start: make " + file.fileName() +
                          " executable to start it from the desktop");
            return false;
        }
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

QStringList DesktopIcons::filePaths(const QStringList &keys) const {
    QStringList paths;
    for (const auto &key : keys) {
        const int i = indexOf(key);
        if (i < 0 || (entries_[i].kind != "file" && entries_[i].kind != "folder"))
            return {};
        if (!paths.contains(entries_[i].path))
            paths.push_back(entries_[i].path);
    }
    return paths;
}

QVariantList DesktopIcons::openers(const QStringList &keys) const {
    QStringList types;
    const QMimeDatabase mime;
    for (const auto &path : filePaths(keys))
        if (const auto type = mime.mimeTypeForFile(path).name(); !types.contains(type))
            types.push_back(type);
    if (types.isEmpty())
        return {};
    struct App {
        QString id, name, icon;
    };
    // For each type, the applications that take it, each once: its default, those recommended
    // for it, then the rest GIO has for it (those of the types it is a kind of, text/plain's for
    // a script's).
    struct Takers {
        QList<App> apps;
        QString preferred;
        QSet<QString> recommended;
        bool take(const QString &id) const {
            return std::any_of(apps.cbegin(), apps.cend(), [&id](const App &app) { return app.id == id; });
        }
    };
    QList<Takers> takers;
    for (const auto &type : types) {
        Takers each;
        auto add = [&each](GAppInfo *info, bool recommended) {
            const char *id = g_app_info_get_id(info);
            if (!id)
                return;
            const auto name = QString::fromUtf8(id);
            if (recommended)
                each.recommended.insert(name);
            if (!each.take(name))
                each.apps.push_back({name, QString::fromUtf8(g_app_info_get_display_name(info)),
                                     gioIconName(g_app_info_get_icon(info), "application-x-executable")});
        };
        const auto content = type.toUtf8();
        if (GAppInfo *preferred = g_app_info_get_default_for_type(content.constData(), FALSE)) {
            add(preferred, true);
            if (const char *id = g_app_info_get_id(preferred))
                each.preferred = QString::fromUtf8(id);
            g_object_unref(preferred);
        }
        GList *recommended = g_app_info_get_recommended_for_type(content.constData());
        for (GList *it = recommended; it; it = it->next)
            add(G_APP_INFO(it->data), true);
        g_list_free_full(recommended, g_object_unref);
        GList *all = g_app_info_get_all_for_type(content.constData());
        for (GList *it = all; it; it = it->next)
            add(G_APP_INFO(it->data), false);
        g_list_free_full(all, g_object_unref);
        takers.push_back(each);
    }
    // Those that take every type, in the first's order: the default for them all, those
    // recommended for each, then the others by name.
    QList<App> preferred, recommended, others;
    for (const auto &app : takers[0].apps) {
        bool everyType = true, everyDefault = true, everyRecommended = true;
        for (const auto &each : takers) {
            everyType = everyType && each.take(app.id);
            everyDefault = everyDefault && each.preferred == app.id;
            everyRecommended = everyRecommended && each.recommended.contains(app.id);
        }
        if (everyType)
            (everyDefault ? preferred : everyRecommended ? recommended : others).push_back(app);
    }
    QCollator names;
    names.setNumericMode(true);
    names.setCaseSensitivity(Qt::CaseInsensitive);
    std::stable_sort(others.begin(), others.end(),
                     [&names](const App &a, const App &b) { return names.compare(a.name, b.name) < 0; });
    QVariantList list;
    auto put = [&list](const QList<App> &apps, const QString &group) {
        for (const auto &app : apps)
            list.push_back(QVariantMap{{"id", app.id}, {"name", app.name}, {"icon", app.icon}, {"group", group}});
    };
    put(preferred, "default");
    put(recommended, "recommended");
    put(others, "other");
    return list;
}

bool DesktopIcons::openWith(const QStringList &keys, const QString &appId) {
    const auto paths = filePaths(keys);
    if (paths.isEmpty())
        return false;
    const auto what = paths.size() == 1 ? QFileInfo(paths[0]).fileName()
                                        : QString::number(paths.size()) + " files";
    GDesktopAppInfo *info = g_desktop_app_info_new(appId.toUtf8().constData());
    if (!info) {
        Q_EMIT failed("Could not open " + what + ": there is no application " + appId);
        return false;
    }
    // Their URIs, all handed over at once: GIO starts the application once for them all when its
    // command takes several (%F, %U), else once for each.
    QList<QByteArray> uris;
    GList *list = nullptr;
    for (const auto &path : paths)
        uris.push_back(QUrl::fromLocalFile(path).toEncoded());
    for (const auto &uri : uris)
        list = g_list_append(list, const_cast<char *>(uri.constData()));
    const auto error = gioLaunch([info, list](GAppLaunchContext *context, GError **gerror) {
        return g_app_info_launch_uris(G_APP_INFO(info), list, context, gerror);
    });
    const auto name = QString::fromUtf8(g_app_info_get_display_name(G_APP_INFO(info)));
    g_list_free(list);
    g_object_unref(info);
    if (error.isEmpty())
        return true;
    Q_EMIT failed("Could not open " + what + " with " + name + ": " + error);
    return false;
}

void DesktopIcons::copy(const QStringList &keys) {
    QList<QUrl> urls;
    QStringList paths;
    for (const auto &key : keys)
        if (const int i = indexOf(key); i >= 0 && !entries_[i].path.isEmpty()) {
            urls.push_back(QUrl::fromLocalFile(entries_[i].path));
            paths.push_back(entries_[i].path);
        }
    if (urls.isEmpty())
        return;
    auto *data = new QMimeData;
    data->setUrls(urls);
    // What GTK's file managers paste files from.
    QByteArray copied = "copy";
    for (const auto &url : urls)
        copied += '\n' + url.toEncoded();
    data->setData("x-special/gnome-copied-files", copied);
    data->setText(paths.join('\n'));
    QGuiApplication::clipboard()->setMimeData(data);
}

int DesktopIcons::trash(const QStringList &keys) {
    QList<Trashed> moved;
    QStringList refused;
    QString why;
    for (const auto &key : keys)
        if (const int i = indexOf(key); i >= 0 && !entries_[i].path.isEmpty()) {
            // Once moved, the file is named by where the trash keeps it (the home trash, or the
            // trash of the mount it is on), as QFile::moveToTrash(path, &pathInTrash) would say,
            // keeping the reason it failed besides.
            QFile file(entries_[i].path);
            if (file.moveToTrash()) {
                moved.push_back({entries_[i].path, file.fileName(), cells_.value(key, entries_[i].cell)});
            } else {
                refused.push_back(entries_[i].name);
                why = file.errorString();
            }
        }
    if (!refused.isEmpty())
        Q_EMIT failed("Could not move " + refused.join(", ") + " to the Trash: " + why);
    if (moved.isEmpty())
        return 0;
    trashed_ = moved;
    rescan();
    Q_EMIT trashedChanged();
    return int(moved.size());
}

QStringList DesktopIcons::trashed() const {
    QStringList names;
    for (const auto &file : trashed_)
        names.push_back(QFileInfo(file.path).fileName());
    return names;
}

QStringList DesktopIcons::untrash() {
    const auto trashed = std::exchange(trashed_, {});
    if (trashed.isEmpty())
        return {};
    // The cells the icons there are in now, which those put back go beside.
    QList<QPoint> taken;
    for (const auto &entry : entries_)
        taken.push_back(entry.cell);
    QStringList keys, refused;
    QString why;
    for (const auto &file : trashed) {
        const QFileInfo was(file.path), kept(file.inTrash);
        if (!kept.exists() && !kept.isSymLink()) {
            refused.push_back(was.fileName());
            why = "it is no longer in the Trash";
            continue;
        }
        const auto folder = was.path();
        const auto name = freeName(folder, was.fileName(), kept.isDir());
        QFile moved(file.inTrash);
        if (!QDir().mkpath(folder) || !moved.rename(QDir(folder).filePath(name))) {
            refused.push_back(was.fileName());
            why = moved.errorString();
            continue;
        }
        QFile::remove(trashInfo(file.inTrash));
        // Its cell is remembered again, under the name it came back as. One it left off a grid
        // that has shrunk stays remembered as it was.
        const QString key = "file:" + name;
        QPoint cell = file.cell;
        if (grid_.isValid() && desktop_grid::contains(grid_, cell))
            cell = desktop_grid::nearestFree(grid_, cell, taken);
        if (cell.x() >= 0 && cell.y() >= 0) {
            cells_.insert(key, cell);
            taken.push_back(cell);
        }
        keys.push_back(key);
    }
    if (!refused.isEmpty())
        Q_EMIT failed("Could not put " + refused.join(", ") + " back from the Trash: " + why);
    Q_EMIT trashedChanged();
    if (!keys.isEmpty()) {
        watch();
        rescan();
        save();
    }
    return keys;
}

QString DesktopIcons::trashInfo(const QString &inTrash) {
    const QFileInfo file(inTrash);
    return QFileInfo(file.path()).path() + "/info/" + file.fileName() + ".trashinfo";
}

QString DesktopIcons::freeName(const QString &folder, const QString &name, bool isFolder) {
    const QDir dir(folder);
    auto taken = [&dir](const QString &name) {
        const QFileInfo file(dir.filePath(name));
        return file.exists() || file.isSymLink();
    };
    if (!taken(name))
        return name;
    // The extension as the type knows it ("tar.gz"), else what follows the last dot; a leading
    // dot (".profile") is no extension.
    QString suffix = isFolder ? QString() : QMimeDatabase().suffixForFileName(name);
    if (!isFolder && suffix.isEmpty() && name.lastIndexOf('.') > 0)
        suffix = name.sliced(name.lastIndexOf('.') + 1);
    const auto stem = suffix.isEmpty() || suffix.size() + 1 >= name.size()
                          ? name : name.first(name.size() - suffix.size() - 1);
    const auto end = stem == name ? QString() : "." + name.sliced(stem.size() + 1);
    for (int n = 2;; ++n)
        if (const auto numbered = QString("%1 (%2)%3").arg(stem, QString::number(n), end); !taken(numbered))
            return numbered;
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
    default:
        return {};
    }
}

QHash<int, QByteArray> DesktopIcons::roleNames() const {
    return {{KeyRole, "key"},       {NameRole, "name"}, {IconRole, "icon"},
            {PictureRole, "picture"}, {PathRole, "path"}, {KindRole, "kind"},
            {AppIdRole, "appId"},   {ColumnRole, "column"}, {RowRole, "row"}};
}
