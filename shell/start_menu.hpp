// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "launch_history.hpp"
#include <QDateTime>
#include <QObject>
#include <QStringList>
#include <QUrl>
#include <QVariantList>

class FileIndex;
class QTimer;
typedef struct _GAppInfoMonitor GAppInfoMonitor;

// The start menu's own state, one for every monitor's menu: the applications pinned to it, apart
// from the taskbar's as on Windows; what was launched when; the applications from A to Z; its
// search; and who is logged in. The controller hands it the applications (setApps) and tells it
// of every launch (record).
//
// Its pins are kept in $XDG_STATE_HOME/paw/start-pinned, a desktop id per line. Until that
// file exists they are the taskbar's pins and a few common applications (seed), and the file is
// written only once they are changed here. Launches are kept in $XDG_STATE_HOME/paw/launches
// (LaunchHistory).
class StartMenu : public QObject {
    Q_OBJECT
    // The pinned applications that are installed, in their order, as the controller's records.
    Q_PROPERTY(QVariantList pinned READ pinned NOTIFY pinnedChanged)
    // The installed applications launched lately, the most recent first, as records with
    // `launches` (how many times) and `launched` (when last).
    Q_PROPERTY(QVariantList recent READ recent NOTIFY recentChanged)
    // Every application by name, as records with the `letter` of the section it is listed under:
    // its name's first letter in upper case, without accents, or "#" when that is not a letter.
    Q_PROPERTY(QVariantList apps READ apps NOTIFY appsChanged)
    // The user: the full name from the password database, else the login name, and the picture
    // (~/.face, ~/.face.icon, or AccountsService's when the shell has D-Bus), empty for none.
    Q_PROPERTY(QString userName READ userName NOTIFY userChanged)
    Q_PROPERTY(QUrl userIcon READ userIcon NOTIFY userChanged)
    // Counts the times what a search finds changed without the applications changing (the
    // files were read again), for a search to be made again.
    Q_PROPERTY(int searchRevision READ searchRevision NOTIFY searchChanged)
  public:
    // `stateDir` holds start-pinned and launches: $XDG_STATE_HOME/paw unless given.
    explicit StartMenu(QString stateDir = {}, QObject *parent = nullptr);
    ~StartMenu() override;
    QVariantList pinned() const;
    QVariantList recent() const;
    QVariantList apps() const { return sorted_; }
    QString userName() const { return userName_; }
    QUrl userIcon() const { return userIcon_; }
    int searchRevision() const { return searchRevision_; }
    // The files its search finds; none until set.
    void setFiles(FileIndex *files);
    // The search engine of its search's last result (web_search::entry); empty for none.
    void setWebSearch(const QString &engine) { webSearch_ = engine; }

    // The applications, as the controller's records, and the taskbar's pins, which seed the
    // start menu's own until it has them.
    void setApps(const QVariantList &apps, const QStringList &taskbarPins);
    // A launch of application `id` from anywhere in the shell; configured launchers, whose ids
    // are only their places in the configuration, are left out.
    void record(const QString &id, const QDateTime &when = QDateTime::currentDateTimeUtc());
    // For a preview: these pins and launches instead, kept only in memory from now on, so
    // nothing the preview does is saved.
    void preview(const QStringList &pins, const QList<LaunchHistory::Entry> &launches);
    // Someone else as the user, for a preview; what the system says of the real one is then
    // ignored.
    void setUser(const QString &name, const QUrl &icon);

    Q_INVOKABLE bool isPinned(const QString &id) const;
    // Pins an installed application at the end, or unpins it. Configured launchers have no
    // place here: they belong to the taskbar.
    Q_INVOKABLE void pin(const QString &id);
    Q_INVOKABLE void unpin(const QString &id);
    // Moves pinned `id` to the place of pinned `target`, those between moving aside.
    Q_INVOKABLE void movePin(const QString &id, const QString &target);
    // What `query` finds, best first, in groups: "best" (the one best match), "apps", "windows"
    // and "actions". `others` are the command palette's entries (Palette::entries), of which the
    // windows, workspaces and actions are searched as the palette searches them. Applications
    // are found by name, generic name, keywords, desktop id and comment, those launched often a
    // little ahead. A result is its entry, an application's being its record with `kind` "app",
    // a `title` and a `subtitle`, with its `group` and `score` added. A calculation's value
    // (calculator::entry) is the best match when there is one. Files (FileIndex::search) come
    // last, as "files", and are the best match only when they match better than the rest; a
    // leading / finds only files. Search the web for “…” (web_search::entry) is the very last,
    // as "web", and the best match when nothing else is found.
    Q_INVOKABLE QVariantList search(const QString &query, const QVariantList &others) const;
    // When `then` was, said from `now`: "Just now", "5 min ago", "2 hours ago", "Yesterday", a
    // day of the week, or a date.
    Q_INVOKABLE QString ago(const QDateTime &then, const QDateTime &now) const;

    // The start menu's first pins: the taskbar's, then common applications until there are
    // `wanted`, of those `installed`.
    static QStringList seed(const QStringList &taskbarPins, const QStringList &common,
                            const QStringList &installed, int wanted = 6);
    // The desktop ids of the applications most people start with: the default web browser, file
    // manager, a terminal, text editor, mail client, image viewer and media player.
    static QStringList commonApps();
    // The section an application called `name` is listed under (see `apps`).
    static QString letterOf(const QString &name);
  Q_SIGNALS:
    void pinnedChanged();
    void recentChanged();
    void appsChanged();
    void userChanged();
    void searchChanged();
    // Applications were installed or removed (GIO's GAppInfoMonitor), and want reading again.
    void installedChanged();
    // Saving the pins or the history failed.
    void failed(const QString &message);

  private:
    QString stateDir_;
    QVariantList apps_, sorted_;
    // The pins, of installed applications or not, in their order; own once start-pinned exists.
    QStringList pins_;
    bool ownPins_ = false, previewOnly_ = false, userSet_ = false;
    FileIndex *files_ = nullptr;
    int searchRevision_ = 0;
    QString webSearch_;
    LaunchHistory history_;
    QString userName_;
    QUrl userIcon_;
    GAppInfoMonitor *monitor_ = nullptr;
    QTimer *installing_ = nullptr;
    QVariantMap appRecord(const QString &id) const;
    bool installed(const QString &id) const;
    void savePins();
    void findUser();
};
