// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QString>
#include <QVariantList>
#include <cstdint>

namespace paw {
struct Config;
}
class ShellController;

// The sheet of keyboard shortcuts (the shortcuts action, and the command palette's Keyboard
// shortcuts): every key and button binding of the configuration, in groups by what their actions
// do (Windows, Focus, Workspaces, ...), and first, while a binding mode is in use, that mode's.
//
// A group is {name, mode, rows}, `mode` true for the binding mode's. A row is {title, detail,
// mods, keys, range, button, command, when, search}:
// - `title` is what the binding does: its `description`, else the command palette's title for
//   its action, else one of the sheet's own; for a spawn without a description its command,
//   shortened, with `command` set;
// - `detail` says where a button binding acts ("Over kitty windows and the desktop"), else "";
// - `mods` are the modifiers held, "Super", "Ctrl", "Alt" and "Shift", in the order the style
//   writes them (the macOS style's ⌃ ⌥ ⇧ ⌘);
// - `keys` are the keys' names as their caps show them (keyName), a mouse button's or a
//   switch's with `button`: several for bindings alike but for their key (Super and the arrows
//   focusing that way), the first and the last with `range` for a run of workspaces (Super and
//   1 to 4);
// - `when` is "tap" for a modifier key's binding that runs as it comes up alone (Super_L with
//   `release`, its cap among `mods` and no `keys`), "on release" for another key's that runs as
//   it comes up, else "";
// - `search` is the lower-case text a search looks in.
namespace shortcuts {
// A key's name as its cap shows it: a letter or a sign as printed ("Q", ";", "/"), an arrow,
// "Esc", "Enter", "Space", "PrtSc", "Volume up"; in the macOS style as a Mac's keyboard has it
// ("Return", "Delete").
QString keyName(uint32_t keysym, bool macos);
// The sheet of `config`'s bindings, with mode `mode`'s first ("" outside any), for the macOS
// style or the taskbar's.
QVariantList sheet(const paw::Config &config, const QString &mode, bool macos);
// `sheet` with only the rows whose `search` holds every word of `query`, without the groups that
// leaves empty.
QVariantList filter(const QVariantList &sheet, const QString &query);
// How many rows the groups of a sheet hold.
int rows(const QVariantList &sheet);
} // namespace shortcuts

// The sheet as the shell shows it: open on an output (the one under the pointer as the action
// ran), searched by `query`, and read again from the configuration as it, or the binding mode in
// use, changes while it is open.
class Shortcuts : public QObject {
    Q_OBJECT
    // The output showing the sheet, empty while it is closed.
    Q_PROPERTY(QString output READ output NOTIFY openChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    // The groups shown: the sheet, as the query leaves it.
    Q_PROPERTY(QVariantList groups READ groups NOTIFY groupsChanged)
    // How many rows they hold, and how many the whole sheet has.
    Q_PROPERTY(int count READ count NOTIFY groupsChanged)
    Q_PROPERTY(int total READ total NOTIFY groupsChanged)
    // The binding mode whose bindings come first, "" outside any.
    Q_PROPERTY(QString mode READ mode NOTIFY groupsChanged)
  public:
    explicit Shortcuts(ShellController &controller);
    QString output() const { return output_; }
    QString query() const { return query_; }
    void setQuery(const QString &query);
    QVariantList groups() const { return groups_; }
    int count() const { return shortcuts::rows(groups_); }
    int total() const { return shortcuts::rows(sheet_); }
    QString mode() const { return mode_; }
    // Opens the sheet on `output`, its search empty, or closes it when it is open there.
    Q_INVOKABLE void toggle(const QString &output);
    Q_INVOKABLE void close();
    // For a preview: the bindings of `config`, with mode `mode`'s first, in place of the
    // configuration's.
    void preview(const paw::Config &config, const QString &mode = {});

  Q_SIGNALS:
    void openChanged();
    void queryChanged();
    void groupsChanged();

  private:
    ShellController &controller_;
    QString output_, query_, mode_;
    QVariantList sheet_, groups_;
    bool standIn_ = false;
    // Reads the sheet again, unless a preview's stands in for it, and searches it.
    void refresh();
    void search();
};
