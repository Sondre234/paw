// SPDX-License-Identifier: GPL-3.0-or-later
// The emoji picker's data and model: the vendored table read whole, its groups, names, keywords
// and skin tones, the search, the tone and the recent emoji kept, and what is picked asked to be
// typed once the picker gives the keyboard back.
#include "emoji.hpp"
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

namespace {
QStringList texts(const QVariantList &list, int count = -1) {
    QStringList result;
    for (const auto &item : list) {
        if (count >= 0 && result.size() == count)
            break;
        result << item.toMap()["text"].toString();
    }
    return result;
}
} // namespace

class EmojiTest : public QObject {
    Q_OBJECT
    const QString table = PAW_EMOJI_TABLE;
    QTemporaryDir state;

  private Q_SLOTS:
    void readsTheTable() {
        QStringList groups;
        const auto emoji = EmojiPicker::read(table, &groups);
        QCOMPARE(groups, QStringList({"Smileys & Emotion", "People & Body", "Animals & Nature",
                                      "Food & Drink", "Travel & Places", "Activities", "Objects",
                                      "Symbols", "Flags"}));
        QVERIFY2(emoji.size() > 1800, qPrintable(QString::number(emoji.size())));
        QCOMPARE(emoji.front().text, QString("😀"));
        QCOMPARE(emoji.front().name, QString("grinning face"));
        QCOMPARE(emoji.front().group, 0);
        auto find = [&](const QString &text) {
            return std::find_if(emoji.begin(), emoji.end(),
                                [&text](const auto &item) { return item.text == text; });
        };
        const auto thumbs = find("👍");
        QVERIFY(thumbs != emoji.end());
        QCOMPARE(thumbs->name, QString("thumbs up"));
        QVERIFY(thumbs->keywords.contains("+1"));
        QCOMPARE(thumbs->tones, QStringList({"👍🏻", "👍🏼", "👍🏽", "👍🏾", "👍🏿"}));
        QCOMPARE(thumbs->group, 1);
        // Sequences: a flag, a family, a profession with a skin tone throughout.
        QCOMPARE(find("🇳🇴")->name, QString("flag: Norway"));
        QCOMPARE(find("🇳🇴")->group, 8);
        QVERIFY(find("👨‍👩‍👧‍👦") != emoji.end());
        QVERIFY(find("🧑‍🚀")->tones.value(2) == "🧑🏽‍🚀");
        // The components (skin tones alone) and the tone forms are no entries of their own.
        QVERIFY(find("🏽") == emoji.end());
        QVERIFY(find("👍🏽") == emoji.end());
        // A table that is not there is no table.
        QVERIFY(EmojiPicker::read(state.filePath("missing.tsv")).empty());
    }

    void searches() {
        EmojiPicker picker(table, state.filePath("search"));
        QVERIFY(picker.size() > 1800);
        QCOMPARE(texts(picker.group(0), 3), QStringList({"😀", "😃", "😄"}));
        QCOMPARE(texts(picker.search("thumbs up"), 1), QStringList{"👍"});
        QCOMPARE(texts(picker.search("thumbs"), 2), QStringList({"👍", "👎"}));
        QCOMPARE(texts(picker.search("Norway"), 1), QStringList{"🇳🇴"});
        QCOMPARE(texts(picker.search("+1"), 1), QStringList{"👍"});
        // By keyword, a word's start, in any case.
        QVERIFY(texts(picker.search("lol")).contains("😂"));
        QVERIFY(texts(picker.search("ROFL")).contains("🤣"));
        QVERIFY(texts(picker.search("pizz")).contains("🍕"));
        // Every word must match.
        QCOMPARE(picker.search("pizza zebra"), QVariantList());
        QCOMPARE(picker.search("zzqqx"), QVariantList());
        QCOMPARE(picker.search("  "), QVariantList());
        // An emoji pasted finds itself first, without its presentation selector too.
        QCOMPARE(texts(picker.search("👍🏽"), 1), QStringList{"👍"});
        QCOMPARE(texts(picker.search("✈"), 1), QStringList{"✈️"});
        QCOMPARE(picker.search("heart", 5).size(), 5);
        const auto first = picker.search("grinning face").value(0).toMap();
        QCOMPARE(first["text"].toString(), QString("😀"));
        QCOMPARE(first["name"].toString(), QString("grinning face"));
    }

    void keepsTheTone() {
        const auto dir = state.filePath("tone");
        {
            EmojiPicker picker(table, dir);
            QCOMPARE(picker.tone(), 0);
            QSignalSpy changed(&picker, &EmojiPicker::toneChanged);
            picker.setTone(3);
            picker.setTone(3);
            QCOMPARE(changed.size(), 1);
            QCOMPARE(texts(picker.search("thumbs up"), 1), QStringList{"👍🏽"});
            const auto people = texts(picker.group(1));
            QVERIFY(people.contains("👍🏽") && !people.contains("👍"));
            // What takes no tone stays as it is.
            QCOMPARE(texts(picker.group(0), 1), QStringList{"😀"});
            picker.setTone(9);
            QCOMPARE(picker.tone(), 5);
            picker.setTone(3);
        }
        EmojiPicker again(table, dir);
        QCOMPARE(again.tone(), 3);
    }

    void picks() {
        const auto dir = state.filePath("pick");
        EmojiPicker picker(table, dir);
        QSignalSpy opened(&picker, &EmojiPicker::openChanged);
        QSignalSpy typed(&picker, &EmojiPicker::typeRequested);
        picker.toggle("DP-1");
        QCOMPARE(picker.output(), QString("DP-1"));
        picker.toggle("DP-2");
        QCOMPARE(picker.output(), QString("DP-2"));
        picker.toggle("DP-2");
        QCOMPARE(picker.output(), QString());
        picker.toggle("DP-1");
        // Picked, it closes, and is typed once the keyboard is back.
        picker.pick("😀");
        QCOMPARE(picker.output(), QString());
        QCOMPARE(typed.size(), 0);
        picker.keyboardReleased();
        QCOMPARE(typed.size(), 1);
        QCOMPARE(typed.takeFirst().value(0).toString(), QString("😀"));
        picker.keyboardReleased();
        QCOMPARE(typed.size(), 0);
        // Or a moment later when the keyboard was not heard of.
        picker.pick("👍🏽");
        QVERIFY(typed.wait(2000));
        QCOMPARE(typed.takeFirst().value(0).toString(), QString("👍🏽"));
        // The most recent first, once each, kept, at most 32.
        picker.pick("😀");
        QCOMPARE(texts(picker.recent()), QStringList({"😀", "👍🏽"}));
        QCOMPARE(picker.recent().value(1).toMap()["name"].toString(), QString("thumbs up"));
        for (const auto &item : picker.group(3))
            picker.pick(item.toMap()["text"].toString());
        QCOMPARE(picker.recent().size(), 32);
        EmojiPicker again(table, dir);
        QCOMPARE(texts(again.recent()), texts(picker.recent()));
        // A preview's are its own.
        again.preview({"🎉"}, 2);
        QCOMPARE(texts(again.recent()), QStringList{"🎉"});
        QCOMPARE(again.tone(), 2);
        again.pick("🍕");
        EmojiPicker third(table, dir);
        QCOMPARE(texts(third.recent()), texts(picker.recent()));
    }
};

QTEST_GUILESS_MAIN(EmojiTest)
#include "emoji_test.moc"
