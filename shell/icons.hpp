// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "icon_lock.hpp"
#include <QQuickImageProvider>

typedef struct _GIcon GIcon;

// image://icons/NAME: a theme icon by name (or the first the theme has of several names
// separated by commas, as a file's type's icon and more generic ones), or an image file by
// absolute path, with a drawn stand-in when neither exists; image://icons/@N/NAME the same,
// after the icon theme changed (ShellController::iconBase).
class Icons : public QQuickImageProvider {
  public:
    Icons() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override;
};

// What image://icons/ finds GIO's `icon` by: its first theme icon name, or its file's path;
// `fallback` for an icon of neither kind, or none.
QString gioIconName(GIcon *icon, const QString &fallback);

// The icon theme the desktop names: GNOME's setting where the user set it (GNOME Settings,
// gsettings, nwg-look), or $PAW_USER_ICON_THEME while paw has a profile's there, else GTK's
// settings.ini, else KDE's kdeglobals; empty when none does.
QString desktopIconTheme();
// Whether an icon theme of this name is in one of Qt's folders of themes.
bool iconThemeInstalled(const QString &theme);
// Looks icons up in desktopIconTheme() when the platform names no theme but hicolor, as it
// does without a platform theme such as qt6ct's or KDE's, and after the theme in a broad one
// that is installed, so that generic names such as utilities-terminal find an icon.
void useDesktopIconTheme();
