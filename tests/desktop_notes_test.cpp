// SPDX-License-Identifier: GPL-3.0-or-later
// The desktop's sticky notes: made in a free block of the grid clear of the icons and the other
// notes, their colours going round, their text cut to its limit, dragged, laid out on a grid that
// shrinks and grows again, deleted and brought back, their checklists, and kept in the state
// folder as JSON written a moment after typing stops, never over a file that could not be read.
#include "desktop_notes.hpp"
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <memory>

class DesktopNotesTest : public QObject {
    Q_OBJECT
    QTemporaryDir home_;
    // The cells the icons are in, which the notes keep clear of.
    QList<QPoint> icons_;

    QString statePath() const { return home_.filePath("state/paw/desktop-notes"); }
    // A model as the shell makes one, on a grid of `columns` by `rows`.
    std::unique_ptr<DesktopNotes> made(int columns = 8, int rows = 5) {
        auto notes = std::make_unique<DesktopNotes>();
        notes->setTaken([this] { return icons_; });
        notes->setGrid(columns, rows);
        return notes;
    }
    static QVariant value(const DesktopNotes &notes, const QString &key, int role) {
        for (int i = 0; i < notes.rowCount(); ++i)
            if (notes.data(notes.index(i), DesktopNotes::KeyRole) == key)
                return notes.data(notes.index(i), role);
        return {};
    }
    static QPoint cell(const DesktopNotes &notes, const QString &key) {
        const auto column = value(notes, key, DesktopNotes::ColumnRole);
        return column.isValid() ? QPoint(column.toInt(), value(notes, key, DesktopNotes::RowRole).toInt())
                                : QPoint(-9, -9);
    }
    static QString text(const DesktopNotes &notes, const QString &key) {
        return value(notes, key, DesktopNotes::TextRole).toString();
    }
    static QStringList keys(const DesktopNotes &notes) {
        QStringList list;
        for (int i = 0; i < notes.rowCount(); ++i)
            list << notes.data(notes.index(i), DesktopNotes::KeyRole).toString();
        return list;
    }
    // The notes the state file holds.
    QJsonArray stored() const {
        QFile file(statePath());
        if (!file.open(QIODevice::ReadOnly))
            return {};
        return QJsonDocument::fromJson(file.readAll()).object().value("notes").toArray();
    }
    bool write(const QString &path, const QByteArray &contents) {
        QDir().mkpath(QFileInfo(path).path());
        QFile file(path);
        return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(contents) == contents.size();
    }

  private Q_SLOTS:
    void initTestCase() {
        QVERIFY(home_.isValid());
        qputenv("HOME", home_.path().toLocal8Bit());
        qputenv("XDG_STATE_HOME", home_.filePath("state").toLocal8Bit());
        qputenv("XDG_DATA_HOME", home_.filePath("data").toLocal8Bit());
    }
    void init() {
        QDir(home_.filePath("state")).removeRecursively();
        // Four icons down the first column and one beside the first.
        icons_ = {{0, 0}, {0, 1}, {0, 2}, {1, 0}};
    }
    void makesNotesInFreeBlocks() {
        auto notes = made();
        // Where it is asked for, two cells by two, in the first colour.
        const auto first = notes->add(3, 1);
        QCOMPARE(first, "note:1");
        QCOMPARE(cell(*notes, first), QPoint(3, 1));
        QCOMPARE(value(*notes, first, DesktopNotes::ColorRole).toString(), "sakura");
        QCOMPARE(value(*notes, first, DesktopNotes::TextRole).toString(), QString());
        QCOMPARE(notes->cells(), (QList<QPoint>{{3, 1}, {3, 2}, {4, 1}, {4, 2}}));
        // Asked for over the icons: the nearest block clear of them, in the next colour.
        const auto second = notes->add(0, 0);
        QCOMPARE(cell(*notes, second), QPoint(1, 1));
        QCOMPARE(value(*notes, second, DesktopNotes::ColorRole).toString(), "sky");
        // Over the grid's edge: the nearest block that fits.
        const auto third = notes->add(7, 4);
        QCOMPARE(cell(*notes, third), QPoint(6, 3));
        QCOMPARE(value(*notes, third, DesktopNotes::ColorRole).toString(), "mint");
        QCOMPARE(notes->count(), 3);
        // The colour fewest notes have comes first, and a number no note has had.
        notes->remove(second);
        const auto fourth = notes->add(4, 3);
        QCOMPARE(fourth, "note:4");
        QCOMPARE(value(*notes, fourth, DesktopNotes::ColorRole).toString(), "sky");
        QCOMPARE(DesktopNotes::colors(), (QStringList{"sakura", "sky", "mint", "lemon", "lavender"}));
        // Before the desktop says how large its grid is, none, and those read have no cells.
        DesktopNotes unplaced;
        QCOMPARE(unplaced.add(0, 0), QString());
        QCOMPARE(unplaced.count(), 3);
        QCOMPARE(cell(unplaced, first), QPoint(-1, -1));
    }
    void cutsTextAndKeepsIt() {
        auto notes = made();
        const auto key = notes->add(4, 0);
        notes->setText(key, "Buy milk\n[ ] eggs");
        QCOMPARE(text(*notes, key), "Buy milk\n[ ] eggs");
        // Written a moment after typing stops.
        QTRY_COMPARE(stored().at(0).toObject().value("text").toString(), "Buy milk\n[ ] eggs");
        // Cut to its limit, never between the two halves of a character outside the BMP.
        notes->setText(key, QString(3000, 'a'));
        QCOMPARE(text(*notes, key).size(), DesktopNotes::MaxLength);
        notes->setText(key, QString(DesktopNotes::MaxLength - 1, 'b') + QString::fromUtf8("\xF0\x9F\x90\xB1"));
        QCOMPARE(text(*notes, key), QString(DesktopNotes::MaxLength - 1, 'b'));
        // What typing has left unsaved is written as the model goes, as when the shell quits.
        notes->setText(key, "last words");
        notes.reset();
        QCOMPARE(stored().at(0).toObject().value("text").toString(), "last words");
        // Read back: its text, colour and place.
        auto again = made();
        QCOMPARE(keys(*again), QStringList{"note:1"});
        QCOMPARE(text(*again, "note:1"), "last words");
        QCOMPARE(cell(*again, "note:1"), QPoint(4, 0));
        QCOMPARE(value(*again, "note:1", DesktopNotes::ColorRole).toString(), "sakura");
        // A colour of the five is written at once; another name changes nothing.
        again->setColor("note:1", "mint");
        QCOMPARE(value(*again, "note:1", DesktopNotes::ColorRole).toString(), "mint");
        QCOMPARE(stored().at(0).toObject().value("color").toString(), "mint");
        again->setColor("note:1", "plaid");
        QCOMPARE(value(*again, "note:1", DesktopNotes::ColorRole).toString(), "mint");
    }
    void dragsNotesClearOfOthers() {
        auto notes = made();
        const auto a = notes->add(4, 0), b = notes->add(4, 3);
        QCOMPARE(cell(*notes, b), QPoint(4, 3));
        QSignalSpy moved(notes.get(), &DesktopNotes::cellsChanged);
        // Two cells left and one down: there, and remembered.
        QCOMPARE(notes->landing(a, -2, 1), (QVariantMap{{"column", 2}, {"row", 1}}));
        notes->move(a, -2, 1);
        QCOMPARE(cell(*notes, a), QPoint(2, 1));
        QCOMPARE(moved.count(), 1);
        QCOMPARE(stored().at(0).toObject().value("column").toInt(), 2);
        QCOMPARE(stored().at(0).toObject().value("row").toInt(), 1);
        // Onto the other note: the nearest block clear of it.
        QCOMPARE(notes->landing(a, 2, 1), (QVariantMap{{"column", 4}, {"row", 1}}));
        // Onto the icons: the nearest block clear of them, its own cells free to it.
        QCOMPARE(notes->landing(a, -2, -1), (QVariantMap{{"column", 1}, {"row", 1}}));
        notes->move(a, -2, -1);
        QCOMPARE(cell(*notes, a), QPoint(1, 1));
        // No room clear of the icons: over them, which go around it.
        icons_.clear();
        for (int column = 0; column < 8; ++column)
            for (int row = 0; row < 5; ++row)
                icons_.push_back({column, row});
        QCOMPARE(notes->landing(a, 5, 0), (QVariantMap{{"column", 6}, {"row", 1}}));
        // A key there is not lands nowhere.
        QVERIFY(notes->landing("note:9", 1, 1).isEmpty());
    }
    void laysOutOnTheGrid() {
        {
            auto notes = made();
            const auto key = notes->add(6, 3);
            // On a smaller grid, the nearest free block for now, its own kept for later.
            notes->setGrid(6, 4);
            QCOMPARE(cell(*notes, key), QPoint(4, 2));
            QCOMPARE(stored().at(0).toObject().value("column").toInt(), 6);
            notes->setGrid(8, 5);
            QCOMPARE(cell(*notes, key), QPoint(6, 3));
        }
        // Two remembered in one place: the first keeps it, the second goes beside it. One of a
        // colour there is not takes the first; one without a place, or with another's number,
        // goes to the top right corner with a number of its own, and that is remembered.
        QVERIFY(write(statePath(), R"({"notes": [
            {"id": 1, "color": "sky", "column": 2, "row": 2, "text": "one"},
            {"id": 2, "color": "plaid", "column": 2, "row": 2, "text": "two"},
            {"id": 2, "text": "three"},
            "not a note"]})"));
        auto notes = made();
        QCOMPARE(keys(*notes), (QStringList{"note:1", "note:2", "note:3"}));
        QCOMPARE(cell(*notes, "note:1"), QPoint(2, 2));
        QCOMPARE(cell(*notes, "note:2"), QPoint(2, 0));
        QCOMPARE(cell(*notes, "note:3"), QPoint(6, 0));
        QCOMPARE(text(*notes, "note:3"), "three");
        QCOMPARE(value(*notes, "note:2", DesktopNotes::ColorRole).toString(), "sakura");
        QCOMPARE(stored().at(2).toObject().value("column").toInt(), 6);
        QCOMPARE(stored().at(2).toObject().value("id").toInt(), 3);
    }
    void deletesAndBringsBack() {
        auto notes = made();
        const auto a = notes->add(4, 0);
        notes->setText(a, "keep me");
        QSignalSpy removed(notes.get(), &DesktopNotes::removedChanged);
        QVERIFY(!notes->restorable());
        notes->remove(a);
        QCOMPARE(notes->count(), 0);
        QVERIFY(notes->restorable());
        QCOMPARE(notes->removedText(), "keep me");
        QCOMPARE(removed.count(), 1);
        QVERIFY(notes->cells().isEmpty());
        QCOMPARE(stored().size(), 0);
        // Its place taken since, it comes back to the nearest free block, in its order.
        const auto b = notes->add(4, 0);
        QCOMPARE(b, "note:2");
        QCOMPARE(notes->restore(), a);
        QCOMPARE(cell(*notes, a), QPoint(2, 0));
        QCOMPARE(text(*notes, a), "keep me");
        QCOMPARE(keys(*notes), (QStringList{a, b}));
        QVERIFY(!notes->restorable());
        QCOMPARE(stored().size(), 2);
        QCOMPARE(notes->restore(), QString());
        // Only the last one deleted comes back.
        notes->remove(a);
        notes->remove(b);
        QCOMPARE(notes->restore(), b);
        QVERIFY(!notes->restorable());
        QCOMPARE(keys(*notes), QStringList{b});
    }
    void ticksChecklists() {
        const auto lines = DesktopNotes::linesOf("Shopping\n[ ] milk\n  - [x] eggs\n* [X]\n[x]bread\n- [ ]  two");
        QCOMPARE(lines.size(), 6);
        auto line = [&](int i) { return lines[i].toMap(); };
        QCOMPARE(line(0), (QVariantMap{{"text", "Shopping"}, {"box", false}, {"checked", false}, {"indent", 0}}));
        QCOMPARE(line(1), (QVariantMap{{"text", "milk"}, {"box", true}, {"checked", false}, {"indent", 0}}));
        QCOMPARE(line(2), (QVariantMap{{"text", "eggs"}, {"box", true}, {"checked", true}, {"indent", 2}}));
        QCOMPARE(line(3), (QVariantMap{{"text", ""}, {"box", true}, {"checked", true}, {"indent", 0}}));
        // A box without a space after it is text.
        QCOMPARE(line(4)["box"].toBool(), false);
        QCOMPARE(line(5)["text"].toString(), " two");
        auto notes = made();
        const auto key = notes->add(4, 0);
        notes->setText(key, "Shopping\n[ ] milk\n  - [x] eggs");
        notes->flush();
        QVERIFY(notes->toggle(key, 1));
        QVERIFY(notes->toggle(key, 2));
        QCOMPARE(text(*notes, key), "Shopping\n[x] milk\n  - [ ] eggs");
        // Written at once.
        QCOMPARE(stored().at(0).toObject().value("text").toString(), "Shopping\n[x] milk\n  - [ ] eggs");
        QVERIFY(!notes->toggle(key, 0));
        QVERIFY(!notes->toggle(key, 3));
        QVERIFY(!notes->toggle("note:9", 1));
    }
    void copiesText() {
        auto notes = made();
        const auto key = notes->add(4, 0);
        notes->setText(key, "call mum\nat six");
        notes->copy(key);
        QCOMPARE(QGuiApplication::clipboard()->text(), "call mum\nat six");
    }
    void keepsAFileItCannotRead() {
        QVERIFY(write(statePath(), "{ not json"));
        auto notes = made();
        QCOMPARE(notes->count(), 0);
        QFile broken(statePath() + ".broken");
        QVERIFY(broken.open(QIODevice::ReadOnly));
        QCOMPARE(broken.readAll(), "{ not json");
        notes->add(4, 0);
        QCOMPARE(stored().size(), 1);
        QVERIFY(QFile::exists(statePath() + ".broken"));
    }
    void writesNothingForAPreview() {
        auto notes = made();
        notes->setRemembering(false);
        const auto key = notes->add(4, 0);
        notes->setText(key, "only here");
        notes.reset();
        QVERIFY(!QFile::exists(statePath()));
    }
};

QTEST_MAIN(DesktopNotesTest)
#include "desktop_notes_test.moc"
