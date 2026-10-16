// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "paw/config.hpp"
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <random>

// The wallpaper slideshow (shell.wallpaper_cycle, experimental): the pictures of a folder taking
// turns, by name or shuffled so that each shows once before any comes again. `step` says which
// pictures follow those shown, or come before them; ShellController picks them as the wallpaper,
// so that they are kept as a picked one is and the lock screen shows them. Every `interval`
// minutes `due` asks for a step; the time waits while `paused` (the session locked), and starts
// over at each step.
//
// The order is a sequence the monitors walk along together, each showing the picture after the
// previous monitor's: one monitor takes one picture per step, two take two. Shuffled, it is made
// of decks, each the whole folder in a random order, laid one after another as the walk reaches
// the end of the last (or the start of the first, stepping back), so stepping back shows again
// what was shown. A deck starts with no picture that the last few of the one before showed.
class WallpaperCycle : public QObject {
    Q_OBJECT
  public:
    explicit WallpaperCycle(QObject *parent = nullptr);
    // From each configuration loaded, with its folder found: the order starts anew when the
    // folder or the order changes, and the time when the interval does.
    void configure(const paw::WallpaperCycleConfig &config, const QString &folder);
    QString folder() const { return folder_; }
    int interval() const { return config_.interval; }
    // The pictures to show after (`direction` 1) or before (-1) `shown`, the pictures the
    // monitors show now, one for each in the same order; nothing when the folder has none. The
    // time until the next step starts over.
    QStringList step(const QStringList &shown, int direction);
    // The pictures directly in `folder` (not its subfolders), by name, numbers in order.
    static QStringList pictures(const QString &folder);
    // While paused (the session locked) the time until the next step waits.
    void setPaused(bool paused);
    bool paused() const { return paused_; }
    // Whether the time until the next step is running.
    bool running() const { return timer_.isActive(); }
    // How long a minute of `interval` lasts, in milliseconds, for the tests.
    void setMinute(int milliseconds);
    // The random order's seed, for the tests.
    void seed(unsigned value) { random_.seed(value); }

  Q_SIGNALS:
    // `interval` minutes have gone by since the last step.
    void due();

  private:
    paw::WallpaperCycleConfig config_;
    QString folder_;
    QStringList listed_;   // the folder's pictures as the sequence was made from them
    QStringList sequence_; // shown and to show, as far as it has been laid out
    qsizetype position_ = 0; // where the first monitor's picture is in sequence_
    bool anchored_ = false;  // whether sequence_ and position_ describe what shows
    QTimer timer_;
    int remaining_ = -1; // milliseconds left when paused, -1 for a whole interval
    bool paused_ = false;
    int minute_ = 60 * 1000;
    std::mt19937 random_;
    // The folder's pictures once, in the order they come: shuffled, or by name.
    QStringList round();
    // Lays out a round after the sequence (`after`) or before it, none of its first (last) `count`
    // pictures among the last (first) `count` of the sequence where the folder allows.
    void extend(bool after, qsizetype count);
    // Lays the sequence out anew for the pictures `shown`, so that a step forward shows those
    // after them and one back those before, about to go `direction`.
    void anchor(const QStringList &shown, int direction);
    void restart();
};
