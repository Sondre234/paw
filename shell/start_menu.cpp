// SPDX-License-Identifier: GPL-3.0-or-later
#include "start_menu.hpp"
#include "calculator.hpp"
#include "file_index.hpp"
#include "fuzzy.hpp"
#include "state_files.hpp"
#include "web_search.hpp"
#include <QAbstractEventDispatcher>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <gio/gdesktopappinfo.h>
#include <pwd.h>
#include <unistd.h>
#if PAW_DBUS || PAW_TRAY
#include "dbus_util.hpp"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#endif

namespace {
// The words of a text, folded to lower case, as what describes an application is searched: by the
// words it says, not by letters strewn through a sentence.
QStringList wordsOf(const QString &text) {
    static const QRegularExpression separators("[^\\w]+");
    return text.toCaseFolded().split(separators, Qt::SkipEmptyParts);
}
// Whether every one of the query's `parts` starts one of `words`.
bool startsWords(const QStringList &parts, const QStringList &words) {
    return std::all_of(parts.begin(), parts.end(), [&](const QString &part) {
        return std::any_of(words.begin(), words.end(),
                           [&](const QString &word) { return word.startsWith(part); });
    });
}
} // namespace

StartMenu::Searched::Searched(const QVariantMap &app)
    : name(app.value("name").toString()), generic(app.value("genericName").toString()),
      keywords(app.value("keywords").toStringList().join(' ')),
      description(app.value("description").toString()) {
    // A configured launcher's id is only its place in the configuration.
    if (!app.value("configured").toBool()) {
        id = app.value("appId").toString();
        if (id.endsWith(".desktop"))
            id.chop(8);
    }
    all = QStringList{name, generic, keywords, id}.join(' ');
    genericWords = wordsOf(generic);
    keywordWords = wordsOf(keywords);
    descriptionWords = wordsOf(description);
    allWords = wordsOf(all);
}

// How well an application matches the query's `parts`, negative when it does not. Its name and
// id are matched as the palette matches, letter by letter; its generic name, keywords and comment
// by their words, and count less, as the palette counts a subtitle; words found only across them
// ("firefox browser") count least.
double StartMenu::Searched::score(const QStringList &parts) const {
    const std::tuple<const QString &, double, const QStringList *> fields[] = {
        {name, 1, nullptr}, {generic, 0.8, &genericWords}, {keywords, 0.7, &keywordWords},
        {id, 0.6, nullptr}, {description, 0.5, &descriptionWords}};
    double best = -1;
    for (const auto &[text, weight, words] : fields) {
        if (text.isEmpty() || (words && !startsWords(parts, *words)))
            continue;
        const double value = fuzzy::scoreWords(parts, text);
        if (value >= 0)
            best = std::max(best, value * weight);
    }
    if (best < 0 && startsWords(parts, allWords)) {
        const double value = fuzzy::scoreWords(parts, all);
        if (value >= 0)
            best = value * 0.4;
    }
    return best;
}

StartMenu::StartMenu(QString stateDir, QObject *parent)
    : QObject(parent), stateDir_(stateDir.isEmpty() ? ::stateDir() : std::move(stateDir)),
      history_(stateDir_ + "/launches") {
    QFile pins(stateDir_ + "/start-pinned");
    if (pins.open(QIODevice::ReadOnly | QIODevice::Text)) {
        ownPins_ = true;
        for (const auto &line : QString::fromUtf8(pins.readAll()).split('\n', Qt::SkipEmptyParts))
            if (!line.trimmed().isEmpty() && !pins_.contains(line.trimmed()))
                pins_.push_back(line.trimmed());
    }
    findUser();
    // GIO says once that the installed applications changed, and again only after they have
    // been listed anew. A package manager writes several files: they are read again once it has
    // been quiet for a moment.
    installing_ = new QTimer(this);
    installing_->setSingleShot(true);
    installing_->setInterval(300);
    connect(installing_, &QTimer::timeout, this, &StartMenu::installedChanged);
    monitor_ = g_app_info_monitor_get();
    g_signal_connect_swapped(monitor_, "changed", G_CALLBACK(+[](QTimer *timer) { timer->start(); }),
                             installing_);
    // The monitor speaks through GLib's main loop, which Qt's runs unless it was built without
    // it or told not to (QT_NO_GLIB); then GLib's is turned now and then instead.
    auto *dispatcher = QAbstractEventDispatcher::instance();
    if (dispatcher && !dispatcher->inherits("QEventDispatcherGlib")) {
        auto *poll = new QTimer(this);
        connect(poll, &QTimer::timeout, this, [] { g_main_context_iteration(nullptr, FALSE); });
        poll->start(2000);
    }
}

StartMenu::~StartMenu() {
    g_signal_handlers_disconnect_by_data(monitor_, installing_);
    g_object_unref(monitor_);
}

void StartMenu::setApps(const QVariantList &apps, const QStringList &taskbarPins) {
    apps_ = apps;
    // By section, "#" first, then by name.
    std::vector<std::pair<QString, QVariantMap>> named;
    for (const auto &app : apps) {
        auto record = app.toMap();
        record["letter"] = letterOf(record["name"].toString());
        named.push_back({record["letter"].toString(), record});
    }
    std::stable_sort(named.begin(), named.end(), [](const auto &a, const auto &b) {
        if (a.first != b.first) {
            if (a.first == "#" || b.first == "#")
                return a.first == "#";
            return QString::localeAwareCompare(a.first, b.first) < 0;
        }
        return QString::localeAwareCompare(a.second["name"].toString(), b.second["name"].toString()) < 0;
    });
    sorted_.clear();
    searched_.clear();
    for (const auto &item : named) {
        sorted_.push_back(item.second);
        searched_.emplace_back(item.second);
    }
    if (!ownPins_) {
        QStringList ids;
        for (const auto &app : apps)
            if (!app.toMap().value("configured").toBool())
                ids.push_back(app.toMap().value("appId").toString());
        pins_ = seed(taskbarPins, commonApps(), ids);
    }
    Q_EMIT appsChanged();
    Q_EMIT pinnedChanged();
    Q_EMIT recentChanged();
}

QVariantMap StartMenu::appRecord(const QString &id) const {
    for (const auto &app : apps_)
        if (app.toMap().value("appId") == id)
            return app.toMap();
    return {};
}

bool StartMenu::installed(const QString &id) const {
    const auto record = appRecord(id);
    return !record.isEmpty() && !record["configured"].toBool();
}

QVariantList StartMenu::pinned() const {
    QVariantList list;
    for (const auto &id : pins_)
        if (installed(id))
            list.push_back(appRecord(id));
    return list;
}

QVariantList StartMenu::recent() const {
    QVariantList list;
    for (const auto &entry : history_.entries()) {
        if (!installed(entry.id))
            continue;
        auto record = appRecord(entry.id);
        record["launches"] = entry.count;
        record["launched"] = entry.last;
        list.push_back(record);
        if (list.size() == 12)
            break;
    }
    return list;
}

void StartMenu::record(const QString &id, const QDateTime &when) {
    if (!installed(id))
        return;
    if (!history_.record(id, when))
        Q_EMIT failed("Could not save the launch history: " + history_.error());
    Q_EMIT recentChanged();
}

void StartMenu::preview(const QStringList &pins, const QList<LaunchHistory::Entry> &launches) {
    previewOnly_ = ownPins_ = true;
    pins_ = pins;
    history_ = LaunchHistory();
    auto ordered = launches;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [](const auto &a, const auto &b) { return a.last < b.last; });
    for (const auto &entry : ordered)
        for (int i = 0; i < entry.count; ++i)
            history_.record(entry.id, entry.last);
    Q_EMIT pinnedChanged();
    Q_EMIT recentChanged();
}

void StartMenu::setUser(const QString &name, const QUrl &icon) {
    userSet_ = true;
    userName_ = name;
    userIcon_ = icon;
    Q_EMIT userChanged();
}

void StartMenu::setFiles(FileIndex *files) {
    files_ = files;
    connect(files, &FileIndex::changed, this, [this] {
        ++searchRevision_;
        Q_EMIT searchChanged();
    });
}

bool StartMenu::isPinned(const QString &id) const { return pins_.contains(id); }

void StartMenu::pin(const QString &id) {
    if (!installed(id) || pins_.contains(id))
        return;
    pins_.push_back(id);
    savePins();
}

void StartMenu::unpin(const QString &id) {
    if (!pins_.removeOne(id))
        return;
    savePins();
}

void StartMenu::movePin(const QString &id, const QString &target) {
    const auto from = pins_.indexOf(id), to = pins_.indexOf(target);
    if (from < 0 || to < 0 || from == to)
        return;
    pins_.move(from, to);
    savePins();
}

// The pins are the start menu's own from their first change on.
void StartMenu::savePins() {
    ownPins_ = true;
    Q_EMIT pinnedChanged();
    if (previewOnly_)
        return;
    const auto path = stateDir_ + "/start-pinned";
    QDir().mkpath(stateDir_);
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        for (const auto &id : pins_)
            file.write(id.toUtf8() + '\n');
        if (file.commit())
            return;
    }
    Q_EMIT failed("Could not save the start menu's pins: " + file.errorString());
}

QVariantList StartMenu::search(const QString &query, const QVariantList &others) const {
    if (query.trimmed().isEmpty())
        return {};
    struct Found {
        double score;
        int launches;
        QDateTime last;
        QVariantMap entry;
    };
    // By name to begin with, the order equal matches keep.
    std::vector<Found> apps;
    const auto parts = fuzzy::words(query);
    for (qsizetype i = 0; i < sorted_.size(); ++i) {
        double value = searched_[i].score(parts);
        if (value < 0)
            continue;
        auto app = sorted_[i].toMap();
        // What is launched often breaks a tie, and comes a little ahead of a close match.
        const auto *launched = history_.find(app["appId"].toString());
        if (launched)
            value += std::min(4.0, std::log2(1.0 + launched->count));
        const auto generic = app["genericName"].toString();
        app["kind"] = "app";
        app["title"] = app["name"];
        app["subtitle"] = generic.isEmpty() ? QString("App") : generic;
        app["score"] = value;
        apps.push_back({value, launched ? launched->count : 0, launched ? launched->last : QDateTime(), app});
    }
    std::stable_sort(apps.begin(), apps.end(), [](const Found &a, const Found &b) {
        if (a.score != b.score)
            return a.score > b.score;
        if (a.launches != b.launches)
            return a.launches > b.launches;
        return a.last > b.last;
    });
    // The windows, workspaces and actions, ranked as the palette ranks them.
    QVariantList candidates, windows, actions;
    for (const auto &item : others) {
        const auto kind = item.toMap().value("kind").toString();
        if (kind == "window" || kind == "workspace" || kind == "action")
            candidates.push_back(item);
    }
    for (const auto &item : fuzzy::rank(candidates, query, 40)) {
        auto &group = item.toMap().value("kind") == "window" ? windows : actions;
        if (group.size() < 5)
            group.push_back(item);
    }
    // Files, by their names alone, so never by letters strewn through one; a leading / finds
    // nothing else.
    const auto typed = query.trimmed();
    const bool onlyFiles = typed.startsWith('/');
    QVariantList files;
    if (files_ && (onlyFiles || !QString(">@#%=").contains(typed[0])))
        files = files_->search(onlyFiles ? typed.sliced(1) : typed, onlyFiles ? 30 : 5);
    if (onlyFiles) {
        apps.clear();
        windows.clear();
        actions.clear();
    }
    // The score of a group's first entry, its best; -1 for none.
    auto scoreOf = [](const QVariantList &group) {
        return group.isEmpty() ? -1.0 : group.first().toMap().value("score").toDouble();
    };
    // What matched far worse than the best is left out: letters strewn through a long name.
    const double best = std::max({apps.empty() ? 0.0 : apps.front().score, scoreOf(windows),
                                  scoreOf(actions), scoreOf(files)});
    const double least = best * 0.5;
    std::erase_if(apps, [least](const Found &found) { return found.score < least; });
    auto weak = [least](const QVariant &item) {
        return item.toMap().value("score").toDouble() < least;
    };
    windows.removeIf(weak);
    actions.removeIf(weak);
    QVariantList results;
    auto add = [&results](QVariantMap entry, const char *group) {
        entry["group"] = QString(group);
        results.push_back(entry);
    };
    // The best match is a calculation's value, else the first of whichever group matched best;
    // an application on a tie, a file only when it matched better than the rest.
    const double app = apps.empty() ? -1 : apps.front().score;
    const double file = scoreOf(files);
    if (const auto calc = calculator::entry(query); !calc.isEmpty()) {
        add(calc, "best");
    } else if (app >= 0 && app >= scoreOf(windows) && app >= scoreOf(actions) && app >= file) {
        add(apps.front().entry, "best");
        apps.erase(apps.begin());
    } else if (scoreOf(windows) >= 0 && scoreOf(windows) >= scoreOf(actions) &&
               scoreOf(windows) >= file) {
        add(windows.takeFirst().toMap(), "best");
    } else if (!actions.isEmpty() && scoreOf(actions) >= file) {
        add(actions.takeFirst().toMap(), "best");
    } else if (!files.isEmpty()) {
        add(files.takeFirst().toMap(), "best");
    }
    for (size_t i = 0; i < apps.size() && i < 8; ++i)
        add(apps[i].entry, "apps");
    for (const auto &item : windows)
        add(item.toMap(), "windows");
    for (const auto &item : actions)
        add(item.toMap(), "actions");
    for (const auto &item : files)
        add(item.toMap(), "files");
    if (const auto web = web_search::entry(webSearch_, query); !web.isEmpty())
        add(web, results.isEmpty() ? "best" : "web");
    return results;
}

QString StartMenu::ago(const QDateTime &then, const QDateTime &now) const {
    const qint64 seconds = then.secsTo(now);
    if (seconds < 60)
        return "Just now";
    if (seconds < 3600)
        return QString("%1 min ago").arg(seconds / 60);
    const QDate day = then.toLocalTime().date(), today = now.toLocalTime().date();
    if (day == today)
        return seconds < 7200 ? QString("1 hour ago") : QString("%1 hours ago").arg(seconds / 3600);
    if (day.addDays(1) == today)
        return "Yesterday";
    if (day.daysTo(today) < 7)
        return QLocale().dayName(day.dayOfWeek());
    return QLocale().toString(day, day.year() == today.year() ? "d MMM" : "d MMM yyyy");
}

QStringList StartMenu::seed(const QStringList &taskbarPins, const QStringList &common,
                            const QStringList &installed, int wanted) {
    QStringList pins;
    for (const auto &id : taskbarPins)
        if (installed.contains(id) && !pins.contains(id))
            pins.push_back(id);
    for (const auto &id : common)
        if (pins.size() < wanted && installed.contains(id) && !pins.contains(id))
            pins.push_back(id);
    return pins;
}

QStringList StartMenu::commonApps() {
    QStringList ids;
    auto add = [&ids](GAppInfo *info) {
        if (!info)
            return;
        const char *id = g_app_info_get_id(info);
        if (id && g_app_info_should_show(info) && !ids.contains(QString::fromUtf8(id)))
            ids.push_back(QString::fromUtf8(id));
        g_object_unref(info);
    };
    add(g_app_info_get_default_for_type("x-scheme-handler/https", FALSE));
    add(g_app_info_get_default_for_type("inode/directory", FALSE));
    // A terminal opens no type of file; its entry says what it is.
    GList *all = g_app_info_get_all();
    for (GList *item = all; item; item = item->next) {
        auto *info = G_APP_INFO(item->data);
        const char *categories = G_IS_DESKTOP_APP_INFO(info)
                                     ? g_desktop_app_info_get_categories(G_DESKTOP_APP_INFO(info))
                                     : nullptr;
        if (categories && g_app_info_should_show(info) &&
            QString::fromUtf8(categories).split(';').contains("TerminalEmulator")) {
            add(G_APP_INFO(g_object_ref(info)));
            break;
        }
    }
    g_list_free_full(all, g_object_unref);
    for (const char *type : {"text/plain", "x-scheme-handler/mailto", "image/png", "video/mp4"})
        add(g_app_info_get_default_for_type(type, FALSE));
    return ids;
}

QString StartMenu::letterOf(const QString &name) {
    const QString plain = name.trimmed().normalized(QString::NormalizationForm_KD);
    if (plain.isEmpty() || !plain[0].isLetter())
        return "#";
    return plain.left(1).toUpper();
}

void StartMenu::findUser() {
    if (const passwd *entry = getpwuid(getuid())) {
        // The GECOS field: the full name, then the office, phones and the like after commas.
        const auto full = QString::fromLocal8Bit(entry->pw_gecos).section(',', 0, 0).trimmed();
        userName_ = full.isEmpty() ? QString::fromLocal8Bit(entry->pw_name) : full;
    }
    if (userName_.isEmpty())
        userName_ = qEnvironmentVariable("USER");
    for (const char *name : {"/.face", "/.face.icon"}) {
        const auto path = QDir::homePath() + name;
        if (QFileInfo(path).isFile()) {
            userIcon_ = QUrl::fromLocalFile(path);
            return;
        }
    }
#if PAW_DBUS || PAW_TRAY
    // AccountsService keeps the picture a login screen shows. Asked only while it runs: starting
    // it is not the shell's business.
    auto bus = QDBusConnection::systemBus();
    if (!bus.isConnected())
        return;
    auto find = QDBusMessage::createMethodCall("org.freedesktop.Accounts", "/org/freedesktop/Accounts",
                                               "org.freedesktop.Accounts", "FindUserById");
    find << qlonglong(getuid());
    find.setAutoStartService(false);
    dbus::whenAnswered(bus.asyncCall(find), this, [this, bus](QDBusPendingCallWatcher *call) {
        QDBusPendingReply<QDBusObjectPath> user = *call;
        if (user.isError() || userSet_)
            return;
        auto get = QDBusMessage::createMethodCall("org.freedesktop.Accounts", user.value().path(),
                                                  "org.freedesktop.DBus.Properties", "Get");
        get << QString("org.freedesktop.Accounts.User") << QString("IconFile");
        get.setAutoStartService(false);
        dbus::whenAnswered(bus.asyncCall(get), this, [this](QDBusPendingCallWatcher *call) {
            QDBusPendingReply<QDBusVariant> icon = *call;
            const auto path = icon.isError() ? QString() : icon.value().variant().toString();
            if (userSet_ || !QFileInfo(path).isFile())
                return;
            userIcon_ = QUrl::fromLocalFile(path);
            Q_EMIT userChanged();
        });
    });
#endif
}
