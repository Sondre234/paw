// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "authentication.hpp"
#include <QObject>
#include <QString>
#include <map>

typedef struct _GCancellable GCancellable;
typedef struct _GList GList;
typedef struct _GTask GTask;
typedef struct _PolkitAgentListener PolkitAgentListener;
typedef struct _PolkitDetails PolkitDetails;

// The session's polkit authentication agent, as KDE's and GNOME's are: registered with polkitd
// for this login session, it takes the requests polkit sends when a program asks to do what needs
// a password (pkexec, GParted, an updater, logind's "challenge"), hands them to `authentication`
// for the dialog, and answers them through polkit's helper (PolkitAgentSession, which runs
// polkit-agent-helper-1 and its PAM conversation). libpolkit-agent-1 is GObject's: its listener
// and sessions run on the thread-default GLib main context, which on Linux Qt's event loop
// dispatches (QEventDispatcherGlib), so it needs no thread of its own.
class PolkitAgent : public QObject {
    Q_OBJECT
  public:
    explicit PolkitAgent(Authentication &authentication, QObject *parent = nullptr);
    // Unregisters, withdrawing what is open.
    ~PolkitAgent() override;
    // Registers with polkitd on the system bus. False, with error(), when it cannot: no polkitd
    // or system bus, a process outside a login session, an event loop that is not GLib's, or
    // another agent serving the session already (taken()).
    bool start();
    QString error() const { return error_; }
    bool taken() const { return taken_; }
    // A request from polkit, through the listener: answered by completing `task`, unless
    // `cancellable` withdraws it first.
    void begin(GTask *task, const char *actionId, const char *message, const char *iconName,
               PolkitDetails *details, const char *cookie, GList *identities,
               GCancellable *cancellable);

  private:
    struct Pending {
        GTask *task;
        GCancellable *cancellable;
        unsigned long handler;
        int number; // the Authentication's
    };
    Authentication &authentication_;
    PolkitAgentListener *listener_ = nullptr;
    void *registration_ = nullptr;
    QString error_;
    bool taken_ = false;
    unsigned long long lastKey_ = 0;
    std::map<unsigned long long, Pending> pending_;
    void complete(unsigned long long key, Authentication::Outcome outcome);
    void withdraw(unsigned long long key);
    static void cancelled(GCancellable *cancellable, void *data);
};
