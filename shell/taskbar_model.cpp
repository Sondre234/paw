// SPDX-License-Identifier: GPL-3.0-or-later
#include "taskbar_model.hpp"
#include "controller.hpp"
#include <QGuiApplication>
#include <QScreen>
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
std::optional<QPoint> TaskbarOrder::screenPlace(const QString &output) {
    if (output.isEmpty())
        return std::nullopt;
    for (const auto *screen : QGuiApplication::screens())
        if (screen->name() == output)
            return screen->geometry().topLeft();
    return std::nullopt;
}
void TaskbarOrder::followScreens(QObject *context, const std::function<void()> &changed) {
    // Not qGuiApp, which casts whatever application there is: a test's may be a QCoreApplication.
    auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    if (!app)
        return;
    auto follow = [context, changed](QScreen *screen) {
        QObject::connect(screen, &QScreen::geometryChanged, context, changed);
    };
    for (auto *screen : QGuiApplication::screens())
        follow(screen);
    QObject::connect(app, &QGuiApplication::screenAdded, context,
                     [follow, changed](QScreen *screen) {
                         follow(screen);
                         changed();
                     });
    QObject::connect(app, &QGuiApplication::screenRemoved, context, changed);
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
    // Which application a window belongs to decides what shows, and the monitor it is on where it
    // goes.
    connect(source, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &first, const QModelIndex &last, const QList<int> &roles) {
                // A taskbar showing one monitor's windows shows others as one moves.
                const bool outputs = roles.isEmpty() || roles.contains(roleId("output"));
                if (const int id = roleId("taskId"); id >= 0 && !first.parent().isValid() && outputs)
                    for (int row = first.row(); row <= last.row(); ++row)
                        noteOutput(source_->data(source_->index(row, 0), id).toInt());
                if (outputs || roles.contains(roleId("appId")))
                    Q_EMIT changed();
            });
    // A window can learn its monitor before the shell has its screen (one just plugged in).
    followScreens(this, [this] { monitorsChanged(); });
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
void TaskbarOrder::monitorsChanged() {
    bool moved = false;
    for (const auto &entry : QList(entries_))
        if (entry.pin.isEmpty())
            moved = noteOutput(entry.taskId) || moved;
    if (moved)
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
QString TaskbarOrder::output(int taskId) const {
    const int row = sourceRow(taskId), id = roleId("output");
    return row < 0 || id < 0 ? QString() : source_->data(source_->index(row, 0), id).toString();
}
QString TaskbarOrder::pinOf(const Entry &entry) const {
    if (!entry.pin.isEmpty())
        return entry.pin;
    return pins_.pinnedAppFor ? pins_.pinnedAppFor(appId(entry.taskId)) : QString();
}
int TaskbarOrder::windowOf(const Entry &entry, const QString &output) const {
    if (entry.pin.isEmpty())
        return entry.taskId;
    for (const auto &other : entries_)
        if (other.pin.isEmpty() && pinOf(other) == entry.pin && onOutput(other.taskId, output))
            return other.taskId;
    return -1;
}
bool TaskbarOrder::onOutput(int taskId, const QString &output) const {
    if (output.isEmpty())
        return true;
    const auto on = this->output(taskId);
    return on.isEmpty() || on == output;
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
    outputs_.removeIf([this](QHash<int, QString>::iterator seen) { return !rows_.contains(seen.key()); });
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
        noteOutput(taskId);
        added = true;
    }
    if (added)
        Q_EMIT changed();
}
bool TaskbarOrder::noteOutput(int taskId) {
    if (!entries_.contains(Entry{{}, taskId}))
        return false; // not placed yet: placeNew notes it
    const auto now = output(taskId);
    const auto seen = outputs_.value(taskId);
    if (outputs_.contains(taskId) && seen == now)
        return false;
    // A monitor without a place yet waits for one (monitorsChanged), not noted until then.
    if (!now.isEmpty() && place_ && !place_(now))
        return false;
    outputs_.insert(taskId, now);
    return followMonitor(taskId, !seen.isEmpty());
}
bool TaskbarOrder::followMonitor(int taskId, bool moved) {
    const Entry window{{}, taskId};
    const auto at = entries_.indexOf(window);
    if (!byMonitor_ || !place_ || at < 0)
        return false;
    // The application a window belongs to: its pin, else its app id.
    auto appOf = [this](const Entry &entry) {
        const auto pin = pinOf(entry);
        return std::pair(pin, pin.isEmpty() ? appId(entry.taskId) : QString());
    };
    const auto app = appOf(window);
    auto placeOf = [this](qsizetype i) { return place_(output(entries_[i].taskId)); };
    const auto place = placeOf(at);
    if ((app.first.isEmpty() && app.second.isEmpty()) || !place)
        return false;
    // Whether monitor `a` comes before `b`: to its left, or above it at the same x.
    auto ahead = [](const QPoint &a, const QPoint &b) { return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y()); };
    auto sameApp = [&](qsizetype i) {
        return i != at && entries_[i].pin.isEmpty() && appOf(entries_[i]) == app;
    };
    // A pinned application's place that shows nothing apart: its own, or one with a window.
    auto passes = [&](qsizetype i) {
        const auto &pin = entries_[i].pin;
        return !pin.isEmpty() && (pin == app.first || windowOf(Entry{pin}) >= 0);
    };
    // The application's other windows next to entry `i` (with it, when one of them) along the bar.
    auto runAround = [&](qsizetype i) {
        QList<qsizetype> run;
        for (auto j = i - 1; j >= 0 && (j == at || sameApp(j) || passes(j)); --j)
            if (sameApp(j))
                run.push_front(j);
        if (sameApp(i))
            run.push_back(i);
        for (auto j = i + 1; j < entries_.size() && (j == at || sameApp(j) || passes(j)); ++j)
            if (sameApp(j))
                run.push_back(j);
        return run;
    };
    auto run = runAround(at);
    // Of the others, the first ahead of it on a monitor after its own, else the last after it on
    // a monitor before its own.
    qsizetype wrong = -1;
    for (qsizetype i = 0; i < at && wrong < 0; ++i)
        if (const auto there = sameApp(i) && !run.contains(i) ? placeOf(i) : std::nullopt; there && ahead(*place, *there))
            wrong = i;
    for (auto i = entries_.size() - 1; i > at && wrong < 0; --i)
        if (const auto there = sameApp(i) && !run.contains(i) ? placeOf(i) : std::nullopt; there && ahead(*there, *place))
            wrong = i;
    if (wrong >= 0) {
        // On the wrong side of one apart from it, it joins that one's.
        run = runAround(wrong);
    } else if (run.isEmpty()) {
        return false;
    } else if (!moved) {
        // Just opened, it stays where it is unless it is on the wrong side of one beside it.
        const bool fits = std::none_of(run.cbegin(), run.cend(), [&](qsizetype i) {
            const auto there = placeOf(i);
            return there && (i < at ? ahead(*place, *there) : ahead(*there, *place));
        });
        if (fits)
            return false;
    }
    // After the last of them on a monitor before its own or on its own, else ahead of the first.
    auto anchor = entries_[run.first()];
    bool after = false;
    for (auto i : run)
        if (const auto there = placeOf(i); there && !ahead(*place, *there))
            anchor = entries_[i], after = true;
    auto next = entries_;
    next.removeAt(at);
    next.insert(next.indexOf(anchor) + (after ? 1 : 0), window);
    if (next == entries_)
        return false;
    entries_ = next;
    return true;
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
QList<TaskbarOrder::Entry> TaskbarOrder::shown(const View &view) const {
    QList<Entry> list;
    // The pinned applications with a window shown.
    QSet<QString> pinned;
    for (const auto &entry : entries_)
        if (entry.pin.isEmpty() && onOutput(entry.taskId, view.output))
            if (const auto pin = pinOf(entry); !pin.isEmpty())
                pinned.insert(pin);
    // Grouped, the app ids with a button already.
    QSet<QString> apps;
    for (const auto &entry : entries_) {
        if (!entry.pin.isEmpty()) {
            const bool windows = pinned.contains(entry.pin);
            if (view.grouped ? windows || view.launchers : !windows && view.launchers)
                list.push_back(entry);
        } else if (!onOutput(entry.taskId, view.output)) {
            continue;
        } else if (!view.grouped) {
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
void TaskbarOrder::move(int from, int to, const View &view) {
    const bool grouped = view.grouped;
    const auto list = shown(view);
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
        // Ahead of the entries no taskbar shows just before the target too: a pinned application's
        // place keeps to the button after it.
        const auto anywhere = view.output.isEmpty() && view.launchers ? list : shown(View{grouped});
        while (at > 0 && !anywhere.contains(rest[at - 1]))
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
    if (view_.grouped == grouped)
        return;
    view_.grouped = grouped;
    if (order_)
        order_->setByMonitor(!grouped);
    update();
    Q_EMIT groupedChanged();
}
void TaskbarModel::setOutput(const QString &output) {
    if (view_.output == output)
        return;
    view_.output = output;
    update();
    Q_EMIT outputChanged();
}
void TaskbarModel::setShowLaunchers(bool show) {
    if (view_.launchers == show)
        return;
    view_.launchers = show;
    update();
    Q_EMIT showLaunchersChanged();
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
    // Apart, a window's button follows its monitor; grouped, an application's button stays put.
    order_->setByMonitor(!view_.grouped);
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
        for (const auto &entry : order_->shown(view_))
            next.push_back({entry, order_->pinOf(entry), entry.pin.isEmpty() ? QVariantMap() : apps.value(entry.pin),
                            order_->windowOf(entry, view_.output)});
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
        order_->move(from, to, view_);
}
int TaskbarModel::rowOf(int taskId) const {
    for (qsizetype i = 0; i < rows_.size(); ++i)
        if (rows_[i].window == taskId)
            return int(i);
    if (!view_.grouped || !order_)
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
