// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QByteArray>
#include <QString>
#include <gio/gio.h>

// Starts something through GIO: `launch(context, &error)`, given a launch context without the
// shell's own platform settings, which are not the application's. Returns GIO's error, empty once
// it started.
template <class Launch> QString gioLaunch(Launch launch) {
    GAppLaunchContext *context = g_app_launch_context_new();
    g_app_launch_context_unsetenv(context, "QT_WAYLAND_SHELL_INTEGRATION");
    GError *error = nullptr;
    const bool started = launch(context, &error);
    g_object_unref(context);
    if (started)
        return {};
    const auto message = QString::fromUtf8(error ? error->message : "unknown error");
    if (error)
        g_error_free(error);
    return message;
}

// Opens `uri` with its default application; GIO's error, empty once it started.
inline QString gioOpen(const QByteArray &uri) {
    return gioLaunch([&uri](GAppLaunchContext *context, GError **error) {
        return g_app_info_launch_default_for_uri(uri.constData(), context, error);
    });
}
