// SPDX-License-Identifier: GPL-3.0-or-later
#include "emoji.hpp"
#include "state_files.hpp"
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <algorithm>
#include <utility>

namespace {
constexpr int recentLimit = 32;

// The words of a name, folded to lower case: "flag: Norway" is "flag" and "norway".
QStringList wordsOf(const QString &text) {
    static const QRegularExpression separators("[^\\w+#*]+");
    return text.toCaseFolded().split(separators, Qt::SkipEmptyParts);
}

// An emoji as typed or pasted may lack the selector that asks for emoji presentation.
QString plain(QString text) { return text.remove(QChar(0xfe0f)); }
} // namespace

EmojiPicker::EmojiPicker(QString table, QString stateDir, QObject *parent)
    : QObject(parent), stateDir_(stateDir.isEmpty() ? ::stateDir() : std::move(stateDir)) {
    emoji_ = read(table.isEmpty() ? QStringLiteral(":/paw/emoji.tsv") : table, &groups_);
    typing_ = new QTimer(this);
    typing_->setSingleShot(true);
    // A picker that never held the keyboard says nothing as it closes; this is late enough for
    // the window under it to have the keyboard back anyway.
    typing_->setInterval(500);
    connect(typing_, &QTimer::timeout, this, &EmojiPicker::typePending);
    QFile state(stateDir_ + "/emoji");
    if (state.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const auto lines = QString::fromUtf8(state.readAll()).split('\n', Qt::SkipEmptyParts);
        for (const auto &line : lines) {
            if (line.startsWith("tone "))
                tone_ = std::clamp(line.sliced(5).toInt(), 0, 5);
            else if (recent_.size() < recentLimit && !recent_.contains(line.trimmed()))
                recent_.push_back(line.trimmed());
        }
    }
}

std::vector<EmojiPicker::Emoji> EmojiPicker::read(const QString &path, QStringList *groups) {
    std::vector<Emoji> list;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return list;
    int group = -1;
    for (const auto &line : QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts)) {
        if (line.startsWith('#'))
            continue;
        if (line.startsWith('@')) {
            ++group;
            if (groups)
                groups->push_back(line.sliced(1));
            continue;
        }
        const auto fields = line.split('\t');
        if (fields.size() < 2 || group < 0 || fields[0].isEmpty())
            continue;
        Emoji emoji;
        emoji.text = fields[0];
        emoji.name = fields[1];
        emoji.keywords = fields.value(2).split('|', Qt::SkipEmptyParts);
        emoji.tones = fields.value(3).split(' ', Qt::SkipEmptyParts);
        if (emoji.tones.size() != 5)
            emoji.tones.clear();
        emoji.group = group;
        emoji.nameWords = wordsOf(emoji.name);
        for (const auto &keyword : emoji.keywords)
            emoji.foldedKeywords.push_back(keyword.toCaseFolded());
        list.push_back(std::move(emoji));
    }
    return list;
}

QVariantMap EmojiPicker::entry(const Emoji &emoji) const {
    return {{"text", tone_ > 0 && !emoji.tones.isEmpty() ? emoji.tones[tone_ - 1] : emoji.text},
            {"name", emoji.name}};
}

const EmojiPicker::Emoji *EmojiPicker::find(const QString &text) const {
    if (forms_.isEmpty())
        for (qsizetype i = 0; i < qsizetype(emoji_.size()); ++i) {
            // A form two emoji share is the first one's.
            forms_.tryEmplace(plain(emoji_[i].text), i);
            for (const auto &form : emoji_[i].tones)
                forms_.tryEmplace(plain(form), i);
        }
    const auto found = forms_.constFind(plain(text));
    return found == forms_.cend() ? nullptr : &emoji_[found.value()];
}

QVariantList EmojiPicker::recent() const {
    QVariantList list;
    for (const auto &text : recent_) {
        const Emoji *emoji = find(text);
        list.push_back(QVariantMap{{"text", text}, {"name", emoji ? emoji->name : QString()}});
    }
    return list;
}

void EmojiPicker::setTone(int tone) {
    tone = std::clamp(tone, 0, 5);
    if (tone == tone_)
        return;
    tone_ = tone;
    save();
    Q_EMIT toneChanged();
}

QVariantList EmojiPicker::group(int index) const {
    QVariantList list;
    for (const auto &emoji : emoji_)
        if (emoji.group == index)
            list.push_back(entry(emoji));
    return list;
}

QVariantList EmojiPicker::search(const QString &query, int limit) const {
    const auto trimmed = query.trimmed();
    if (trimmed.isEmpty())
        return {};
    QVariantList results;
    const Emoji *itself = find(trimmed);
    if (itself)
        results.push_back(entry(*itself));
    static const QRegularExpression space("\\s+");
    const auto parts = trimmed.toCaseFolded().split(space, Qt::SkipEmptyParts);
    struct Hit {
        int score;
        size_t index;
    };
    std::vector<Hit> hits;
    for (size_t i = 0; i < emoji_.size(); ++i) {
        const auto &emoji = emoji_[i];
        int total = 0;
        for (const auto &part : parts) {
            int best = 0;
            for (const auto &word : emoji.nameWords)
                best = std::max(best, word == part ? 6 : word.startsWith(part) ? 4 : 0);
            for (const auto &keyword : emoji.foldedKeywords)
                best = std::max(best, keyword == part ? 3 : keyword.startsWith(part) ? 2 : 0);
            if (!best && emoji.name.contains(part, Qt::CaseInsensitive))
                best = 1;
            if (!best) {
                total = -1;
                break;
            }
            total += best;
        }
        if (total < 0)
            continue;
        if (emoji.name.compare(trimmed, Qt::CaseInsensitive) == 0)
            total += 10;
        hits.push_back({total, i});
    }
    // Among equal matches, Unicode's order.
    std::stable_sort(hits.begin(), hits.end(),
                     [](const Hit &a, const Hit &b) { return a.score > b.score; });
    for (const auto &hit : hits) {
        if (results.size() >= limit)
            break;
        if (&emoji_[hit.index] != itself)
            results.push_back(entry(emoji_[hit.index]));
    }
    return results;
}

void EmojiPicker::toggle(const QString &output) {
    output_ = output_ == output ? QString() : output;
    Q_EMIT openChanged();
}

void EmojiPicker::close() {
    if (output_.isEmpty())
        return;
    output_.clear();
    Q_EMIT openChanged();
}

void EmojiPicker::pick(const QString &text) {
    if (text.isEmpty())
        return;
    recent_.removeAll(text);
    recent_.prepend(text);
    while (recent_.size() > recentLimit)
        recent_.removeLast();
    save();
    Q_EMIT recentChanged();
    pending_ = text;
    close();
    typing_->start();
}

void EmojiPicker::keyboardReleased() {
    if (!pending_.isEmpty())
        typePending();
}

void EmojiPicker::typePending() {
    typing_->stop();
    const auto text = std::exchange(pending_, {});
    if (!text.isEmpty())
        Q_EMIT typeRequested(text);
}

void EmojiPicker::preview(const QStringList &recent, int tone) {
    previewOnly_ = true;
    recent_ = recent;
    tone_ = std::clamp(tone, 0, 5);
    Q_EMIT recentChanged();
    Q_EMIT toneChanged();
}

void EmojiPicker::save() const {
    if (previewOnly_)
        return;
    QDir().mkpath(stateDir_);
    QSaveFile file(stateDir_ + "/emoji");
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    file.write(QString("tone %1\n").arg(tone_).toUtf8());
    for (const auto &text : recent_)
        file.write(text.toUtf8() + '\n');
    file.commit();
}
