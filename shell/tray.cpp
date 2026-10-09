// SPDX-License-Identifier: GPL-3.0-or-later
#include "tray.hpp"
#include <QDirIterator>
#include <QFileInfo>
#include <QHash>
#include <QIcon>
#include <QPainter>
#include <QRegularExpression>
#include <QtEndian>
#include <algorithm>

QString TrayItem::toolTip() const {
    const QString head = !toolTipTitle.isEmpty() ? toolTipTitle : !title.isEmpty() ? title : id;
    return toolTipText.isEmpty() || toolTipText == head ? head : head + '\n' + toolTipText;
}

QString trayMenuLabel(const QString &label) {
    QString result;
    result.reserve(label.size());
    for (qsizetype i = 0; i < label.size(); ++i) {
        if (label[i] != '_')
            result += label[i];
        else if (i + 1 < label.size() && label[i + 1] == '_')
            result += label[++i];
    }
    return result;
}
void TrayMenuEntry::read() {
    auto string = [this](const char *name) {
        const QVariant value = properties.value(name);
        return value.metaType().id() == QMetaType::QString ? value.toString() : QString();
    };
    auto flag = [this](const char *name) {
        const QVariant value = properties.value(name);
        return value.metaType().id() == QMetaType::Bool ? value.toBool() : true;
    };
    label = trayMenuLabel(string("label").left(400)).left(200);
    enabled = flag("enabled");
    visible = flag("visible");
    separator = string("type") == "separator";
    toggleType = string("toggle-type");
    if (toggleType != "checkmark" && toggleType != "radio")
        toggleType.clear();
    const QVariant state = properties.value("toggle-state");
    switch (state.metaType().id()) {
    case QMetaType::Int:
    case QMetaType::UInt:
    case QMetaType::Short:
    case QMetaType::UShort:
    case QMetaType::UChar:
    case QMetaType::LongLong:
    case QMetaType::ULongLong:
        toggleState = state.toLongLong() == 0 ? 0 : state.toLongLong() == 1 ? 1 : -1;
        break;
    default:
        toggleState = -1;
    }
    iconName = string("icon-name").left(1024);
    const QVariant data = properties.value("icon-data");
    icon = data.metaType().id() == QMetaType::QByteArray && data.toByteArray().size() < 256 * 1024
               ? QImage::fromData(data.toByteArray(), "PNG")
               : QImage();
    if (icon.width() > 64 || icon.height() > 64)
        icon = icon.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    submenu = !children.empty() || string("children-display") == "submenu";
}

QImage trayImageFromArgb32(int width, int height, const QByteArray &data) {
    if (width < 1 || height < 1 || width > 512 || height > 512 || data.size() < qsizetype(width) * height * 4)
        return {};
    QImage image(width, height, QImage::Format_ARGB32);
    const auto *bytes = reinterpret_cast<const uchar *>(data.constData());
    for (int y = 0; y < height; ++y) {
        auto *line = reinterpret_cast<quint32 *>(image.scanLine(y));
        for (int x = 0; x < width; ++x)
            line[x] = qFromBigEndian<quint32>(bytes + 4 * (qsizetype(y) * width + x));
    }
    return image;
}
QImage trayPickPixmap(const QList<QImage> &pixmaps, QSize size) {
    const QImage *best = nullptr;
    for (const auto &pixmap : pixmaps) {
        const bool fits = pixmap.width() >= size.width() && pixmap.height() >= size.height();
        const bool bestFits = best && best->width() >= size.width() && best->height() >= size.height();
        if (!best || (fits && (!bestFits || pixmap.width() < best->width())) ||
            (!fits && !bestFits && pixmap.width() > best->width()))
            best = &pixmap;
    }
    if (!best)
        return {};
    return best->size() == size ? *best : best->scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}
QString trayIconFile(const QString &name, const QString &themePath) {
    if (name.isEmpty() || themePath.isEmpty() || name.contains('/') || name.startsWith('.'))
        return {};
    static QHash<QString, QString> found;
    const QString key = themePath + '\n' + name;
    if (const auto cached = found.constFind(key); cached != found.constEnd() && QFileInfo(*cached).isFile())
        return *cached;
    // The folder and three levels below it, a bounded number of entries: an application could
    // name its home or the root.
    static const QRegularExpression sized("/(\\d+)x\\d+(/|$)");
    QString best;
    int bestSize = -1, budget = 4000;
    QList<std::pair<QString, int>> folders{{themePath, 0}};
    while (!folders.isEmpty() && budget > 0) {
        const auto [folder, depth] = folders.takeFirst();
        for (QDirIterator it(folder, QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot); it.hasNext() && budget-- > 0;) {
            const QFileInfo entry = it.nextFileInfo();
            if (entry.isDir()) {
                if (depth < 3 && !entry.isSymLink())
                    folders.append({entry.filePath(), depth + 1});
                continue;
            }
            const QString suffix = entry.suffix().toLower();
            if (entry.completeBaseName() != name || !entry.isFile() ||
                (suffix != "png" && suffix != "svg" && suffix != "xpm"))
                continue;
            const auto match = sized.match(folder);
            const int size = suffix == "svg" ? 100000 : match.hasMatch() ? match.captured(1).toInt() : depth == 0 ? 1000 : 0;
            if (size > bestSize) {
                best = entry.filePath();
                bestSize = size;
            }
        }
    }
    if (!best.isEmpty())
        found.insert(key, best);
    return best;
}

namespace {
// An icon by name, else the pixmap nearest in size.
QImage icon(const QString &name, const QList<QImage> &pixmaps, const QString &themePath, QSize size) {
    QImage image;
    if (!name.isEmpty()) {
        // Ayatana's items may name a file. Only a regular file of a sane size is read: reading a
        // pipe or a device would never end.
        const QString file = name.startsWith('/') ? name : trayIconFile(name, themePath);
        const QFileInfo info(file);
        if (!file.isEmpty() && info.isFile() && info.size() < 8 * 1024 * 1024)
            image = QIcon(file).pixmap(size, 1.0).toImage();
        else if (!name.contains('/') && QIcon::hasThemeIcon(name))
            image = QIcon::fromTheme(name).pixmap(size, 1.0).toImage();
    }
    return image.isNull() ? trayPickPixmap(pixmaps, size) : image;
}
} // namespace

QImage TrayModel::picture(int serial, QSize size) const {
    const TrayItem *item = find(serial);
    if (!item || size.isEmpty())
        return {};
    QImage image;
    if (item->status == "NeedsAttention")
        image = icon(item->attentionIconName, item->attentionIcon, item->iconThemePath, size);
    if (image.isNull())
        image = icon(item->iconName, item->icon, item->iconThemePath, size);
    if (image.isNull())
        return {};
    const QImage overlay = icon(item->overlayIconName, item->overlayIcon, item->iconThemePath, size / 2);
    if (!overlay.isNull()) {
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        QPainter painter(&image);
        painter.drawImage(image.width() - overlay.width(), image.height() - overlay.height(), overlay);
    }
    return image;
}

void TrayModel::menuEdited(const QString &key) {
    if (TrayItem *item = find(key)) {
        ++item->menuRevision;
        Q_EMIT menuChanged(key);
    }
}
QImage TrayModel::menuPicture(int serial, int id, QSize size) const {
    const TrayItem *item = find(serial);
    const auto entry = item ? item->menu.find(id) : std::map<int, TrayMenuEntry>::const_iterator();
    if (!item || entry == item->menu.end() || size.isEmpty())
        return {};
    if (!entry->second.icon.isNull())
        return entry->second.icon.scaled(size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return icon(entry->second.iconName, {}, item->iconThemePath, size);
}
QVariantList TrayModel::menu(const QString &key, int parent) const {
    const int i = row(key);
    if (i < 0)
        return {};
    const TrayItem &item = items_[i];
    const auto top = item.menu.find(parent);
    if (top == item.menu.end())
        return {};
    QVariantList entries;
    bool separator = false; // one is owed before the next entry
    for (int id : top->second.children) {
        const auto found = item.menu.find(id);
        if (found == item.menu.end() || !found->second.visible)
            continue;
        const TrayMenuEntry &entry = found->second;
        if (entry.separator) {
            separator = !entries.isEmpty();
            continue;
        }
        if (separator)
            entries << QVariantMap{{"id", -1},          {"label", QString()}, {"enabled", false},
                                   {"separator", true}, {"toggle", QString()}, {"checked", false},
                                   {"icon", QString()}, {"submenu", false}};
        separator = false;
        const bool hasIcon = !entry.icon.isNull() || !entry.iconName.isEmpty();
        entries << QVariantMap{{"id", id},
                               {"label", entry.label},
                               {"enabled", entry.enabled},
                               {"separator", false},
                               {"toggle", entry.toggleType},
                               {"checked", entry.toggleState == 1},
                               {"icon", hasIcon ? QString("image://tray/%1/menu/%2/%3")
                                                      .arg(item.serial).arg(id).arg(item.menuRevision)
                                                : QString()},
                               {"submenu", entry.submenu}};
    }
    return entries;
}

int TrayModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : count(); }
QVariant TrayModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= count())
        return {};
    const auto &item = items_[index.row()];
    switch (role) {
    case KeyRole: return item.key;
    case TitleRole: return !item.title.isEmpty() ? item.title : item.id;
    case StatusRole: return item.status;
    case ImageRole: return QString("image://tray/%1/%2").arg(item.serial).arg(item.revision);
    case ToolTipRole: return item.toolTip();
    case ItemIsMenuRole: return item.itemIsMenu;
    case HasMenuRole: return !item.menuPath.isEmpty();
    }
    return {};
}
QHash<int, QByteArray> TrayModel::roleNames() const {
    return {{KeyRole, "key"},     {TitleRole, "title"},           {StatusRole, "status"},
            {ImageRole, "image"}, {ToolTipRole, "toolTip"},       {ItemIsMenuRole, "itemIsMenu"},
            {HasMenuRole, "hasMenu"}};
}
int TrayModel::row(const QString &key) const {
    for (int i = 0; i < count(); ++i)
        if (items_[i].key == key)
            return i;
    return -1;
}
TrayItem *TrayModel::find(const QString &key) {
    const int i = row(key);
    return i < 0 ? nullptr : &items_[i];
}
const TrayItem *TrayModel::find(int serial) const {
    for (const auto &item : items_)
        if (item.serial == serial)
            return &item;
    return nullptr;
}
bool TrayModel::contains(const QString &key) const { return row(key) >= 0; }
void TrayModel::add(TrayItem item) {
    if (const int i = row(item.key); i >= 0) {
        item.serial = items_[i].serial;
        item.revision = items_[i].revision + 1;
        items_[i] = std::move(item);
        Q_EMIT dataChanged(index(i), index(i));
        updateShown();
        return;
    }
    item.serial = nextSerial_++;
    beginInsertRows({}, count(), count());
    items_.push_back(std::move(item));
    endInsertRows();
    Q_EMIT countChanged();
    updateShown();
}
void TrayModel::changed(const QString &key, bool picture) {
    const int i = row(key);
    if (i < 0)
        return;
    if (picture)
        ++items_[i].revision;
    Q_EMIT dataChanged(index(i), index(i));
    updateShown();
}
void TrayModel::remove(const QString &which) {
    const QString key = which; // `which` may be the item's own key
    const int i = row(key);
    if (i < 0)
        return;
    beginRemoveRows({}, i, i);
    items_.erase(items_.begin() + i);
    endRemoveRows();
    Q_EMIT countChanged();
    updateShown();
    Q_EMIT menuChanged(key);
}
void TrayModel::clear() {
    if (items_.empty())
        return;
    QStringList keys;
    for (const auto &item : items_)
        keys << item.key;
    beginResetModel();
    items_.clear();
    endResetModel();
    Q_EMIT countChanged();
    updateShown();
    for (const auto &key : keys)
        Q_EMIT menuChanged(key);
}
void TrayModel::updateShown() {
    const int shown = int(std::count_if(items_.begin(), items_.end(),
                                        [](const TrayItem &item) { return item.status != "Passive"; }));
    if (shown != shown_) {
        shown_ = shown;
        Q_EMIT shownChanged();
    }
}
