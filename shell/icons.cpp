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

QImage Icons::requestImage(const QString &asked, QSize *size, const QSize &requested) {
    QSize dimensions = requested.isEmpty() ? QSize(48, 48) : requested;
    // @N/ before the name or path once the icon theme has changed N times
    // (ShellController::iconBase): a URL of its own, looked up anew.
    const QString id = asked.startsWith('@') ? asked.section('/', 1) : asked;
    QImage image;
    {
        std::lock_guard guard(iconThemeLock());
        QIcon icon = id.startsWith('/') ? QIcon(id) : QIcon();
        // A theme icon's name, or several separated by commas, the first the theme has standing.
        for (const auto &name :
             id.startsWith('/') ? QStringList() : id.split(',', Qt::SkipEmptyParts))
            if (icon = QIcon::fromTheme(name); !icon.isNull())
                break;
        if (icon.isNull())
            icon = QIcon::fromTheme("application-x-executable");
        if (!icon.isNull())
            image = icon.pixmap(dimensions).toImage();
    }
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

QString gioIconName(GIcon *icon, const QString &fallback) {
    if (icon && G_IS_THEMED_ICON(icon)) {
        const char *const *names = g_themed_icon_get_names(G_THEMED_ICON(icon));
        if (names && *names)
            return QString::fromUtf8(*names);
    } else if (icon && G_IS_FILE_ICON(icon)) {
        if (char *path = g_file_get_path(g_file_icon_get_file(G_FILE_ICON(icon)))) {
            const auto name = QString::fromUtf8(path);
            g_free(path);
            return name;
        }
    }
    return fallback;
}

namespace {
// GNOME's icon-theme, only where someone set it: its default (Adwaita) says nothing of what the
// user chose, and settings.ini may name the theme while it is unset.
QString gnomeIconTheme() {
    // While a login session has a profile's icon theme in GNOME's setting, the compositor keeps
    // the one it replaced here (src/compositor/profile_themes.c).
    if (qEnvironmentVariableIsSet("PAW_USER_ICON_THEME"))
        return qEnvironmentVariable("PAW_USER_ICON_THEME").trimmed();
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
} // namespace

bool iconThemeInstalled(const QString &theme) {
    if (theme.isEmpty() || theme.contains('/') || theme == "." || theme == "..")
        return false;
    for (const auto &folder : QIcon::themeSearchPaths())
        if (QFileInfo::exists(folder + '/' + theme + "/index.theme"))
            return true;
    return false;
}

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
    std::lock_guard guard(iconThemeLock());
    if (const auto current = QIcon::themeName(); current.isEmpty() || current == "hicolor")
        if (const auto theme = desktopIconTheme(); !theme.isEmpty() && iconThemeInstalled(theme))
            QIcon::setThemeName(theme);
    // Every theme falls back to this one before hicolor, which holds applications' own icons
    // but none of the generic ones (application-x-executable, utilities-terminal) that desktop
    // entries and the shell name. A theme that inherits one of these has it already.
    for (const char *broad : {"breeze", "Adwaita", "Papirus", "elementary", "gnome"})
        if (QIcon::themeName() != broad && iconThemeInstalled(broad)) {
            QIcon::setFallbackThemeName(broad);
            break;
        }
}
