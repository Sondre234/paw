// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "ext-image-capture-source-v1-client-protocol.h" // before the next, which names its interface
#include "paw-window-control-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QThreadPool>
#include <QTimer>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <wayland-client.h>

// Small pictures of windows for the taskbar's thumbnails, copied by the compositor through
// ext-image-copy-capture-v1 from the capture source paw-window-control-v1 gives for a
// window: from version 3 one the compositor scales down to the picture's size itself, smoothly,
// and from version 2 one at the window's size. It runs on its owner's Wayland connection, whose
// events the owner dispatches.
//
// While a window is watched it is captured into a shared-memory buffer: once, or again each time
// it redraws, at most every `interval` milliseconds (`fullSizeInterval` at the window's size).
// Each frame is scaled down if it needs to be, on a pool of threads, and kept, small, until the
// window closes; the session and its buffer last only while the window is watched. Windows are
// named by the owner's numbers for them.
class WindowPictures : public QObject {
    Q_OBJECT
  public:
    // How often a live picture may follow its window, never at the monitor's rate. At the
    // picture's size a frame is a few hundred kilobytes to copy and next to nothing to scale, so
    // 30 times a second, where a video moves; a large window at its own size is tens of megabytes
    // and a smooth scaling of all of them, so ten times.
    static constexpr int interval = 33;
    static constexpr int fullSizeInterval = 100;
    // `flush` sends what was asked on the owner's connection.
    explicit WindowPictures(std::function<void()> flush);
    ~WindowPictures() override;
    // The globals pictures are taken with, which the owner binds; without either, there are none.
    void setGlobals(wl_shm *shm, ext_image_copy_capture_manager_v1 *manager);
    // Counted per window: the first watch starts capturing, the last unwatch stops and keeps the
    // picture. `window` is the window's object, which gives a capture source from version 2 on
    // (with an older one, or none, nothing is captured). The picture is scaled to fit
    // `pixelWidth` by `pixelWidth * 0.625`; with `live` it follows the window until unwatched.
    // The latest watch's width and liveness apply, a scaled source being asked again for a new
    // width.
    void watch(int id, paw_window_v1 *window, int pixelWidth, bool live);
    void unwatch(int id);
    // The window closed: its capture ends and its picture goes.
    void forget(int id);
    // Ends every capture, keeping the pictures. The owner calls it before its connection goes,
    // and before destroying this.
    void stop();
    // image://windows/ID/SERIAL once the window has a picture, the serial changing with each new
    // one; "" until then.
    QString url(int id) const;
    // The window's last picture, null when it has none. Safe from any thread.
    QImage picture(int id) const;

    // The QImage format reading a wl_shm buffer of `format` as it lies in memory, Invalid for one
    // this cannot read.
    static QImage::Format imageFormat(uint32_t format);
    // The box a picture fits in: `pixelWidth` (at least 1) by round(`pixelWidth` * 0.625), the
    // size asked of a scaled capture source.
    static QSize pictureBox(int pixelWidth);
    // The size a frame of `frame` is kept at: scaled down, never up, to fit within pictureBox,
    // keeping its aspect ratio.
    static QSize pictureSize(const QSize &frame, int pixelWidth);
    // `frame` at pictureSize, sharing no memory with it, and opaque when its format has no alpha
    // (whatever the padding holds).
    static QImage scaled(const QImage &frame, int pixelWidth);
  Q_SIGNALS:
    // The window has a new picture.
    void changed(int id);

  private:
    struct Mapping;
    struct Buffer;
    struct Capture;
    std::function<void()> flush_;
    wl_shm *shm_ = nullptr;
    ext_image_copy_capture_manager_v1 *manager_ = nullptr;
    std::unordered_map<int, std::unique_ptr<Capture>> captures_;
    // The pictures, read by the image provider on QML's threads.
    mutable QMutex mutex_;
    QHash<int, QImage> pictures_;
    QThreadPool pool_;
    Capture *find(int id) const;
    void start(Capture &capture, paw_window_v1 *window);
    void capture(Capture &capture);
    void end(Capture &capture);
    void published(int id, int session, const QImage &picture);
    static void bufferSize(void *, ext_image_copy_capture_session_v1 *, uint32_t, uint32_t);
    static void shmFormat(void *, ext_image_copy_capture_session_v1 *, uint32_t);
    static void dmabufDevice(void *, ext_image_copy_capture_session_v1 *, wl_array *);
    static void dmabufFormat(void *, ext_image_copy_capture_session_v1 *, uint32_t, wl_array *);
    static void constraintsDone(void *, ext_image_copy_capture_session_v1 *);
    static void stopped(void *, ext_image_copy_capture_session_v1 *);
    static void transform(void *, ext_image_copy_capture_frame_v1 *, uint32_t);
    static void damage(void *, ext_image_copy_capture_frame_v1 *, int32_t, int32_t, int32_t, int32_t);
    static void presentationTime(void *, ext_image_copy_capture_frame_v1 *, uint32_t, uint32_t,
                                 uint32_t);
    static void ready(void *, ext_image_copy_capture_frame_v1 *);
    static void failed(void *, ext_image_copy_capture_frame_v1 *, uint32_t);
};
