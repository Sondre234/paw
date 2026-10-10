// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QAbstractListModel>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QVariantList>
#include <cstdint>
#include <memory>
#include <vector>

// The applications playing sound, one row each, updated in place so a slider being dragged
// is not rebuilt under the pointer.
class AudioStreams : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
  public:
    struct Stream {
        uint32_t id = 0;
        QString name, icon;
        int volume = 0;
        bool muted = false;
        // Paused: open, but playing nothing.
        bool corked = false;
        // The process playing it and that process's parents, nearest first (processAncestry);
        // empty when the server does not say which process it is.
        QList<int> processes = {};
    };
    enum Role { IdRole = Qt::UserRole + 1, NameRole, IconRole, VolumeRole, MutedRole };
    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(streams_.size()); }
    const std::vector<Stream> &all() const { return streams_; }
    void update(std::vector<Stream> streams);
    Stream *find(uint32_t id);
    void changed(uint32_t id);
  Q_SIGNALS:
    void countChanged();

  private:
    std::vector<Stream> streams_;
};

// The sound server as the panel shows it: the default output's volume, the outputs to choose
// from, the applications playing, and whether the default input (the microphone) is muted.
// Volumes are percentages. A backend delivers the state
// through update() and carries out the send*() requests; changes show at once, before the
// server confirms them, so successive wheel steps build on each other.
class Audio : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(int volume READ volume NOTIFY changed)
    Q_PROPERTY(bool muted READ muted NOTIFY changed)
    Q_PROPERTY(QString output READ output NOTIFY changed)
    // [{name, description}], in the server's order.
    Q_PROPERTY(QVariantList outputs READ outputs NOTIFY changed)
    Q_PROPERTY(AudioStreams *streams READ streams CONSTANT)
    // Whether there is a default input, and whether it is muted.
    Q_PROPERTY(bool hasInput READ hasInput NOTIFY changed)
    Q_PROPERTY(bool inputMuted READ inputMuted NOTIFY changed)
  public:
    struct Output {
        QString name, description;
        int volume = 0;
        bool muted = false;
    };
    struct State {
        QString output;
        std::vector<Output> outputs;
        std::vector<AudioStreams::Stream> streams;
        // The default input and the inputs there are, monitors of outputs left out.
        QString input = {};
        std::vector<Output> inputs = {};
    };
    static constexpr int maxVolume = 100;
    explicit Audio(QObject *parent = nullptr) : QObject(parent), streams_(this) {}
    bool available() const { return available_; }
    int volume() const;
    bool muted() const;
    QString output() const { return state_.output; }
    QVariantList outputs() const;
    AudioStreams *streams() { return &streams_; }
    bool hasInput() const { return currentInput() != nullptr; }
    bool inputMuted() const;
    void update(State state);
    void setUnavailable();
    Q_INVOKABLE void setVolume(int percent);
    // Steps the default output's volume, as the wheel does.
    Q_INVOKABLE void changeVolume(int delta);
    Q_INVOKABLE void toggleMute();
    // Makes `name` the default output and moves the playing applications to it.
    Q_INVOKABLE void setOutput(const QString &name);
    Q_INVOKABLE void setStreamVolume(int id, int percent);
    Q_INVOKABLE void setStreamMuted(int id, bool muted);
    Q_INVOKABLE void toggleStreamMute(int id);
    // Mutes the default input, or unmutes it.
    Q_INVOKABLE void toggleInputMute();
  Q_SIGNALS:
    void changed();

  protected:
    // What the sound server's backend carries out; without one (the preview's, or a build without
    // libpulse) nothing is.
    virtual void sendVolume(const QString & /*output*/, int /*percent*/) {}
    virtual void sendMute(const QString & /*output*/, bool /*muted*/) {}
    virtual void sendOutput(const QString & /*output*/, const std::vector<uint32_t> & /*streams*/) {}
    virtual void sendStreamVolume(uint32_t /*id*/, int /*percent*/) {}
    virtual void sendStreamMute(uint32_t /*id*/, bool /*muted*/) {}
    virtual void sendInputMute(const QString & /*input*/, bool /*muted*/) {}

  private:
    bool available_ = false;
    State state_;
    AudioStreams streams_;
    Output *current();
    const Output *current() const;
    Output *currentInput();
    const Output *currentInput() const;
};

// The PulseAudio (or PipeWire's pulse) backend, or one that stays unavailable when paw was
// built without libpulse.
std::unique_ptr<Audio> makeAudio();

// The process `pid` and its parents, nearest first, as the procfs at `proc` has them: no more than
// `depth` of them, ending before pid 1, and at the first whose parent cannot be read. Empty for
// pid 1 and below.
QList<int> processAncestry(int pid, const QString &proc = QStringLiteral("/proc"), int depth = 64);
// Whose a stream is: the nearest of `processes` (its process and that one's parents, nearest
// first) among `windows`, the processes that have windows; 0 when none is.
int soundOwner(const QList<int> &processes, const QSet<int> &windows);

// The sound of a window's process, for the taskbar's pictures of windows: whether it is `playing`
// (a stream of it is open, not paused and not muted) or else `muted` (a stream of it is muted,
// paused or not, so that it can be unmuted), and the loudest playing stream's `volume`. A stream
// is the window's when the window's process (`pid`) is the nearest of the stream's process and
// that one's parents to have a window, among the `pid` roles of `windows`, the taskbar's model:
// the windows of one process share its sound, and a program started from a terminal plays in the
// terminal's window unless it has one of its own. `audio` is the sound server.
class WindowSound : public QObject {
    Q_OBJECT
    Q_PROPERTY(QObject *audio READ audio WRITE setAudio NOTIFY audioChanged)
    Q_PROPERTY(QObject *windows READ windows WRITE setWindows NOTIFY windowsChanged)
    Q_PROPERTY(int pid READ pid WRITE setPid NOTIFY pidChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY changed)
    Q_PROPERTY(bool muted READ muted NOTIFY changed)
    Q_PROPERTY(int volume READ volume NOTIFY changed)
  public:
    using QObject::QObject;
    QObject *audio() const { return audio_; }
    void setAudio(QObject *audio);
    QObject *windows() const { return windows_; }
    void setWindows(QObject *windows);
    int pid() const { return pid_; }
    void setPid(int pid);
    bool playing() const { return playing_; }
    bool muted() const { return muted_; }
    int volume() const { return volume_; }
    // Mutes every stream of the window while one plays, or unmutes them while muted.
    Q_INVOKABLE void toggleMute();
  Q_SIGNALS:
    void audioChanged();
    void windowsChanged();
    void pidChanged();
    void changed();

  private:
    QPointer<Audio> audio_;
    QPointer<QAbstractItemModel> windows_;
    QList<QMetaObject::Connection> audioConnections_, windowConnections_;
    int pid_ = 0;
    bool playing_ = false, muted_ = false;
    int volume_ = 0;
    // The window's streams, as last worked out.
    std::vector<uint32_t> streams_;
    void update();
};
