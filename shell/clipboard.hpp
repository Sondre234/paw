// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDateTime>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QVariantList>
#include <map>
#include <memory>
#include <utility>
#include <vector>

struct wl_display;
struct wl_registry;
struct wl_seat;
struct ext_data_control_manager_v1;
struct ext_data_control_device_v1;
struct ext_data_control_offer_v1;
struct ext_data_control_source_v1;
class QSocketNotifier;
class QTimer;

// The history of what is copied, as Windows' Win + V and KDE's Klipper keep it: text, and
// pictures, newest first, the pinned ones ahead and kept however long the history grows.
//
// It is a client of the compositor's ext-data-control-v1 on a Wayland connection of its own
// (connectDisplay), which hears every change of the clipboard whoever makes it, and reads what was
// copied without taking the keyboard: the text (its best type), HTML and a list of files when
// they are offered too, and a picture (PNG, JPEG and the like). What a password manager marks as
// secret (x-kde-passwordManagerHint = secret) is not kept, nor anything copied while the session
// is locked. restore() makes an entry what is copied again, offering every type it was copied
// with (the text under its usual names), and the newest entry; without data-control it goes
// through Qt's clipboard instead, which then needs the keyboard. Kept in memory only unless
// `persist`, then in a file only the user can read.
//
// The palette-like popup that lists it (ClipboardPicker.qml) opens on one output at a time:
// `output`, which the clipboard_history action toggles.
class ClipboardHistory : public QObject {
    Q_OBJECT
    // The output showing the history, empty while it is closed.
    Q_PROPERTY(QString output READ output NOTIFY openChanged)
    // The entries, pinned first, each part newest first, as {id, kind ("text" or "image"), text
    // (up to 4000 characters), image (an image://clipboard/ URL, "" for none), width, height,
    // pinned, when}.
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(bool enabled READ enabled NOTIFY settingsChanged)
  public:
    struct Settings {
        bool enabled = true;
        int limit = 50; // entries besides the pinned ones
        bool images = true;
        bool persist = false;
        // Where it is kept with `persist`; empty: $XDG_STATE_HOME/paw/clipboard.
        QString path;
        bool operator==(const Settings &) const = default;
    };
    // One form of what was copied: its type and its bytes.
    using Format = std::pair<QString, QByteArray>;

    explicit ClipboardHistory(QObject *parent = nullptr);
    ~ClipboardHistory() override;
    // New settings: turned off, it forgets every entry (and its file); persisting, it reads the
    // file the first time, and without, it removes it.
    void configure(const Settings &settings);
    bool enabled() const { return settings_.enabled; }
    // Connects to the compositor ($WAYLAND_DISPLAY) to follow the clipboard; false when it
    // cannot or the compositor offers no data-control, and then only restore() works, through
    // Qt's clipboard.
    bool connectDisplay();
    bool connected() const { return device_ != nullptr; }
    // While the session is locked nothing copied is kept.
    void setLocked(bool locked);
    bool locked() const { return locked_; }

    QString output() const { return output_; }
    QVariantList entries() const;
    // The picture of entry `id`, shrunk to fit 512 pixels, for image://clipboard/ID/SERIAL;
    // null for none. Safe from any thread.
    QImage picture(int id) const;

    // Opens the popup on `output`, or closes it when it is open there.
    Q_INVOKABLE void toggle(const QString &output);
    Q_INVOKABLE void close();
    // Makes entry `id` what is copied, and the newest entry; returns whether it could.
    Q_INVOKABLE bool restore(int id);
    Q_INVOKABLE void remove(int id);
    Q_INVOKABLE void setPinned(int id, bool pinned);
    // Forgets every entry but the pinned ones.
    Q_INVOKABLE void clear();

    // Keeps what was copied as the newest entry, unless it is secret, locked away, holds neither
    // text nor a picture, or the history is off; one the same as an entry moves that entry up.
    // Returns whether it was kept.
    bool record(const QList<Format> &formats, const QDateTime &when = QDateTime::currentDateTime());
    // For a preview: these copies as its entries, the first the newest, never read from the
    // clipboard or written to disk.
    void preview(const QList<QList<Format>> &copies, const QList<int> &pinned = {});

  Q_SIGNALS:
    void openChanged();
    void entriesChanged();
    void settingsChanged();
    // Something it was asked to do failed, or its file could not be written.
    void failed(const QString &message);

  private:
    struct Entry {
        int id = 0;
        QList<Format> formats; // as offered back: text as text/plain;charset=utf-8
        QString text;
        QImage picture; // shrunk; null for text alone
        QSize size;     // the picture's own size
        bool pinned = false;
        QDateTime when;
        int serial = 0; // new for every picture, so that QML reads it again
    };
    // A form being read from what was copied.
    struct Reading;
    // An entry's bytes being written for a program pasting it.
    struct Writing;

    Settings settings_;
    std::vector<Entry> entries_; // newest first
    int lastId_ = 0;
    bool locked_ = false, loaded_ = false, previewOnly_ = false;
    QString output_;
    mutable QMutex pictures_;
    QTimer *saving_ = nullptr;

    wl_display *display_ = nullptr;
    wl_registry *registry_ = nullptr;
    wl_seat *seat_ = nullptr;
    ext_data_control_manager_v1 *manager_ = nullptr;
    ext_data_control_device_v1 *device_ = nullptr;
    std::unique_ptr<QSocketNotifier> read_, write_;
    // The offers announced, with their types, until they are the selection or replaced.
    std::map<ext_data_control_offer_v1 *, QStringList> offers_;
    ext_data_control_offer_v1 *selection_ = nullptr;
    std::unique_ptr<Reading> reading_;
    ext_data_control_source_v1 *source_ = nullptr; // what restore() offers, until replaced
    QList<Format> offered_;
    std::vector<std::unique_ptr<Writing>> writings_;

    QString path() const;
    void changed();
    void save();
    void load();
    void flush();
    void disconnectDisplay();
    void selectionChanged(ext_data_control_offer_v1 *offer);
    void readNext();
    void finishReading(bool keep);
    void send(const QString &type, int fd);
    static void global(void *data, wl_registry *registry, uint32_t name, const char *interface,
                       uint32_t version);
    static void globalRemoved(void *data, wl_registry *registry, uint32_t name);
};

// image://clipboard/ID/SERIAL: the picture of the clipboard history's entry ID, as its `image`
// names it (the serial only keeps QML from reusing a picture that changed); an empty one when the
// entry has gone.
QImage clipboardImage(const ClipboardHistory &history, const QString &id);
