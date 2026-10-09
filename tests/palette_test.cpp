// SPDX-License-Identifier: GPL-3.0-or-later
// The command palette's fuzzy matching and ranking.
#include "fuzzy.hpp"
#include <QElapsedTimer>
#include <QTest>
#include <QVariantMap>

namespace {
QVariantMap entry(const QString &kind, const QString &title, const QString &subtitle = {}) {
    return {{"kind", kind}, {"title", title}, {"subtitle", subtitle}};
}
QStringList titles(const QVariantList &list) {
    QStringList result;
    for (const auto &item : list)
        result << item.toMap()["title"].toString();
    return result;
}
} // namespace

class PaletteTest : public QObject {
    Q_OBJECT
  private Q_SLOTS:
    void matchesSubsequences() {
        QVERIFY(fuzzy::score("frf", "Firefox") >= 0);
        QVERIFY(fuzzy::score("firefox", "Firefox") > fuzzy::score("fire", "Firefox"));
        QVERIFY(fuzzy::score("xyz", "Firefox") < 0);
        QVERIFY(fuzzy::score("fox", "Fire") < 0);        // letters must come in order
        QVERIFY(fuzzy::score("ffff", "Firefox") < 0);    // and each is used once
        QVERIFY(fuzzy::score("TERM", "kitty terminal") >= 0);  // case does not matter
        QCOMPARE(fuzzy::score("", "anything"), 0.0);
        QCOMPARE(fuzzy::score("  ", "anything"), 0.0);
        QVERIFY(fuzzy::score("abcdefgh", "abc") < 0);    // longer than the text
    }
    void prefersBetterMatches() {
        // A prefix beats the same letters further in, consecutive beats scattered, and the
        // start of a word beats the middle of one.
        QVERIFY(fuzzy::score("term", "Terminal") > fuzzy::score("term", "Xterminal"));
        QVERIFY(fuzzy::score("term", "Terminal") > fuzzy::score("term", "T e r m"));
        QVERIFY(fuzzy::score("no", "Open Notes") > fuzzy::score("no", "Bonobo"));
        QVERIFY(fuzzy::score("gc", "Google Chrome") > fuzzy::score("gc", "Ungrouped acid"));
        QVERIFY(fuzzy::score("gc", "GoogleChrome") > fuzzy::score("gc", "Googlexxxxxxxxxxxxxc"));
        // Between equal matches the shorter text wins.
        QVERIFY(fuzzy::score("code", "Code") > fuzzy::score("code", "Code - a long editor name"));
        // The whole text is the best match.
        QVERIFY(fuzzy::score("mail", "Mail") > fuzzy::score("mail", "Mailbox"));
    }
    void everyWordMustMatch() {
        QVERIFY(fuzzy::score("layout dwin", "Layout: dwindle") >= 0);
        QVERIFY(fuzzy::score("dwin layout", "Layout: dwindle") >= 0);  // in any order
        QVERIFY(fuzzy::score("layout spiral", "Layout: dwindle") < 0);
    }
    void handlesUnusualText() {
        QVERIFY(fuzzy::score("é", "Café") >= 0);
        (void)fuzzy::score("straße", "STRASSE"); // must not crash on folding
        QVERIFY(fuzzy::score("a", QString(5000, 'b') + "a") >= 0);
        QVERIFY(fuzzy::score(QString(200, 'a'), QString(5000, 'a')) >= 0);
    }
    void ranksEntries() {
        const QVariantList entries{
            entry("app", "Terminal", "Application"),
            entry("window", "Terminal - htop", "Window · kitty"),
            entry("action", "Toggle tiling", "Action · toggle_tiling"),
            entry("workspace", "Workspace 2: web", "Switch workspace"),
            entry("session", "Restore session work", "Session · 3 windows"),
        };
        // No words: everything, in the given order.
        QCOMPARE(titles(fuzzy::rank(entries, "")).size(), 5);
        QCOMPARE(titles(fuzzy::rank(entries, ""))[0], QString("Terminal - htop")); // windows first
        // A window ties with the application it is of; the open window comes first.
        QCOMPARE(titles(fuzzy::rank(entries, "termi"))[0], QString("Terminal - htop"));
        QCOMPARE(titles(fuzzy::rank(entries, "termi")).size(), 2);
        // Each says how well it matched, for a search that mixes them with results of its own.
        const auto ranked = fuzzy::rank(entries, "termi");
        QVERIFY(ranked[0].toMap()["score"].toDouble() > ranked[1].toMap()["score"].toDouble());
        QCOMPARE(ranked[1].toMap()["score"].toDouble(), fuzzy::score("termi", "Terminal"));
        // The subtitle is searched too, less weighted.
        QCOMPARE(titles(fuzzy::rank(entries, "kitty")), QStringList{"Terminal - htop"});
        QCOMPARE(titles(fuzzy::rank(entries, "toggle_tiling")), QStringList{"Toggle tiling"});
        // Filters by kind.
        QCOMPARE(titles(fuzzy::rank(entries, ">")), QStringList{"Toggle tiling"});
        QCOMPARE(titles(fuzzy::rank(entries, "@")), QStringList{"Terminal - htop"});
        QCOMPARE(titles(fuzzy::rank(entries, "# web")), QStringList{"Workspace 2: web"});
        QCOMPARE(titles(fuzzy::rank(entries, "%work")), QStringList{"Restore session work"});
        QCOMPARE(titles(fuzzy::rank(entries, "> term")), QStringList{});
        QCOMPARE(titles(fuzzy::rank(entries, "zzz")), QStringList{});
        // The limit keeps the best.
        QVariantList many;
        for (int i = 0; i < 100; ++i)
            many << entry("app", QString("App %1").arg(i));
        many << entry("app", "Zed");
        QCOMPARE(fuzzy::rank(many, "", 10).size(), 10);
        QCOMPARE(titles(fuzzy::rank(many, "zed", 10)), QStringList{"Zed"});
    }
    void urgentWindowsComeFirst() {
        auto window = [](const QString &title, bool urgent) {
            QVariantMap map = entry("window", title, "Window · kitty");
            map["urgent"] = urgent;
            return QVariant(map);
        };
        const QVariantList entries{window("Terminal one", false), window("Terminal two", true),
                                   window("Terminal three", false)};
        // Of equal matches, the one asking for attention is the first; with no words, order rules.
        QCOMPARE(titles(fuzzy::rank(entries, "terminal"))[0], QString("Terminal two"));
        QCOMPARE(titles(fuzzy::rank(entries, "")), (QStringList{"Terminal one", "Terminal two", "Terminal three"}));
    }
    void staysFastWithManyEntries() {
        // The palette ranks on every keystroke; a big desktop has a few hundred applications.
        QVariantList many;
        for (int i = 0; i < 600; ++i)
            many << entry("app", QString("Application number %1 with a rather long title").arg(i),
                          "Action · some_action_name");
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 20; ++i)
            (void)fuzzy::rank(many, "appl num tit");
        const auto perRank = timer.elapsed() / 20.0;
        qInfo() << "rank of 600 entries:" << perRank << "ms";
        QVERIFY2(perRank < 50, "ranking is too slow to type against");
    }
    void validatesSessionNames() {
        QVERIFY(fuzzy::validSessionName("work"));
        QVERIFY(fuzzy::validSessionName("a.b_c-9"));
        QVERIFY(!fuzzy::validSessionName(""));
        QVERIFY(!fuzzy::validSessionName("two words"));
        QVERIFY(!fuzzy::validSessionName(".."));
        QVERIFY(!fuzzy::validSessionName("a/b"));
        QVERIFY(!fuzzy::validSessionName(QString(65, 'a')));
    }
};
QTEST_APPLESS_MAIN(PaletteTest)
#include "palette_test.moc"
