// SPDX-License-Identifier: GPL-3.0-or-later
#include "window_pictures.hpp"
#include <QMutexLocker>
#include <QtGlobal>
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>

// A buffer's memory, mapped for reading. A frame being scaled holds it too, so that it outlasts
// its buffer when the capture ends meanwhile.
struct WindowPictures::Mapping {
    void *data;
    size_t size;
    Mapping(void *data, size_t size) : data(data), size(size) {}
    Mapping(const Mapping &) = delete;
    Mapping &operator=(const Mapping &) = delete;
    ~Mapping() { munmap(data, size); }
};
// The shared-memory buffer a window is copied into, kept while the constraints it was made for
// hold. Every format imageFormat reads has four bytes a pixel.
struct WindowPictures::Buffer {
    wl_buffer *buffer;
    std::shared_ptr<const Mapping> mapping;
    QSize size;
    uint32_t format;
    int stride;
    Buffer(wl_buffer *buffer, std::shared_ptr<const Mapping> mapping, QSize size, uint32_t format)
        : buffer(buffer), mapping(std::move(mapping)), size(size), format(format),
          stride(size.width() * 4) {}
    Buffer(const Buffer &) = delete;
    Buffer &operator=(const Buffer &) = delete;
    ~Buffer() { wl_buffer_destroy(buffer); }
};
// A window watched now or before: what it was asked, and its session while capturing.
struct WindowPictures::Capture {
    WindowPictures *owner;
    int id;
    int watchers = 0, pixelWidth = 1;
    bool live = false;
    ext_image_copy_capture_session_v1 *session = nullptr;
    ext_image_copy_capture_frame_v1 *frame = nullptr;
    // The box a scaled source was asked to fit, empty for one at the window's size.
    QSize box;
    // Counts the sessions, so that a frame of an earlier one is told apart when it is scaled.
    int sessions = 0;
    // The constraints being sent, and those the last done completed: the size of the buffer and
    // the first of the formats offered that imageFormat reads. Counting the batches tells
    // whether new ones came since a frame was asked for.
    QSize nextSize, size;
    std::optional<uint32_t> nextFormat, format;
    int constraints = 0, frameConstraints = 0;
    std::unique_ptr<Buffer> buffer;
    // A frame is being scaled from the buffer, which nothing is copied into meanwhile.
    bool scaling = false;
    // Frames that failed in a row for no stated reason.
    int failures = 0;
    QElapsedTimer started;
    // The next frame of a live picture, the interval after the last one started.
    QTimer next;
    quint64 serial = 0;
    // How long a live picture waits between frames.
    int interval() const { return box.isEmpty() ? fullSizeInterval : WindowPictures::interval; }
};

WindowPictures::WindowPictures(std::function<void()> flush) : flush_(std::move(flush)) {
    // Scaling a large window takes a while: two at once leave the rest of the machine alone,
    // and a window waiting for its turn only follows itself less often.
    pool_.setMaxThreadCount(2);
}
WindowPictures::~WindowPictures() {
    // The frames being scaled report back to this object.
    pool_.waitForDone();
}
void WindowPictures::setGlobals(wl_shm *shm, ext_image_copy_capture_manager_v1 *manager) {
    shm_ = shm;
    manager_ = manager;
}
WindowPictures::Capture *WindowPictures::find(int id) const {
    auto it = captures_.find(id);
    return it == captures_.end() ? nullptr : it->second.get();
}
void WindowPictures::watch(int id, paw_window_v1 *window, int pixelWidth, bool live) {
    auto &slot = captures_[id];
    if (!slot) {
        slot = std::make_unique<Capture>();
        slot->owner = this;
        slot->id = id;
        slot->next.setSingleShot(true);
        connect(&slot->next, &QTimer::timeout, this, [this, capture = slot.get()] {
            this->capture(*capture);
            flush_();
        });
    }
    auto &capture = *slot;
    ++capture.watchers;
    capture.pixelWidth = std::max(1, pixelWidth);
    capture.live = live;
    // A scaled source makes frames for the box it was asked: a new width wants a new one.
    const bool resized = capture.session && !capture.box.isEmpty() &&
                         capture.box != pictureBox(capture.pixelWidth);
    if (resized)
        end(capture);
    const bool starting =
        !capture.session && shm_ && manager_ && window &&
        paw_window_v1_get_version(window) >= PAW_WINDOW_V1_GET_CAPTURE_SOURCE_SINCE_VERSION;
    if (starting)
        start(capture, window);
    if (resized || starting)
        flush_();
}
void WindowPictures::unwatch(int id) {
    auto *capture = find(id);
    if (!capture || !capture->watchers)
        return;
    if (!--capture->watchers) {
        end(*capture);
        flush_();
    }
}
void WindowPictures::forget(int id) {
    auto it = captures_.find(id);
    if (it == captures_.end())
        return;
    end(*it->second);
    captures_.erase(it);
    QMutexLocker lock(&mutex_);
    pictures_.remove(id);
}
void WindowPictures::stop() {
    for (auto &[id, capture] : captures_)
        end(*capture);
}
QString WindowPictures::url(int id) const {
    auto *capture = find(id);
    if (!capture || !capture->serial)
        return {};
    return QStringLiteral("image://windows/%1/%2").arg(id).arg(capture->serial);
}
QImage WindowPictures::picture(int id) const {
    QMutexLocker lock(&mutex_);
    return pictures_.value(id);
}
// From version 3 the compositor scales the window down to the picture's box itself, on the GPU;
// before, a frame comes at the window's size.
void WindowPictures::start(Capture &capture, paw_window_v1 *window) {
    ext_image_capture_source_v1 *source;
    if (paw_window_v1_get_version(window) >=
        PAW_WINDOW_V1_GET_SCALED_CAPTURE_SOURCE_SINCE_VERSION) {
        capture.box = pictureBox(capture.pixelWidth);
        source = paw_window_v1_get_scaled_capture_source(
            window, uint32_t(capture.box.width()), uint32_t(capture.box.height()));
    } else {
        capture.box = {};
        source = paw_window_v1_get_capture_source(window);
    }
    capture.session = ext_image_copy_capture_manager_v1_create_session(manager_, source, 0);
    // The session holds on to what the source names.
    ext_image_capture_source_v1_destroy(source);
    static const ext_image_copy_capture_session_v1_listener listener{
        bufferSize, shmFormat, dmabufDevice, dmabufFormat, constraintsDone, stopped};
    ext_image_copy_capture_session_v1_add_listener(capture.session, &listener, &capture);
    ++capture.sessions;
    capture.failures = 0;
}
// Asks for a frame when the session can take one: its constraints are in, no frame is being
// copied or scaled, and a live picture's next is due.
void WindowPictures::capture(Capture &capture) {
    if (!capture.session || capture.frame || capture.scaling || capture.next.isActive() ||
        !capture.format || capture.size.isEmpty())
        return;
    const QSize size = capture.size;
    const uint32_t format = *capture.format;
    if (!capture.buffer || capture.buffer->size != size || capture.buffer->format != format) {
        capture.buffer.reset();
        const size_t bytes = size_t(size.width()) * size_t(size.height()) * 4;
        if (bytes > size_t(INT_MAX)) // wl_shm takes the size as an int
            return;
        const int fd = memfd_create("paw-window-picture", MFD_CLOEXEC);
        if (fd < 0 || ftruncate(fd, off_t(bytes)) != 0) {
            qWarning("Cannot allocate a window's picture: %s", std::strerror(errno));
            if (fd >= 0)
                close(fd);
            return;
        }
        void *data = mmap(nullptr, bytes, PROT_READ, MAP_SHARED, fd, 0);
        if (data == MAP_FAILED) {
            qWarning("Cannot map a window's picture: %s", std::strerror(errno));
            close(fd);
            return;
        }
        auto *pool = wl_shm_create_pool(shm_, fd, int(bytes));
        auto *buffer = wl_shm_pool_create_buffer(pool, 0, size.width(), size.height(),
                                                 size.width() * 4, format);
        wl_shm_pool_destroy(pool);
        close(fd);
        capture.buffer = std::make_unique<Buffer>(
            buffer, std::make_shared<const Mapping>(data, bytes), size, format);
    }
    capture.frame = ext_image_copy_capture_session_v1_create_frame(capture.session);
    static const ext_image_copy_capture_frame_v1_listener listener{transform, damage,
                                                                   presentationTime, ready, failed};
    ext_image_copy_capture_frame_v1_add_listener(capture.frame, &listener, &capture);
    ext_image_copy_capture_frame_v1_attach_buffer(capture.frame, capture.buffer->buffer);
    // Damage is not tracked: the whole buffer, every time.
    ext_image_copy_capture_frame_v1_damage_buffer(capture.frame, 0, 0, size.width(), size.height());
    ext_image_copy_capture_frame_v1_capture(capture.frame);
    capture.frameConstraints = capture.constraints;
    capture.started.start();
}
// Ends the capture, keeping the picture: everything made for it goes but a frame being scaled,
// which reports back.
void WindowPictures::end(Capture &capture) {
    if (capture.frame)
        ext_image_copy_capture_frame_v1_destroy(capture.frame);
    if (capture.session)
        ext_image_copy_capture_session_v1_destroy(capture.session);
    capture.frame = nullptr;
    capture.session = nullptr;
    capture.buffer.reset();
    capture.next.stop();
    capture.nextSize = capture.size = {};
    capture.nextFormat = capture.format = std::nullopt;
}
void WindowPictures::published(int id, int session, const QImage &picture) {
    auto *capture = find(id);
    if (!capture) // the window closed meanwhile
        return;
    capture->scaling = false;
    if (!picture.isNull()) {
        {
            QMutexLocker lock(&mutex_);
            pictures_.insert(id, picture);
        }
        ++capture->serial;
        Q_EMIT changed(id);
        capture = find(id);
        if (!capture)
            return;
    }
    if (!capture->session)
        return;
    if (session != capture->sessions) // of an earlier session, which this one's frame waited for
        this->capture(*capture);
    else if (!capture->live)
        end(*capture);
    else
        capture->next.start(std::max(0, capture->interval() - int(capture->started.elapsed())));
    flush_();
}

QImage::Format WindowPictures::imageFormat(uint32_t format) {
    switch (format) {
    // wl_shm's formats are packed into little-endian words: these lie in memory as R, G, B, A,
    // which is how Qt's byte-ordered formats lie on any machine.
    case WL_SHM_FORMAT_ABGR8888:
        return QImage::Format_RGBA8888_Premultiplied;
    case WL_SHM_FORMAT_XBGR8888:
        return QImage::Format_RGBX8888;
#if Q_BYTE_ORDER == Q_LITTLE_ENDIAN
    // Qt's 32-bit ARGB formats are packed into words of the machine's order, so these match
    // only on a little-endian one.
    case WL_SHM_FORMAT_ARGB8888:
        return QImage::Format_ARGB32_Premultiplied;
    case WL_SHM_FORMAT_XRGB8888:
        return QImage::Format_RGB32;
    case WL_SHM_FORMAT_ARGB2101010:
        return QImage::Format_A2RGB30_Premultiplied;
    case WL_SHM_FORMAT_XRGB2101010:
        return QImage::Format_RGB30;
    case WL_SHM_FORMAT_ABGR2101010:
        return QImage::Format_A2BGR30_Premultiplied;
    case WL_SHM_FORMAT_XBGR2101010:
        return QImage::Format_BGR30;
#endif
    default:
        return QImage::Format_Invalid;
    }
}
QSize WindowPictures::pictureBox(int pixelWidth) {
    const int width = std::max(1, pixelWidth);
    return {width, std::max(1, int(std::lround(width * 0.625)))};
}
QSize WindowPictures::pictureSize(const QSize &frame, int pixelWidth) {
    if (frame.isEmpty())
        return {};
    const QSize box = pictureBox(pixelWidth);
    const int width = box.width(), height = box.height();
    if (frame.width() <= width && frame.height() <= height)
        return frame;
    const double scale = std::min(double(width) / frame.width(), double(height) / frame.height());
    return {std::max(1, int(std::lround(frame.width() * scale))),
            std::max(1, int(std::lround(frame.height() * scale)))};
}
QImage WindowPictures::scaled(const QImage &frame, int pixelWidth) {
    const QSize size = pictureSize(frame.size(), pixelWidth);
    if (size.isEmpty())
        return {};
    // Copied even at its own size: the frame may lie in the buffer the next one is copied into.
    QImage picture = size == frame.size()
                         ? frame.copy()
                         : frame.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (!frame.hasAlphaChannel()) {
        // Qt takes the padding of a format without alpha for opaque, and the compositor need not
        // have written it so.
        picture.convertTo(QImage::Format_RGB32);
        for (int y = 0; y < picture.height(); ++y) {
            auto *line = reinterpret_cast<QRgb *>(picture.scanLine(y));
            for (int x = 0; x < picture.width(); ++x)
                line[x] |= 0xff000000;
        }
    }
    return picture;
}

void WindowPictures::bufferSize(void *data, ext_image_copy_capture_session_v1 *, uint32_t width,
                                uint32_t height) {
    // A window of more than 16384 pixels either way is not pictured.
    static_cast<Capture *>(data)->nextSize =
        width <= 16384 && height <= 16384 ? QSize(int(width), int(height)) : QSize();
}
void WindowPictures::shmFormat(void *data, ext_image_copy_capture_session_v1 *, uint32_t format) {
    auto &capture = *static_cast<Capture *>(data);
    if (!capture.nextFormat && imageFormat(format) != QImage::Format_Invalid)
        capture.nextFormat = format;
}
// Only shared memory is used.
void WindowPictures::dmabufDevice(void *, ext_image_copy_capture_session_v1 *, wl_array *) {}
void WindowPictures::dmabufFormat(void *, ext_image_copy_capture_session_v1 *, uint32_t,
                                  wl_array *) {}
void WindowPictures::constraintsDone(void *data, ext_image_copy_capture_session_v1 *) {
    auto &capture = *static_cast<Capture *>(data);
    capture.size = capture.nextSize;
    capture.format = capture.nextFormat;
    capture.nextSize = {};
    capture.nextFormat = std::nullopt;
    ++capture.constraints;
    capture.owner->capture(capture);
}
void WindowPictures::stopped(void *data, ext_image_copy_capture_session_v1 *) {
    auto &capture = *static_cast<Capture *>(data);
    capture.owner->end(capture);
}
// A window's frame comes as it is drawn, untransformed, and is copied whole.
void WindowPictures::transform(void *, ext_image_copy_capture_frame_v1 *, uint32_t) {}
void WindowPictures::damage(void *, ext_image_copy_capture_frame_v1 *, int32_t, int32_t, int32_t,
                            int32_t) {}
void WindowPictures::presentationTime(void *, ext_image_copy_capture_frame_v1 *, uint32_t,
                                      uint32_t, uint32_t) {}
void WindowPictures::ready(void *data, ext_image_copy_capture_frame_v1 *frame) {
    auto &capture = *static_cast<Capture *>(data);
    ext_image_copy_capture_frame_v1_destroy(frame);
    capture.frame = nullptr;
    capture.failures = 0;
    capture.scaling = true;
    // Scaled off the GUI thread, straight from the buffer: a large window would stall the bar.
    const auto &buffer = *capture.buffer;
    capture.owner->pool_.start([owner = capture.owner, id = capture.id, session = capture.sessions,
                                mapping = buffer.mapping, size = buffer.size, stride = buffer.stride,
                                format = imageFormat(buffer.format), width = capture.pixelWidth] {
        const QImage frame(static_cast<const uchar *>(mapping->data), size.width(), size.height(),
                           stride, format);
        const QImage picture = scaled(frame, width);
        QMetaObject::invokeMethod(
            owner, [owner, id, session, picture] { owner->published(id, session, picture); },
            Qt::QueuedConnection);
    });
}
void WindowPictures::failed(void *data, ext_image_copy_capture_frame_v1 *frame, uint32_t reason) {
    auto &capture = *static_cast<Capture *>(data);
    auto &owner = *capture.owner;
    ext_image_copy_capture_frame_v1_destroy(frame);
    capture.frame = nullptr;
    switch (reason) {
    case EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_BUFFER_CONSTRAINTS:
        // The window changed size. The new constraints may be in already; if not, their done
        // asks again.
        if (capture.constraints != capture.frameConstraints)
            owner.capture(capture);
        break;
    case EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_STOPPED:
        owner.end(capture);
        break;
    default:
        // The compositor says it may be tried again, but not forever.
        if (++capture.failures < 3)
            capture.next.start(capture.interval());
        else
            owner.end(capture);
    }
}
