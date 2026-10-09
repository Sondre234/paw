// SPDX-License-Identifier: GPL-3.0-or-later
#include "task_filter.hpp"
#include "taskbar_model.hpp"
#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTest>
#include <algorithm>
#include <functional>
#include <iostream>
#include <memory>
#include <vector>

namespace {
// The roles and change signals of the shell's TaskModel, counting how often the filters read it.
class FakeTasks : public QAbstractListModel {
  public:
    enum Role { TaskId = Qt::UserRole + 1, Title, AppId, Active, Minimized, Maximized, Urgent };
    struct Row {
        int id;
        QString title, appId;
        bool active = false, minimized = false, urgent = false;
    };
    std::vector<Row> rows;
    mutable int reads = 0;
    int rowCount(const QModelIndex &parent = {}) const override {
        return parent.isValid() ? 0 : int(rows.size());
    }
    QVariant data(const QModelIndex &index, int role) const override {
        ++reads;
        const auto &row = rows[size_t(index.row())];
        switch (role) {
        case TaskId: return row.id;
        case Title: return row.title;
        case AppId: return row.appId;
        case Active: return row.active;
        case Minimized: return row.minimized;
        case Maximized: return false;
        case Urgent: return row.urgent;
        }
        return {};
    }
    QHash<int, QByteArray> roleNames() const override {
        return {{TaskId, "taskId"}, {Title, "title"},         {AppId, "appId"},
                {Active, "active"}, {Minimized, "minimized"}, {Maximized, "maximized"},
                {Urgent, "urgent"}};
    }
    void add(const QString &app, const QString &title) {
        beginInsertRows({}, int(rows.size()), int(rows.size()));
        rows.push_back({nextId++, title, app});
        endInsertRows();
    }
    void remove(int row) {
        beginRemoveRows({}, row, row);
        rows.erase(rows.begin() + row);
        endRemoveRows();
    }
    void changed(int row, const QList<int> &roles) {
        Q_EMIT dataChanged(index(row), index(row), roles);
    }
    void reset(size_t keep, const std::function<void(FakeTasks &)> &edit) {
        beginResetModel();
        rows.resize(keep);
        edit(*this);
        endResetModel();
    }
    void moveLastToFront() {
        beginMoveRows({}, int(rows.size()) - 1, int(rows.size()) - 1, {}, 0);
        std::rotate(rows.begin(), rows.end() - 1, rows.end());
        endMoveRows();
    }
    int nextId = 1;
};
} // namespace

class TaskFilterTest : public QObject {
    Q_OBJECT
    FakeTasks source;
    std::vector<std::unique_ptr<TaskFilter>> filters;

    QList<int> taskIds(TaskFilter &filter) {
        QList<int> ids;
        for (int row = 0; row < filter.rowCount(); ++row)
            ids.push_back(filter.data(filter.index(row, 0), FakeTasks::TaskId).toInt());
        return ids;
    }

  private Q_SLOTS:
    void init() {
        source.rows.clear();
        source.nextId = 1;
        filters.clear();
        // Twenty applications with three windows each, as a busy desktop has.
        for (int window = 0; window < 3; ++window)
            for (int app = 0; app < 20; ++app)
                source.add(QString("app%1").arg(app), QString("window %1").arg(window));
        // The taskbar's own filter and one for each of a few stacked buttons.
        for (int i = 0; i < 8; ++i) {
            auto filter = std::make_unique<TaskFilter>();
            filter->setGrouped(true);
            filter->setSourceModel(&source);
            filters.push_back(std::move(filter));
        }
    }
    void groupsOneWindowPerApplication() {
        QCOMPARE(filters[0]->rowCount(), 20);
        QCOMPARE(taskIds(*filters[0]).first(), 1);
        // Removing the first window of an application promotes the next one, which stands
        // where that window is, after the other applications' first windows.
        source.remove(0);
        QCOMPARE(filters[0]->rowCount(), 20);
        QCOMPARE(taskIds(*filters[0]).first(), 2);
        QCOMPARE(taskIds(*filters[0]).last(), 21);
        // A window changing application moves between groups.
        source.rows[5].appId = "app0";
        source.changed(5, {FakeTasks::AppId});
        QCOMPARE(filters[0]->rowCount(), 20);
        source.rows[25].appId = "brand-new";
        source.changed(25, {FakeTasks::AppId});
        QCOMPARE(filters[0]->rowCount(), 21);
    }
    // A window's title changing is the commonest event of all (a terminal, a browser tab, a
    // media player), and must not make every filter look at every window again.
    void titleChangesDoNotRefilter() {
        const int before = source.reads;
        QSignalSpy counts(filters[0].get(), &TaskFilter::countChanged);
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 100; ++i) {
            source.rows[size_t(i % 60)].title = QString("title %1").arg(i);
            source.changed(i % 60, {FakeTasks::Title});
        }
        std::cerr << "100 title changes: " << timer.nsecsElapsed() / 1000 << " us, "
                  << (source.reads - before) << " reads\n";
        QVERIFY2(source.reads - before < 100 * 8 * 2, "title changes made the filters re-read everything");
        QCOMPARE(counts.count(), 0);
    }
    void followsResetsAndMoves() {
        source.reset(4, [](FakeTasks &tasks) { tasks.rows[1].appId = tasks.rows[0].appId; });
        QCOMPARE(filters[0]->rowCount(), 3);
        QCOMPARE(filters[3]->rowCount(), 3);
        source.moveLastToFront();
        QCOMPARE(filters[0]->rowCount(), 3);
        QCOMPARE(taskIds(*filters[0]).first(), source.rows[0].id);
    }
    void stateChangesUpdateTheSummary() {
        QSignalSpy summaries(filters[0].get(), &TaskFilter::summaryChanged);
        source.rows[0].active = true;
        source.changed(0, {FakeTasks::Active});
        QVERIFY(summaries.count() >= 1);
        QCOMPARE(filters[0]->activeTask(), 1);
        source.rows[0].active = false;
        source.changed(0, {FakeTasks::Active});
        QCOMPARE(filters[0]->activeTask(), -1);
    }
    // One window by its id, wherever it belongs, and the windows as data that follows changes.
    void oneWindowAndItsData() {
        TaskFilter one;
        one.setSourceModel(&source);
        one.setApp("pinned-app");
        one.setTaskId(source.rows[2].id);
        QCOMPARE(taskIds(one), QList<int>{source.rows[2].id});
        auto windows = one.windows();
        QCOMPARE(windows.size(), 1);
        QCOMPARE(windows[0].toMap()["title"], source.rows[2].title);
        QCOMPARE(windows[0].toMap()["appId"], source.rows[2].appId);
        QSignalSpy summaries(&one, &TaskFilter::summaryChanged);
        source.rows[2].minimized = true;
        source.changed(2, {FakeTasks::Minimized});
        QVERIFY(summaries.count() >= 1);
        QCOMPARE(one.windows()[0].toMap()["minimized"], true);
        source.rows[2].minimized = false;
        source.changed(2, {FakeTasks::Minimized});
        one.setTaskId(-1); // back to the pinned application's windows, of which there are none
        QVERIFY(one.windows().isEmpty());
    }
    // A stacked button is urgent when any window of its application is.
    void urgencyOfAStackedButton() {
        TaskFilter app3, app4;
        app3.setSourceModel(&source);
        app3.setWindowApp("app3");
        app4.setSourceModel(&source);
        app4.setWindowApp("app4");
        QCOMPARE(app3.rowCount(), 3);
        QVERIFY(!app3.urgent() && !app4.urgent() && !filters[0]->urgent());
        QSignalSpy summaries(&app3, &TaskFilter::summaryChanged);
        source.rows[43].urgent = true; // the third window of app3
        source.changed(43, {FakeTasks::Urgent});
        QVERIFY(summaries.count() >= 1);
        QVERIFY(app3.urgent());
        QVERIFY(!app4.urgent());
        source.rows[43].urgent = false;
        source.changed(43, {FakeTasks::Urgent});
        QVERIFY(!app3.urgent());
    }
};
// The taskbar's one row: pinned applications and windows, as TaskbarOrder keeps them.
class TaskbarOrderTest : public QObject {
    Q_OBJECT
    std::unique_ptr<FakeTasks> source;
    TaskbarOrder *order = nullptr;
    // A window's app id is the id of the application it belongs to, pinned when it is here.
    QStringList pins;
    QList<QStringList> reordered;
    using Entry = TaskbarOrder::Entry;

    // The shown entries, as "a" for a pinned application's and "3" for window 3's.
    QStringList shown(bool grouped = false) {
        QStringList list;
        for (const auto &entry : order->shown(grouped))
            list.push_back(entry.pin.isEmpty() ? QString::number(entry.taskId) : entry.pin);
        return list;
    }
    // Lets the windows just added take their places.
    void settle() { QCoreApplication::processEvents(); }

  private Q_SLOTS:
    void init() {
        source = std::make_unique<FakeTasks>();
        pins = {"a", "b"};
        reordered.clear();
        order = TaskbarOrder::of(source.get());
        order->setPins({[this] { return pins; },
                        [this](const QString &appId) { return pins.contains(appId) ? appId : QString(); },
                        [this](const QStringList &order) { reordered.push_back(order); }});
    }
    // A pinned application's first window opens in its launcher's place, any other at the end,
    // and grouped, a pinned application's button stands for its windows wherever they are.
    void windowsOpenWhereTheyBelong() {
        QCOMPARE(shown(), (QStringList{"a", "b"}));
        source->add("a", "a one");
        source->add("a", "a two");
        source->add("c", "c one");
        QCOMPARE(shown(), (QStringList{"a", "b"}));
        settle();
        QCOMPARE(shown(), (QStringList{"1", "b", "2", "3"}));
        QCOMPARE(shown(true), (QStringList{"a", "b", "3"}));
        QCOMPARE(order->windowOf({"a"}), 1);
        QCOMPARE(order->windowOf({"b"}), -1);
    }
    // The windows open as the taskbar starts go where the pins say.
    void windowsOpenAlreadyFollowThePins() {
        auto tasks = std::make_unique<FakeTasks>();
        tasks->add("c", "c one");
        tasks->add("b", "b one");
        tasks->add("a", "a one");
        auto *started = TaskbarOrder::of(tasks.get());
        started->setPins({[this] { return pins; },
                          [this](const QString &appId) { return pins.contains(appId) ? appId : QString(); },
                          [](const QStringList &) {}});
        QStringList list;
        for (const auto &entry : started->shown(false))
            list.push_back(entry.pin.isEmpty() ? QString::number(entry.taskId) : entry.pin);
        QCOMPARE(list, (QStringList{"3", "2", "1"}));
    }
    // A window gets its app id after it opens, before the event loop turns.
    void aWindowIsPlacedOnceItsAppIdIsIn() {
        source->add("c", "c one");
        source->add("", "a one");
        source->rows[1].appId = "a";
        source->changed(1, {FakeTasks::AppId});
        settle();
        QCOMPARE(shown(), (QStringList{"2", "b", "1"}));
    }
    // Each window moves on its own, its pinned application's place staying with the button after
    // it; closing the last one shows the launcher there again.
    void windowsMoveApart() {
        source->add("a", "a one");
        source->add("a", "a two");
        source->add("c", "c one");
        settle();
        order->move(2, 0, false); // the second window ahead of the first
        QCOMPARE(shown(), (QStringList{"2", "1", "b", "3"}));
        order->move(1, 3, false); // the first past the others
        QCOMPARE(shown(), (QStringList{"2", "b", "3", "1"}));
        QVERIFY(reordered.isEmpty());
        source->remove(0);
        QCOMPARE(shown(), (QStringList{"2", "b", "3"}));
        source->remove(0);
        QCOMPARE(shown(), (QStringList{"a", "b", "3"}));
    }
    // Only a launcher's drag moves its pin, and the pins keep the order it leaves them in.
    void launchersMovePins() {
        source->add("c", "c one");
        settle();
        order->move(0, 2, false);
        QCOMPARE(shown(), (QStringList{"b", "1", "a"}));
        QCOMPARE(reordered, (QList<QStringList>{{"b", "a"}}));
    }
    // Grouped, the windows of an application not pinned move together.
    void groupsMoveTogether() {
        source->add("c", "c one");
        source->add("d", "d one");
        source->add("c", "c two");
        settle();
        QCOMPARE(shown(true), (QStringList{"a", "b", "1", "2"}));
        order->move(2, 3, true);
        QCOMPARE(shown(true), (QStringList{"a", "b", "2", "1"}));
        QCOMPARE(shown(), (QStringList{"a", "b", "2", "1", "3"}));
    }
    // An application pinned with a window open takes its place where the window is; unpinned, its
    // windows stay.
    void pinningAndUnpinning() {
        source->add("c", "c one");
        source->add("d", "d one");
        settle();
        pins.push_back("d");
        order->pinsChanged();
        QCOMPARE(shown(true), (QStringList{"a", "b", "1", "d"}));
        order->move(3, 0, true);
        QCOMPARE(shown(true), (QStringList{"d", "a", "b", "1"}));
        pins = {"a", "b"};
        order->pinsChanged();
        QCOMPARE(shown(), (QStringList{"2", "a", "b", "1"}));
    }
};

int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    TaskFilterTest filters;
    TaskbarOrderTest order;
    return QTest::qExec(&filters, argc, argv) | QTest::qExec(&order, argc, argv);
}
#include "task_filter_test.moc"
