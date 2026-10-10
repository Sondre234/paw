// SPDX-License-Identifier: GPL-3.0-or-later
#include "taskbar_model.hpp"
#include "controller.hpp"
#include <QSet>
#include <algorithm>
#include <optional>

TaskbarOrder *TaskbarOrder::of(QAbstractItemModel *source) {
    if (!source)
        return nullptr;
    if (auto *order = source->findChild<TaskbarOrder *>(QString(), Qt::FindDirectChildrenOnly))
        return order;
    return new TaskbarOrder(source);
}
TaskbarOrder::TaskbarOrder(QAbstractItemModel *source) : QObject(source), source_(source) {
    // A window that closed goes at once, so no taskbar names a window the source no longer has; a
    // new one waits for the turn of the event loop that gives it its app id, which says where it
    // goes.
    connect(source, &QAbstractItemModel::rowsInserted, this, [this] {
        indexRows();
        placeLater();
    });
    connect(source, &QAbstractItemModel::rowsRemoved, this, [this] {
        indexRows();
        forgetGone();
    });
    connect(source, &QAbstractItemModel::rowsMoved, this, &TaskbarOrder::indexRows);
    connect(source, &QAbstractItemModel::layoutChanged, this, &TaskbarOrder::indexRows);
    connect(source, &QAbstractItemModel::modelReset, this, [this] {
        roleIds_.clear();
        indexRows();
        forgetGone();
        placeLater();
    });
    // Which application a window belongs to decides what shows.
    connect(source, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
                if (roles.isEmpty() || roles.contains(roleId("appId")))
                    Q_EMIT changed();
            });
    // The windows open already wait for the pins, which say where they go.
    indexRows();
    placeLater();
}
void TaskbarOrder::setPins(Pins pins) {
    if (hasPins())
        return;
    pins_ = std::move(pins);
    syncPins();
    placeNew();
    Q_EMIT changed();
}
void TaskbarOrder::pinsChanged() {
    syncPins();
    Q_EMIT changed();
}
int TaskbarOrder::roleId(const char *role) const {
    auto found = roleIds_.constFind(role);
    if (found != roleIds_.cend())
        return *found;
    // A model may gain roles as it fills (a QML ListModel), so only a role found is remembered.
    const int id = source_ ? source_->roleNames().key(role, -1) : -1;
    if (id >= 0)
        roleIds_.insert(role, id);
    return id;
}
QString TaskbarOrder::appId(int taskId) const {
    const int row = sourceRow(taskId), id = roleId("appId");
    return row < 0 || id < 0 ? QString() : source_->data(source_->index(row, 0), id).toString();
}
QString TaskbarOrder::pinOf(const Entry &entry) const {
    if (!entry.pin.isEmpty())
        return entry.pin;
    return pins_.pinnedAppFor ? pins_.pinnedAppFor(appId(entry.taskId)) : QString();
}
int TaskbarOrder::windowOf(const Entry &entry) const {
    if (entry.pin.isEmpty())
        return entry.taskId;
    for (const auto &other : entries_)
        if (other.pin.isEmpty() && pinOf(other) == entry.pin)
            return other.taskId;
    return -1;
}
void TaskbarOrder::indexRows() {
    rows_.clear();
    const int id = roleId("taskId");
    if (!source_ || id < 0)
        return;
    for (int row = 0; row < source_->rowCount(); ++row)
        rows_.insert(source_->data(source_->index(row, 0), id).toInt(), row);
}
void TaskbarOrder::forgetGone() {
    const auto before = entries_.size();
    entries_.removeIf([this](const Entry &entry) { return entry.pin.isEmpty() && !rows_.contains(entry.taskId); });
    if (entries_.size() != before)
        Q_EMIT changed();
}
void TaskbarOrder::placeLater() {
    if (placing_)
        return;
    placing_ = true;
    QMetaObject::invokeMethod(this, &TaskbarOrder::placeNew, Qt::QueuedConnection);
}
void TaskbarOrder::placeNew() {
    placing_ = false;
    const int id = roleId("taskId");
    if (!source_ || id < 0)
        return;
    QSet<int> known;
    for (const auto &entry : entries_)
        if (entry.pin.isEmpty())
            known.insert(entry.taskId);
    bool added = false;
    for (int row = 0; row < source_->rowCount(); ++row) {
        const int taskId = source_->data(source_->index(row, 0), id).toInt();
        if (known.contains(taskId))
            continue;
        known.insert(taskId);
        const Entry window{{}, taskId};
        // A pinned application's first window opens where its launcher is.
        auto at = entries_.size();
        const auto pin = pinOf(window);
        if (!pin.isEmpty() && windowOf({pin}) < 0)
            if (const auto place = entries_.indexOf(Entry{pin}); place >= 0)
                at = place + 1;
        entries_.insert(at, window);
        added = true;
    }
    if (added)
        Q_EMIT changed();
}
void TaskbarOrder::syncPins() {
    const auto pinned = pins_.pinned ? pins_.pinned() : QStringList();
    entries_.removeIf([&pinned](const Entry &entry) { return !entry.pin.isEmpty() && !pinned.contains(entry.pin); });
    for (const auto &pin : pinned) {
        if (entries_.contains(Entry{pin}))
            continue;
        // Pinned with windows open: where the first of them is. Else after the other pins.
        qsizetype at = -1;
        for (qsizetype i = 0; i < entries_.size() && at < 0; ++i)
            if (entries_[i].pin.isEmpty() && pinOf(entries_[i]) == pin)
                at = i;
        if (at < 0) {
            at = 0;
            for (qsizetype i = 0; i < entries_.size(); ++i)
                if (!entries_[i].pin.isEmpty())
                    at = i + 1;
        }
        entries_.insert(at, Entry{pin});
    }
}
QList<TaskbarOrder::Entry> TaskbarOrder::shown(bool grouped) const {
    QList<Entry> list;
    // Apart: the pinned applications with a window. Grouped: the app ids with a button already.
    QSet<QString> apps;
    if (!grouped)
        for (const auto &entry : entries_)
            if (entry.pin.isEmpty())
                if (const auto pin = pinOf(entry); !pin.isEmpty())
                    apps.insert(pin);
    for (const auto &entry : entries_) {
        if (!entry.pin.isEmpty()) {
            if (grouped || !apps.contains(entry.pin))
                list.push_back(entry);
        } else if (!grouped) {
            list.push_back(entry);
        } else if (pinOf(entry).isEmpty()) {
            const auto app = appId(entry.taskId);
            if (app.isEmpty() || !apps.contains(app)) {
                list.push_back(entry);
                if (!app.isEmpty())
                    apps.insert(app);
            }
        }
    }
    return list;
}
void TaskbarOrder::move(int from, int to, bool grouped) {
    const auto list = shown(grouped);
    if (from < 0 || to < 0 || from >= list.size() || to >= list.size() || from == to)
        return;
    const auto moved = list[from], target = list[to];
    // Grouped, an application's windows go with its button, so that they are still together
    // should it be unpinned or the windows shown apart.
    const auto group = grouped && moved.pin.isEmpty() && pinOf(moved).isEmpty() ? appId(moved.taskId) : QString();
    auto goes = [&](const Entry &entry) {
        if (entry == moved)
            return true;
        if (!grouped || !entry.pin.isEmpty())
            return false;
        const auto pin = pinOf(entry);
        return moved.pin.isEmpty() ? !group.isEmpty() && pin.isEmpty() && appId(entry.taskId) == group
                                   : pin == moved.pin;
    };
    QList<Entry> taken, rest;
    for (const auto &entry : entries_)
        (goes(entry) ? taken : rest).push_back(entry);
    auto at = rest.indexOf(target);
    if (to > from) {
        ++at;
    } else {
        // Ahead of the entries not shown just before the target too: a pinned application's place
        // keeps to the button after it.
        while (at > 0 && !list.contains(rest[at - 1]))
            --at;
    }
    for (qsizetype i = 0; i < taken.size(); ++i)
        rest.insert(at + i, taken[i]);
    entries_ = rest;
    if (!moved.pin.isEmpty() && pins_.reorder) {
        QStringList pins;
        for (const auto &entry : entries_)
            if (!entry.pin.isEmpty())
                pins.push_back(entry.pin);
        pins_.reorder(pins);
    }
    Q_EMIT changed();
}

TaskbarModel::TaskbarModel(QObject *parent) : QAbstractListModel(parent) {
    connect(this, &QAbstractItemModel::rowsInserted, this, &TaskbarModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &TaskbarModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &TaskbarModel::countChanged);
}
QObject *TaskbarModel::controller() const { return shell_; }
void TaskbarModel::setController(QObject *controller) {
    if (shell_ == controller)
        return;
    shell_ = controller;
    attach();
    Q_EMIT controllerChanged();
}
void TaskbarModel::setSourceModel(QAbstractItemModel *source) {
    if (source_ == source)
        return;
    source_ = source;
    attach();
    Q_EMIT sourceModelChanged();
}
void TaskbarModel::setGrouped(bool grouped) {
    if (grouped_ == grouped)
        return;
    grouped_ = grouped;
    update();
    Q_EMIT groupedChanged();
}
void TaskbarModel::attach() {
    for (const auto &connection : connections_)
        disconnect(connection);
    connections_.clear();
    sourceRoles_.clear();
    // Another source's rows mean nothing here.
    if (!rows_.isEmpty()) {
        beginResetModel();
        rows_.clear();
        endResetModel();
    }
    order_ = TaskbarOrder::of(source_);
    if (!order_)
        return;
    if (auto *shell = qobject_cast<ShellController *>(shell_); shell && !order_->hasPins()) {
        QPointer<ShellController> pins(shell);
        order_->setPins({
            [pins] {
                QStringList ids;
                if (pins)
                    for (const auto &app : pins->pinned())
                        ids.push_back(app.toMap().value("appId").toString());
                return ids;
            },
            [pins](const QString &appId) { return pins ? pins->pinnedAppFor(appId) : QString(); },
            [pins](const QStringList &order) {
                if (pins)
                    pins->orderPins(order);
            },
        });
        connect(shell, &ShellController::appsChanged, order_, &TaskbarOrder::pinsChanged);
    }
    connections_ = {
        connect(order_, &TaskbarOrder::changed, this, &TaskbarModel::update),
        connect(source_, &QAbstractItemModel::dataChanged, this, &TaskbarModel::forward),
        connect(source_, &QAbstractItemModel::modelReset, this, [this] { sourceRoles_.clear(); }),
    };
    update();
}
namespace {
// The longest run of `positions` that only grows, by its indexes.
QSet<qsizetype> growing(const QList<qsizetype> &positions) {
    const auto n = positions.size();
    QList<qsizetype> length(n, 1), previous(n, -1);
    qsizetype best = -1;
    for (qsizetype i = 0; i < n; ++i) {
        for (qsizetype j = 0; j < i; ++j)
            if (positions[j] < positions[i] && length[j] + 1 > length[i]) {
                length[i] = length[j] + 1;
                previous[i] = j;
            }
        if (best < 0 || length[i] > length[best])
            best = i;
    }
    QSet<qsizetype> run;
    for (auto i = best; i >= 0; i = previous[i])
        run.insert(i);
    return run;
}
} // namespace
void TaskbarModel::update() {
    QList<Row> next;
    if (order_) {
        QHash<QString, QVariantMap> apps;
        if (auto *shell = qobject_cast<ShellController *>(shell_))
            for (const auto &app : shell->pinned()) {
                const auto record = app.toMap();
                apps.insert(record["appId"].toString(), record);
            }
        for (const auto &entry : order_->shown(grouped_))
            next.push_back({entry, order_->pinOf(entry), entry.pin.isEmpty() ? QVariantMap() : apps.value(entry.pin),
                            order_->windowOf(entry)});
    }
    auto indexIn = [](const QList<Row> &list, const TaskbarOrder::Entry &entry) -> qsizetype {
        for (qsizetype i = 0; i < list.size(); ++i)
            if (list[i].entry == entry)
                return i;
        return -1;
    };
    // The nearest row ahead of `at` that `other` has too, nullopt for none.
    auto anchor = [&indexIn](const QList<Row> &list, qsizetype at, const QList<Row> &other) {
        for (auto i = at - 1; i >= 0; --i)
            if (indexIn(other, list[i].entry) >= 0)
                return std::optional<TaskbarOrder::Entry>(list[i].entry);
        return std::optional<TaskbarOrder::Entry>();
    };
    QList<TaskbarOrder::Entry> changedRows;
    // A launcher giving its place to its application's first window or taking it back from the
    // last one, or, grouped, a window's button becoming its application's as it is pinned or back
    // as it is unpinned, is one button that changes, not one going and another coming.
    for (qsizetype i = 0; i < rows_.size(); ++i) {
        const auto &row = rows_[i];
        if (indexIn(next, row.entry) >= 0)
            continue;
        const bool open = row.entry.pin.isEmpty() && order_ && order_->sourceRow(row.entry.taskId) >= 0;
        const auto pin = open ? order_->pinOf(row.entry) : row.pin;
        for (qsizetype j = 0; j < next.size(); ++j) {
            const bool sameWindow = row.window >= 0 && row.window == next[j].window;
            const bool handOver = !pin.isEmpty() && next[j].pin == pin && next[j].entry.pin.isEmpty() != row.entry.pin.isEmpty();
            if ((sameWindow || handOver) && indexIn(rows_, next[j].entry) < 0 &&
                anchor(rows_, i, next) == anchor(next, j, rows_)) {
                rows_[i] = next[j];
                changedRows.push_back(next[j].entry);
                break;
            }
        }
    }
    // What stays takes its new state before rows move around it.
    for (auto &row : rows_)
        if (const auto j = indexIn(next, row.entry); j >= 0 && (row.window != next[j].window || row.app != next[j].app)) {
            row = next[j];
            changedRows.push_back(row.entry);
        }
    for (auto i = rows_.size() - 1; i >= 0; --i)
        if (indexIn(next, rows_[i].entry) < 0) {
            beginRemoveRows({}, int(i), int(i));
            rows_.removeAt(i);
            endRemoveRows();
        }
    // As few rows as can be move, the others keeping their order: a button dragged along the bar
    // is the one that moves, the ones it passed only make way.
    QList<qsizetype> positions;
    for (const auto &row : rows_)
        positions.push_back(indexIn(next, row.entry));
    const auto staying = growing(positions);
    QList<Row> kept;
    for (const auto &row : next)
        if (indexIn(rows_, row.entry) >= 0)
            kept.push_back(row);
    QList<TaskbarOrder::Entry> moving;
    for (qsizetype i = 0; i < rows_.size(); ++i)
        if (!staying.contains(i))
            moving.push_back(rows_[i].entry);
    for (qsizetype k = 0; k < kept.size(); ++k) {
        if (!moving.contains(kept[k].entry))
            continue;
        const auto from = indexIn(rows_, kept[k].entry);
        const auto to = k > 0 ? indexIn(rows_, kept[k - 1].entry) + 1 : 0;
        if (to == from || to == from + 1)
            continue;
        beginMoveRows({}, int(from), int(from), {}, int(to));
        rows_.move(from, to > from ? to - 1 : to);
        endMoveRows();
    }
    for (qsizetype i = 0; i < next.size(); ++i)
        if (i >= rows_.size() || !(rows_[i].entry == next[i].entry)) {
            beginInsertRows({}, int(i), int(i));
            rows_.insert(i, next[i]);
            endInsertRows();
        }
    for (const auto &entry : changedRows)
        if (const auto i = indexIn(rows_, entry); i >= 0)
            Q_EMIT dataChanged(index(int(i)), index(int(i)));
    const auto launchers = int(std::count_if(rows_.cbegin(), rows_.cend(), [](const Row &row) { return row.window < 0; }));
    if (launchers != launchers_) {
        launchers_ = launchers;
        Q_EMIT launchersChanged();
    }
}
int TaskbarModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : int(rows_.size()); }
QHash<int, QByteArray> TaskbarModel::roleNames() const {
    return {{TaskId, "taskId"},       {Title, "title"},   {AppId, "appId"}, {Active, "active"},
            {Minimized, "minimized"}, {Urgent, "urgent"}, {App, "app"}};
}
int TaskbarModel::sourceRole(int role) const {
    auto found = sourceRoles_.constFind(role);
    if (found != sourceRoles_.cend())
        return *found;
    // As in TaskbarOrder::roleId, only a role found is remembered.
    const int id = source_ ? source_->roleNames().key(roleNames().value(role), -1) : -1;
    if (id >= 0)
        sourceRoles_.insert(role, id);
    return id;
}
QVariant TaskbarModel::data(const QModelIndex &index, int role) const {
    if (index.parent().isValid() || index.row() < 0 || index.row() >= rows_.size())
        return {};
    const auto &row = rows_[index.row()];
    if (role == App)
        return row.app;
    if (role == TaskId)
        return row.window;
    const int source = row.window >= 0 && order_ ? order_->sourceRow(row.window) : -1;
    const int id = sourceRole(role);
    if (source >= 0 && id >= 0)
        return source_->data(source_->index(source, 0), id);
    // A launcher, or a role the source does not have.
    switch (role) {
    case Title:
        return row.window < 0 ? row.app.value("name").toString() : QString();
    case AppId:
        return QString();
    case Active:
    case Minimized:
    case Urgent:
        return false;
    }
    return {};
}
void TaskbarModel::forward(const QModelIndex &first, const QModelIndex &last, const QList<int> &roles) {
    if (!order_ || first.parent().isValid())
        return;
    QList<int> mine;
    for (int role : roles)
        for (int own : roleNames().keys())
            if (sourceRole(own) == role)
                mine.push_back(own);
    if (!roles.isEmpty() && mine.isEmpty())
        return;
    const int id = source_->roleNames().key("taskId", -1);
    for (int row = first.row(); row <= last.row(); ++row) {
        const int taskId = source_->data(source_->index(row, 0), id).toInt();
        for (qsizetype i = 0; i < rows_.size(); ++i)
            if (rows_[i].window == taskId)
                Q_EMIT dataChanged(index(int(i)), index(int(i)), mine);
    }
}
void TaskbarModel::move(int from, int to) {
    if (order_)
        order_->move(from, to, grouped_);
}
int TaskbarModel::rowOf(int taskId) const {
    for (qsizetype i = 0; i < rows_.size(); ++i)
        if (rows_[i].window == taskId)
            return int(i);
    if (!grouped_ || !order_)
        return -1;
    // Grouped, the row of its application.
    const auto pin = order_->pinOf({{}, taskId}), app = order_->appId(taskId);
    for (qsizetype i = 0; i < rows_.size(); ++i) {
        const auto &row = rows_[i];
        if (pin.isEmpty() ? row.entry.pin.isEmpty() && row.pin.isEmpty() && !app.isEmpty() &&
                                order_->appId(row.window) == app
                          : row.entry.pin == pin)
            return int(i);
    }
    return -1;
}
