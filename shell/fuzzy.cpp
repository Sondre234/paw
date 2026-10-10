// SPDX-License-Identifier: GPL-3.0-or-later
#include "fuzzy.hpp"
#include <QMap>
#include <QRegularExpression>
#include <algorithm>
#include <vector>

QStringList fuzzy::words(const QString &query) {
    static const QRegularExpression space("\\s+");
    return query.toCaseFolded().split(space, Qt::SkipEmptyParts);
}

namespace {
// Best score of `word` as a subsequence of `text`, or a large negative number. Both are folded
// to lower case; `original` is the text as written, for spotting word starts.
double wordScore(const QString &word, const QString &text, const QString &original) {
    const int m = word.size(), n = text.size();
    if (m == 0)
        return 0;
    if (m > n)
        return -1e9;
    const double none = -1e9;
    // before[j]: the best score with the previous letters matched, the last at or before j.
    // The gap between matches costs 2 a letter, folded in by carrying `spread` = best + 2k.
    // Most entries fail here, in one pass.
    for (int i = 0, j = 0; i < m; ++i, ++j) {
        while (j < n && text[j] != word[i])
            ++j;
        if (j == n)
            return none;
    }
    static thread_local std::vector<double> previous, current;
    previous.assign(n, none);
    current.assign(n, none);
    auto boundary = [&](int j) {
        if (j == 0)
            return 10.0;
        const QChar before = original[j - 1], here = original[j];
        if (!before.isLetterOrNumber())
            return 8.0;
        if (before.isLower() && here.isUpper())
            return 6.0;
        return 0.0;
    };
    for (int i = 0; i < m; ++i) {
        std::fill(current.begin(), current.end(), none);
        double spread = none; // max over k <= j - 2 of previous[k] + 2k
        for (int j = 0; j < n; ++j) {
            if (j >= 2 && previous[j - 2] > none / 2)
                spread = std::max(spread, previous[j - 2] + 2.0 * (j - 2));
            if (text[j] != word[i])
                continue;
            const double here = 16 + boundary(j);
            if (i == 0) {
                // Starting late costs, unless it starts a word.
                current[j] = here - (boundary(j) > 0 ? 0 : std::min(j, 8) * 0.75);
                continue;
            }
            double best = none;
            if (j >= 1 && previous[j - 1] > none / 2)
                best = previous[j - 1] + here + 12;
            if (spread > none / 2)
                best = std::max(best, spread - 2.0 * (j - 1) + here);
            current[j] = best;
        }
        std::swap(previous, current);
    }
    double best = none;
    for (double value : previous)
        best = std::max(best, value);
    if (best <= none / 2)
        return none;
    // A word that is the whole text, or its start, beats one found further in.
    if (text == word)
        best += 30;
    else if (text.startsWith(word))
        best += 15;
    return best;
}

int kindOrder(const QString &kind) {
    static const QStringList order{"window", "session", "workspace", "action", "app"};
    const int index = order.indexOf(kind);
    return index < 0 ? order.size() : index;
}
} // namespace

double fuzzy::scoreWords(const QStringList &parts, const QString &text) {
    if (parts.isEmpty())
        return 0;
    const QString folded = text.toCaseFolded();
    // Case folding can change the length (ß); word starts are then judged on the folded text.
    const QString &original = folded.size() == text.size() ? text : folded;
    double total = 0;
    for (const auto &part : parts) {
        const double value = wordScore(part, folded, original);
        if (value < -1e8)
            return -1;
        total += std::max(value, 1.0); // a match, however loose, is never negative
    }
    // Between equal matches the shorter text is the closer one.
    return std::max(0.0, total - std::min<qsizetype>(text.size(), 100) * 0.1);
}

double fuzzy::score(const QString &query, const QString &text) {
    return scoreWords(words(query), text);
}

QVariantList fuzzy::rank(const QVariantList &entries, const QString &rawQuery, int limit) {
    QString query = rawQuery.trimmed();
    QString only;
    if (!query.isEmpty()) {
        static const QMap<QChar, QString> filters{
            {'>', "action"}, {'@', "window"}, {'#', "workspace"}, {'%', "session"}};
        if (filters.contains(query[0])) {
            only = filters[query[0]];
            query = query.sliced(1).trimmed();
        }
    }
    struct Scored {
        double value;
        int kind, position;
        QVariant entry;
    };
    std::vector<Scored> found;
    const QStringList parts = words(query);
    int position = 0;
    for (const auto &item : entries) {
        const auto map = item.toMap();
        const auto kind = map["kind"].toString();
        ++position;
        if (!only.isEmpty() && kind != only)
            continue;
        double value = 0;
        if (!parts.isEmpty()) {
            const double title = scoreWords(parts, map["title"].toString());
            const double subtitle = scoreWords(parts, map["subtitle"].toString());
            if (title < 0 && subtitle < 0)
                continue;
            value = std::max(title, subtitle < 0 ? -1 : subtitle * 0.6);
            if (kind == "window")
                value += 4; // what is open is likelier wanted than what could be opened
            if (map["urgent"].toBool())
                value += 3; // and a window asking for attention likelier than the rest
        }
        found.push_back({value, kindOrder(kind), position, item});
    }
    std::stable_sort(found.begin(), found.end(), [](const Scored &a, const Scored &b) {
        if (a.value != b.value)
            return a.value > b.value;
        if (a.kind != b.kind)
            return a.kind < b.kind;
        return a.position < b.position;
    });
    QVariantList results;
    for (const auto &item : found) {
        if (results.size() >= limit)
            break;
        auto entry = item.entry.toMap();
        entry["score"] = item.value;
        results.push_back(entry);
    }
    return results;
}

bool fuzzy::validSessionName(const QString &name) {
    static const QRegularExpression valid("^[A-Za-z0-9._-]{1,64}$");
    return valid.match(name).hasMatch() && name != "." && name != "..";
}

