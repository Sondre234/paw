// SPDX-License-Identifier: GPL-3.0-or-later
#include "icons.hpp"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QPainter>
#include <QStandardPaths>
#include <QTextStream>
#include <gio/gio.h>

QImage Icons::requestImage(const QString &id, QSize *size, const QSize &requested) {
    QSize dimensions = requested.isEmpty() ? QSize(48, 48) : requested;
    QIcon icon = id.startsWith('/') ? QIcon(id) : QIcon();
    // A theme icon's name, or several separated by commas, the first the theme has standing.
    for (const auto &name : id.startsWith('/') ? QStringList() : id.split(',', Qt::SkipEmptyParts))
        if (icon = QIcon::fromTheme(name); !icon.isNull())
            break;
    if (icon.isNull())
        icon = QIcon::fromTheme("application-x-executable");
    QImage image;
    if (!icon.isNull())
        image = icon.pixmap(dimensions).toImage();
    if (image.isNull()) {
        image = QImage(dimensions, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor("#8aaff4"));
        painter.drawRoundedRect(image.rect().adjusted(4, 4, -4, -4), 8, 8);
        painter.setPen(QColor("#172237"));
        QFont font = painter.font();
        font.setPixelSize(dimensions.height() / 2);
        font.setBold(true);
        painter.setFont(font);
        painter.drawText(image.rect(), Qt::AlignCenter, "+");
    }
    if (size)
        *size = image.size();
    return image;
}

namespace {
// GNOME's icon-theme, only where someone set it: its default (Adwaita) says nothing of what the
// user chose, and settings.ini may name the theme while it is unset.
QString gnomeIconTheme() {
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    GSettingsSchema *schema =
        source ? g_settings_schema_source_lookup(source, "org.gnome.desktop.interface", TRUE)
               : nullptr;
    if (!schema)
        return {};
    const bool known = g_settings_schema_has_key(schema, "icon-theme");
    g_settings_schema_unref(schema);
    if (!known)
        return {};
    GSettings *settings = g_settings_new("org.gnome.desktop.interface");
    QString theme;
    if (GVariant *value = g_settings_get_user_value(settings, "icon-theme")) {
        if (g_variant_is_of_type(value, G_VARIANT_TYPE_STRING))
            theme = QString::fromUtf8(g_variant_get_string(value, nullptr));
        g_variant_unref(value);
    }
    g_object_unref(settings);
    return theme.trimmed();
}
// A key's value in a group of an ini-style file, as GTK's settings.ini and kdeglobals are.
QString iniValue(const QString &path, const QString &group, const QString &key) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    QTextStream in(&file);
    bool inGroup = false;
    while (!in.atEnd()) {
        const auto line = in.readLine().trimmed();
        if (line.startsWith('[')) {
            inGroup = line == '[' + group + ']';
            continue;
        }
        const auto equals = line.indexOf('=');
        if (inGroup && equals > 0 && line.left(equals).trimmed() == key)
            return line.mid(equals + 1).trimmed();
    }
    return {};
}
bool installed(const QString &theme) {
    for (const auto &folder : QIcon::themeSearchPaths())
        if (QFileInfo::exists(folder + '/' + theme + "/index.theme"))
            return true;
    return false;
}
} // namespace

QString desktopIconTheme() {
    if (auto theme = gnomeIconTheme(); !theme.isEmpty())
        return theme;
    const auto config = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
    for (const char *gtk : {"/gtk-4.0/settings.ini", "/gtk-3.0/settings.ini"})
        if (auto theme = iniValue(config + gtk, "Settings", "gtk-icon-theme-name");
            !theme.isEmpty())
            return theme;
    return iniValue(config + "/kdeglobals", "Icons", "Theme");
}

void useDesktopIconTheme() {
    if (const auto current = QIcon::themeName(); current.isEmpty() || current == "hicolor")
        if (const auto theme = desktopIconTheme(); !theme.isEmpty() && installed(theme))
            QIcon::setThemeName(theme);
    // Every theme falls back to this one before hicolor, which holds applications' own icons
    // but none of the generic ones (application-x-executable, utilities-terminal) that desktop
    // entries and the shell name. A theme that inherits one of these has it already.
    for (const char *broad : {"breeze", "Adwaita", "Papirus", "elementary", "gnome"})
        if (QIcon::themeName() != broad && installed(broad)) {
            QIcon::setFallbackThemeName(broad);
            break;
        }
}
