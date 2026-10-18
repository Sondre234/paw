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

void DesktopIcons::move(const QStringList &keys, int columns, int rows) {
    if (!grid_.isValid())
        return;
    QList<int> moved;
    QList<QPoint> from, others;
    for (const auto &key : keys)
        if (const int i = indexOf(key); i >= 0 && !moved.contains(i)) {
            moved.push_back(i);
            from.push_back(entries_[i].cell);
        }
    for (qsizetype i = 0; i < entries_.size(); ++i)
        if (!moved.contains(int(i)))
            others.push_back(entries_[i].cell);
    const auto landed = desktop_grid::drop(from, QPoint(columns, rows), others, grid_);
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
    static const auto pictures = QImageReader::supportedMimeTypes();
    QMimeDatabase types;
    for (const auto &file : files) {
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
            const auto type = types.mimeTypeForFile(file);
            entry.icon = type.iconName() + ',' + type.genericIconName() + ",text-x-generic";
            if (pictures.contains(type.name().toLatin1()) && file.size() <= maxPicture)
                entry.picture = QUrl::fromLocalFile(entry.path).toString();
        }
        entries.push_back(entry);
    }
    return entries;
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
