// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ext-image-capture-source-v1-client-protocol.h" // before the next, which names its interface
#include "paw-window-control-v1-client-protocol.h"
#include "window_pictures.hpp"
#include "wlr-foreign-toplevel-management-unstable-v1-client-protocol.h"
#include <QAbstractListModel>
#include <QSocketNotifier>
#include <memory>
#include <vector>
#include <wayland-client.h>

// The windows the compositor lists for taskbars (wlr-foreign-toplevel), with where each is from
// paw-window-control-v1 when the compositor offers it: `output` (connector name), `workspace`
// (of that output, from 1; 0 until it is known), whether it is `sticky` or `floating`, and whether
// its workspace is `tiling`. From its version 2, a window watched with watchPicture also has a
// `picture` (WindowPictures), "" until one has arrived; from version 3 each has the `pid` of the
// process that made it, 0 until it is known; and from version 4 each has its `windowId`, the
// number the compositor's control socket names it by (as the window switcher does), 0 until it
// is known, and the model can peek at one; from version 6 whether it is kept `above` the others,
// undefined below it.
class TaskModel : public QAbstractListModel {
    Q_OBJECT
    // The window the model peeks at, -1 for none.
    Q_PROPERTY(int peekedTask READ peekedTask NOTIFY peekedTaskChanged)
  public:
    enum Role {
        TaskId = Qt::UserRole + 1,
        Title,
        AppId,
        Active,
        Minimized,
        Maximized,
        Urgent,
        Fullscreen,
        Output,
        Workspace,
        Sticky,
        Floating,
        Tiling,
        Picture,
        Pid,
        WindowId,
        Above
    };
    explicit TaskModel(QObject *parent = nullptr);
    ~TaskModel() override;
    bool connectDisplay();
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE void activate(int id);
    Q_INVOKABLE void minimize(int id);
    Q_INVOKABLE void maximize(int id);
    Q_INVOKABLE void setFullscreen(int id, bool fullscreen);
    // Through the window control, which these need: to workspace `number` (from 1) of the output
    // the window is on, onto another output, shown on every workspace, kept out of the tiling.
    // None of them focuses the window.
    Q_INVOKABLE void moveToWorkspace(int id, int number);
    Q_INVOKABLE void moveToOutput(int id, const QString &output);
    Q_INVOKABLE void setSticky(int id, bool sticky);
    Q_INVOKABLE void setFloating(int id, bool floating);
    // Keeps the window above the others or lets it go, through the window control's version 6.
    Q_INVOKABLE void setAbove(int id, bool above);
    Q_INVOKABLE void close(int id);
    // Ends the process that made window `id` with SIGKILL, when its pid is known and is neither
    // the shell's nor the compositor's, which started it.
    Q_INVOKABLE void kill(int id);
    Q_INVOKABLE void showDesktop();
    // Counted per window: while watched, the window is pictured, once or as it redraws when
    // `live`, at `pixelWidth` device pixels wide. Its last picture stays until it closes.
    Q_INVOKABLE void watchPicture(int taskId, int pixelWidth, bool live);
    Q_INVOKABLE void unwatchPicture(int taskId);
    // The window's last picture, for the image provider on any thread; null while it has none.
    QImage picture(int taskId) const;
    // Through the window control's version 4: the other windows fade while this one shows alone
    // where it is, minimized or on a workspace not shown too, until endPeek or until another is
    // peeked at; nothing about it changes. endPeek ends the peek only at the window peeked at.
    // The compositor also ends it by itself, as the window is focused or the session locks.
    Q_INVOKABLE void peek(int taskId);
    Q_INVOKABLE void endPeek(int taskId);
    int peekedTask() const { return peeked_; }
    // The windows the compositor says are asking for attention, as {appId, title} pairs: the
    // foreign-toplevel protocol has no such state, so a task is urgent when a pair matches its
    // app id and title (each pair marks one task, the first not marked already).
    void setUrgent(const QList<QPair<QString, QString>> &windows);
  Q_SIGNALS:
    void disconnected();
    void peekedTaskChanged();

  private:
    // What a window is, as the compositor last said.
    struct State {
        QString title, appId, output;
        bool active = false, minimized = false, maximized = false, fullscreen = false, urgent = false;
        int workspace = 0;
        bool sticky = false, floating = false, tiling = false, above = false;
        int pid = 0;
        int windowId = 0;
    };
    struct Task {
        TaskModel *model;
        zwlr_foreign_toplevel_handle_v1 *handle;
        paw_window_v1 *window = nullptr;
        int id;
        State state;
        // What the model last announced; a `done` that changes none of it announces nothing.
        State shown;
    };
    std::vector<std::unique_ptr<Task>> tasks_;
    wl_display *display_ = nullptr;
    wl_registry *registry_ = nullptr;
    wl_seat *seat_ = nullptr;
    zwlr_foreign_toplevel_manager_v1 *manager_ = nullptr;
    paw_window_control_v1 *control_ = nullptr;
    wl_shm *shm_ = nullptr;
    ext_image_copy_capture_manager_v1 *captureManager_ = nullptr;
    std::unique_ptr<QSocketNotifier> read_, write_;
    QList<QPair<QString, QString>> urgent_;
    int nextId_ = 1;
    int peeked_ = -1;
    void setPeeked(int taskId);
    WindowPictures pictures_{[this] { flush(); }};
    Task *find(int id);
    // Sets or unsets one of the window control's states of window `id`: from version `since` of
    // the window's object on.
    void setState(int id, bool on, void (*set)(paw_window_v1 *), void (*unset)(paw_window_v1 *),
                  uint32_t since = 1);
    void flush();
    // Works out which tasks are urgent; those but `except` that change announce it.
    void matchUrgent(const Task *except = nullptr);
    void changed(Task *task);
    // Asks the compositor where the task's window is, and to keep saying.
    void watch(Task *task);
    void removed(Task *task);
    static void global(void *, wl_registry *, uint32_t, const char *, uint32_t);
    static void globalRemoved(void *, wl_registry *, uint32_t);
    static void newTask(void *, zwlr_foreign_toplevel_manager_v1 *,
                        zwlr_foreign_toplevel_handle_v1 *);
    static void finished(void *, zwlr_foreign_toplevel_manager_v1 *);
    static void title(void *, zwlr_foreign_toplevel_handle_v1 *, const char *);
    static void appId(void *, zwlr_foreign_toplevel_handle_v1 *, const char *);
    static void output(void *, zwlr_foreign_toplevel_handle_v1 *, wl_output *);
    static void state(void *, zwlr_foreign_toplevel_handle_v1 *, wl_array *);
    static void done(void *, zwlr_foreign_toplevel_handle_v1 *);
    static void closed(void *, zwlr_foreign_toplevel_handle_v1 *);
    static void windowOutput(void *, paw_window_v1 *, const char *);
    static void windowWorkspace(void *, paw_window_v1 *, uint32_t);
    static void windowState(void *, paw_window_v1 *, uint32_t);
    static void windowDone(void *, paw_window_v1 *);
    static void windowPid(void *, paw_window_v1 *, uint32_t);
    static void windowId(void *, paw_window_v1 *, uint32_t);
    static void windowIcon(void *, paw_window_v1 *, const char *);
    static void windowIconImage(void *, paw_window_v1 *, int32_t, uint32_t, uint32_t);
};

// image://windows/ID/SERIAL: the last picture of the window TaskModel numbers ID, as its `picture`
// role names it (the serial only keeps QML from reusing a picture that has changed); an empty one,
// a pixel wide, when the window closed as its picture was asked for.
QImage windowImage(const TaskModel &tasks, const QString &id);
