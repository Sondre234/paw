// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio.hpp"
#include <QFile>
#include <algorithm>

int AudioStreams::rowCount(const QModelIndex &parent) const {
    return parent.isValid() ? 0 : int(streams_.size());
}
QVariant AudioStreams::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= int(streams_.size()))
        return {};
    const auto &stream = streams_[index.row()];
    switch (role) {
    case IdRole:
        return int(stream.id);
    case NameRole:
        return stream.name;
    case IconRole:
        return stream.icon;
    case VolumeRole:
        return stream.volume;
    case MutedRole:
        return stream.muted;
    }
    return {};
}
QHash<int, QByteArray> AudioStreams::roleNames() const {
    return {{IdRole, "streamId"},
            {NameRole, "name"},
            {IconRole, "icon"},
            {VolumeRole, "volume"},
            {MutedRole, "muted"}};
}
// Keeps the rows of streams that are still playing and appends the new ones.
void AudioStreams::update(std::vector<Stream> streams) {
    auto listed = [](auto &list, uint32_t id) {
        return std::find_if(list.begin(), list.end(), [id](auto &s) { return s.id == id; });
    };
    const int before = count();
    for (int row = count() - 1; row >= 0; --row)
        if (listed(streams, streams_[row].id) == streams.end()) {
            beginRemoveRows({}, row, row);
            streams_.erase(streams_.begin() + row);
            endRemoveRows();
        }
    for (auto &stream : streams) {
        auto existing = listed(streams_, stream.id);
        if (existing == streams_.end()) {
            beginInsertRows({}, count(), count());
            streams_.push_back(std::move(stream));
            endInsertRows();
        } else if (existing->name != stream.name || existing->icon != stream.icon ||
                   existing->volume != stream.volume || existing->muted != stream.muted ||
                   existing->corked != stream.corked || existing->processes != stream.processes) {
            *existing = std::move(stream);
            changed(existing->id);
        }
    }
    if (count() != before)
        Q_EMIT countChanged();
}
AudioStreams::Stream *AudioStreams::find(uint32_t id) {
    for (auto &stream : streams_)
        if (stream.id == id)
            return &stream;
    return nullptr;
}
void AudioStreams::changed(uint32_t id) {
    for (int row = 0; row < count(); ++row)
        if (streams_[row].id == id)
            Q_EMIT dataChanged(index(row), index(row));
}

Audio::Output *Audio::current() {
    for (auto &output : state_.outputs)
        if (output.name == state_.output)
            return &output;
    return nullptr;
}
const Audio::Output *Audio::current() const { return const_cast<Audio *>(this)->current(); }
Audio::Output *Audio::currentInput() {
    for (auto &input : state_.inputs)
        if (input.name == state_.input)
            return &input;
    return nullptr;
}
const Audio::Output *Audio::currentInput() const {
    return const_cast<Audio *>(this)->currentInput();
}
bool Audio::inputMuted() const { return currentInput() && currentInput()->muted; }
int Audio::volume() const { return current() ? current()->volume : 0; }
bool Audio::muted() const { return current() && current()->muted; }
QVariantList Audio::outputs() const {
    QVariantList list;
    for (const auto &output : state_.outputs)
        list.push_back(QVariantMap{{"name", output.name}, {"description", output.description}});
    return list;
}
void Audio::update(State state) {
    streams_.update(std::move(state.streams));
    state_ = std::move(state);
    available_ = true;
    Q_EMIT changed();
}
void Audio::setUnavailable() {
    streams_.update({});
    state_ = {};
    available_ = false;
    Q_EMIT changed();
}
void Audio::setVolume(int percent) {
    auto *output = current();
    percent = std::clamp(percent, 0, maxVolume);
    if (!output || output->volume == percent)
        return;
    output->volume = percent;
    sendVolume(output->name, percent);
    Q_EMIT changed();
}
void Audio::changeVolume(int delta) { setVolume(volume() + delta); }
void Audio::toggleMute() {
    if (auto *output = current()) {
        output->muted = !output->muted;
        sendMute(output->name, output->muted);
        Q_EMIT changed();
    }
}
void Audio::toggleInputMute() {
    if (auto *input = currentInput()) {
        input->muted = !input->muted;
        sendInputMute(input->name, input->muted);
        Q_EMIT changed();
    }
}
void Audio::setOutput(const QString &name) {
    if (name == state_.output)
        return;
    std::vector<uint32_t> ids;
    for (int row = 0; row < streams_.count(); ++row)
        ids.push_back(streams_.data(streams_.index(row), AudioStreams::IdRole).toUInt());
    state_.output = name;
    sendOutput(name, ids);
    Q_EMIT changed();
}
void Audio::setStreamVolume(int id, int percent) {
    auto *stream = streams_.find(uint32_t(id));
    percent = std::clamp(percent, 0, maxVolume);
    if (!stream || stream->volume == percent)
        return;
    stream->volume = percent;
    sendStreamVolume(stream->id, percent);
    streams_.changed(stream->id);
}
void Audio::setStreamMuted(int id, bool muted) {
    auto *stream = streams_.find(uint32_t(id));
    if (!stream || stream->muted == muted)
        return;
    stream->muted = muted;
    sendStreamMute(stream->id, muted);
    streams_.changed(stream->id);
}
void Audio::toggleStreamMute(int id) {
    if (auto *stream = streams_.find(uint32_t(id)))
        setStreamMuted(id, !stream->muted);
}

QList<int> processAncestry(int pid, const QString &proc, int depth) {
    QList<int> processes;
    // A loop in a made-up procfs ends too.
    while (pid > 1 && processes.size() < depth && !processes.contains(pid)) {
        processes.push_back(pid);
        QFile stat(proc + '/' + QString::number(pid) + "/stat");
        if (!stat.open(QIODevice::ReadOnly))
            break;
        // "pid (name) state ppid ...": the name may hold spaces and parentheses.
        const QByteArray text = stat.read(512);
        const qsizetype end = text.lastIndexOf(')');
        const QList<QByteArray> fields = text.mid(end + 1).simplified().split(' ');
        bool read = false;
        if (end >= 0 && fields.size() >= 2)
            pid = fields[1].toInt(&read);
        if (!read)
            break;
    }
    return processes;
}
int soundOwner(const QList<int> &processes, const QSet<int> &windows) {
    for (int pid : processes)
        if (windows.contains(pid))
            return pid;
    return 0;
}

void WindowSound::setAudio(QObject *object) {
    auto *audio = qobject_cast<Audio *>(object);
    if (audio == audio_)
        return;
    for (const auto &connection : std::as_const(audioConnections_))
        disconnect(connection);
    audioConnections_.clear();
    audio_ = audio;
    if (audio) {
        auto *streams = audio->streams();
        audioConnections_ = {
            connect(streams, &QAbstractItemModel::rowsInserted, this, &WindowSound::update),
            connect(streams, &QAbstractItemModel::rowsRemoved, this, &WindowSound::update),
            connect(streams, &QAbstractItemModel::modelReset, this, &WindowSound::update),
            connect(streams, &QAbstractItemModel::dataChanged, this, &WindowSound::update)};
    }
    Q_EMIT audioChanged();
    update();
}
void WindowSound::setWindows(QObject *object) {
    auto *windows = qobject_cast<QAbstractItemModel *>(object);
    if (windows == windows_)
        return;
    for (const auto &connection : std::as_const(windowConnections_))
        disconnect(connection);
    windowConnections_.clear();
    windows_ = windows;
    if (windows) {
        // Which processes have windows changes as they come and go, or as one learns its
        // process; a new picture of one, many times a second, changes nothing here.
        auto pidChanged = [this](const QModelIndex &, const QModelIndex &,
                                 const QList<int> &roles) {
            if (roles.isEmpty() ||
                (windows_ && roles.contains(windows_->roleNames().key("pid", -1))))
                update();
        };
        windowConnections_ = {
            connect(windows, &QAbstractItemModel::rowsInserted, this, &WindowSound::update),
            connect(windows, &QAbstractItemModel::rowsRemoved, this, &WindowSound::update),
            connect(windows, &QAbstractItemModel::modelReset, this, &WindowSound::update),
            connect(windows, &QAbstractItemModel::dataChanged, this, pidChanged)};
    }
    Q_EMIT windowsChanged();
    update();
}
void WindowSound::setPid(int pid) {
    if (pid == pid_)
        return;
    pid_ = pid;
    Q_EMIT pidChanged();
    update();
}
void WindowSound::update() {
    bool playing = false, muted = false;
    int volume = 0;
    std::vector<uint32_t> streams;
    if (audio_ && pid_ > 1) {
        QSet<int> owners{pid_};
        if (windows_) {
            const int role = windows_->roleNames().key("pid", -1);
            for (int row = 0; role >= 0 && row < windows_->rowCount(); ++row)
                if (const int pid = windows_->data(windows_->index(row, 0), role).toInt(); pid > 1)
                    owners.insert(pid);
        }
        for (const auto &stream : audio_->streams()->all()) {
            if (soundOwner(stream.processes, owners) != pid_)
                continue;
            streams.push_back(stream.id);
            muted |= stream.muted;
            if (!stream.muted && !stream.corked) {
                playing = true;
                volume = std::max(volume, stream.volume);
            }
        }
        muted = muted && !playing;
    }
    streams_ = std::move(streams);
    if (playing == playing_ && muted == muted_ && volume == volume_)
        return;
    playing_ = playing;
    muted_ = muted;
    volume_ = volume;
    Q_EMIT changed();
}
void WindowSound::toggleMute() {
    if (!audio_ || (!playing_ && !muted_))
        return;
    // Each change comes back through the streams' model and works this out anew.
    const bool mute = playing_;
    const auto streams = streams_;
    for (auto id : streams)
        if (audio_)
            audio_->setStreamMuted(int(id), mute);
}

#if !PAW_PULSE
namespace {
class NoAudio : public Audio {
  public:
    using Audio::Audio;

  protected:
    void sendVolume(const QString &, int) override {}
    void sendMute(const QString &, bool) override {}
    void sendOutput(const QString &, const std::vector<uint32_t> &) override {}
    void sendStreamVolume(uint32_t, int) override {}
    void sendStreamMute(uint32_t, bool) override {}
};
} // namespace
std::unique_ptr<Audio> makeAudio() { return std::make_unique<NoAudio>(); }
#endif
