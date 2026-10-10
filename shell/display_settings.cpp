// SPDX-License-Identifier: GPL-3.0-or-later
#include "display_settings.hpp"
#include "display_layout.hpp"
#include <algorithm>
#include <cmath>

namespace {
// "2560x1440@143.912", "2560x1440", or with "*" after it for the preferred one.
bool parseMode(QString text, int &width, int &height, int &refresh, bool &preferred) {
    preferred = text.endsWith('*');
    if (preferred)
        text.chop(1);
    const auto at = text.indexOf('@');
    const auto size = text.left(at < 0 ? text.size() : at).split('x');
    bool wide = false, high = false, rated = true;
    if (size.size() != 2)
        return false;
    width = size[0].toInt(&wide);
    height = size[1].toInt(&high);
    const double hz = at < 0 ? 0 : text.mid(at + 1).toDouble(&rated);
    refresh = static_cast<int>(std::lround(hz * 1000));
    return wide && high && rated && width > 0 && height > 0;
}

QString modeText(int width, int height, int refresh) {
    if (refresh <= 0)
        return QString("%1x%2").arg(width).arg(height);
    return QString("%1x%2@%3.%4")
        .arg(width)
        .arg(height)
        .arg(refresh / 1000)
        .arg(refresh % 1000, 3, 10, QChar('0'));
}

QString rateLabel(int refresh) {
    if (refresh <= 0)
        return "Unknown";
    // Two decimals where the rate has any: 143.91 Hz, 60 Hz.
    const bool whole = refresh % 1000 == 0;
    return QString::number(refresh / 1000.0, 'f', whole ? 0 : 2) + " Hz";
}

// A sentence from the compositor's error: capitalised (or not, to follow other words) and ended.
QString sentence(QString text, bool capital = true) {
    text = text.trimmed();
    if (text.startsWith("error: "))
        text = text.mid(7);
    if (!text.isEmpty())
        text[0] = capital ? text[0].toUpper() : text[0].toLower();
    if (!text.isEmpty() && !text.endsWith('.'))
        text += '.';
    return text;
}
} // namespace

QRect DisplaySettings::Monitor::rect() const {
    return {QPoint(x, y), display_layout::logicalSize({width, height}, scale, transform)};
}

DisplaySettings::DisplaySettings(Ask ask, QObject *parent) : QObject(parent), ask_(std::move(ask)) {
    countdown_.setInterval(250);
    connect(&countdown_, &QTimer::timeout, this, &DisplaySettings::tick);
}

DisplaySettings::Monitor *DisplaySettings::find(const QString &name) {
    for (auto &monitor : monitors_)
        if (monitor.name == name)
            return &monitor;
    return nullptr;
}
const DisplaySettings::Monitor *DisplaySettings::find(const QString &name) const {
    for (const auto &monitor : monitors_)
        if (monitor.name == name)
            return &monitor;
    return nullptr;
}

QVariantMap DisplaySettings::record(const Monitor &m) const {
    // Each resolution once, the largest first, and the rates at the one it has.
    QVariantList resolutions, rates;
    QList<Mode> sizes;
    for (const auto &mode : m.modes) {
        auto same = std::find_if(sizes.begin(), sizes.end(), [&](const Mode &size) {
            return size.width == mode.width && size.height == mode.height;
        });
        if (same == sizes.end())
            sizes.push_back(mode);
        else
            same->preferred |= mode.preferred;
        if (mode.width == m.width && mode.height == m.height)
            rates.push_back(
                QVariantMap{{"refresh", mode.refresh}, {"label", rateLabel(mode.refresh)}});
    }
    std::stable_sort(sizes.begin(), sizes.end(), [](const Mode &a, const Mode &b) {
        return qint64(a.width) * a.height > qint64(b.width) * b.height ||
               (qint64(a.width) * a.height == qint64(b.width) * b.height && a.width > b.width);
    });
    for (const auto &size : sizes)
        resolutions.push_back(
            QVariantMap{{"width", size.width},
                        {"height", size.height},
                        {"preferred", size.preferred},
                        {"label", QString("%1 × %2").arg(size.width).arg(size.height) +
                                      (size.preferred ? " (recommended)" : "")}});
    std::stable_sort(rates.begin(), rates.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value("refresh").toInt() > b.toMap().value("refresh").toInt();
    });
    QStringList mirroredBy;
    for (const auto &other : monitors_)
        if (other.mirror == m.name)
            mirroredBy << other.name;
    const QString description = m.description.simplified();
    const auto logical = m.rect().size();
    return {
        {"name", m.name},
        {"title", m.builtIn               ? QString("Built-in display")
                  : description.isEmpty() ? m.name
                                          : description},
        {"builtIn", m.builtIn},
        {"enabled", m.enabled},
        {"state", m.state},
        {"mirror", m.mirror},
        {"mirroredBy", mirroredBy},
        {"x", m.x},
        {"y", m.y},
        {"width", m.width},
        {"height", m.height},
        {"refresh", m.refresh},
        {"scale", m.scale},
        {"transform", m.transform},
        {"logicalWidth", logical.width()},
        {"logicalHeight", logical.height()},
        {"inLayout", m.inLayout()},
        {"vrrSupported", m.vrrSupported},
        {"vrr", m.vrr},
        {"bitDepth", m.bitDepth},
        {"drawnDepth", m.drawnDepth},
        {"hdr", m.hdr},
        {"hdrActive", m.hdrActive},
        {"hdrPossible", m.hdrWhy.isEmpty()},
        {"hdrWhy", m.hdrWhy},
        {"primary", m.primary},
        {"resolutions", resolutions},
        {"rates", rates}};
}

QVariantList DisplaySettings::monitors() const {
    QVariantList list;
    for (const auto &monitor : monitors_)
        list.push_back(record(monitor));
    return list;
}

QVariantMap DisplaySettings::current() const {
    const auto *monitor = find(selected_);
    return monitor ? record(*monitor) : QVariantMap();
}

QSize DisplaySettings::extent() const {
    QRect bounds;
    for (const auto &monitor : monitors_)
        if (monitor.inLayout())
            bounds = bounds.united(monitor.rect());
    return {bounds.x() + bounds.width(), bounds.y() + bounds.height()};
}

int DisplaySettings::layoutCount() const {
    return static_cast<int>(
        std::count_if(monitors_.begin(), monitors_.end(),
                      [](const Monitor &monitor) { return monitor.inLayout(); }));
}

bool DisplaySettings::changed() const {
    if (monitors_.size() != loaded_.size())
        return true;
    for (qsizetype i = 0; i < monitors_.size(); ++i) {
        const auto &a = monitors_[i], &b = loaded_[i];
        if (a.enabled != b.enabled || a.mirror != b.mirror || a.width != b.width ||
            a.height != b.height || a.refresh != b.refresh || std::abs(a.scale - b.scale) > 1e-6 ||
            a.transform != b.transform || a.vrr != b.vrr || a.bitDepth != b.bitDepth ||
            a.hdr != b.hdr || a.primary != b.primary ||
            (a.inLayout() && (a.x != b.x || a.y != b.y)))
            return true;
    }
    return false;
}

bool DisplaySettings::kept() const {
    return std::any_of(monitors_.begin(), monitors_.end(),
                       [](const Monitor &m) { return m.source == "window"; });
}

void DisplaySettings::request(const QByteArray &line,
                              std::function<void(const QByteArray &)> done) {
    ++pending_;
    if (pending_ == 1)
        Q_EMIT busyChanged();
    ask_(line, [this, done = std::move(done)](const QByteArray &reply) {
        --pending_;
        if (pending_ == 0)
            Q_EMIT busyChanged();
        done(reply);
    });
}

void DisplaySettings::show(const QString &output) {
    const bool was = open_;
    open_ = true;
    output_ = output;
    if (!was)
        setMessage({});
    Q_EMIT openChanged();
    // Opened again while a trial runs, it keeps what is on trial in view.
    if (!was || !trial_ || monitors_.isEmpty())
        reload();
}

void DisplaySettings::close() {
    if (!open_)
        return;
    open_ = false;
    if (trial_)
        revert();
    Q_EMIT openChanged();
}

void DisplaySettings::reload() {
    request("get monitors\n", [this](const QByteArray &reply) {
        if (!load(reply))
            setMessage("The compositor did not say what the monitors are.");
    });
}

bool DisplaySettings::load(const QByteArray &reply) {
    const auto lines = QString::fromUtf8(reply).split('\n');
    if (lines.isEmpty() || lines[0] != "ok")
        return false;
    QList<Monitor> monitors;
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const auto fields = lines[i].split('\t');
        if (fields.size() < 20)
            continue;
        Monitor m;
        m.name = fields[0];
        m.description = fields[1];
        m.builtIn = fields[2] == "1";
        m.source = fields[3];
        m.enabled = fields[4] == "1";
        m.state = fields[5];
        m.mirror = fields[6] == "-" ? QString() : fields[6];
        m.x = fields[7].toInt();
        m.y = fields[8].toInt();
        bool preferred = false;
        if (!parseMode(fields[9], m.width, m.height, m.refresh, preferred))
            continue;
        m.scale = fields[10].toDouble();
        m.transform = fields[11].toInt();
        m.vrrSupported = fields[12] != "-";
        m.vrr = fields[12] == "on";
        m.bitDepth = fields[13].toInt() == 10 ? 10 : 8;
        m.drawnDepth = fields[14].toInt() == 10 ? 10 : 8;
        m.hdr = fields[15] == "on";
        m.hdrActive = fields[16] == "hdr";
        m.hdrWhy = fields[17] == "-" ? QString() : fields[17];
        m.primary = fields[18] == "1";
        for (const auto &text : fields[19].split(',', Qt::SkipEmptyParts)) {
            Mode mode;
            if (parseMode(text, mode.width, mode.height, mode.refresh, mode.preferred))
                m.modes.push_back(mode);
        }
        if (m.modes.isEmpty())
            m.modes.push_back({m.width, m.height, m.refresh, false});
        monitors.push_back(m);
    }
    // The arrangement starts at 0, 0; those out of it move with it.
    QRect bounds;
    for (const auto &monitor : monitors)
        if (monitor.inLayout())
            bounds = bounds.isNull() ? monitor.rect() : bounds.united(monitor.rect());
    for (auto &monitor : monitors) {
        monitor.x -= bounds.x();
        monitor.y -= bounds.y();
    }
    monitors_ = loaded_ = monitors;
    Q_EMIT monitorsChanged();
    // The selection stays on its monitor; at first, or with that one gone, it is the monitor the
    // window opened on, else the first.
    if (!find(selected_))
        selected_ = find(output_) ? output_ : monitors_.isEmpty() ? QString() : monitors_[0].name;
    Q_EMIT selectedChanged();
    return true;
}

bool DisplaySettings::handle(const QString &line) {
    if (line.startsWith("display-settings ")) {
        show(line.mid(17));
        return true;
    }
    if (line.startsWith("monitors-trial ")) {
        setTrial(true, line.mid(15).toInt());
        return true;
    }
    if (line == "monitors-kept") {
        ended({});
        return true;
    }
    if (line.startsWith("monitors-reverted ")) {
        const auto reason = line.mid(18);
        ended(reason == "timeout" ? "The settings were not kept; the previous ones are back."
              : reason == "reload"
                  ? "The configuration was reloaded; the previous settings are back."
              : reason.startsWith("refused ")
                  ? reason.mid(8) + " did not take its settings; the previous ones are back."
                  : QString());
        return true;
    }
    return false;
}

void DisplaySettings::ended(const QString &message) {
    const bool was = trial_;
    setTrial(false);
    if (!message.isEmpty() && open_)
        setMessage(message);
    if (open_ && was)
        reload();
}

void DisplaySettings::setTrial(bool trial, int milliseconds) {
    if (trial) {
        deadline_.setRemainingTime(milliseconds);
        countdown_.start();
    } else {
        countdown_.stop();
    }
    if (trial != trial_) {
        trial_ = trial;
        Q_EMIT trialChanged();
    }
    tick();
}

void DisplaySettings::tick() {
    const int left = trial_ ? static_cast<int>((deadline_.remainingTime() + 999) / 1000) : 0;
    if (left != secondsLeft_) {
        secondsLeft_ = left;
        Q_EMIT secondsLeftChanged();
    }
}

void DisplaySettings::setMessage(const QString &message) {
    if (message == message_)
        return;
    message_ = message;
    Q_EMIT messageChanged();
}

void DisplaySettings::select(const QString &name) {
    if (name == selected_ || !find(name))
        return;
    selected_ = name;
    Q_EMIT selectedChanged();
}

void DisplaySettings::edited() {
    setMessage({});
    Q_EMIT monitorsChanged();
    Q_EMIT selectedChanged();
}

void DisplaySettings::arrange(const QString &first) {
    QList<Monitor *> placed;
    for (auto &monitor : monitors_)
        if (monitor.inLayout())
            placed.push_back(&monitor);
    // The one asked for stays, else the top left one; the others settle round it.
    std::stable_sort(placed.begin(), placed.end(), [&first](const Monitor *a, const Monitor *b) {
        if ((a->name == first) != (b->name == first))
            return a->name == first;
        return a->x < b->x || (a->x == b->x && a->y < b->y);
    });
    QList<QRect> rects;
    for (const auto *monitor : placed)
        rects.push_back(monitor->rect());
    rects = display_layout::normalise(display_layout::settle(rects));
    for (qsizetype i = 0; i < placed.size(); ++i) {
        placed[i]->x = rects[i].x();
        placed[i]->y = rects[i].y();
    }
}

void DisplaySettings::placeRight(Monitor &monitor) {
    // Level with the top of the one furthest right.
    int x = 0, y = 0;
    for (const auto &other : monitors_)
        if (&other != &monitor && other.inLayout() && other.x + other.rect().width() > x) {
            x = other.x + other.rect().width();
            y = other.y;
        }
    monitor.x = x;
    monitor.y = y;
}

void DisplaySettings::setEnabled(const QString &name, bool enabled) {
    auto *monitor = find(name);
    if (!monitor || monitor->enabled == enabled ||
        (!enabled && monitor->inLayout() && layoutCount() == 1))
        return;
    monitor->enabled = enabled;
    if (enabled && monitor->inLayout())
        placeRight(*monitor);
    if (!enabled) {
        // Its mirrors show the desktop of their own then, right of the others.
        for (auto &other : monitors_)
            if (other.mirror == name)
                setMirror(other.name, {});
        if (monitor->primary) {
            monitor->primary = false;
            for (auto &other : monitors_)
                if (other.inLayout()) {
                    other.primary = true;
                    break;
                }
        }
    }
    arrange();
    edited();
}

void DisplaySettings::setMirror(const QString &name, const QString &source) {
    auto *monitor = find(name);
    const auto *shown = source.isEmpty() ? nullptr : find(source);
    if (!monitor || monitor->mirror == source ||
        (!source.isEmpty() && (!shown || shown == monitor || !shown->inLayout())))
        return;
    if (source.isEmpty()) {
        monitor->mirror.clear();
        placeRight(*monitor);
    } else {
        // Those mirroring it show what it will show; the primary monitor stays in the
        // arrangement.
        if (layoutCount() == 1 && monitor->inLayout())
            return;
        monitor->mirror = source;
        for (auto &other : monitors_)
            if (other.mirror == name)
                other.mirror = source;
        if (monitor->primary) {
            monitor->primary = false;
            find(source)->primary = true;
        }
    }
    arrange();
    edited();
}

void DisplaySettings::setResolution(const QString &name, int width, int height) {
    auto *monitor = find(name);
    if (!monitor || (monitor->width == width && monitor->height == height))
        return;
    int refresh = -1, fastest = -1;
    for (const auto &mode : monitor->modes) {
        if (mode.width != width || mode.height != height)
            continue;
        if (std::abs(mode.refresh - monitor->refresh) < 500)
            refresh = mode.refresh;
        fastest = std::max(fastest, mode.refresh);
    }
    if (fastest < 0)
        return;
    monitor->width = width;
    monitor->height = height;
    monitor->refresh = refresh >= 0 ? refresh : fastest;
    arrange(name);
    edited();
}

void DisplaySettings::setRefresh(const QString &name, int refresh) {
    auto *monitor = find(name);
    if (!monitor || monitor->refresh == refresh ||
        std::none_of(monitor->modes.begin(), monitor->modes.end(), [&](const Mode &mode) {
            return mode.width == monitor->width && mode.height == monitor->height &&
                   mode.refresh == refresh;
        }))
        return;
    monitor->refresh = refresh;
    edited();
}

void DisplaySettings::setScale(const QString &name, double scale) {
    auto *monitor = find(name);
    scale = std::clamp(scale, 0.25, 10.0);
    if (!monitor || std::abs(monitor->scale - scale) < 1e-6)
        return;
    monitor->scale = scale;
    arrange(name);
    edited();
}

void DisplaySettings::setTransform(const QString &name, int transform) {
    auto *monitor = find(name);
    if (!monitor || transform < 0 || transform > 7 || monitor->transform == transform)
        return;
    monitor->transform = transform;
    arrange(name);
    edited();
}

void DisplaySettings::setVrr(const QString &name, bool on) {
    auto *monitor = find(name);
    if (!monitor || !monitor->vrrSupported || monitor->vrr == on)
        return;
    monitor->vrr = on;
    edited();
}

void DisplaySettings::setPrimary(const QString &name) {
    auto *monitor = find(name);
    if (!monitor || !monitor->inLayout() || monitor->primary)
        return;
    for (auto &other : monitors_)
        other.primary = &other == monitor;
    edited();
}

void DisplaySettings::setBitDepth(const QString &name, int depth) {
    auto *monitor = find(name);
    if (!monitor || (depth != 8 && depth != 10) || monitor->bitDepth == depth)
        return;
    monitor->bitDepth = depth;
    edited();
}

void DisplaySettings::setHdr(const QString &name, bool on) {
    auto *monitor = find(name);
    if (!monitor || monitor->hdr == on || (on && !monitor->hdrWhy.isEmpty()))
        return;
    monitor->hdr = on;
    edited();
}

QPoint DisplaySettings::snapped(const QString &name, int x, int y, int threshold) const {
    const auto *monitor = find(name);
    if (!monitor)
        return {x, y};
    QList<QRect> others;
    for (const auto &other : monitors_)
        if (&other != monitor && other.inLayout())
            others.push_back(other.rect());
    return display_layout::snap(others, monitor->rect().size(), {x, y}, threshold);
}

void DisplaySettings::place(const QString &name, int x, int y, int threshold) {
    auto *monitor = find(name);
    if (!monitor || !monitor->inLayout())
        return;
    const auto at = snapped(name, x, y, threshold);
    monitor->x = at.x();
    monitor->y = at.y();
    // The others settle round it where it left a gap.
    arrange(name);
    select(name);
    edited();
}

QByteArray DisplaySettings::applyRequest() const {
    QStringList words{"monitors", "apply"};
    for (const auto &m : monitors_) {
        words << m.name << QString("enabled=%1").arg(m.enabled ? "on" : "off")
              << "mode=" + modeText(m.width, m.height, m.refresh)
              << "scale=" + QString::number(m.scale, 'g', 9)
              << QString("transform=%1").arg(m.transform)
              << QString("position=%1,%2").arg(m.x).arg(m.y);
        if (m.vrrSupported)
            words << QString("vrr=%1").arg(m.vrr ? "on" : "off");
        words << "mirror=" + (m.mirror.isEmpty() ? QString("-") : m.mirror)
              << QString("bit_depth=%1").arg(m.bitDepth)
              << QString("hdr=%1").arg(m.hdr ? "on" : "off")
              << QString("primary=%1").arg(m.primary ? "on" : "off");
    }
    return words.join(' ').toUtf8() + "\n";
}

void DisplaySettings::apply() {
    if (busy() || trial_ || monitors_.isEmpty())
        return;
    setMessage({});
    request(applyRequest(), [this](const QByteArray &reply) {
        const auto lines = QString::fromUtf8(reply).split('\n');
        if (lines.value(0) == "ok") {
            // The compositor says so to every subscriber too; this is the same. What is on trial
            // is what is in force now.
            setTrial(true, lines.value(1).toInt());
            reload();
            return;
        }
        setMessage(sentence(QString::fromUtf8(reply)));
        reload();
    });
}

void DisplaySettings::keep() {
    if (!trial_)
        return;
    request("monitors keep\n", [this](const QByteArray &reply) {
        if (!reply.startsWith("ok"))
            setMessage("Kept for now, but " + sentence(QString::fromUtf8(reply), false));
        ended({});
    });
}

void DisplaySettings::revert() {
    if (!trial_)
        return;
    request("monitors revert\n", [this](const QByteArray &) { ended({}); });
}

void DisplaySettings::reset() {
    if (busy() || trial_)
        return;
    setMessage({});
    request("monitors reset\n", [this](const QByteArray &reply) {
        const auto lines = QString::fromUtf8(reply).split('\n');
        if (lines.value(0) == "ok") {
            setTrial(true, lines.value(1).toInt());
            reload();
            return;
        }
        setMessage(sentence(QString::fromUtf8(reply)));
    });
}

QVariantList DisplaySettings::scales() { return {1.0, 1.25, 1.5, 1.75, 2.0, 2.25, 2.5, 3.0}; }

QString DisplaySettings::transformName(int transform) {
    static const char *const names[] = {"Normal",        "90°",          "180°",
                                        "270°",          "Flipped",      "Flipped, 90°",
                                        "Flipped, 180°", "Flipped, 270°"};
    return transform >= 0 && transform < 8 ? names[transform] : QString();
}
