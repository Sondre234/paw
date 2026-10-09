// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
#include <QVariantMap>

// The last result of the palette's and the start menu's search: Search the web for “…”, which
// opens the default browser at a search engine's address (shell.search.web).
namespace web_search {
// The address of `engine` (a URL with %s where the words go) searching for `query`, the query
// encoded for a URL; every %s is replaced.
QString url(const QString &engine, const QString &query);
// The engine as the search names it: its host, without a leading www.
QString name(const QString &engine);
// The search's entry for `query`: {kind: "web", title, subtitle, icon, target (the address)}, or
// an empty map without an engine, without words, or for a query that names a kind of result to
// find (it starts with > @ # % = or /).
QVariantMap entry(const QString &engine, const QString &query);
} // namespace web_search
