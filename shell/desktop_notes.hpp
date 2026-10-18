// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QAbstractListModel>
#include <QByteArray>
#include <QList>
#include <QPoint>
#include <QSize>
#include <QStringList>
#include <QTimer>
#include <functional>
#include <optional>

// The sticky notes on the desktop (with shell.desktop_icons): plain text on pastel paper, each
// covering a block of the icons' grid (desktop_grid) two cells wide and two tall, which the icons
// go around (cells(), for DesktopIcons::setBlocked). A note is made or dragged to the free block
// nearest where it is wanted, clear of the icons and the other notes, and keeps that place, its
// text and its colour across sessions in the state folder's desktop-notes, a JSON file written a
// moment after typing stops and at once after anything else. One remembered off a grid that has
// shrunk takes a free block for now, and goes back once the grid grows again. The last note
// deleted can be brought back.
class DesktopNotes : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // How many cells of the grid a note covers, across and down.
    Q_PROPERTY(int columns READ columns CONSTANT)
    Q_PROPERTY(int rows READ rows CONSTANT)
    // The most characters a note holds.
    Q_PROPERTY(int maxLength READ maxLength CONSTANT)
    // The colours of a note's paper, by name, in the order the menu offers them.
    Q_PROPERTY(QStringList colors READ colors CONSTANT)
    // Whether restore() has a note to bring back, and that note's text.
    Q_PROPERTY(bool restorable READ restorable NOTIFY removedChanged)
    Q_PROPERTY(QString removedText READ removedText NOTIFY removedChanged)
  public:
    enum Role {
        KeyRole = Qt::UserRole + 1, // "note:ID"
        IdRole,                     // a number no other note has, which never changes
        TextRole,
        ColorRole, // one of colors()
        ColumnRole, // its top left cell
        RowRole,
    };
    static constexpr int Columns = 2, Rows = 2, MaxLength = 2000;
    explicit DesktopNotes(QObject *parent = nullptr);
    // Writes what typing has left unsaved.
    ~DesktopNotes() override;
    int count() const { return int(notes_.size()); }
    int columns() const { return Columns; }
    int rows() const { return Rows; }
    int maxLength() const { return MaxLength; }
    static QStringList colors();
    bool restorable() const { return removed_.has_value(); }
    QString removedText() const { return removed_ ? removed_->text : QString(); }
    // The grid the desktop has room for, `columns` by `rows` cells, as the icons have it; until it
    // is set no note has a cell (both -1).
    Q_INVOKABLE void setGrid(int columns, int rows);
    // What tells the cells the icons are in, which a note made, moved or brought back keeps clear
    // of where there is room.
    void setTaken(std::function<QList<QPoint>()> taken) { taken_ = std::move(taken); }
    // The cells the notes cover.
    QList<QPoint> cells() const;
    // Makes an empty note in the block whose top left cell is at `column` and `row`, else in the
    // free one nearest it, in the colour the fewest notes have (the first of those); its key, or ""
    // before the grid is known.
    Q_INVOKABLE QString add(int column, int row);
    // Changes the text of `key`'s note, cut to maxLength characters (never between the two halves
    // of a character outside the BMP); saved once typing has stopped for a moment.
    Q_INVOKABLE void setText(const QString &key, const QString &text);
    // Gives `key`'s note one of colors(); another name changes nothing.
    Q_INVOKABLE void setColor(const QString &key, const QString &color);
    // Moves `key`'s note `columns` and `rows` cells, as dragged: to that block, else to the free one
    // nearest it (landing()).
    Q_INVOKABLE void move(const QString &key, int columns, int rows);
    // Where move() would put it: {column, row} of its top left cell; empty for a key there is not.
    Q_INVOKABLE QVariantMap landing(const QString &key, int columns, int rows) const;
    // Ticks the box of line `line` (from 0) of `key`'s note, or clears it: a line that starts
    // with "[ ]" or "[x]", after spaces and a "- ", "* " or "+ " (lines()). False for another line.
    Q_INVOKABLE bool toggle(const QString &key, int line);
    // Puts the text of `key`'s note on the clipboard.
    Q_INVOKABLE void copy(const QString &key);
    // Deletes `key`'s note, which restore() can bring back until another is deleted.
    Q_INVOKABLE void remove(const QString &key);
    // Brings the last note deleted back, to its place, else to the free block nearest it; its
    // key, or "" when there is none.
    Q_INVOKABLE QString restore();
    // The lines of `text` as a note shows them: {text, box, checked, indent} for each, `box`
    // whether it is a line of a checklist ("[ ] milk", "- [x] eggs"), whose `text` is what follows
    // its box, `indent` the spaces before it.
    Q_INVOKABLE QVariantList lines(const QString &text) const { return linesOf(text); }
    static QVariantList linesOf(const QString &text);
    // Writes now what is waiting to be saved.
    void flush();
    // Whether the notes are written to the state folder; a preview's are not.
    void setRemembering(bool remembering) { remembering_ = remembering; }
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
  Q_SIGNALS:
    void countChanged();
    // cells() changed: a note was made, moved, deleted or brought back, or the grid moved one.
    void cellsChanged();
    // remove() took a note, or restore() brought it back.
    void removedChanged();
    // Something asked of it went wrong, in a sentence for the panel.
    void failed(const QString &message);

  private:
    struct Note {
        int id = 0;
        QString text, color;
        QPoint saved{-1, -1}; // where it was put, as remembered
        QPoint cell{-1, -1};  // where it is: there, or a free block while that is off the grid
    };
    QList<Note> notes_;                // by id
    std::optional<Note> removed_;      // what restore() brings back
    QSize grid_;                       // invalid until the desktop says
    std::function<QList<QPoint>()> taken_;
    bool remembering_ = true;
    QByteArray saved_;                 // what the state file holds, so as to write it on a change
    QTimer saveSoon_;                  // typing settling before it is written
    int indexOf(const QString &key) const;
    // The cells of the notes but the one at `skip`.
    QList<QPoint> covered(int skip = -1) const;
    // The top left cell of the block for a note wanted at `wanted`, the cells `notes` taken by
    // the others: there, or the free block nearest it clear of the icons too, else of the notes
    // alone, else `wanted` brought onto the grid.
    QPoint place(QPoint wanted, const QList<QPoint> &notes) const;
    void relayout();
    void changed(int i, const QList<int> &roles);
    void save();
};
