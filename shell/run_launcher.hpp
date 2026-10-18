// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "launch_history.hpp"
#include "row_model.hpp"
#include "run_search.hpp"
#include <QDateTime>
#include <QHash>
#include <QObject>
#include <QStringList>

class ShellController;

// The rows the run launcher lists, kept in place as they change (RowModel): a row still there as
// the text changes keeps its delegate, which glides to its new place, and a new one eases in.
class RunResults : public RowModel<run_search::Row> {
    Q_OBJECT
  public:
    enum Role { Kind = Qt::UserRole + 1, Title, Subtitle, Icon, Target, Matches, Current };
    using RowModel::RowModel;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    void set(const std::vector<run_search::Row> &rows) { replace(rows); }
    // The row at `index`, or nullptr.
    const run_search::Row *row(int index) const { return at(this->index(index)); }
};

// The run launcher (the run action, Super + R), after rofi: a line to type in, and under it what
// that finds in one of three modes, which Tab and Shift + Tab go through: Apps, the installed
// applications as the start menu finds them, those launched most and lately first; Run, the
// programs on $PATH, the commands run from here before first, by frecency; and Windows, the open
// windows, the one chosen focused on its workspace. Before what Apps and Run find comes what the
// text is besides a search (run_search::special): a calculation's value, a path to open or run
// with what its folder has that it starts, or an address to open in the browser.
//
// Enter runs the selected row, or with none the text as a command (but in Windows mode);
// Ctrl + Enter runs the text as it is, whatever is selected; Shift + Enter runs the selected
// command, else the text, in the terminal the terminal action opens. A command runs through sh in
// the home folder, unless its program is not there, which the launcher says (`problem`) instead,
// staying open. The commands run are remembered in $XDG_STATE_HOME/paw/run-history, a
// LaunchHistory's file.
class RunLauncher : public QObject {
    Q_OBJECT
    // The output showing it, empty while it is closed.
    Q_PROPERTY(QString output READ output NOTIFY openChanged)
    Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
    // The mode, a Mode; `modes` names them, in their order.
    Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(QStringList modes READ modes CONSTANT)
    Q_PROPERTY(RunResults *results READ results CONSTANT)
    Q_PROPERTY(int selected READ selected WRITE setSelected NOTIFY selectedChanged)
    // Why what was asked last did not run ("There is no program called “gti”"), until the text
    // changes or the launcher opens again; empty for nothing.
    Q_PROPERTY(QString problem READ problem NOTIFY problemChanged)
  public:
    enum Mode { Apps, Run, Windows };
    Q_ENUM(Mode)
    // `stateDir` holds run-history: $XDG_STATE_HOME/paw unless given.
    explicit RunLauncher(ShellController &controller, QString stateDir = {});
    QString output() const { return output_; }
    QString query() const { return query_; }
    void setQuery(const QString &query);
    int mode() const { return mode_; }
    void setMode(int mode);
    QStringList modes() const { return {"Apps", "Run", "Windows"}; }
    RunResults *results() { return &results_; }
    int selected() const { return selected_; }
    void setSelected(int selected);
    QString problem() const { return problem_; }
    // Opens it on `output`, in Apps mode with nothing typed, or closes it when it is open there.
    Q_INVOKABLE void toggle(const QString &output);
    Q_INVOKABLE void close();
    // Moves the selection, wrapping.
    Q_INVOKABLE void move(int delta);
    // Goes `step` modes on, wrapping: 1 for Tab, -1 for Shift + Tab.
    Q_INVOKABLE void cycle(int step);
    // Runs the row at `index` (the selected one when negative) and closes, as Enter does; with
    // no row, the text as a command. False when nothing ran.
    Q_INVOKABLE bool activate(int index = -1);
    // Runs the text as a command, as it is (Ctrl + Enter).
    Q_INVOKABLE bool runTyped();
    // Runs the command at `index` (the selected one when negative), else the text, in the
    // terminal (Shift + Enter).
    Q_INVOKABLE bool runInTerminal(int index = -1);
    // A row's title with the letters the query matched in `color` (run_search::markup).
    Q_INVOKABLE QString markup(const QString &text, const QVariantList &places,
                               const QString &color) const;
    // For a preview: these commands as run before and these programs on $PATH instead, kept only
    // in memory from now on, so that nothing the preview does is saved.
    void preview(const QList<LaunchHistory::Entry> &history, const QStringList &programs);
    // The commands run, the most recent first.
    const LaunchHistory &history() const { return history_; }
  Q_SIGNALS:
    void openChanged();
    void queryChanged();
    void modeChanged();
    void selectedChanged();
    void problemChanged();
    // Saving the history failed.
    void failed(const QString &message);

  private:
    ShellController &controller_;
    QString output_, query_, problem_;
    int mode_ = Apps, selected_ = 0;
    // The window focused as it opened, which the compositor no longer says is once the launcher
    // has the keyboard; -1 for none.
    int focused_ = -1;
    RunResults results_;
    LaunchHistory history_;
    bool previewOnly_ = false;
    // The programs on $PATH, read again as it opens once a folder of $PATH changed, and the
    // folders and the times they changed as they were read.
    QStringList programs_, folders_;
    QList<QDateTime> folderTimes_;
    bool programsChecked_ = false;
    // The icons of the installed applications by the program each runs, made as first needed.
    QHash<QString, QString> programIcons_;
    const QStringList &programs();
    const QHash<QString, QString> &programIcons();
    void refresh();
    std::vector<run_search::Row> apps() const;
    QList<run_search::Window> windows() const;
    // Runs `command` through sh, in the terminal with `terminal`, unless its program is not
    // there; closes once it runs.
    bool runCommand(const QString &command, bool terminal);
    void setProblem(const QString &problem);
};
