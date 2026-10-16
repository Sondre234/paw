// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QAbstractListModel>
#include <QPoint>
#include <QPointer>
#include <functional>
#include <optional>

// The order of the taskbar's buttons, shared by every panel showing the same windows (a child of
// their source model, `of`): each pinned application and each window, pinned or not, in one row.
// A pinned application keeps its place there, where its launcher shows while it has no window;
// its first window opens in that place, any other window at the end. Dragging moves one entry
// (grouped, an application's button with all its windows), and only the drag of a pinned
// application's own button moves its pin.
//
// With `byMonitor`, a window's entry follows its monitor among its application's windows as it
// opens or moves to another monitor, monitors ordered by their place in the layout (left to
// right, and top to bottom at the same x). One of them apart from it on the wrong side (ahead of
// it on a monitor after its own, or after it on one before) takes it into the run of windows next
// to that one; else it keeps to the run next to it. There it goes after those on monitors before
// its own or on its own, and ahead of the others, or stays where it is if it just opened and
// already fits. Nothing else moves an entry but a drag.
class TaskbarOrder : public QObject {
    Q_OBJECT
  public:
    // A pinned application's place, by its id, or a window, by its task id.
    struct Entry {
        QString pin;
        int taskId = -1;
        bool operator==(const Entry &) const = default;
    };
    // What the order asks of the shell: the pinned applications' ids in their order, the pinned
    // application a window's app id belongs to (empty for none), and to keep the pins in an order.
    struct Pins {
        std::function<QStringList()> pinned;
        std::function<QString(const QString &)> pinnedAppFor;
        std::function<void(const QStringList &)> reorder;
    };
    // Where a monitor is in the layout, its top left corner, by its name; nullopt for one not
    // known.
    using OutputPlace = std::function<std::optional<QPoint>(const QString &output)>;
    // The order of `source`, made with it the first time it is asked for.
    static TaskbarOrder *of(QAbstractItemModel *source);
    // The place of the screen of that name (its geometry, which xdg-output gives).
    static std::optional<QPoint> screenPlace(const QString &output);
    // Where the monitors are: screenPlace until set otherwise.
    void setOutputPlace(OutputPlace place) { place_ = std::move(place); }
    // Whether windows' buttons follow their monitors: while the windows have one apart.
    void setByMonitor(bool byMonitor) { byMonitor_ = byMonitor; }
    bool byMonitor() const { return byMonitor_; }
    // The first panel to set them wins; until then nothing is pinned.
    void setPins(Pins pins);
    bool hasPins() const { return bool(pins_.pinned); }
    // The pins changed, or which application a window belongs to may have.
    void pinsChanged();
    QAbstractItemModel *source() const { return source_; }
    const QList<Entry> &entries() const { return entries_; }
    // The entries the taskbar shows. Apart, a pinned application shows its launcher while it has
    // no window, and every window shows. Grouped, a pinned application shows always, standing for
    // its windows too, and of the other windows the first of each app id (those without one stand
    // alone).
    QList<Entry> shown(bool grouped) const;
    // The pinned application an entry belongs to, empty for a window of none.
    QString pinOf(const Entry &entry) const;
    // The window that stands for a shown entry: the first of a pinned application's, -1 for none.
    int windowOf(const Entry &entry) const;
    // The source row of window `taskId`, -1 when it has gone, and its app id.
    int sourceRow(int taskId) const { return rows_.value(taskId, -1); }
    QString appId(int taskId) const;
    // Moves shown entry `from` to where shown entry `to` is.
    void move(int from, int to, bool grouped);
  Q_SIGNALS:
    // What is shown may have changed.
    void changed();

  private:
    explicit TaskbarOrder(QAbstractItemModel *source);
    QPointer<QAbstractItemModel> source_;
    Pins pins_;
    QList<Entry> entries_;
    // Task id to source row.
    QHash<int, int> rows_;
    // The monitor each window placed was last seen on, by its task id.
    QHash<int, QString> outputs_;
    OutputPlace place_ = &TaskbarOrder::screenPlace;
    bool byMonitor_ = false;
    bool placing_ = false;
    mutable QHash<QByteArray, int> roleIds_;
    int roleId(const char *role) const;
    QString output(int taskId) const;
    void indexRows();
    // The window's monitor having changed since it was last seen, notes it and places the window
    // by it; returns whether the window moved.
    bool noteOutput(int taskId);
    // Places window `taskId` among its application's by their monitors, as it opened or `moved`
    // from another monitor; returns whether its place changed.
    bool followMonitor(int taskId, bool moved);
    // Takes out the windows that have gone, then, once their app ids are in, adds the new ones.
    void forgetGone();
    void placeLater();
    void placeNew();
    void syncPins();
};

// The taskbar's buttons as rows, in TaskbarOrder's order. A pinned application's row has its
// record (as shell.pinned has it) as `app`: without a window, its launcher, it has taskId -1 and
// the application's name as its title; grouped, with windows, the roles of the first of them. A
// window's row has the source's roles and {} as `app`. `controller` is the shell.
class TaskbarModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QObject *controller READ controller WRITE setController NOTIFY controllerChanged)
    Q_PROPERTY(QAbstractItemModel *sourceModel READ sourceModel WRITE setSourceModel NOTIFY sourceModelChanged)
    Q_PROPERTY(bool grouped READ grouped WRITE setGrouped NOTIFY groupedChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    // How many of the rows are launchers.
    Q_PROPERTY(int launchers READ launchers NOTIFY launchersChanged)
  public:
    enum Role {
        TaskId = Qt::UserRole + 1,
        Title,
        AppId,
        Active,
        Minimized,
        Urgent,
        App
    };
    explicit TaskbarModel(QObject *parent = nullptr);
    QObject *controller() const;
    void setController(QObject *controller);
    QAbstractItemModel *sourceModel() const { return source_; }
    void setSourceModel(QAbstractItemModel *source);
    bool grouped() const { return grouped_; }
    void setGrouped(bool grouped);
    int rowCount(const QModelIndex &parent = {}) const override;
    int launchers() const { return launchers_; }
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    // Moves row `from` to where row `to` is, on every taskbar.
    Q_INVOKABLE void move(int from, int to);
    // The row standing for window `taskId`, -1 for none.
    Q_INVOKABLE int rowOf(int taskId) const;
  Q_SIGNALS:
    void controllerChanged();
    void sourceModelChanged();
    void groupedChanged();
    void countChanged();
    void launchersChanged();

  private:
    struct Row {
        TaskbarOrder::Entry entry;
        // The pinned application it belongs to, kept for when its window has gone, and its record.
        QString pin;
        QVariantMap app;
        // The window whose roles it has, -1 for none.
        int window = -1;
    };
    QPointer<QObject> shell_;
    QPointer<QAbstractItemModel> source_;
    QPointer<TaskbarOrder> order_;
    bool grouped_ = false;
    int launchers_ = 0;
    QList<Row> rows_;
    QList<QMetaObject::Connection> connections_;
    // Role to the source's role of that name.
    mutable QHash<int, int> sourceRoles_;
    int sourceRole(int role) const;
    void attach();
    void update();
    void forward(const QModelIndex &first, const QModelIndex &last, const QList<int> &roles);
};
