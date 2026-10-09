// SPDX-License-Identifier: GPL-3.0-or-later
#include "web_search.hpp"
#include <QUrl>

QString web_search::url(const QString &engine, const QString &query) {
    return QString(engine).replace("%s", QString::fromLatin1(QUrl::toPercentEncoding(query)));
}

QString web_search::name(const QString &engine) {
    auto host = QUrl(QString(engine).replace("%s", "")).host();
    if (host.startsWith("www."))
        host.remove(0, 4);
    return host;
}

QVariantMap web_search::entry(const QString &engine, const QString &query) {
    const auto words = query.simplified();
    if (engine.isEmpty() || words.isEmpty() || QString(">@#%=/").contains(words[0]))
        return {};
    return {{"kind", "web"},
            {"title", "Search the web for “" + words + "”"},
            {"subtitle", "Web · " + name(engine)},
            {"icon", "web-browser,internet-web-browser,applications-internet"},
            {"target", url(engine, words)}};
}
