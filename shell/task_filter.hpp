// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QPointer>
#include <QSortFilterProxyModel>

class ShellController;
// The windows of one button on the taskbar or the dock: those of the pinned application `app`, or,
// when `app` is empty, every window no pinned application takes in. `controller` is the shell.
// `windowApp`, when set, keeps only the windows with that app id. `grouped` keeps one window per
// application, the first of them: in a pinned slot the whole slot is one application; elsewhere
// windows with the same app id are, and those without one stand alone. `taskId`, when 0 or more,
// keeps only the window with that id, wherever it belongs, and `windowId`, when above 0, only the
// window the compositor gives that number (the source's windowId role), as the switcher lists it.
class TaskFilter : public QSortFilterProxyModel {
    Q_OBJECT
    Q_PROPERTY(QObject *controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(QString app READ app WRITE setApp NOTIFY appChanged)
    Q_PROPERTY(QString windowApp READ windowApp WRITE setWindowApp NOTIFY windowAppChanged)
    Q_PROPERTY(bool grouped READ grouped WRITE setGrouped NOTIFY groupedChanged)
    Q_PROPERTY(int taskId READ taskId WRITE setTaskId NOTIFY taskIdChanged)
    Q_PROPERTY(int windowId READ windowId WRITE setWindowId NOTIFY windowIdChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // The focused window among these, or -1, and whether every one of them is minimized.
    Q_PROPERTY(int activeTask READ activeTask NOTIFY summaryChanged)
    Q_PROPERTY(bool minimized READ minimized NOTIFY summaryChanged)
    // Whether any of them is asking for attention.
    Q_PROPERTY(bool urgent READ urgent NOTIFY summaryChanged)
    // Every window, as a map of the source model's roles ({taskId, title, ...}), for QML that
    // reads them as data: a binding reading it follows every change to these windows.
    Q_PROPERTY(QVariantList windows READ windows NOTIFY summaryChanged)
  public:
    explicit TaskFilter(QObject *parent = nullptr);
    QObject *controller() const;
    void setController(QObject *controller);
    QString app() const { return app_; }
    void setApp(const QString &app);
    QString windowApp() const { return windowApp_; }
    void setWindowApp(const QString &windowApp);
    bool grouped() const { return grouped_; }
    void setGrouped(bool grouped);
    int taskId() const { return taskId_; }
    void setTaskId(int taskId);
    int windowId() const { return windowId_; }
    void setWindowId(int windowId);
    int count() const { return rowCount(); }
    void setSourceModel(QAbstractItemModel *source) override;
    int activeTask() const;
    bool minimized() const;
    bool urgent() const;
    QVariantList windows() const;
    // The window after the focused one, wrapping around, or the first when none is focused.
    Q_INVOKABLE int nextTask() const;
  Q_SIGNALS:
    void controllerChanged();
    void appChanged();
    void windowAppChanged();
    void groupedChanged();
    void taskIdChanged();
    void windowIdChanged();
    void countChanged();
    void summaryChanged();

  protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;

  private:
    QPointer<ShellController> shell_;
    QString app_, windowApp_;
    bool grouped_ = false;
    int taskId_ = -1, windowId_ = 0;
    QList<QMetaObject::Connection> sourceConnections_;
    // Which source rows the filter accepts, worked out for all of them at once the first time
    // the proxy asks after a change (a filter that looks at each row's predecessors is
    // otherwise quadratic, and the panel has a filter for every stacked button).
    mutable QList<char> accepted_;
    mutable bool acceptedValid_ = false;
    mutable QHash<QByteArray, int> roleIds_;
    int roleId(const char *role) const;
    // Whether a change of these roles may change which rows belong.
    bool decides(const QList<int> &roles) const;
    void computeAccepted() const;
    void forget() { acceptedValid_ = false; }
    void refilter();
    QVariant sourceValue(int row, const char *role) const;
    bool belongs(int row) const;
    // The application a source row groups under; empty when it stands alone.
    QString groupOf(int row) const;
    int value(int row, const char *role) const;
};
