// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <deque>
#include <functional>

// One user a request may be answered as: polkit's name for it ("unix-user:1000"), the login
// name and the full name from the password database ("" when it has none).
struct AuthIdentity {
    QString id;
    QString name;
    QString fullName;
    uint uid = 0;
};

// What a program asks polkit for, which a password has to allow: polkit's action
// ("org.freedesktop.policykit.exec"), the message it gives ("Authentication is needed to run
// `/usr/bin/gparted' as the super user"), an icon's name, its details (pkexec's "command_line"
// and "program"), the cookie that names the request to polkit's helper, and the users who may
// answer it.
struct AuthRequest {
    QString actionId;
    QString message;
    QString iconName;
    QVariantMap details;
    QString cookie;
    QList<AuthIdentity> identities;
};

// A conversation with polkit's helper as one user, about one request: the helper asks for
// something (`request`, the password, `echo` false), says something (`error`, `info`) and ends
// saying whether the answers authorized the request (`completed`). The agent makes one with
// polkit's PolkitAgentSession; the tests make stand-ins.
class AuthConversation : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    virtual void start() = 0;
    virtual void respond(const QString &answer) = 0;
    // Ends it; nothing is signalled after this.
    virtual void cancel() = 0;
  Q_SIGNALS:
    void request(const QString &prompt, bool echo);
    void error(const QString &text);
    void info(const QString &text);
    void completed(bool authorized);
};

// The authentication dialog's model: the requests of the polkit agent, one at a time and the rest
// waiting in turn, each answered with a password for one of its users. A request opens on the
// output overlays belong on, the first of its users who is the one logged in (else root, else
// the first) chosen; the conversation for that user starts at once, and an answer typed before
// the helper asks is kept until it does. A wrong answer says so and starts the conversation again,
// as many times as the user tries; Cancel, or polkit giving the request up, closes it.
class Authentication : public QObject {
    Q_OBJECT
    // Whether a request is open, and its serial, new for each request that opens.
    Q_PROPERTY(bool open READ open NOTIFY changed)
    Q_PROPERTY(int serial READ serial NOTIFY changed)
    // The output it shows on, the one overlays belonged on as it opened.
    Q_PROPERTY(QString output READ output NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString actionId READ actionId NOTIFY changed)
    Q_PROPERTY(QString iconName READ iconName NOTIFY changed)
    // pkexec's command line, when the request is to run one; else "".
    Q_PROPERTY(QString command READ command NOTIFY changed)
    // Its users, as {name, fullName, label}: label is "Full Name (name)", or the name alone.
    Q_PROPERTY(QVariantList identities READ identities NOTIFY changed)
    Q_PROPERTY(int identity READ identity WRITE setIdentity NOTIFY changed)
    // What the helper asks, without its colon ("Password"), and whether the answer shows as it
    // is typed.
    Q_PROPERTY(QString prompt READ prompt NOTIFY changed)
    Q_PROPERTY(bool echo READ echo NOTIFY changed)
    // An answer was given and the helper is checking it.
    Q_PROPERTY(bool checking READ checking NOTIFY changed)
    // What went wrong last ("" for nothing), and what the helper said besides.
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString info READ info NOTIFY changed)
  public:
    enum class Outcome { Authorized, Dismissed, Withdrawn };
    using Conversations =
        std::function<AuthConversation *(const AuthIdentity &identity, const QString &cookie)>;
    using Done = std::function<void(Outcome)>;

    explicit Authentication(QObject *parent = nullptr);
    ~Authentication() override;
    // How conversations are made: the agent's makes polkit's, a test's stand-ins.
    void setConversations(Conversations conversations) { conversations_ = std::move(conversations); }
    // Where a request that opens shows: the output overlays belong on.
    void setOutputSource(std::function<QString()> source) { outputSource_ = std::move(source); }
    // Takes a request: opens it now, or once those before it are done. `done` is called once,
    // with how it ended. Returns a number for withdraw().
    int add(AuthRequest request, Done done);
    // polkit gave the request up (its program went, or it asked another agent): it closes, or
    // leaves the queue, and is done as withdrawn.
    void withdraw(int number);
    // Every request is withdrawn: the agent goes.
    void withdrawAll();
    // How many requests there are, the open one included.
    int count() const { return static_cast<int>(queue_.size()); }

    bool open() const { return !queue_.empty(); }
    int serial() const { return serial_; }
    QString output() const { return output_; }
    QString message() const;
    QString actionId() const;
    QString iconName() const;
    QString command() const;
    QVariantList identities() const;
    int identity() const { return identity_; }
    void setIdentity(int index);
    QString prompt() const { return prompt_; }
    bool echo() const { return echo_; }
    bool checking() const { return checking_; }
    QString error() const { return error_; }
    QString info() const { return info_; }
    // The answer the user gave: sent to the helper, or kept until it asks.
    Q_INVOKABLE void submit(const QString &answer);
    // The user gave the request up.
    Q_INVOKABLE void cancel();
  Q_SIGNALS:
    void changed();
    // An answer was not accepted: the field empties for the next try.
    void rejected();

  private:
    struct Entry {
        int number;
        AuthRequest request;
        Done done;
    };
    std::deque<Entry> queue_;
    // The open request, or the last while the dialog fades out.
    AuthRequest shown_;
    Conversations conversations_;
    std::function<QString()> outputSource_;
    QPointer<AuthConversation> conversation_;
    int next_ = 1, serial_ = 0, identity_ = -1;
    QString output_, prompt_, error_, info_, pending_;
    bool echo_ = false, checking_ = false, asked_ = false, havePending_ = false;
    void openFirst();
    void converse();
    void endConversation();
    void finish(Outcome outcome);
};
