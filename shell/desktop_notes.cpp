// SPDX-License-Identifier: GPL-3.0-or-later
#include "desktop_notes.hpp"
#include "desktop_grid.hpp"
#include "state_files.hpp"
#include <QClipboard>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QSignalBlocker>
#include <algorithm>
#include <limits>
#include <utility>

namespace {
// Where the notes are kept, as JSON: {"notes": [{"id", "color", "column", "row", "text"}, ...]},
// the column and row those of each note's top left cell.
QString statePath() { return stateDir() + "/desktop-notes"; }
QString keyOf(int id) { return "note:" + QString::number(id); }
// `text` cut to `length` characters, never between the two halves of a surrogate pair.
QString cut(const QString &text, int length) {
    if (text.size() <= length)
        return text;
    return text.first(length > 0 && text.at(length - 1).isHighSurrogate() ? length - 1 : length);
}
// A line of a checklist: the spaces before it, a bullet, the box with its mark, and its text.
const QRegularExpression &checkbox() {
    static const QRegularExpression box(R"(^(\s*)((?:[-*+]\s+)?)\[([ xX])\](?:\s(.*))?$)");
    return box;
}
} // namespace

DesktopNotes::DesktopNotes(QObject *parent) : QAbstractListModel(parent) {
    saveSoon_.setSingleShot(true);
    saveSoon_.setInterval(400);
    connect(&saveSoon_, &QTimer::timeout, this, &DesktopNotes::save);
    QFile file(statePath());
    if (!file.open(QIODevice::ReadOnly))
        return;
    saved_ = file.readAll();
    QJsonParseError error{};
    const auto document = QJsonDocument::fromJson(saved_, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        // What cannot be read is someone's notes all the same: it is kept beside the file, which
        // the next note written takes the place of.
        if (!saved_.trimmed().isEmpty()) {
            QFile::remove(statePath() + ".broken");
            QFile::copy(statePath(), statePath() + ".broken");
            qWarning("paw-shell: the desktop's notes could not be read (%s), kept as %s",
                     qPrintable(error.errorString()), qPrintable(statePath() + ".broken"));
        }
        return;
    }
    QSet<int> ids;
    for (const auto &value : document.object().value("notes").toArray()) {
        if (!value.isObject())
            continue;
        const auto object = value.toObject();
        Note note;
        note.id = object.value("id").toInt();
        note.text = cut(object.value("text").toString(), MaxLength);
        note.color = object.value("color").toString();
        if (!colors().contains(note.color))
            note.color = colors().first();
        const auto column = object.value("column"), row = object.value("row");
        if (column.isDouble() && row.isDouble() && column.toInt() >= 0 && row.toInt() >= 0)
            note.saved = QPoint(column.toInt(), row.toInt());
        // One without an id, or with another's, is given a new one below.
        if (note.id <= 0 || ids.contains(note.id))
            note.id = 0;
        ids.insert(note.id);
        notes_.push_back(note);
    }
    int next = ids.isEmpty() ? 1 : *std::max_element(ids.cbegin(), ids.cend()) + 1;
    for (auto &note : notes_)
        if (note.id == 0)
            note.id = next++;
    std::stable_sort(notes_.begin(), notes_.end(), [](const Note &a, const Note &b) { return a.id < b.id; });
}

DesktopNotes::~DesktopNotes() {
    // Whoever heard of a failure may be going too.
    const QSignalBlocker quiet(this);
    flush();
}

QStringList DesktopNotes::colors() { return {"sakura", "sky", "mint", "lemon", "lavender"}; }

void DesktopNotes::setGrid(int columns, int rows) {
    const QSize grid(columns, rows);
    if (grid == grid_ || columns <= 0 || rows <= 0)
        return;
    grid_ = grid;
    relayout();
}

QList<QPoint> DesktopNotes::cells() const { return covered(); }

QList<QPoint> DesktopNotes::covered(int skip) const {
    QList<QPoint> cells;
    for (qsizetype i = 0; i < notes_.size(); ++i)
        if (i != skip && notes_[i].cell.x() >= 0)
            cells += desktop_grid::block(notes_[i].cell, QSize(Columns, Rows));
    return cells;
}

int DesktopNotes::indexOf(const QString &key) const {
    for (qsizetype i = 0; i < notes_.size(); ++i)
        if (keyOf(notes_[i].id) == key)
            return int(i);
    return -1;
}

QPoint DesktopNotes::place(QPoint wanted, const QList<QPoint> &notes) const {
    const QSize size(Columns, Rows);
    const auto icons = taken_ ? taken_() : QList<QPoint>{};
    if (const auto free = desktop_grid::freeBlock(grid_, wanted, size, notes + icons))
        return *free;
    if (const auto free = desktop_grid::freeBlock(grid_, wanted, size, notes))
        return *free;
    return {std::clamp(wanted.x(), 0, std::max(0, grid_.width() - Columns)),
            std::clamp(wanted.y(), 0, std::max(0, grid_.height() - Rows))};
}

void DesktopNotes::relayout() {
    if (!grid_.isValid())
        return;
    const auto before = cells();
    const QSize size(Columns, Rows);
    QList<QPoint> placed;
    bool remembered = false;
    for (qsizetype i = 0; i < notes_.size(); ++i) {
        auto &note = notes_[i];
        // Its own block while that is on the grid and no note before it has it, the icons going
        // around it; else a free block for now, its own remembered for when the grid grows. One
        // without a place of its own goes to the top right corner, away from where icons start.
        QPoint cell;
        if (note.saved.x() >= 0 && desktop_grid::freeBlock(grid_, note.saved, size, placed) == note.saved) {
            cell = note.saved;
        } else {
            cell = place(note.saved.x() >= 0 ? note.saved : QPoint(grid_.width() - Columns, 0), placed);
            if (note.saved.x() < 0) {
                note.saved = cell;
                remembered = true;
            }
        }
        placed += desktop_grid::block(cell, size);
        if (note.cell != cell) {
            note.cell = cell;
            changed(int(i), {ColumnRole, RowRole});
        }
    }
    if (cells() != before)
        Q_EMIT cellsChanged();
    if (remembered)
        save();
}

QString DesktopNotes::add(int column, int row) {
    if (!grid_.isValid())
        return {};
    Note note;
    // A number no note has had while it can come back.
    for (const auto &other : notes_)
        note.id = std::max(note.id, other.id);
    note.id = std::max(note.id, removed_ ? removed_->id : 0) + 1;
    // The colour the fewest notes have, the first of those.
    int fewest = std::numeric_limits<int>::max();
    for (const auto &color : colors()) {
        const auto uses = std::count_if(notes_.cbegin(), notes_.cend(), [&](const Note &n) { return n.color == color; });
        if (uses < fewest) {
            fewest = int(uses);
            note.color = color;
        }
    }
    note.cell = note.saved = place(QPoint(column, row), covered());
    beginInsertRows({}, count(), count());
    notes_.push_back(note);
    endInsertRows();
    Q_EMIT countChanged();
    Q_EMIT cellsChanged();
    save();
    return keyOf(note.id);
}

void DesktopNotes::setText(const QString &key, const QString &text) {
    const int i = indexOf(key);
    const auto kept = cut(text, MaxLength);
    if (i < 0 || notes_[i].text == kept)
        return;
    notes_[i].text = kept;
    changed(i, {TextRole});
    saveSoon_.start();
}

void DesktopNotes::setColor(const QString &key, const QString &color) {
    const int i = indexOf(key);
    if (i < 0 || !colors().contains(color) || notes_[i].color == color)
        return;
    notes_[i].color = color;
    changed(i, {ColorRole});
    save();
}

QVariantMap DesktopNotes::landing(const QString &key, int columns, int rows) const {
    const int i = indexOf(key);
    if (i < 0 || !grid_.isValid())
        return {};
    const auto cell = place(notes_[i].cell + QPoint(columns, rows), covered(i));
    return {{"column", cell.x()}, {"row", cell.y()}};
}

void DesktopNotes::move(const QString &key, int columns, int rows) {
    const int i = indexOf(key);
    if (i < 0 || !grid_.isValid())
        return;
    auto &note = notes_[i];
    note.saved = place(note.cell + QPoint(columns, rows), covered(i));
    if (note.cell != note.saved) {
        note.cell = note.saved;
        changed(i, {ColumnRole, RowRole});
        Q_EMIT cellsChanged();
    }
    save();
}

QVariantList DesktopNotes::linesOf(const QString &text) {
    QVariantList lines;
    for (const auto &line : text.split('\n')) {
        const auto box = checkbox().match(line);
        if (box.hasMatch())
            lines.push_back(QVariantMap{{"text", box.captured(4)}, {"box", true},
                                        {"checked", box.captured(3) != " "},
                                        {"indent", int(box.capturedLength(1))}});
        else
            lines.push_back(QVariantMap{{"text", line}, {"box", false}, {"checked", false}, {"indent", 0}});
    }
    return lines;
}

bool DesktopNotes::toggle(const QString &key, int line) {
    const int i = indexOf(key);
    if (i < 0)
        return false;
    auto lines = notes_[i].text.split('\n');
    if (line < 0 || line >= lines.size())
        return false;
    const auto box = checkbox().match(lines[line]);
    if (!box.hasMatch())
        return false;
    lines[line][box.capturedStart(3)] = box.captured(3) == " " ? QChar('x') : QChar(' ');
    notes_[i].text = lines.join('\n');
    changed(i, {TextRole});
    save();
    return true;
}

void DesktopNotes::copy(const QString &key) {
    if (const int i = indexOf(key); i >= 0)
        QGuiApplication::clipboard()->setText(notes_[i].text);
}

void DesktopNotes::remove(const QString &key) {
    const int i = indexOf(key);
    if (i < 0)
        return;
    beginRemoveRows({}, i, i);
    removed_ = notes_.takeAt(i);
    endRemoveRows();
    Q_EMIT countChanged();
    Q_EMIT cellsChanged();
    Q_EMIT removedChanged();
    save();
}

void DesktopNotes::discard(const QString &key) {
    const int i = indexOf(key);
    if (i < 0)
        return;
    beginRemoveRows({}, i, i);
    notes_.removeAt(i);
    endRemoveRows();
    Q_EMIT countChanged();
    Q_EMIT cellsChanged();
    save();
}

QString DesktopNotes::restore() {
    if (!removed_)
        return {};
    auto note = *std::exchange(removed_, std::nullopt);
    if (grid_.isValid())
        note.cell = note.saved = place(note.saved.x() >= 0 ? note.saved : note.cell, covered());
    const auto at = std::find_if(notes_.cbegin(), notes_.cend(), [&](const Note &n) { return n.id > note.id; });
    const int i = int(at - notes_.cbegin());
    beginInsertRows({}, i, i);
    notes_.insert(i, note);
    endInsertRows();
    Q_EMIT countChanged();
    Q_EMIT cellsChanged();
    Q_EMIT removedChanged();
    save();
    return keyOf(note.id);
}

void DesktopNotes::flush() {
    if (saveSoon_.isActive())
        save();
}

void DesktopNotes::changed(int i, const QList<int> &roles) {
    Q_EMIT dataChanged(index(i), index(i), roles);
}

void DesktopNotes::save() {
    saveSoon_.stop();
    QJsonArray list;
    for (const auto &note : notes_)
        list.push_back(QJsonObject{{"id", note.id}, {"color", note.color}, {"column", note.saved.x()},
                                   {"row", note.saved.y()}, {"text", note.text}});
    const auto contents = QJsonDocument(QJsonObject{{"notes", list}}).toJson();
    if (contents == saved_ || !remembering_)
        return;
    if (const auto error = saveFile(statePath(), contents); !error.isEmpty()) {
        Q_EMIT failed("Could not remember the desktop's notes: " + error);
        return;
    }
    saved_ = contents;
}

int DesktopNotes::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : count();
}

QVariant DesktopNotes::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= count())
        return {};
    const auto &note = notes_[index.row()];
    switch (role) {
    case KeyRole:
        return keyOf(note.id);
    case IdRole:
        return note.id;
    case Qt::DisplayRole:
    case TextRole:
        return note.text;
    case ColorRole:
        return note.color;
    case ColumnRole:
        return note.cell.x();
    case RowRole:
        return note.cell.y();
    default:
        return {};
    }
}

QHash<int, QByteArray> DesktopNotes::roleNames() const {
    return {{KeyRole, "key"},     {IdRole, "noteId"},     {TextRole, "text"},
            {ColorRole, "color"}, {ColumnRole, "column"}, {RowRole, "row"}};
}
