// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QHash>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <vector>

class QTimer;

// The emoji picker (the emoji_picker action, Super + semicolon), as Windows' Win + . and Win + ;
// open it: every emoji of vendor/emoji/emoji.tsv by group, a search by name and keyword, those
// picked lately, and a skin tone for those that take one. What is picked goes into the window that
// had the keyboard: once the picker has given the keyboard back (keyboardReleased), typeRequested
// asks for it to be typed there, which the controller has the compositor do (`type TEXT`).
//
// The tone and what was picked lately are kept in $XDG_STATE_HOME/paw/emoji: "tone N" on the
// first line, then an emoji a line, the most recent first.
class EmojiPicker : public QObject {
    Q_OBJECT
    // The output showing the picker, empty while it is closed.
    Q_PROPERTY(QString output READ output NOTIFY openChanged)
    // The groups' names, in Unicode's order ("Smileys & Emotion", "People & Body", ...).
    Q_PROPERTY(QStringList groups READ groups CONSTANT)
    // The emoji picked lately, the most recent first, as {text, name}, each as it was picked.
    Q_PROPERTY(QVariantList recent READ recent NOTIFY recentChanged)
    // The skin tone of the emoji that take one: 0 for none (yellow), 1 to 5 from light to dark.
    Q_PROPERTY(int tone READ tone WRITE setTone NOTIFY toneChanged)
  public:
    struct Emoji {
        QString text, name;
        QStringList keywords;
        int group = 0;
        QStringList tones; // the five forms with a skin tone, light to dark, or none
        // The name's words and the keywords, folded to lower case, for the search.
        QStringList nameWords, foldedKeywords;
    };

    // `table` is the table to read (the one compiled in unless given), `stateDir` where the tone
    // and the recent emoji are kept ($XDG_STATE_HOME/paw unless given).
    explicit EmojiPicker(QString table = {}, QString stateDir = {}, QObject *parent = nullptr);
    // The emoji of the table at `path`, in its order, and its groups' names in `groups`.
    static std::vector<Emoji> read(const QString &path, QStringList *groups = nullptr);

    QString output() const { return output_; }
    QStringList groups() const { return groups_; }
    QVariantList recent() const;
    int tone() const { return tone_; }
    void setTone(int tone);
    int size() const { return static_cast<int>(emoji_.size()); }

    // The emoji of group `index`, as {text, name}, each in the tone chosen if it takes one.
    Q_INVOKABLE QVariantList group(int index) const;
    // What `query` finds, best first, at most `limit`, as group() gives them: each word of it
    // must start a word of an emoji's name or one of its keywords, or be in its name; a word that
    // is the whole of one, and the name's words, count most. An emoji typed or pasted finds itself.
    Q_INVOKABLE QVariantList search(const QString &query, int limit = 160) const;
    // Opens the picker on `output`, or closes it when it is open there.
    Q_INVOKABLE void toggle(const QString &output);
    Q_INVOKABLE void close();
    // Picks `text`: it is the most recent, the picker closes, and once it has given the keyboard
    // back (or half a second on) typeRequested asks for it to be typed.
    Q_INVOKABLE void pick(const QString &text);
    // The picker's surface gave the keyboard back: what was picked can be typed now.
    Q_INVOKABLE void keyboardReleased();
    // For a preview: these as picked lately and this tone, never written to disk.
    void preview(const QStringList &recent, int tone);

  Q_SIGNALS:
    void openChanged();
    void recentChanged();
    void toneChanged();
    // `text` is to be typed into what has the keyboard.
    void typeRequested(const QString &text);

  private:
    std::vector<Emoji> emoji_;
    QStringList groups_, recent_;
    int tone_ = 0;
    QString stateDir_, output_, pending_;
    bool previewOnly_ = false;
    QTimer *typing_ = nullptr;
    // Each emoji's index in emoji_ by its text and each of its forms in another skin tone, without
    // the emoji presentation selector; made as find() is first asked.
    mutable QHash<QString, qsizetype> forms_;
    QVariantMap entry(const Emoji &emoji) const;
    const Emoji *find(const QString &text) const;
    void save() const;
    void typePending();
};
