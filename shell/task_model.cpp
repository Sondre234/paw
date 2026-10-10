// SPDX-License-Identifier: GPL-3.0-or-later
#include "task_model.hpp"
#include <algorithm>
#include <cerrno>
#include <climits>
#include <csignal>
#include <cstring>
#include <unistd.h>

TaskModel::TaskModel(QObject *parent) : QAbstractListModel(parent) {
    connect(&pictures_, &WindowPictures::changed, this, [this](int id) {
        for (int i = 0; i < rowCount(); ++i)
            if (tasks_[i]->id == id)
                Q_EMIT dataChanged(index(i), index(i), {Picture});
    });
}
TaskModel::~TaskModel() {
    read_.reset();
    write_.reset();
    pictures_.stop();
    for (auto &task : tasks_) {
        if (task->window)
            paw_window_v1_destroy(task->window);
        zwlr_foreign_toplevel_handle_v1_destroy(task->handle);
    }
    if (control_)
        paw_window_control_v1_destroy(control_);
    if (captureManager_)
        ext_image_copy_capture_manager_v1_destroy(captureManager_);
    if (shm_)
        wl_shm_destroy(shm_);
    if (manager_)
        zwlr_foreign_toplevel_manager_v1_destroy(manager_);
    if (seat_)
        wl_seat_destroy(seat_);
    if (registry_)
        wl_registry_destroy(registry_);
    if (display_)
        wl_display_disconnect(display_);
}
bool TaskModel::connectDisplay() {
    display_ = wl_display_connect(nullptr);
    if (!display_)
        return false;
    registry_ = wl_display_get_registry(display_);
    static const wl_registry_listener listener{global, globalRemoved};
    wl_registry_add_listener(registry_, &listener, this);
    if (wl_display_roundtrip(display_) < 0 || !manager_ || !seat_)
        return false;
    if (wl_display_roundtrip(display_) < 0)
        return false;
    read_ = std::make_unique<QSocketNotifier>(wl_display_get_fd(display_), QSocketNotifier::Read);
    write_ = std::make_unique<QSocketNotifier>(wl_display_get_fd(display_), QSocketNotifier::Write);
    write_->setEnabled(false);
    connect(read_.get(), &QSocketNotifier::activated, this, [this] {
        if (wl_display_dispatch(display_) < 0) {
            read_->setEnabled(false);
            write_->setEnabled(false);
            pictures_.stop();
            Q_EMIT disconnected();
        } else
            flush();
    });
    connect(write_.get(), &QSocketNotifier::activated, this, [this] { flush(); });
    flush();
    return true;
}
void TaskModel::flush() {
    if (!display_)
        return;
    int result = wl_display_flush(display_);
    if (write_)
        write_->setEnabled(result < 0 && errno == EAGAIN);
    if (result < 0 && errno != EAGAIN)
        Q_EMIT disconnected();
}
int TaskModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : static_cast<int>(tasks_.size());
}
QVariant TaskModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount())
        return {};
    const auto &task = *tasks_[index.row()];
    const auto &state = task.state;
    switch (role) {
    case TaskId:
        return task.id;
    case Title:
        return state.title.isEmpty() ? state.appId : state.title;
    case AppId:
        return state.appId;
    case Active:
        return state.active;
    case Minimized:
        return state.minimized;
    case Maximized:
        return state.maximized;
    case Urgent:
        return state.urgent;
    case Fullscreen:
        return state.fullscreen;
    case Output:
        return state.output;
    case Workspace:
        return state.workspace;
    case Sticky:
        return state.sticky;
    case Floating:
        return state.floating;
    case Tiling:
        return state.tiling;
    case Picture:
        return pictures_.url(task.id);
    case Pid:
        return state.pid;
    case WindowId:
        return state.windowId;
    case Above:
        // Undefined while the compositor cannot keep a window above, so the menus leave it out.
        if (!task.window ||
            paw_window_v1_get_version(task.window) < PAW_WINDOW_V1_STATE_ABOVE_SINCE_VERSION)
            return {};
        return state.above;
    default:
        return {};
    }
}
QHash<int, QByteArray> TaskModel::roleNames() const {
    return {{TaskId, "taskId"},       {Title, "title"},           {AppId, "appId"},
            {Active, "active"},       {Minimized, "minimized"},   {Maximized, "maximized"},
            {Urgent, "urgent"},       {Fullscreen, "fullscreen"}, {Output, "output"},
            {Workspace, "workspace"}, {Sticky, "sticky"},         {Floating, "floating"},
            {Tiling, "tiling"},       {Picture, "picture"},       {Pid, "pid"},
            {WindowId, "windowId"},   {Above, "above"}};
}
TaskModel::Task *TaskModel::find(int id) {
    for (auto &task : tasks_)
        if (task->id == id)
            return task.get();
    return nullptr;
}
void TaskModel::activate(int id) {
    auto *task = find(id);
    if (!task || !seat_)
        return;
    if (task->state.active && !task->state.minimized)
        zwlr_foreign_toplevel_handle_v1_set_minimized(task->handle);
    else {
        zwlr_foreign_toplevel_handle_v1_unset_minimized(task->handle);
        zwlr_foreign_toplevel_handle_v1_activate(task->handle, seat_);
    }
    flush();
}
void TaskModel::minimize(int id) {
    if (auto *task = find(id))
        zwlr_foreign_toplevel_handle_v1_set_minimized(task->handle);
    flush();
}
void TaskModel::maximize(int id) {
    if (auto *task = find(id)) {
        if (task->state.maximized)
            zwlr_foreign_toplevel_handle_v1_unset_maximized(task->handle);
        else
            zwlr_foreign_toplevel_handle_v1_set_maximized(task->handle);
    }
    flush();
}
void TaskModel::setFullscreen(int id, bool fullscreen) {
    if (auto *task = find(id)) {
        // On the output it is on.
        if (fullscreen)
            zwlr_foreign_toplevel_handle_v1_set_fullscreen(task->handle, nullptr);
        else
            zwlr_foreign_toplevel_handle_v1_unset_fullscreen(task->handle);
    }
    flush();
}
void TaskModel::moveToWorkspace(int id, int number) {
    auto *task = find(id);
    if (task && task->window && number > 0)
        paw_window_v1_move_to_workspace(task->window, static_cast<uint32_t>(number));
    flush();
}
void TaskModel::moveToOutput(int id, const QString &output) {
    auto *task = find(id);
    if (task && task->window && !output.isEmpty())
        paw_window_v1_move_to_output(task->window, output.toUtf8().constData());
    flush();
}
void TaskModel::setState(int id, bool on, void (*set)(paw_window_v1 *),
                         void (*unset)(paw_window_v1 *), uint32_t since) {
    auto *task = find(id);
    if (task && task->window && paw_window_v1_get_version(task->window) >= since)
        (on ? set : unset)(task->window);
    flush();
}
void TaskModel::setSticky(int id, bool sticky) {
    setState(id, sticky, paw_window_v1_set_sticky, paw_window_v1_unset_sticky);
}
void TaskModel::setAbove(int id, bool above) {
    setState(id, above, paw_window_v1_set_above, paw_window_v1_unset_above,
             PAW_WINDOW_V1_SET_ABOVE_SINCE_VERSION);
}
void TaskModel::setFloating(int id, bool floating) {
    setState(id, floating, paw_window_v1_set_floating, paw_window_v1_unset_floating);
}
void TaskModel::close(int id) {
    if (auto *task = find(id))
        zwlr_foreign_toplevel_handle_v1_close(task->handle);
    flush();
}
void TaskModel::kill(int id) {
    auto *task = find(id);
    const pid_t pid = task ? task->state.pid : 0;
    if (pid > 1 && pid != getpid() && pid != getppid())
        ::kill(pid, SIGKILL);
}
void TaskModel::showDesktop() {
    for (const auto &task : tasks_)
        zwlr_foreign_toplevel_handle_v1_set_minimized(task->handle);
    flush();
}
void TaskModel::watchPicture(int taskId, int pixelWidth, bool live) {
    if (auto *task = find(taskId))
        pictures_.watch(taskId, task->window, pixelWidth, live);
}
void TaskModel::unwatchPicture(int taskId) { pictures_.unwatch(taskId); }
QImage TaskModel::picture(int taskId) const { return pictures_.picture(taskId); }
QImage windowImage(const TaskModel &tasks, const QString &id) {
    QImage image = tasks.picture(id.section('/', 0, 0).toInt());
    if (image.isNull()) {
        image = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
    }
    return image;
}
void TaskModel::peek(int taskId) {
    auto *task = find(taskId);
    if (!task || !task->window || peeked_ == taskId ||
        paw_window_v1_get_version(task->window) < PAW_WINDOW_V1_SET_PEEK_SINCE_VERSION)
        return;
    // Over to this window from the one peeked at so far, if any, the others staying faded.
    paw_window_v1_set_peek(task->window);
    setPeeked(taskId);
    flush();
}
void TaskModel::endPeek(int taskId) {
    if (peeked_ < 0 || peeked_ != taskId)
        return;
    if (auto *task = find(taskId); task && task->window)
        paw_window_v1_unset_peek(task->window);
    setPeeked(-1);
    flush();
}
void TaskModel::setPeeked(int taskId) {
    if (peeked_ == taskId)
        return;
    peeked_ = taskId;
    Q_EMIT peekedTaskChanged();
}
void TaskModel::setUrgent(const QList<QPair<QString, QString>> &windows) {
    if (windows == urgent_)
        return;
    urgent_ = windows;
    matchUrgent();
}
void TaskModel::matchUrgent(const Task *except) {
    QList<const Task *> claimed;
    for (const auto &[appId, title] : urgent_) {
        for (auto &task : tasks_) {
            // The compositor cuts a very long title short.
            const auto &state = task->state;
            const bool sameTitle = state.title == title ||
                                   (title.toUtf8().size() >= 250 && state.title.startsWith(title));
            if (state.appId == appId && sameTitle && !claimed.contains(task.get())) {
                claimed.push_back(task.get());
                break;
            }
        }
    }
    for (int i = 0; i < rowCount(); ++i) {
        auto &task = *tasks_[i];
        task.state.urgent = claimed.contains(&task);
        if (&task == except || task.state.urgent == task.shown.urgent)
            continue;
        task.shown.urgent = task.state.urgent;
        Q_EMIT dataChanged(index(i), index(i), {Urgent});
    }
}
void TaskModel::changed(Task *task) {
    // A window is found by its app id and title, so either changing may mark or unmark it.
    const auto &state = task->state, &shown = task->shown;
    if (!urgent_.isEmpty() && (state.title != shown.title || state.appId != shown.appId))
        matchUrgent(task);
    for (int i = 0; i < rowCount(); ++i)
        if (tasks_[i].get() == task) {
            QList<int> roles;
            auto compare = [&](auto field, Role role) {
                if (state.*field != shown.*field)
                    roles.push_back(role);
            };
            compare(&State::title, Title);
            compare(&State::appId, AppId);
            compare(&State::active, Active);
            compare(&State::minimized, Minimized);
            compare(&State::maximized, Maximized);
            compare(&State::fullscreen, Fullscreen);
            compare(&State::urgent, Urgent);
            compare(&State::output, Output);
            compare(&State::workspace, Workspace);
            compare(&State::sticky, Sticky);
            compare(&State::floating, Floating);
            compare(&State::tiling, Tiling);
            compare(&State::pid, Pid);
            compare(&State::windowId, WindowId);
            compare(&State::above, Above);
            if (roles.isEmpty())
                return;
            task->shown = task->state;
            Q_EMIT dataChanged(index(i), index(i), roles);
            return;
        }
}
void TaskModel::removed(Task *task) {
    for (int i = 0; i < rowCount(); ++i)
        if (tasks_[i].get() == task) {
            beginRemoveRows({}, i, i);
            pictures_.forget(task->id);
            const bool peeked = peeked_ == task->id;
            if (task->window)
                paw_window_v1_destroy(task->window);
            zwlr_foreign_toplevel_handle_v1_destroy(task->handle);
            tasks_.erase(tasks_.begin() + i);
            endRemoveRows();
            if (peeked)
                setPeeked(-1); // the compositor ended the peek as the window closed
            return;
        }
}
void TaskModel::global(void *data, wl_registry *registry, uint32_t name, const char *interface,
                       uint32_t version) {
    auto &self = *static_cast<TaskModel *>(data);
    if (!std::strcmp(interface, "zwlr_foreign_toplevel_manager_v1")) {
        self.manager_ = static_cast<zwlr_foreign_toplevel_manager_v1 *>(wl_registry_bind(
            registry, name, &zwlr_foreign_toplevel_manager_v1_interface, std::min(version, 2u)));
        static const zwlr_foreign_toplevel_manager_v1_listener listener{newTask, finished};
        zwlr_foreign_toplevel_manager_v1_add_listener(self.manager_, &listener, &self);
    } else if (!std::strcmp(interface, "wl_seat") && !self.seat_) {
        self.seat_ =
            static_cast<wl_seat *>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
    } else if (!std::strcmp(interface, paw_window_control_v1_interface.name) && !self.control_) {
        // Version 2 gives the windows' capture sources, for their pictures, version 3 ones
        // scaled down to the pictures' size and the windows' processes, for the sound they play,
        // version 4 peeks at a window and gives the windows' numbers, for the window
        // switcher's pictures, and version 6 keeps a window above the others.
        self.control_ = static_cast<paw_window_control_v1 *>(wl_registry_bind(
            registry, name, &paw_window_control_v1_interface, std::min(version, 6u)));
        for (auto &task : self.tasks_)
            self.watch(task.get());
    } else if (!std::strcmp(interface, "wl_shm") && !self.shm_) {
        self.shm_ = static_cast<wl_shm *>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
        self.pictures_.setGlobals(self.shm_, self.captureManager_);
    } else if (!std::strcmp(interface, ext_image_copy_capture_manager_v1_interface.name) &&
               !self.captureManager_) {
        self.captureManager_ = static_cast<ext_image_copy_capture_manager_v1 *>(
            wl_registry_bind(registry, name, &ext_image_copy_capture_manager_v1_interface, 1));
        self.pictures_.setGlobals(self.shm_, self.captureManager_);
    }
}
void TaskModel::watch(Task *task) {
    if (!control_ || task->window)
        return;
    task->window = paw_window_control_v1_get_window(control_, task->handle);
    static const paw_window_v1_listener listener{windowOutput, windowWorkspace, windowState,
                                                      windowDone,   windowPid,       windowId,
                                                      windowIcon,   windowIconImage};
    paw_window_v1_add_listener(task->window, &listener, task);
}
void TaskModel::globalRemoved(void *, wl_registry *, uint32_t) {}
void TaskModel::newTask(void *data, zwlr_foreign_toplevel_manager_v1 *,
                        zwlr_foreign_toplevel_handle_v1 *handle) {
    auto &self = *static_cast<TaskModel *>(data);
    auto task = std::make_unique<Task>();
    task->model = &self;
    task->handle = handle;
    task->id = self.nextId_++;
    static const zwlr_foreign_toplevel_handle_v1_listener listener{title, appId, output, output,
                                                                   state, done,  closed, nullptr};
    zwlr_foreign_toplevel_handle_v1_add_listener(handle, &listener, task.get());
    self.watch(task.get());
    int row = self.rowCount();
    self.beginInsertRows({}, row, row);
    self.tasks_.push_back(std::move(task));
    self.endInsertRows();
}
void TaskModel::finished(void *data, zwlr_foreign_toplevel_manager_v1 *) {
    Q_EMIT static_cast<TaskModel *>(data)->disconnected();
}
void TaskModel::title(void *data, zwlr_foreign_toplevel_handle_v1 *, const char *value) {
    static_cast<Task *>(data)->state.title = QString::fromUtf8(value);
}
void TaskModel::appId(void *data, zwlr_foreign_toplevel_handle_v1 *, const char *value) {
    static_cast<Task *>(data)->state.appId = QString::fromUtf8(value);
}
void TaskModel::output(void *, zwlr_foreign_toplevel_handle_v1 *, wl_output *) {}
void TaskModel::state(void *data, zwlr_foreign_toplevel_handle_v1 *, wl_array *states) {
    auto &state = static_cast<Task *>(data)->state;
    state.active = state.minimized = state.maximized = state.fullscreen = false;
    const auto *values = static_cast<const uint32_t *>(states->data);
    for (size_t i = 0; i < states->size / sizeof(uint32_t); ++i) {
        state.active |= values[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED;
        state.minimized |= values[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED;
        state.maximized |= values[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED;
        state.fullscreen |= values[i] == ZWLR_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN;
    }
}
void TaskModel::done(void *data, zwlr_foreign_toplevel_handle_v1 *) {
    auto *task = static_cast<Task *>(data);
    task->model->changed(task);
}
void TaskModel::closed(void *data, zwlr_foreign_toplevel_handle_v1 *) {
    auto *task = static_cast<Task *>(data);
    task->model->removed(task);
}
void TaskModel::windowOutput(void *data, paw_window_v1 *, const char *name) {
    static_cast<Task *>(data)->state.output = QString::fromUtf8(name);
}
void TaskModel::windowWorkspace(void *data, paw_window_v1 *, uint32_t number) {
    static_cast<Task *>(data)->state.workspace = static_cast<int>(number);
}
void TaskModel::windowState(void *data, paw_window_v1 *, uint32_t flags) {
    auto &state = static_cast<Task *>(data)->state;
    state.sticky = flags & PAW_WINDOW_V1_STATE_STICKY;
    state.floating = flags & PAW_WINDOW_V1_STATE_FLOATING;
    state.tiling = flags & PAW_WINDOW_V1_STATE_TILING;
    state.above = flags & PAW_WINDOW_V1_STATE_ABOVE;
}
void TaskModel::windowDone(void *data, paw_window_v1 *) {
    auto *task = static_cast<Task *>(data);
    task->model->changed(task);
}
void TaskModel::windowPid(void *data, paw_window_v1 *, uint32_t pid) {
    static_cast<Task *>(data)->state.pid = pid <= INT_MAX ? static_cast<int>(pid) : 0;
}
void TaskModel::windowId(void *data, paw_window_v1 *, uint32_t id) {
    static_cast<Task *>(data)->state.windowId = id <= INT_MAX ? static_cast<int>(id) : 0;
}
// Version 5's icons, which come with version 6, are not shown: the file goes at once.
void TaskModel::windowIcon(void *, paw_window_v1 *, const char *) {}
void TaskModel::windowIconImage(void *, paw_window_v1 *, int32_t fd, uint32_t, uint32_t) {
    ::close(fd);
}
