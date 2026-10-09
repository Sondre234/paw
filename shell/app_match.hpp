// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QList>
#include <QString>
#include <QStringList>

// Which desktop entry a window belongs to, by the app id it gives. Most windows give their
// desktop file's name, but many spell it otherwise: X11 clients give their WM_CLASS, which the
// entry may name as StartupWMClass; others give their program's name (gimp-2.10), a name with
// the vendor's domain the entry lacks (org.mozilla.firefox) or the other way round
// (gnome-calculator for org.gnome.Calculator), a NixOS wrapper's name
// (.blueman-manager-wrapped), or a Steam game's number (steam_app_570).
namespace app_match {

// What a desktop entry is known by.
struct Entry {
    QString id;      // its desktop file id, org.gnome.Nautilus.desktop
    QString wmClass; // StartupWMClass
    QString exec;    // the Exec line
    QString name;    // Name
};

// Entries, with what each is known by worked out once.
class Index {
  public:
    Index() = default;
    explicit Index(const QList<Entry> &entries);
    // The id of the entry a window with this app id belongs to, or an empty string. The ways
    // to match are tried from the most exact down, each against every entry before the next,
    // then again for each of the app id's aliases().
    QString find(const QString &appId) const;

  private:
    struct Keys {
        QString id, base, last, wmClass, steamGame;
        // Folded with key(): the base, its last part and last two parts as a reverse-DNS name,
        // StartupWMClass, the program and Name.
        QString baseKey, lastKey, lastTwoKey, wmClassKey, programKey, nameKey;
    };
    QString findOne(const QString &appId) const;
    QList<Keys> entries_;
};

// The name lowered, with what programs add to it taken off: a leading dot and -wrapped
// (NixOS), a file extension (.exe, .AppImage), a version (-2.10, _1.20.1), an architecture
// (-x86_64) and suffixes such as -bin, -desktop, -stable and -browser. Its dashes stay, as icon
// names have them.
QString trimmed(const QString &name);
// trimmed() with only its letters and digits left, so that gnome-calculator, GnomeCalculator
// and gnome_calculator are one key.
QString key(const QString &name);
// The program an Exec line runs: its file name, past env and its variables, wrappers such as
// gamemoderun or prime-run, sh -c, and flatpak run (its --command, else the application id).
QString program(const QString &exec);
// Other names of the applications whose app ids name nothing like their desktop entries or
// icons (steamwebhelper is Steam's, soffice LibreOffice's), lowered; empty for the rest.
QStringList aliases(const QString &appId);
// Icon names to try, best first, for a window with this app id that no entry matches.
QStringList iconGuesses(const QString &appId);

} // namespace app_match
