// SPDX-License-Identifier: GPL-3.0-or-later
#include "rice_trial.hpp"

#include "paw/rice.hpp"

#include <cmath>

RiceTrial::RiceTrial(Send send, std::function<QString()> output, QObject *parent)
    : QObject(parent), send_(std::move(send)), overlayOutput_(std::move(output)) {
    countdown_.setInterval(250);
    connect(&countdown_, &QTimer::timeout, this, &RiceTrial::tick);
}

bool RiceTrial::handle(const QString &line) {
    if (line.startsWith("rice-trial ")) {
        const auto words = line.split(' ');
        if (words.size() != 3)
            return true;
        // What its rice.lua says of it; the name alone when it cannot be read.
        QString author, description;
        try {
            const auto rice = paw::read_rice(paw::rices_directory() / words[2].toStdString());
            author = QString::fromStdString(rice.author);
            description = QString::fromStdString(rice.description);
        } catch (const std::exception &) {
        }
        start(words[1].toInt(), words[2], author, description);
        return true;
    }
    if (line.startsWith("rice-kept ") || line.startsWith("rice-reverted ")) {
        end();
        return true;
    }
    return false;
}

void RiceTrial::start(int milliseconds, const QString &name, const QString &author,
                      const QString &description) {
    name_ = name;
    author_ = author;
    description_ = description;
    output_ = overlayOutput_();
    deadline_.setRemainingTime(milliseconds);
    countdown_.start();
    active_ = true;
    Q_EMIT changed();
    tick();
}

void RiceTrial::keep() {
    if (active_)
        send_("rice keep");
}

void RiceTrial::revert() {
    if (active_)
        send_("rice revert");
}

void RiceTrial::tick() {
    const int seconds =
        active_ ? static_cast<int>(std::ceil(deadline_.remainingTime() / 1000.0)) : 0;
    if (seconds != secondsLeft_) {
        secondsLeft_ = seconds;
        Q_EMIT secondsLeftChanged();
    }
}

void RiceTrial::end() {
    countdown_.stop();
    if (active_) {
        active_ = false;
        Q_EMIT changed();
    }
    tick();
}
