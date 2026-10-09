// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
#include <QVariantList>

// Fuzzy matching for the command palette.
namespace fuzzy {
// How well `text` matches `query` (words separated by spaces, each a case-insensitive
// subsequence of the text): higher is better, negative when a word does not match. Runs of
// consecutive letters and letters starting a word score most.
double score(const QString &query, const QString &text);
// The entries ({kind, title, subtitle, ...}) matching `query`, best first, each with its `score`
// added. A leading > @ # or % keeps only actions, windows, workspaces, or sessions. With no
// words, all of them in their given order (windows, sessions, workspaces, actions, applications
// on a tie).
QVariantList rank(const QVariantList &entries, const QString &query, int limit = 60);
// Whether `name` can name a saved session.
bool validSessionName(const QString &name);
} // namespace fuzzy
