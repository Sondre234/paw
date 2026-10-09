// SPDX-License-Identifier: GPL-3.0-or-later
#include "power.hpp"
#include "controller.hpp"

namespace {
struct Action {
    const char *name, *title;
    // For those that ask first: the heading and button, and the question with and without a
    // countdown (%1 the time left).
    const char *confirm, *counting, *asking;
};
// The actions the menu offers, in its order.
constexpr Action actions[] = {
    {"lock", "Lock screen", nullptr, nullptr, nullptr},
    {"suspend", "Suspend", nullptr, nullptr, nullptr},
    {"hibernate", "Hibernate", nullptr, nullptr, nullptr},
    {"reboot", "Restart…", "Restart", "The computer restarts in %1.", "Restart the computer now?"},
    {"poweroff", "Power off…", "Power off", "The computer powers off in %1.",
     "Power off the computer now?"},
    {"logout", "Log out…", "Log out", "The session ends in %1.", "End the session now?"},
};
const Action *find(const QString &name) {
    for (const auto &action : actions)
        if (name == action.name)
            return &action;
    return nullptr;
}
} // namespace

Power::Power(ShellController &controller) : QObject(&controller), controller_(controller) {
    tick_.setInterval(1000);
    connect(&tick_, &QTimer::timeout, this, [this] {
        if (--countdown_ > 0) {
            Q_EMIT countdownChanged();
            return;
        }
        confirm();
    });
}

QString Power::title(const QString &action) {
    const auto *found = find(action);
    return found ? QString(found->title) : QString();
}

QVariantList Power::entries() const {
    QVariantList list;
    for (const auto &action : available_)
        list.push_back(QVariantMap{{"action", action}, {"title", title(action)}});
    return list;
}

QString Power::pendingTitle() const {
    const auto *found = find(pending_);
    return found && found->confirm ? QString(found->confirm) : QString();
}

QString Power::message() const {
    const auto *found = find(pending_);
    if (!found || !found->confirm)
        return {};
    if (countdown_ <= 0)
        return found->asking;
    return QString(found->counting)
        .arg(countdown_ == 1 ? QString("1 second") : QString("%1 seconds").arg(countdown_));
}

void Power::setAvailable(const QString &list) {
    QStringList offered;
    for (const auto &action : list.split(',', Qt::SkipEmptyParts))
        if (find(action))
            offered.push_back(action);
    if (offered == available_)
        return;
    available_ = offered;
    Q_EMIT availableChanged();
    // One that may no longer run is not asked about any more.
    if (!pending_.isEmpty() && !available_.contains(pending_))
        cancel();
}

void Power::request(const QString &action, const QString &output) {
    const auto *found = find(action);
    if (!found || !available_.contains(action))
        return;
    if (!found->confirm) {
        run(action);
        return;
    }
    pending_ = action;
    output_ = output.isEmpty() ? controller_.overlayOutput() : output;
    countdown_ = seconds_;
    if (countdown_ > 0)
        tick_.start();
    else
        tick_.stop();
    Q_EMIT pendingChanged();
    Q_EMIT countdownChanged();
}

void Power::confirm() {
    const auto action = pending_;
    cancel();
    if (!action.isEmpty())
        run(action);
}

void Power::cancel() {
    tick_.stop();
    countdown_ = 0;
    if (pending_.isEmpty())
        return;
    pending_.clear();
    output_.clear();
    Q_EMIT pendingChanged();
    Q_EMIT countdownChanged();
}

void Power::run(const QString &action) {
    controller_.ask((action + "\n").toUtf8(), [this, action](const QByteArray &reply) {
        if (reply.startsWith("ok"))
            return;
        auto message = QString::fromUtf8(reply).trimmed();
        if (message.startsWith("error: "))
            message = message.sliced(7);
        auto name = title(action);
        if (name.endsWith(QChar(0x2026))) // "Power off…" asked already
            name.chop(1);
        Q_EMIT failed(name + ": " + (message.isEmpty() ? "no reply" : message));
    });
}
