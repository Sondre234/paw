// SPDX-License-Identifier: GPL-3.0-or-later
#include "display_modes.hpp"

namespace {
bool known(const QString &mode) {
    return mode == "internal" || mode == "duplicate" || mode == "extend" || mode == "external";
}
} // namespace

bool DisplayModes::handle(const QString &line) {
    if (line == "display-mode-close") {
        if (active_) {
            active_ = false;
            Q_EMIT changed();
        }
        return true;
    }
    if (!line.startsWith("display-mode "))
        return false;
    const auto words = line.split(' ', Qt::SkipEmptyParts);
    if (words.size() != 5 || !known(words[2]) || !known(words[3]))
        return true;
    QStringList choices;
    for (const auto &choice : words[4].split(',', Qt::SkipEmptyParts))
        if (known(choice))
            choices.push_back(choice);
    active_ = true;
    output_ = words[1];
    shown_ = words[2];
    current_ = words[3];
    choices_ = choices;
    Q_EMIT changed();
    return true;
}

void DisplayModes::choose(const QString &mode) {
    if (known(mode) && choices_.contains(mode))
        Q_EMIT request("display_mode " + mode);
}
