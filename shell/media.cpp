// SPDX-License-Identifier: GPL-3.0-or-later
#include "media.hpp"
#include <algorithm>

Media::Media(QObject *parent) : QObject(parent) {}

Media::Entry *Media::find(const QString &name) {
    auto it = std::find_if(players_.begin(), players_.end(),
                           [&name](const Entry &entry) { return entry.player.name == name; });
    return it == players_.end() ? nullptr : &*it;
}
const Media::Player *Media::current() const {
    if (shown_.empty())
        return nullptr;
    for (const auto *entry : shown_)
        if (entry->player.name == chosen_)
            return &entry->player;
    return &shown_.front()->player;
}
QString Media::field(QString Player::*member) const {
    const auto *player = current();
    return player ? player->*member : QString();
}
QString Media::player() const { return field(&Player::name); }
int Media::index() const {
    const auto *player = current();
    for (size_t i = 0; i < shown_.size(); ++i)
        if (&shown_[i]->player == player)
            return int(i);
    return -1;
}
QString Media::status() const {
    const auto *player = current();
    return player ? player->status : QStringLiteral("Stopped");
}
bool Media::canPlayPause() const {
    const auto *player = current();
    return player && player->canControl && (player->canPlay || player->canPause);
}
bool Media::canGoNext() const {
    const auto *player = current();
    return player && player->canControl && player->canGoNext;
}
bool Media::canGoPrevious() const {
    const auto *player = current();
    return player && player->canControl && player->canGoPrevious;
}
bool Media::canSeek() const {
    const auto *player = current();
    return player && player->canControl && player->canSeek && player->length > 0 &&
           !player->trackId.isEmpty();
}
bool Media::canRaise() const {
    const auto *player = current();
    return player && player->canRaise;
}
double Media::length() const {
    const auto *player = current();
    return player ? double(player->length) / 1000 : 0;
}
QVariantList Media::players() const {
    QVariantList list;
    for (const auto *entry : shown_) {
        const auto &player = entry->player;
        list.push_back(QVariantMap{{"name", player.name},
                                   {"identity", player.identity},
                                   {"desktopEntry", player.desktopEntry},
                                   {"status", player.status},
                                   {"title", player.title},
                                   {"artist", player.artist}});
    }
    return list;
}

// Playing first, the one that started last ahead; then those seen playing, by when they last
// played; then the rest, the newest first, as those already paused or stopped when they came,
// whose last playing is not known. One stopped with no track is not shown.
void Media::sort() {
    shown_.clear();
    for (const auto &entry : players_)
        if (!entry.player.title.isEmpty() || entry.player.status != "Stopped")
            shown_.push_back(&entry);
    auto rank = [](const Entry *entry) {
        return entry->player.status == "Playing" ? 2 : entry->played ? 1 : 0;
    };
    std::stable_sort(shown_.begin(), shown_.end(), [&rank](const Entry *a, const Entry *b) {
        return rank(a) != rank(b) ? rank(a) > rank(b) : a->order > b->order;
    });
}
// Where the entry's player is now, in microseconds: the position last read, moved on by the time
// since at its rate while it plays, within the track.
qint64 Media::now(const Entry &entry) {
    const auto &player = entry.player;
    double at = double(std::max<qint64>(0, player.position));
    if (player.status == "Playing")
        at += double(entry.since.elapsed()) * 1000 * player.rate;
    if (player.length > 0)
        at = std::min(at, double(player.length));
    return qint64(std::max(0.0, at));
}
void Media::setPlayer(Player player) {
    Entry *entry = find(player.name);
    const QString before = this->player();
    const bool known = entry != nullptr;
    if (!entry) {
        players_.push_back(Entry{});
        entry = &players_.back();
        entry->order = ++clock_;
        entry->since.start();
    }
    const bool started = player.status == "Playing" && entry->player.status != "Playing";
    if (started) {
        entry->order = ++clock_;
        entry->played = true;
        // A player that starts playing becomes the current one again.
        if (player.name != chosen_)
            chosen_.clear();
    } else if (entry->player.status == "Playing" && player.status != "Playing") {
        // It played until now.
        entry->order = ++clock_;
    }
    // The position is not announced as it moves: unless the new state gives it, a new track
    // starts at 0, and otherwise the player is where it had got to, from now on at its new
    // status and rate.
    const bool statusChanged = entry->player.status != player.status;
    if (player.position < 0)
        player.position = known && entry->player.trackId != player.trackId ? 0 : now(*entry);
    entry->since.restart();
    entry->player = std::move(player);
    // The vector may have grown; the pointers into it are taken again.
    sort();
    Q_EMIT changed();
    if (statusChanged || before != this->player())
        Q_EMIT positionChanged();
}
void Media::removePlayer(const QString &name) {
    auto it = std::find_if(players_.begin(), players_.end(),
                           [&name](const Entry &entry) { return entry.player.name == name; });
    if (it == players_.end())
        return;
    players_.erase(it);
    if (chosen_ == name)
        chosen_.clear();
    sort();
    Q_EMIT changed();
    Q_EMIT positionChanged();
}
void Media::setPosition(const QString &name, qint64 position) {
    Entry *entry = find(name);
    if (!entry)
        return;
    entry->player.position = position;
    entry->since.restart();
    if (name == player())
        Q_EMIT positionChanged();
}
double Media::position() const {
    for (const auto *entry : shown_)
        if (&entry->player == current())
            return double(now(*entry)) / 1000;
    return 0;
}
void Media::refreshPosition() {
    if (const auto *player = current())
        queryPosition(player->name);
}
void Media::select(const QString &name) {
    if (name == player() || !std::any_of(shown_.begin(), shown_.end(),
                                         [&name](const Entry *entry) { return entry->player.name == name; }))
        return;
    chosen_ = name;
    Q_EMIT changed();
    Q_EMIT positionChanged();
}
void Media::selectNext(int step) {
    const int count = int(shown_.size()), at = index();
    if (count < 2 || at < 0)
        return;
    select(shown_[size_t(((at + step) % count + count) % count)]->player.name);
}
void Media::playPause() {
    if (canPlayPause())
        sendCommand(player(), QStringLiteral("PlayPause"));
}
void Media::next() {
    if (canGoNext())
        sendCommand(player(), QStringLiteral("Next"));
}
void Media::previous() {
    if (canGoPrevious())
        sendCommand(player(), QStringLiteral("Previous"));
}
void Media::stop() {
    const auto *player = current();
    if (player && player->canControl)
        sendCommand(player->name, QStringLiteral("Stop"));
}
void Media::raise() {
    if (canRaise())
        sendCommand(player(), QStringLiteral("Raise"));
}
void Media::seek(double milliseconds) {
    if (!canSeek())
        return;
    const auto *player = current();
    const auto target = std::clamp(qint64(milliseconds * 1000), qint64(0), player->length);
    const QString name = player->name;
    sendPosition(name, player->trackId, target);
    // Shown at once, as the player confirms only by its Seeked signal, if at all.
    setPosition(name, target);
}
bool Media::command(const QString &verb) {
    if (verb == "play-pause")
        playPause();
    else if (verb == "next")
        next();
    else if (verb == "previous")
        previous();
    else if (verb == "stop")
        stop();
    else
        return false;
    return true;
}

#if !PAW_MEDIA
// Without Qt's D-Bus module there are no players to find.
std::unique_ptr<Media> makeMedia() { return std::make_unique<Media>(); }
#endif
