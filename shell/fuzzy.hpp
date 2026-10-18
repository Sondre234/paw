// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QList>
#include <QString>
#include <QVariantList>

// Fuzzy matching for the command palette.
namespace fuzzy {
// How well `text` matches `query` (words separated by spaces, each a case-insensitive
// subsequence of the text): higher is better, negative when a word does not match. Runs of
// consecutive letters and letters starting a word score most.
double score(const QString &query, const QString &text);
// The words of a query as score() matches them, folded to lower case.
QStringList words(const QString &query);
// score() for a query already split into its words(), to match many texts against.
double scoreWords(const QStringList &words, const QString &text);
// Where in `text` the letters of `words` (a query's words()) are as scoreWords() matches them
// best, in order and each once, for a search to show them; empty when a word does not match, or
// when folding the text to lower case changes its length (ß).
QList<int> matches(const QStringList &words, const QString &text);
// The entries ({kind, title, subtitle, ...}) matching `query`, best first, each with its `score`
// added. A leading > @ # or % keeps only actions, windows, workspaces, or sessions. With no
// words, all of them in their given order (windows, sessions, workspaces, actions, applications
// on a tie).
QVariantList rank(const QVariantList &entries, const QString &query, int limit = 60);
// Whether `name` can name a saved session.
bool validSessionName(const QString &name);
} // namespace fuzzy
