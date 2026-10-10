// SPDX-License-Identifier: GPL-3.0-or-later
#include "clipboard.hpp"
#include "state_files.hpp"
#include "ext-data-control-v1-client-protocol.h"
#include <QClipboard>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMimeData>
#include <QSaveFile>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>
#include <wayland-client.h>

namespace {
// The names programs give text, the best first; X11's last.
const QStringList textTypes{"text/plain;charset=utf-8",
                            "text/plain;charset=UTF-8",
                            "UTF8_STRING",
                            "text/plain",
                            "TEXT",
                            "STRING"};
// How an entry keeps its text, and the other names it offers it under.
const QString textType = "text/plain;charset=utf-8";
const QStringList textAliases{"text/plain", "UTF8_STRING", "TEXT", "STRING"};
// Kept beside the text when they are offered too: formatted text, and files copied.
const QStringList richTypes{"text/html", "text/uri-list"};
// Pictures, the type kept first of those offered.
const QStringList pictureTypes{"image/png", "image/jpeg", "image/webp",
                               "image/gif", "image/bmp",  "image/tiff"};
// What KDE's password managers (KeePassXC among them) add to a password they copy.
const QString secretHint = "x-kde-passwordManagerHint";
constexpr qsizetype textLimit = 1 << 20;     // bytes of one form of text read at most
constexpr qsizetype pictureLimit = 16 << 20; // of a picture
constexpr int readTimeout = 2000;            // ms a program gets to hand over one form
constexpr int pictureSide = 512;             // the pictures kept for the list fit this
constexpr quint32 fileMagic = 0x53484342;    // "SHCB"
constexpr quint32 fileVersion = 1;

QByteArray formatData(const QList<ClipboardHistory::Format> &formats, const QString &type) {
    for (const auto &[name, data] : formats)
        if (name == type)
            return data;
    return {};
}
bool hasFormat(const QList<ClipboardHistory::Format> &formats, const QString &type) {
    return std::any_of(formats.begin(), formats.end(),
                       [&type](const auto &format) { return format.first == type; });
}

// Writes to a pipe whose reader may have gone without SIGPIPE ending the shell: the signal is
// held while writing and taken back when the write raised it.
ssize_t quietWrite(int fd, const char *data, size_t size) {
    sigset_t pipe, old;
    sigemptyset(&pipe);
    sigaddset(&pipe, SIGPIPE);
    pthread_sigmask(SIG_BLOCK, &pipe, &old);
    const ssize_t written = ::write(fd, data, size);
    const int error = errno;
    if (written < 0 && error == EPIPE) {
        const timespec none{};
        sigtimedwait(&pipe, nullptr, &none);
    }
    pthread_sigmask(SIG_SETMASK, &old, nullptr);
    errno = error;
    return written;
}

// Deletes a notifier once its signal is over, as it may be in the middle of one.
void dispose(std::unique_ptr<QSocketNotifier> &notifier) {
    if (notifier) {
        notifier->setEnabled(false);
        notifier.release()->deleteLater();
    }
}
} // namespace

struct ClipboardHistory::Reading {
    ext_data_control_offer_v1 *offer = nullptr;
    QStringList types; // to read, in order
    qsizetype next = 0;
    QList<Format> formats; // read so far
    int fd = -1;
    QByteArray buffer;
    qsizetype limit = 0;
    std::unique_ptr<QSocketNotifier> notifier;
    QTimer timer; // gives up on a program that never finishes writing
    // Stops reading the form being read.
    void stop() {
        dispose(notifier);
        timer.stop();
        if (fd >= 0)
            ::close(fd);
        fd = -1;
    }
    ~Reading() { stop(); }
};

struct ClipboardHistory::Writing {
    int fd = -1;
    QByteArray data;
    qsizetype written = 0;
    std::unique_ptr<QSocketNotifier> notifier;
    ~Writing() {
        dispose(notifier);
        if (fd >= 0)
            ::close(fd);
    }
};

ClipboardHistory::ClipboardHistory(QObject *parent) : QObject(parent) {
    saving_ = new QTimer(this);
    saving_->setSingleShot(true);
    saving_->setInterval(500);
    connect(saving_, &QTimer::timeout, this, &ClipboardHistory::save);
}

ClipboardHistory::~ClipboardHistory() {
    if (saving_->isActive())
        save();
    disconnectDisplay();
}

// The file, written only where the history is the session's (connected to it) or was told one.
QString ClipboardHistory::path() const {
    if (previewOnly_)
        return {};
    if (!settings_.path.isEmpty())
        return settings_.path;
    if (!connected())
        return {};
    return stateDir() + "/clipboard";
}

void ClipboardHistory::configure(const Settings &settings) {
    const bool persisted = settings_.persist && settings_.enabled;
    settings_ = settings;
    {
        QMutexLocker lock(&pictures_);
        if (!settings_.enabled) {
            reading_.reset();
            entries_.clear();
        }
        // Over the limit, the oldest go.
        int kept = 0;
        std::erase_if(entries_, [&](const Entry &entry) {
            return !entry.pinned && ++kept > settings_.limit;
        });
    }
    const auto file = path();
    if (!file.isEmpty()) {
        if (settings_.enabled && settings_.persist) {
            if (!loaded_)
                load();
            if (!persisted)
                save();
        } else {
            saving_->stop();
            QFile::remove(file);
        }
    }
    Q_EMIT settingsChanged();
    Q_EMIT entriesChanged();
}

void ClipboardHistory::setLocked(bool locked) {
    if (locked == locked_)
        return;
    locked_ = locked;
    // What was being read was copied before; it is not kept either.
    if (locked_)
        reading_.reset();
}

QVariantList ClipboardHistory::entries() const {
    QVariantList pinned, others;
    for (const auto &entry : entries_) {
        const bool picture = !entry.picture.isNull();
        QVariantMap map{
            {"id", entry.id},
            {"kind", picture ? "image" : "text"},
            {"text", entry.text.left(4000)},
            {"image", picture ? QString("image://clipboard/%1/%2").arg(entry.id).arg(entry.serial)
                              : QString()},
            {"width", entry.size.width()},
            {"height", entry.size.height()},
            {"pinned", entry.pinned},
            {"when", entry.when}};
        (entry.pinned ? pinned : others).push_back(map);
    }
    return pinned + others;
}

QImage ClipboardHistory::picture(int id) const {
    QMutexLocker lock(&pictures_);
    for (const auto &entry : entries_)
        if (entry.id == id)
            return entry.picture;
    return {};
}
QImage clipboardImage(const ClipboardHistory &history, const QString &id) {
    QImage image = history.picture(id.section('/', 0, 0).toInt());
    if (image.isNull()) {
        image = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
    }
    return image;
}

void ClipboardHistory::toggle(const QString &output) {
    output_ = output_ == output ? QString() : output;
    Q_EMIT openChanged();
}

void ClipboardHistory::close() {
    if (output_.isEmpty())
        return;
    output_.clear();
    Q_EMIT openChanged();
}

void ClipboardHistory::changed() {
    Q_EMIT entriesChanged();
    if (settings_.persist && !path().isEmpty())
        saving_->start();
}

bool ClipboardHistory::record(const QList<Format> &formats, const QDateTime &when) {
    if (!keep(formats, when))
        return false;
    changed();
    return true;
}
bool ClipboardHistory::keep(const QList<Format> &formats, const QDateTime &when) {
    if (!settings_.enabled || locked_)
        return false;
    if (formatData(formats, secretHint).trimmed() == "secret")
        return false;
    Entry entry;
    for (const auto &type : textTypes)
        if (hasFormat(formats, type)) {
            entry.text = QString::fromUtf8(formatData(formats, type));
            entry.formats.push_back({textType, entry.text.toUtf8()});
            break;
        }
    for (const auto &type : richTypes)
        if (hasFormat(formats, type))
            entry.formats.push_back({type, formatData(formats, type)});
    for (const auto &type : pictureTypes) {
        if (!settings_.images || !hasFormat(formats, type))
            continue;
        const auto data = formatData(formats, type);
        const auto picture = QImage::fromData(data);
        if (picture.isNull())
            continue;
        entry.size = picture.size();
        entry.picture = picture.width() > pictureSide || picture.height() > pictureSide
                            ? picture.scaled(pictureSide, pictureSide, Qt::KeepAspectRatio,
                                             Qt::SmoothTransformation)
                            : picture;
        entry.formats.push_back({type, data});
        break;
    }
    // A picture is what it is, whatever text came with it (a file name, a link).
    if (!entry.picture.isNull() && entry.text.trimmed().isEmpty())
        entry.text.clear();
    if (entry.text.isEmpty() && entry.picture.isNull())
        return false;
    entry.when = when;
    // The same again moves up, pinned or not.
    auto same = std::find_if(entries_.begin(), entries_.end(), [&entry](const Entry &other) {
        return other.formats == entry.formats;
    });
    {
        QMutexLocker lock(&pictures_);
        if (same != entries_.end()) {
            entry.id = same->id;
            entry.pinned = same->pinned;
            entry.serial = same->serial;
            entries_.erase(same);
        } else {
            entry.id = ++lastId_;
        }
        entries_.insert(entries_.begin(), std::move(entry));
        int kept = 0;
        std::erase_if(entries_, [&](const Entry &other) {
            return !other.pinned && ++kept > settings_.limit;
        });
    }
    return true;
}

void ClipboardHistory::preview(const QList<QList<Format>> &copies, const QList<int> &pinned) {
    previewOnly_ = true;
    {
        QMutexLocker lock(&pictures_);
        entries_.clear();
        lastId_ = 0;
    }
    const auto now = QDateTime::currentDateTime();
    for (qsizetype i = copies.size() - 1; i >= 0; --i)
        keep(copies[i], now.addSecs(-60 * 7 * i));
    for (auto &entry : entries_)
        entry.pinned = pinned.contains(static_cast<int>(copies.size() - entry.id));
    Q_EMIT entriesChanged();
}

void ClipboardHistory::remove(int id) {
    {
        QMutexLocker lock(&pictures_);
        if (!std::erase_if(entries_, [id](const Entry &entry) { return entry.id == id; }))
            return;
    }
    changed();
}

void ClipboardHistory::setPinned(int id, bool pinned) {
    for (auto &entry : entries_)
        if (entry.id == id && entry.pinned != pinned) {
            entry.pinned = pinned;
            changed();
            return;
        }
}

void ClipboardHistory::clear() {
    {
        QMutexLocker lock(&pictures_);
        if (!std::erase_if(entries_, [](const Entry &entry) { return !entry.pinned; }))
            return;
    }
    changed();
}

bool ClipboardHistory::restore(int id) {
    auto found = std::find_if(entries_.begin(), entries_.end(),
                              [id](const Entry &entry) { return entry.id == id; });
    if (found == entries_.end())
        return false;
    // It becomes the newest.
    found->when = QDateTime::currentDateTime();
    {
        QMutexLocker lock(&pictures_);
        std::rotate(entries_.begin(), found, found + 1);
    }
    const auto &entry = entries_.front();
    changed();
    if (manager_ && device_) {
        static const ext_data_control_source_v1_listener listener{
            .send =
                [](void *data, ext_data_control_source_v1 *, const char *type, int32_t fd) {
                    static_cast<ClipboardHistory *>(data)->send(QString::fromUtf8(type), fd);
                },
            .cancelled =
                [](void *data, ext_data_control_source_v1 *source) {
                    auto *self = static_cast<ClipboardHistory *>(data);
                    if (self->source_ == source) {
                        self->source_ = nullptr;
                        self->offered_.clear();
                    }
                    ext_data_control_source_v1_destroy(source);
                },
        };
        if (source_)
            ext_data_control_source_v1_destroy(source_);
        source_ = ext_data_control_manager_v1_create_data_source(manager_);
        ext_data_control_source_v1_add_listener(source_, &listener, this);
        offered_ = entry.formats;
        for (const auto &[type, data] : entry.formats) {
            ext_data_control_source_v1_offer(source_, type.toUtf8().constData());
            if (type == textType)
                for (const auto &alias : textAliases)
                    ext_data_control_source_v1_offer(source_, alias.toUtf8().constData());
        }
        ext_data_control_device_v1_set_selection(device_, source_);
        flush();
        return true;
    }
    // Without data-control, through Qt's clipboard, which needs the keyboard.
    auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance());
    if (!app)
        return false;
    auto *mime = new QMimeData;
    for (const auto &[type, data] : entry.formats)
        mime->setData(type, data);
    if (!entry.text.isEmpty())
        mime->setText(entry.text);
    if (!entry.picture.isNull())
        mime->setImageData(QImage::fromData(formatData(entry.formats, entry.formats.last().first)));
    QGuiApplication::clipboard()->setMimeData(mime);
    return true;
}

// Hands a program pasting the entry offered the bytes it asked for, as fast as it reads them.
void ClipboardHistory::send(const QString &type, int fd) {
    auto writing = std::make_unique<Writing>();
    writing->fd = fd;
    writing->data = formatData(offered_, textAliases.contains(type) ? textType : type);
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
    auto *raw = writing.get();
    // Writes what it can; returns whether it is over (done, or the reader gone).
    auto step = [raw] {
        while (raw->written < raw->data.size()) {
            const ssize_t count = quietWrite(raw->fd, raw->data.constData() + raw->written,
                                             static_cast<size_t>(raw->data.size() - raw->written));
            if (count > 0)
                raw->written += count;
            else if (count < 0 && errno == EINTR)
                continue;
            else
                return !(count < 0 && errno == EAGAIN);
        }
        return true;
    };
    if (step())
        return; // closed as `writing` goes
    writing->notifier = std::make_unique<QSocketNotifier>(fd, QSocketNotifier::Write);
    connect(writing->notifier.get(), &QSocketNotifier::activated, this, [this, raw, step] {
        if (!step())
            return;
        std::erase_if(writings_, [raw](const auto &other) { return other.get() == raw; });
    });
    writings_.push_back(std::move(writing));
}

bool ClipboardHistory::connectDisplay() {
    display_ = wl_display_connect(nullptr);
    if (!display_)
        return false;
    registry_ = wl_display_get_registry(display_);
    static const wl_registry_listener listener{global, globalRemoved};
    wl_registry_add_listener(registry_, &listener, this);
    if (wl_display_roundtrip(display_) < 0 || !manager_ || !seat_) {
        disconnectDisplay();
        return false;
    }
    static const ext_data_control_offer_v1_listener offerListener{
        .offer =
            [](void *data, ext_data_control_offer_v1 *offer, const char *type) {
                static_cast<ClipboardHistory *>(data)->offers_[offer].push_back(
                    QString::fromUtf8(type));
            },
    };
    static const ext_data_control_device_v1_listener deviceListener{
        .data_offer =
            [](void *data, ext_data_control_device_v1 *, ext_data_control_offer_v1 *offer) {
                auto *self = static_cast<ClipboardHistory *>(data);
                self->offers_[offer] = {};
                ext_data_control_offer_v1_add_listener(offer, &offerListener, self);
            },
        .selection =
            [](void *data, ext_data_control_device_v1 *, ext_data_control_offer_v1 *offer) {
                static_cast<ClipboardHistory *>(data)->selectionChanged(offer);
            },
        .finished =
            [](void *data, ext_data_control_device_v1 *device) {
                auto *self = static_cast<ClipboardHistory *>(data);
                self->reading_.reset();
                ext_data_control_device_v1_destroy(device);
                self->device_ = nullptr;
            },
        // Only the clipboard is kept, not what is selected.
        .primary_selection =
            [](void *data, ext_data_control_device_v1 *, ext_data_control_offer_v1 *offer) {
                auto *self = static_cast<ClipboardHistory *>(data);
                if (offer && offer != self->selection_) {
                    self->offers_.erase(offer);
                    ext_data_control_offer_v1_destroy(offer);
                }
            },
    };
    device_ = ext_data_control_manager_v1_get_data_device(manager_, seat_);
    ext_data_control_device_v1_add_listener(device_, &deviceListener, this);
    read_ = std::make_unique<QSocketNotifier>(wl_display_get_fd(display_), QSocketNotifier::Read);
    write_ = std::make_unique<QSocketNotifier>(wl_display_get_fd(display_), QSocketNotifier::Write);
    write_->setEnabled(false);
    connect(read_.get(), &QSocketNotifier::activated, this, [this] {
        if (wl_display_dispatch(display_) < 0) {
            // The compositor went: nothing more is heard, and restore() falls back on Qt.
            read_->setEnabled(false);
            write_->setEnabled(false);
            device_ = nullptr;
            manager_ = nullptr;
            source_ = nullptr;
            return;
        }
        flush();
    });
    connect(write_.get(), &QSocketNotifier::activated, this, [this] { flush(); });
    flush();
    // Now the history is the session's: its file is read, or removed.
    configure(settings_);
    return true;
}

void ClipboardHistory::flush() {
    if (!display_)
        return;
    const int result = wl_display_flush(display_);
    if (write_)
        write_->setEnabled(result < 0 && errno == EAGAIN);
}

void ClipboardHistory::disconnectDisplay() {
    reading_.reset();
    writings_.clear();
    read_.reset();
    write_.reset();
    if (!display_)
        return;
    if (source_)
        ext_data_control_source_v1_destroy(source_);
    for (auto &[offer, types] : offers_)
        ext_data_control_offer_v1_destroy(offer);
    offers_.clear();
    selection_ = nullptr;
    if (device_)
        ext_data_control_device_v1_destroy(device_);
    if (manager_)
        ext_data_control_manager_v1_destroy(manager_);
    if (seat_)
        wl_seat_destroy(seat_);
    if (registry_)
        wl_registry_destroy(registry_);
    wl_display_disconnect(display_);
    source_ = nullptr;
    device_ = nullptr;
    manager_ = nullptr;
    seat_ = nullptr;
    registry_ = nullptr;
    display_ = nullptr;
}

void ClipboardHistory::global(void *data, wl_registry *registry, uint32_t name,
                              const char *interface, uint32_t) {
    auto *self = static_cast<ClipboardHistory *>(data);
    if (!strcmp(interface, ext_data_control_manager_v1_interface.name) && !self->manager_)
        self->manager_ = static_cast<ext_data_control_manager_v1 *>(
            wl_registry_bind(registry, name, &ext_data_control_manager_v1_interface, 1));
    else if (!strcmp(interface, wl_seat_interface.name) && !self->seat_)
        self->seat_ =
            static_cast<wl_seat *>(wl_registry_bind(registry, name, &wl_seat_interface, 1));
}

void ClipboardHistory::globalRemoved(void *, wl_registry *, uint32_t) {}

// What is copied changed: what it was is no longer offered, and what it is now is read, unless
// it is the history's own (restore()), or nothing is kept now.
void ClipboardHistory::selectionChanged(ext_data_control_offer_v1 *offer) {
    reading_.reset();
    if (selection_ && selection_ != offer) {
        offers_.erase(selection_);
        ext_data_control_offer_v1_destroy(selection_);
    }
    selection_ = offer;
    if (!offer || source_ || locked_ || !settings_.enabled)
        return;
    const auto types = offers_[offer];
    QStringList wanted;
    // The hint first: a secret is read no further.
    if (types.contains(secretHint))
        wanted << secretHint;
    for (const auto &type : textTypes)
        if (types.contains(type)) {
            wanted << type;
            break;
        }
    for (const auto &type : richTypes)
        if (types.contains(type))
            wanted << type;
    for (const auto &type : pictureTypes)
        if (settings_.images && types.contains(type)) {
            wanted << type;
            break;
        }
    wanted.removeAll(QString());
    if (wanted.isEmpty() || wanted == QStringList{secretHint})
        return;
    reading_ = std::make_unique<Reading>();
    reading_->offer = offer;
    reading_->types = wanted;
    reading_->timer.setSingleShot(true);
    connect(&reading_->timer, &QTimer::timeout, this, [this] {
        // A program that never finishes: that form is left out.
        reading_->stop();
        readNext();
    });
    readNext();
}

void ClipboardHistory::readNext() {
    auto &reading = *reading_;
    if (reading.next >= reading.types.size())
        return finishReading(true);
    const auto type = reading.types[reading.next++];
    int fds[2];
    if (pipe2(fds, O_CLOEXEC | O_NONBLOCK) != 0)
        return finishReading(false);
    // libwayland sends a copy of the descriptor, so the history's own closes at once.
    ext_data_control_offer_v1_receive(reading.offer, type.toUtf8().constData(), fds[1]);
    ::close(fds[1]);
    flush();
    reading.fd = fds[0];
    reading.buffer.clear();
    reading.limit = pictureTypes.contains(type) ? pictureLimit : textLimit;
    reading.notifier = std::make_unique<QSocketNotifier>(fds[0], QSocketNotifier::Read);
    connect(reading.notifier.get(), &QSocketNotifier::activated, this, [this, type] {
        auto &reading = *reading_;
        char chunk[65536];
        ssize_t count;
        for (;;) {
            count = ::read(reading.fd, chunk, sizeof chunk);
            if (count > 0) {
                reading.buffer.append(chunk, count);
                if (reading.buffer.size() <= reading.limit)
                    continue;
                // Too much: this form is left out.
                count = -1;
                break;
            }
            if (count < 0 && errno == EINTR)
                continue;
            if (count < 0 && errno == EAGAIN)
                return;
            break; // the end, or an error
        }
        reading.stop();
        if (count == 0)
            reading.formats.push_back({type, reading.buffer});
        if (type == secretHint && reading.buffer.trimmed() == "secret")
            return finishReading(false);
        readNext();
    });
    reading.timer.start(readTimeout);
}

void ClipboardHistory::finishReading(bool keep) {
    if (!reading_)
        return;
    const auto formats = std::move(reading_->formats);
    reading_.reset();
    if (keep)
        record(formats);
}

void ClipboardHistory::save() {
    const auto file = path();
    if (file.isEmpty() || !settings_.persist)
        return;
    QDir().mkpath(QFileInfo(file).path());
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly)) {
        Q_EMIT failed("Could not save the clipboard history: " + out.errorString());
        return;
    }
    // Only the user may read it, from the moment it is there.
    out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    QDataStream stream(&out);
    stream << fileMagic << fileVersion << quint32(entries_.size());
    // The oldest first, as they are read back.
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
        stream << it->pinned << it->when << quint32(it->formats.size());
        for (const auto &[type, data] : it->formats)
            stream << type << data;
    }
    if (!out.commit())
        Q_EMIT failed("Could not save the clipboard history: " + out.errorString());
}

void ClipboardHistory::load() {
    loaded_ = true;
    QFile in(path());
    if (!in.open(QIODevice::ReadOnly))
        return;
    QDataStream stream(&in);
    quint32 magic = 0, version = 0, count = 0;
    stream >> magic >> version >> count;
    if (magic != fileMagic || version != fileVersion)
        return;
    for (quint32 i = 0; i < count && stream.status() == QDataStream::Ok; ++i) {
        bool pinned = false;
        QDateTime when;
        quint32 formats = 0;
        stream >> pinned >> when >> formats;
        QList<Format> copy;
        for (quint32 j = 0; j < formats && j < 16 && stream.status() == QDataStream::Ok; ++j) {
            QString type;
            QByteArray data;
            stream >> type >> data;
            copy.push_back({type, data});
        }
        if (stream.status() != QDataStream::Ok || !keep(copy, when))
            continue;
        entries_.front().pinned = pinned;
    }
}
