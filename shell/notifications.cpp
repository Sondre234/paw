// SPDX-License-Identifier: GPL-3.0-or-later
#include "notifications.hpp"
#include <QProcess>
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>

NotificationModel::NotificationModel(QObject *parent) : QAbstractListModel(parent) {
    connect(this, &QAbstractItemModel::rowsInserted, this, &NotificationModel::touchGroups);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &NotificationModel::touchGroups);
    connect(this, &QAbstractItemModel::dataChanged, this, &NotificationModel::touchGroups);
    connect(this, &QAbstractItemModel::modelReset, this, &NotificationModel::touchGroups);
}
void NotificationModel::touchGroups() {
    if (groupsPending_)
        return;
    groupsPending_ = true;
    QMetaObject::invokeMethod(this, [this] {
        groupsPending_ = false;
        Q_EMIT groupsChanged();
    }, Qt::QueuedConnection);
}
QVariantList NotificationModel::groups() const {
    struct Group {
        QVariantMap record;
        QVariantList notifications;
    };
    std::vector<Group> groups;
    QHash<QString, size_t> found;
    const auto roles = roleNames();
    for (int row = 0; row < int(items_.size()); ++row) {
        const auto &n = items_[size_t(row)];
        const QString key = n.desktopEntry.isEmpty() ? n.app : n.desktopEntry;
        QVariantMap entry;
        for (auto role = roles.begin(); role != roles.end(); ++role)
            entry.insert(QString::fromUtf8(role.value()), data(index(row), role.key()));
        auto at = found.find(key);
        if (at == found.end()) {
            at = found.insert(key, groups.size());
            groups.push_back({QVariantMap{{"key", key},
                                          {"app", n.app},
                                          {"icon", n.icon},
                                          {"desktopEntry", n.desktopEntry}},
                              {}});
        }
        groups[*at].notifications.push_back(entry);
    }
    QVariantList list;
    for (auto &group : groups) {
        group.record.insert("notifications", group.notifications);
        list.push_back(group.record);
    }
    return list;
}
int NotificationModel::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(items_.size());
}
QHash<int, QByteArray> NotificationModel::roleNames() const {
    return {{IdRole, "notificationId"}, {AppRole, "app"},         {IconRole, "icon"},
            {SummaryRole, "summary"},   {BodyRole, "body"},       {ActionsRole, "actions"},
            {HasDefaultRole, "hasDefault"}, {UrgencyRole, "urgency"}, {ProgressRole, "progress"},
            {HasImageRole, "hasImage"}, {DesktopEntryRole, "desktopEntry"},
            {TimeRole, "time"},         {ReadRole, "read"}};
}
QVariant NotificationModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= int(items_.size()))
        return {};
    const auto &n = items_[size_t(index.row())];
    switch (role) {
    case IdRole: return n.id;
    case AppRole: return n.app;
    case IconRole: return n.icon;
    case SummaryRole: return n.summary;
    case BodyRole: return n.body;
    case ActionsRole: {
        QVariantList list;
        for (const auto &[key, label] : n.actions)
            if (key != "default")
                list.push_back(QVariantMap{{"key", key}, {"label", label}});
        return list;
    }
    case HasDefaultRole:
        return std::any_of(n.actions.begin(), n.actions.end(),
                           [](const auto &action) { return action.first == "default"; });
    case UrgencyRole: return n.urgency;
    case ProgressRole: return n.progress;
    case HasImageRole: return !n.image.isNull();
    case DesktopEntryRole: return n.desktopEntry;
    case TimeRole: return n.time;
    case ReadRole: return n.read;
    default: return {};
    }
}
int NotificationModel::indexOf(uint id) const {
    for (size_t i = 0; i < items_.size(); ++i)
        if (items_[i].id == id)
            return int(i);
    return -1;
}
const Notification *NotificationModel::find(uint id) const {
    int row = indexOf(id);
    return row < 0 ? nullptr : &items_[size_t(row)];
}
void NotificationModel::prepend(Notification notification) {
    beginInsertRows({}, 0, 0);
    items_.insert(items_.begin(), std::move(notification));
    endInsertRows();
    Q_EMIT countChanged();
}
void NotificationModel::replace(int row, Notification notification) {
    items_[size_t(row)] = std::move(notification);
    Q_EMIT dataChanged(index(row), index(row));
}
void NotificationModel::markRead(int row) {
    if (row < 0 || items_[size_t(row)].read)
        return;
    items_[size_t(row)].read = true;
    Q_EMIT dataChanged(index(row), index(row), {ReadRole});
}
void NotificationModel::removeAt(int row) {
    beginRemoveRows({}, row, row);
    items_.erase(items_.begin() + row);
    endRemoveRows();
    Q_EMIT countChanged();
}
void NotificationModel::truncate(int size) {
    if (int(items_.size()) <= size)
        return;
    beginRemoveRows({}, size, int(items_.size()) - 1);
    items_.resize(size_t(size));
    endRemoveRows();
    Q_EMIT countChanged();
}
void NotificationModel::clear() {
    if (items_.empty())
        return;
    beginResetModel();
    items_.clear();
    endResetModel();
    Q_EMIT countChanged();
}

QString notificationMarkup(const QString &body) {
    static const QRegularExpression entity(
        QStringLiteral("^&(amp|lt|gt|quot|apos|nbsp|#[0-9]{1,7}|#[xX][0-9a-fA-F]{1,6});"));
    static const QRegularExpression href(
        QStringLiteral("href\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)')"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression alt(QStringLiteral("alt\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)')"),
                                        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression nameEnd(QStringLiteral("[\\s/]"));
    auto escape = [](QString &out, QChar c) {
        if (c == '&')
            out += QStringLiteral("&amp;");
        else if (c == '<')
            out += QStringLiteral("&lt;");
        else if (c == '>')
            out += QStringLiteral("&gt;");
        else
            out += c;
    };
    QString out;
    QStringList open;
    // Closes the open tags, innermost first, up to the first that `last` says is the last one; an
    // "!a" is a link that was left out, so nothing closes it.
    auto closeUntil = [&](auto last) {
        while (!open.isEmpty()) {
            const QString top = open.takeLast();
            if (top != "!a")
                out += "</" + top + '>';
            if (last(top))
                break;
        }
    };
    for (qsizetype i = 0; i < body.size();) {
        const QChar c = body[i];
        if (c == '<') {
            // The tag ends at the first '>' outside quotes.
            qsizetype end = -1;
            QChar quote;
            for (qsizetype j = i + 1; j < body.size(); ++j) {
                if (!quote.isNull())
                    quote = body[j] == quote ? QChar() : quote;
                else if (body[j] == '"' || body[j] == '\'')
                    quote = body[j];
                else if (body[j] == '>') {
                    end = j;
                    break;
                }
            }
            const QChar next = i + 1 < body.size() ? body[i + 1] : QChar();
            if (end > 0 && (next.isLetter() || next == '/')) {
                QString tag = body.mid(i + 1, end - i - 1);
                const bool closing = tag.startsWith('/');
                if (closing)
                    tag.remove(0, 1);
                const QString name = tag.section(nameEnd, 0, 0).toLower();
                if (name == "b" || name == "i" || name == "u") {
                    if (!closing) {
                        open.push_back(name);
                        out += '<' + name + '>';
                    } else if (open.contains(name)) {
                        // Close what was opened inside it, then reopen nothing: nesting stays valid.
                        closeUntil([&name](const QString &top) { return top == name; });
                    }
                } else if (name == "a") {
                    if (!closing) {
                        const auto match = href.match(tag);
                        const QString target =
                            match.hasMatch()
                                ? (match.captured(1).isNull() ? match.captured(2) : match.captured(1))
                                : QString();
                        const QUrl url(target);
                        const QString scheme = url.scheme().toLower();
                        if (url.isValid() && (scheme == "http" || scheme == "https" || scheme == "mailto")) {
                            open.push_back("a");
                            QString safe = target;
                            safe.replace('&', "&amp;").replace('"', "&quot;").replace('<', "&lt;").replace('>', "&gt;");
                            out += "<a href=\"" + safe + "\">";
                        } else {
                            open.push_back("!a");
                        }
                    } else {
                        auto link = [](const QString &top) { return top == "a" || top == "!a"; };
                        if (std::any_of(open.begin(), open.end(), link))
                            closeUntil(link);
                    }
                } else if (name == "br") {
                    out += QStringLiteral("<br/>");
                } else if (name == "img" && !closing) {
                    const auto match = alt.match(tag);
                    if (match.hasMatch()) {
                        const QString text =
                            match.captured(1).isNull() ? match.captured(2) : match.captured(1);
                        for (QChar letter : text)
                            escape(out, letter);
                    }
                }
                i = end + 1;
                continue;
            }
            out += QStringLiteral("&lt;");
            ++i;
        } else if (c == '&') {
            const auto match = entity.match(body.mid(i, 12));
            if (match.hasMatch()) {
                const QString name = match.captured(1);
                QString decoded;
                if (name == "amp") decoded = "&";
                else if (name == "lt") decoded = "<";
                else if (name == "gt") decoded = ">";
                else if (name == "quot") decoded = "\"";
                else if (name == "apos") decoded = "'";
                else if (name == "nbsp") decoded = " ";
                else {
                    bool ok = false;
                    const uint code = name[1].toLower() == 'x' ? name.mid(2).toUInt(&ok, 16)
                                                                : name.mid(1).toUInt(&ok);
                    if (ok && code >= 32 && code <= 0x10FFFF && !(code >= 0xD800 && code <= 0xDFFF))
                        decoded = QString::fromUcs4(reinterpret_cast<const char32_t *>(&code), 1);
                }
                for (QChar letter : decoded)
                    escape(out, letter);
                i += match.capturedLength();
            } else {
                out += QStringLiteral("&amp;");
                ++i;
            }
        } else if (c == '\n') {
            out += QStringLiteral("<br/>");
            ++i;
        } else if (c == '\r') {
            ++i;
        } else {
            escape(out, c);
            ++i;
        }
    }
    closeUntil([](const QString &) { return false; });
    return out;
}

NotificationCenter::NotificationCenter(QObject *parent) : QObject(parent), cards_(this), history_(this) {}

void NotificationCenter::configure(const paw::NotificationsConfig &config) {
    config_ = config;
    if (!configured_) {
        configured_ = true;
        if (dnd_ != config.dnd) {
            dnd_ = config.dnd;
            Q_EMIT dndChanged();
        }
    }
    history_.truncate(config.history);
    while (cards_.count() > config.max_visible)
        removeCard(cards_.items().back().id, Expired);
    countUnread();
    Q_EMIT configChanged();
}
void NotificationCenter::setDnd(bool on) {
    if (dnd_ == on)
        return;
    dnd_ = on;
    Q_EMIT dndChanged();
}
void NotificationCenter::setServing(bool serving) {
    if (serving_ == serving)
        return;
    serving_ = serving;
    Q_EMIT servingChanged();
}
int NotificationCenter::lifetime(const Notification &notification) const {
    if (notification.urgency == Notification::Critical)
        return 0;
    return notification.timeout < 0 ? config_.timeout : notification.timeout;
}
int NotificationCenter::cardLifetime(uint id) const {
    const Notification *card = cards_.find(id);
    return card ? lifetime(*card) : 0;
}
bool NotificationCenter::timerRunning(uint id) const {
    const auto found = timers_.find(id);
    return found != timers_.end() && found->timer && found->timer->isActive();
}
void NotificationCenter::startTimer(uint id, int milliseconds) {
    stopTimer(id);
    if (milliseconds <= 0)
        return;
    Timer &entry = timers_[id];
    entry.remaining = milliseconds;
    entry.timer = new QTimer(this);
    entry.timer->setSingleShot(true);
    connect(entry.timer, &QTimer::timeout, this, [this, id] { removeCard(id, Expired); });
    if (!held_.contains(id))
        entry.timer->start(milliseconds);
}
void NotificationCenter::stopTimer(uint id) {
    const auto found = timers_.find(id);
    if (found == timers_.end())
        return;
    if (found->timer) {
        found->timer->stop();
        found->timer->deleteLater();
    }
    timers_.erase(found);
}
void NotificationCenter::hold(uint id, bool held) {
    if (!cards_.find(id))
        return;
    if (held)
        held_.insert(id);
    else
        held_.remove(id);
    const auto found = timers_.find(id);
    if (found == timers_.end() || !found->timer)
        return;
    if (held && found->timer->isActive()) {
        found->remaining = std::max(1, found->timer->remainingTime());
        found->timer->stop();
    } else if (!held && !found->timer->isActive()) {
        found->timer->start(found->remaining);
    }
}
uint NotificationCenter::notify(Notification n, uint replacesId) {
    uint id = 0;
    if (replacesId && (history_.find(replacesId) || cards_.find(replacesId)))
        id = replacesId;
    if (!id && !n.tag.isEmpty())
        for (const auto &old : history_.items())
            if (old.app == n.app && old.tag == n.tag) {
                id = old.id;
                break;
            }
    if (!id)
        id = ++lastId_;
    n.id = id;
    n.time = QDateTime::currentDateTime();
    n.read = false;
    // Persistence: keep it in the history unless it says it is not worth keeping.
    const int old = history_.indexOf(id);
    if (!n.transient && config_.history > 0) {
        if (old >= 0)
            history_.replace(old, n);
        else {
            history_.prepend(n);
            history_.truncate(config_.history);
        }
    } else if (old >= 0) {
        history_.removeAt(old);
    }
    const bool show = config_.enabled && (!dnd_ || n.urgency == Notification::Critical);
    const int card = cards_.indexOf(id);
    if (show) {
        const int milliseconds = lifetime(n);
        if (card >= 0)
            cards_.replace(card, n);
        else {
            cards_.prepend(n);
            while (cards_.count() > config_.max_visible)
                removeCard(cards_.items().back().id, Expired);
        }
        startTimer(id, milliseconds);
    } else if (card >= 0) {
        removeCard(id, Expired, false);
    }
    countUnread();
    Q_EMIT received(id);
    return id;
}
void NotificationCenter::removeCard(uint id, uint reason, bool report) {
    const int row = cards_.indexOf(id);
    if (row < 0)
        return;
    stopTimer(id);
    held_.remove(id);
    cards_.removeAt(row);
    if (reason == Dismissed)
        history_.markRead(history_.indexOf(id));
    countUnread();
    if (report)
        Q_EMIT closed(id, reason);
}
bool NotificationCenter::closeFromApplication(uint id) {
    const bool card = cards_.indexOf(id) >= 0;
    const int row = history_.indexOf(id);
    if (!card && row < 0)
        return false;
    removeCard(id, Closed);
    const int again = history_.indexOf(id);
    if (again >= 0)
        history_.removeAt(again);
    countUnread();
    return true;
}
void NotificationCenter::dismiss(uint id) {
    removeCard(id, Dismissed);
    history_.markRead(history_.indexOf(id));
    countUnread();
}
void NotificationCenter::invoke(uint id, const QString &key) {
    const Notification *n = cards_.find(id);
    if (!n)
        n = history_.find(id);
    if (!n)
        return;
    const bool resident = n->resident;
    Q_EMIT actionInvoked(id, key);
    if (resident) {
        history_.markRead(history_.indexOf(id));
        countUnread();
    } else
        dismiss(id);
}
void NotificationCenter::activate(uint id) {
    const Notification *n = cards_.find(id);
    if (!n)
        n = history_.find(id);
    if (!n)
        return;
    const bool hasDefault = std::any_of(n->actions.begin(), n->actions.end(),
                                        [](const auto &action) { return action.first == "default"; });
    if (hasDefault)
        invoke(id, "default");
    else
        dismiss(id);
}
void NotificationCenter::markAllRead() {
    for (int row = 0; row < history_.count(); ++row)
        history_.markRead(row);
    countUnread();
}
void NotificationCenter::clearHistory() {
    history_.clear();
    countUnread();
}
void NotificationCenter::removeFromHistory(uint id) {
    removeCard(id, Dismissed);
    const int row = history_.indexOf(id);
    if (row >= 0)
        history_.removeAt(row);
    countUnread();
}
void NotificationCenter::countUnread() {
    int count = 0;
    for (const auto &n : history_.items())
        count += !n.read;
    if (count != unread_) {
        unread_ = count;
        Q_EMIT unreadChanged();
    }
}
void NotificationCenter::openLink(const QString &target) {
    const QUrl url(target);
    const QString scheme = url.scheme().toLower();
    if (url.isValid() && (scheme == "http" || scheme == "https" || scheme == "mailto"))
        QProcess::startDetached("xdg-open", {url.toString(QUrl::FullyEncoded)});
}
