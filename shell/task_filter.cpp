// SPDX-License-Identifier: GPL-3.0-or-later
#include "task_filter.hpp"
#include "controller.hpp"

TaskFilter::TaskFilter(QObject *parent) : QSortFilterProxyModel(parent) {
    setDynamicSortFilter(true);
    connect(this, &QAbstractItemModel::rowsInserted, this, &TaskFilter::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &TaskFilter::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &TaskFilter::countChanged);
    connect(this, &QAbstractItemModel::rowsInserted, this, &TaskFilter::summaryChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &TaskFilter::summaryChanged);
    connect(this, &QAbstractItemModel::rowsMoved, this, &TaskFilter::summaryChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &TaskFilter::summaryChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &TaskFilter::summaryChanged);
    connect(this, &QAbstractItemModel::dataChanged, this, &TaskFilter::summaryChanged);
}
void TaskFilter::setSourceModel(QAbstractItemModel *source) {
    for (const auto &connection : sourceConnections_)
        disconnect(connection);
    sourceConnections_.clear();
    forget();
    roleIds_.clear();
    if (source) {
        // Connected ahead of the proxy's own handlers, which ask filterAcceptsRow at once.
        const auto forgetting = [this] { forget(); };
        sourceConnections_ = {
            connect(source, &QAbstractItemModel::dataChanged, this,
                    [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
                        if (decides(roles))
                            forget();
                    }),
            connect(source, &QAbstractItemModel::rowsInserted, this, forgetting),
            connect(source, &QAbstractItemModel::rowsRemoved, this, forgetting),
            connect(source, &QAbstractItemModel::rowsMoved, this, forgetting),
            connect(source, &QAbstractItemModel::layoutChanged, this, forgetting),
            connect(source, &QAbstractItemModel::modelReset, this,
                    [this] {
                        forget();
                        roleIds_.clear();
                    }),
        };
    }
    QSortFilterProxyModel::setSourceModel(source);
    if (!source)
        return;
    // Connected after the proxy's own handlers, so it has caught up with the change first. A
    // window whose app id changes may join or leave this slot; grouped, which window stands for
    // its application depends on those ahead of it too.
    auto regroup = [this] {
        if (grouped_)
            refilter();
    };
    // Only the application id (and the window's number, which comes after the window) decides
    // where a window belongs, so its other changes (a title changing is the commonest event of
    // all) leave the rows as they are.
    sourceConnections_ += {
        connect(source, &QAbstractItemModel::dataChanged, this,
                [this](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
                    if (decides(roles))
                        refilter();
                }),
        connect(source, &QAbstractItemModel::rowsInserted, this, regroup),
        connect(source, &QAbstractItemModel::rowsRemoved, this, regroup),
        connect(source, &QAbstractItemModel::rowsMoved, this, regroup),
    };
}
void TaskFilter::refilter() {
    forget();
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    endFilterChange(Direction::Rows);
#else
    invalidateRowsFilter();
#endif
}
QObject *TaskFilter::controller() const { return shell_; }
void TaskFilter::setController(QObject *controller) {
    if (shell_ == controller)
        return;
    if (shell_)
        disconnect(shell_, nullptr, this, nullptr);
    shell_ = qobject_cast<ShellController *>(controller);
    // Pinning and unpinning move windows between slots.
    if (shell_)
        connect(shell_, &ShellController::appsChanged, this, &TaskFilter::refilter);
    refilter();
    Q_EMIT controllerChanged();
}
void TaskFilter::setApp(const QString &app) {
    if (app_ == app)
        return;
    app_ = app;
    refilter();
    Q_EMIT appChanged();
}
void TaskFilter::setWindowApp(const QString &windowApp) {
    if (windowApp_ == windowApp)
        return;
    windowApp_ = windowApp;
    refilter();
    Q_EMIT windowAppChanged();
}
void TaskFilter::setGrouped(bool grouped) {
    if (grouped_ == grouped)
        return;
    grouped_ = grouped;
    refilter();
    Q_EMIT groupedChanged();
}
void TaskFilter::setTaskId(int taskId) {
    if (taskId_ == taskId)
        return;
    taskId_ = taskId;
    refilter();
    Q_EMIT taskIdChanged();
}
void TaskFilter::setWindowId(int windowId) {
    if (windowId_ == windowId)
        return;
    windowId_ = windowId;
    refilter();
    Q_EMIT windowIdChanged();
}
bool TaskFilter::decides(const QList<int> &roles) const {
    return roles.isEmpty() || roles.contains(roleId("appId")) ||
           (windowId_ > 0 && roles.contains(roleId("windowId")));
}
int TaskFilter::roleId(const char *role) const {
    auto found = roleIds_.constFind(role);
    if (found != roleIds_.cend())
        return *found;
    // A model may gain roles as it fills (a QML ListModel), so only a role found is remembered.
    const int id = sourceModel()->roleNames().key(role, -1);
    if (id >= 0)
        roleIds_.insert(role, id);
    return id;
}
QVariant TaskFilter::sourceValue(int row, const char *role) const {
    auto *source = sourceModel();
    // A model may not have the role (yet), which a QML ListModel would not check.
    const int id = roleId(role);
    return id < 0 ? QVariant() : source->data(source->index(row, 0), id);
}
int TaskFilter::value(int row, const char *role) const {
    return sourceValue(mapToSource(index(row, 0)).row(), role).toInt();
}
bool TaskFilter::belongs(int row) const {
    if (taskId_ >= 0)
        return sourceValue(row, "taskId").toInt() == taskId_;
    if (windowId_ > 0)
        return sourceValue(row, "windowId").toInt() == windowId_;
    const auto appId = sourceValue(row, "appId").toString();
    if (!windowApp_.isEmpty() && appId != windowApp_)
        return false;
    if (!shell_)
        return app_.isEmpty();
    return shell_->pinnedAppFor(appId) == app_;
}
QString TaskFilter::groupOf(int row) const {
    return app_.isEmpty() ? sourceValue(row, "appId").toString() : app_;
}
void TaskFilter::computeAccepted() const {
    const int rows = sourceModel()->rowCount();
    accepted_.assign(rows, 0);
    QSet<QString> seen;
    for (int row = 0; row < rows; ++row) {
        if (!belongs(row))
            continue;
        const auto group = grouped_ ? groupOf(row) : QString();
        // The first window of an application stands for it; windows without one stand alone.
        accepted_[row] = group.isEmpty() || !seen.contains(group);
        if (!group.isEmpty())
            seen.insert(group);
    }
    acceptedValid_ = true;
}
bool TaskFilter::filterAcceptsRow(int row, const QModelIndex &parent) const {
    if (parent.isValid() || row < 0)
        return false;
    if (!acceptedValid_ || row >= accepted_.size())
        computeAccepted();
    return row < accepted_.size() && accepted_[row];
}
int TaskFilter::activeTask() const {
    for (int row = 0; row < rowCount(); ++row)
        if (value(row, "active"))
            return value(row, "taskId");
    return -1;
}
bool TaskFilter::minimized() const {
    for (int row = 0; row < rowCount(); ++row)
        if (!value(row, "minimized"))
            return false;
    return rowCount() > 0;
}
bool TaskFilter::urgent() const {
    for (int row = 0; row < rowCount(); ++row)
        if (value(row, "urgent"))
            return true;
    return false;
}
QVariantList TaskFilter::windows() const {
    QVariantList list;
    const auto roles = roleNames();
    for (int row = 0; row < rowCount(); ++row) {
        QVariantMap window;
        for (auto role = roles.cbegin(); role != roles.cend(); ++role)
            window.insert(QString::fromUtf8(role.value()), data(index(row, 0), role.key()));
        list.push_back(window);
    }
    return list;
}
int TaskFilter::nextTask() const {
    if (rowCount() == 0)
        return -1;
    for (int row = 0; row < rowCount(); ++row)
        if (value(row, "active"))
            return value((row + 1) % rowCount(), "taskId");
    return value(0, "taskId");
}
