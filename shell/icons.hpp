// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QQuickImageProvider>

// image://icons/NAME: a theme icon by name (or the first the theme has of several names
// separated by commas, as a file's type's icon and more generic ones), or an image file by
// absolute path, with a drawn stand-in when neither exists.
class Icons : public QQuickImageProvider {
  public:
    Icons() : QQuickImageProvider(QQuickImageProvider::Image) {}
    QImage requestImage(const QString &id, QSize *size, const QSize &requested) override;
};

// The icon theme the desktop names: GNOME's setting where the user set it (GNOME Settings,
// gsettings, nwg-look), else GTK's settings.ini, else KDE's kdeglobals; empty when none does.
QString desktopIconTheme();
// Looks icons up in desktopIconTheme() when the platform names no theme but hicolor, as it
// does without a platform theme such as qt6ct's or KDE's, and after the theme in a broad one
// that is installed, so that generic names such as utilities-terminal find an icon.
void useDesktopIconTheme();
