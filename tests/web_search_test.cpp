// SPDX-License-Identifier: GPL-3.0-or-later
// The search's last result, Search the web for “…”: the engine's address with the words encoded.
#include "web_search.hpp"
#include <QTest>

class WebSearchTest : public QObject {
    Q_OBJECT
  private Q_SLOTS:
    void encodesTheWords() {
        const QString engine = "https://duckduckgo.com/?q=%s";
        QCOMPARE(web_search::url(engine, "wayland"), QString("https://duckduckgo.com/?q=wayland"));
        QCOMPARE(web_search::url(engine, "c++ tips & tricks"),
                 QString("https://duckduckgo.com/?q=c%2B%2B%20tips%20%26%20tricks"));
        QCOMPARE(web_search::url(engine, "100% #1 a=b?/"),
                 QString("https://duckduckgo.com/?q=100%25%20%231%20a%3Db%3F%2F"));
        QCOMPARE(web_search::url(engine, "smørbrød 東京"),
                 QString("https://duckduckgo.com/?q=sm%C3%B8rbr%C3%B8d%20%E6%9D%B1%E4%BA%AC"));
        // Every %s, and words that hold one themselves are not replaced again.
        QCOMPARE(web_search::url("https://example.org/%s/search?q=%s", "%s"),
                 QString("https://example.org/%25s/search?q=%25s"));
    }
    void namesTheEngine() {
        QCOMPARE(web_search::name("https://duckduckgo.com/?q=%s"), QString("duckduckgo.com"));
        QCOMPARE(web_search::name("https://www.google.com/search?q=%s"), QString("google.com"));
        QCOMPARE(web_search::name("https://search.brave.com/search?q=%s"),
                 QString("search.brave.com"));
    }
    void makesTheEntry() {
        const QString engine = "https://www.startpage.com/do/search?q=%s";
        const auto entry = web_search::entry(engine, "  release   notes ");
        QCOMPARE(entry["kind"].toString(), QString("web"));
        QCOMPARE(entry["title"].toString(), QString("Search the web for “release notes”"));
        QCOMPARE(entry["subtitle"].toString(), QString("Web · startpage.com"));
        QCOMPARE(entry["target"].toString(),
                 QString("https://www.startpage.com/do/search?q=release%20notes"));
        QVERIFY(!entry["icon"].toString().isEmpty());
        // None without an engine or words, nor for a search of one kind of result.
        QVERIFY(web_search::entry("", "release notes").isEmpty());
        QVERIFY(web_search::entry(engine, "   ").isEmpty());
        for (const char *query : {">tiling", "@firefox", "#2", "%work", "=2+2", "/report"})
            QVERIFY2(web_search::entry(engine, query).isEmpty(), query);
    }
};

QTEST_APPLESS_MAIN(WebSearchTest)
#include "web_search_test.moc"
