// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QDir>
#include <QFileInfo>
#include <QSaveFile>
#include <QString>

// Where the shell keeps what it remembers (pins, the launches, the clipboard history, ...): paw in
// $XDG_STATE_HOME, or in ~/.local/state while that is unset or not an absolute path.
inline QString stateDir() {
    auto state = qEnvironmentVariable("XDG_STATE_HOME");
    if (state.isEmpty() || QDir::isRelativePath(state))
        state = QDir::homePath() + "/.local/state";
    return state + "/paw";
}

// Writes `contents` to the file at `path`, whole or not at all, making its folder first. Returns
// what went wrong, empty once it is written.
inline QString saveFile(const QString &path, const QByteArray &contents) {
    QDir().mkpath(QFileInfo(path).path());
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        file.write(contents);
        if (file.commit())
            return {};
    }
    return file.errorString();
}
