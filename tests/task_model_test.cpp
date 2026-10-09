// SPDX-License-Identifier: GPL-3.0-or-later
#include "task_model.hpp"
#include "window_images.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QThread>
#include <functional>
#include <iostream>
#include <stdexcept>

// Exercise the model used by QML against an actual compositor and xdg client.
// No fake protocol callbacks: every state change must come back over Wayland.
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    QProcess client;
    try {
        if (argc != 2)
            throw std::runtime_error("expected Wayland probe executable");
        TaskModel model;
        if (!model.connectDisplay())
            throw std::runtime_error("cannot connect task model");
        auto wait = [&](const std::function<bool()> &condition, const char *message) {
            QElapsedTimer timer;
            timer.start();
            while (!condition()) {
                QCoreApplication::processEvents();
                if (timer.elapsed() > 2000)
                    throw std::runtime_error(message);
                QThread::msleep(1);
            }
        };
        client.start(QString::fromLocal8Bit(argv[1]), {"--external-control"});
        if (!client.waitForStarted(2000))
            throw std::runtime_error("cannot start probe client");
        auto value = [&](int role) { return model.data(model.index(0), role); };
        wait([&] { return model.rowCount() == 1 && value(TaskModel::Active).toBool(); },
             "task did not appear with active state");
        if (value(TaskModel::Title).toString() != "paw protocol probe" ||
            value(TaskModel::AppId).toString() != "paw-probe")
            throw std::runtime_error("task metadata incorrect");
        int id = value(TaskModel::TaskId).toInt();
        // A window has no picture until it is watched, and watching what is not there is
        // harmless.
        if (model.roleNames().value(TaskModel::Picture) != "picture" ||
            !value(TaskModel::Picture).toString().isEmpty() || !model.picture(id).isNull())
            throw std::runtime_error("the window has a picture before one was asked for");
        model.watchPicture(id, 160, true);
        model.watchPicture(id + 100, 160, true); // no such window
        model.unwatchPicture(id);
        model.unwatchPicture(id); // one more than it was watched
        // QML asking for a picture that is not there gets an empty one, not an error.
        QSize served;
        const QImage none = WindowImages(model).requestImage(QString("%1/1").arg(id), &served, {});
        if (served != QSize(1, 1) || none.size() != served || none.pixelColor(0, 0).alpha() != 0)
            throw std::runtime_error("a missing picture was not served as an empty one");
        // Where it is comes from the compositor's window control: its only output, the first
        // workspace, floating since the example configuration does not tile.
        wait([&] { return value(TaskModel::Workspace).toInt() == 1; }, "no workspace arrived");
        if (value(TaskModel::Output).toString() != "HEADLESS-1" ||
            value(TaskModel::Sticky).toBool() || value(TaskModel::Tiling).toBool() ||
            model.roleNames().value(TaskModel::Workspace) != "workspace" ||
            model.roleNames().value(TaskModel::Tiling) != "tiling")
            throw std::runtime_error("the window's place is not as the compositor has it");
        // So does the process that made it, the probe.
        if (value(TaskModel::Pid).toLongLong() != client.processId() ||
            model.roleNames().value(TaskModel::Pid) != "pid")
            throw std::runtime_error("the window's process is not the probe's");
        // And its number, the first window's in this compositor.
        if (value(TaskModel::WindowId).toInt() != 1 ||
            model.roleNames().value(TaskModel::WindowId) != "windowId")
            throw std::runtime_error("the window's number is not the compositor's first");
        // The window menu's requests come back as the window's new place.
        model.moveToWorkspace(id, 3);
        wait([&] { return value(TaskModel::Workspace).toInt() == 3; }, "moving to a workspace failed");
        model.setSticky(id, true);
        wait([&] { return value(TaskModel::Sticky).toBool() && value(TaskModel::Workspace) == 1; },
             "making the window sticky failed");
        if (!value(TaskModel::Floating).toBool())
            throw std::runtime_error("a sticky window does not float");
        model.setSticky(id, false);
        wait([&] { return !value(TaskModel::Sticky).toBool(); }, "unsticking failed");
        model.setFloating(id, false);
        wait([&] { return !value(TaskModel::Floating).toBool(); }, "letting the window tile failed");
        model.setFloating(id, true);
        wait([&] { return value(TaskModel::Floating).toBool(); }, "floating the window failed");
        // Kept above the others and let go, through version 6.
        if (!value(TaskModel::Above).isValid() || value(TaskModel::Above).toBool() ||
            model.roleNames().value(TaskModel::Above) != "above")
            throw std::runtime_error("the window's above state is not as the compositor has it");
        model.setAbove(id, true);
        wait([&] { return value(TaskModel::Above).toBool(); }, "keeping the window above failed");
        model.setAbove(id, false);
        wait([&] { return !value(TaskModel::Above).toBool(); }, "letting the window go failed");
        model.moveToOutput(id, "HEADLESS-1"); // the one it is on: nothing changes
        // Moved away, it lost the focus, and none of these gave it back.
        if (value(TaskModel::Active).toBool())
            throw std::runtime_error("a request of the window menu focused the window");
        model.activate(id);
        wait([&] { return value(TaskModel::Active).toBool(); }, "activation failed");
        model.activate(id); // Clicking the active task minimizes it.
        wait([&] { return value(TaskModel::Minimized).toBool(); }, "minimize failed");
        model.activate(id);
        wait(
            [&] {
                return value(TaskModel::Active).toBool() && !value(TaskModel::Minimized).toBool();
            },
            "restore/activate failed");
        model.maximize(id);
        wait([&] { return value(TaskModel::Maximized).toBool(); }, "maximize failed");
        model.maximize(id);
        wait([&] { return !value(TaskModel::Maximized).toBool(); }, "unmaximize failed");
        model.setFullscreen(id, true);
        wait([&] { return value(TaskModel::Fullscreen).toBool(); }, "fullscreen failed");
        model.setFullscreen(id, false);
        wait([&] { return !value(TaskModel::Fullscreen).toBool(); }, "leaving fullscreen failed");
        if (model.roleNames().value(TaskModel::Fullscreen) != "fullscreen")
            throw std::runtime_error("the fullscreen role is not named for QML");
        model.showDesktop();
        wait([&] { return value(TaskModel::Minimized).toBool(); }, "show desktop failed");
        model.activate(id);
        wait([&] { return value(TaskModel::Active).toBool(); }, "second activation failed");
        // The model peeks at one window at a time, and says which; ending a peek at another
        // window, or at none, leaves it. What the compositor shows is window_peek_smoke's.
        int peekSignals = 0;
        QObject::connect(&model, &TaskModel::peekedTaskChanged, &model, [&] { ++peekSignals; });
        model.peek(id + 100); // no such window
        model.endPeek(id);
        if (model.peekedTask() != -1 || peekSignals != 0)
            throw std::runtime_error("the model peeks at a window that is not there");
        model.peek(id);
        model.peek(id);
        model.endPeek(id + 100);
        if (model.peekedTask() != id || peekSignals != 1)
            throw std::runtime_error("the model does not say which window it peeks at");
        model.endPeek(id);
        if (model.peekedTask() != -1 || peekSignals != 2)
            throw std::runtime_error("ending the peek did not say so");
        // The compositor names urgent windows by app id and title; a match marks the task, and
        // only that announces a change of the role.
        int urgentSignals = 0;
        QObject::connect(&model, &QAbstractItemModel::dataChanged, &model,
                         [&](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
                             if (roles.contains(TaskModel::Urgent))
                                 ++urgentSignals;
                         });
        if (value(TaskModel::Urgent).toBool())
            throw std::runtime_error("a task is urgent before anything asked");
        model.setUrgent({{"paw-probe", "another title"}, {"other-app", "paw protocol probe"}});
        if (value(TaskModel::Urgent).toBool() || urgentSignals != 0)
            throw std::runtime_error("an urgent window with another title or app marked the task");
        model.setUrgent({{"paw-probe", "paw protocol probe"}});
        if (!value(TaskModel::Urgent).toBool() || urgentSignals != 1)
            throw std::runtime_error("the urgent window did not mark its task");
        model.setUrgent({{"paw-probe", "paw protocol probe"}});
        if (urgentSignals != 1)
            throw std::runtime_error("an unchanged urgent list announced a change");
        model.setUrgent({});
        if (value(TaskModel::Urgent).toBool() || urgentSignals != 2)
            throw std::runtime_error("clearing the urgent list did not unmark the task");
        if (model.roleNames().value(TaskModel::Urgent) != "urgent")
            throw std::runtime_error("the urgent role is not named for QML");
        // The window's picture, copied by the compositor from its capture source: the probe draws
        // 320 by 240 pixels, a dark strip 36 high over blue, which fits 160 pixels wide at 133 by
        // 100. A new picture announces its role alone.
        QList<QList<int>> pictureRoles;
        QObject::connect(&model, &QAbstractItemModel::dataChanged, &model,
                         [&](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
                             if (roles.contains(TaskModel::Picture))
                                 pictureRoles.push_back(roles);
                         });
        auto url = [&] { return value(TaskModel::Picture).toString(); };
        auto pictured = [&](const QString &before, const char *message) {
            wait([&] { return !url().isEmpty() && url() != before; }, message);
        };
        auto isProbe = [](const QImage &picture) {
            auto near = [](QRgb actual, QRgb expected) {
                return qAlpha(actual) == 0xff && std::abs(qRed(actual) - qRed(expected)) <= 2 &&
                       std::abs(qGreen(actual) - qGreen(expected)) <= 2 &&
                       std::abs(qBlue(actual) - qBlue(expected)) <= 2;
            };
            const int x = picture.width() / 2;
            return near(picture.pixel(x, 2), 0x23314a) &&
                   near(picture.pixel(x, picture.height() * 3 / 4), 0x417bc4);
        };
        model.watchPicture(id, 160, false);
        pictured({}, "no picture of the window arrived");
        const QString prefix = QString("image://windows/%1/").arg(id);
        if (!url().startsWith(prefix) || model.picture(id).size() != QSize(133, 100) ||
            !isProbe(model.picture(id)))
            throw std::runtime_error("the window's picture is not the probe's window at 133 by 100");
        if (WindowImages(model).requestImage(url().mid(QString("image://windows/").size()), &served,
                                             {}) != model.picture(id))
            throw std::runtime_error("the image provider did not serve the window's picture");
        model.unwatchPicture(id);
        // Watched live, it follows the window as it redraws: maximized, at another size, and back.
        QString last = url();
        model.watchPicture(id, 160, true);
        pictured(last, "watching again took no new picture");
        model.maximize(id);
        wait(
            [&] {
                const QSize size = model.picture(id).size();
                return size != QSize(133, 100) && (size.width() == 160 || size.height() == 100);
            },
            "the live picture did not follow the maximized window");
        if (!isProbe(model.picture(id)))
            throw std::runtime_error("the maximized window's picture is not the probe's window");
        model.maximize(id);
        wait([&] { return model.picture(id).size() == QSize(133, 100); },
             "the live picture did not follow the window back");
        // Watched at another width meanwhile, the picture takes it, smaller and larger again: the
        // compositor, which scales the window down itself, is asked again each time.
        model.watchPicture(id, 80, true);
        wait([&] { return model.picture(id).size() == QSize(67, 50); },
             "the picture did not shrink to a new width");
        model.watchPicture(id, 160, true);
        wait([&] { return model.picture(id).size() == QSize(133, 100); },
             "the picture did not grow to a new width");
        if (!isProbe(model.picture(id)))
            throw std::runtime_error("the picture at a new width is not the probe's window");
        model.unwatchPicture(id);
        model.unwatchPicture(id);
        model.unwatchPicture(id);
        // Unwatched, the last picture stays; a minimized window is pictured too.
        if (url().isEmpty() || model.picture(id).isNull())
            throw std::runtime_error("unwatching dropped the window's picture");
        model.minimize(id);
        wait([&] { return value(TaskModel::Minimized).toBool(); }, "minimizing for a picture failed");
        last = url();
        model.watchPicture(id, 160, false);
        pictured(last, "a minimized window gave no picture");
        if (!isProbe(model.picture(id)))
            throw std::runtime_error("the minimized window's picture is not the probe's window");
        model.unwatchPicture(id);
        for (const auto &roles : pictureRoles)
            if (roles != QList<int>{TaskModel::Picture})
                throw std::runtime_error("a new picture announced other roles than its own");
        model.activate(id);
        wait([&] { return value(TaskModel::Active).toBool(); }, "activating after the pictures failed");
        model.setUrgent({{"paw-probe", "paw protocol probe"}});
        model.close(id);
        wait([&] { return model.rowCount() == 0; }, "closed task was not removed");
        if ((client.state() != QProcess::NotRunning && !client.waitForFinished(2000)) ||
            client.exitStatus() != QProcess::NormalExit || client.exitCode() != 0)
            throw std::runtime_error("probe did not exit cleanly");
        // Stale IDs must be harmless after a window closes, and its picture has gone with it.
        model.activate(id);
        model.close(id);
        model.watchPicture(id, 160, true);
        model.unwatchPicture(id);
        if (!model.picture(id).isNull())
            throw std::runtime_error("a closed window's picture stayed");
        std::cout
            << "Task model metadata, place, minimize, restore, maximize, fullscreen, show desktop, "
               "pictures, close passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n' << client.readAllStandardError().toStdString();
        if (client.state() != QProcess::NotRunning) {
            client.kill();
            client.waitForFinished(2000);
        }
        return 1;
    }
}
