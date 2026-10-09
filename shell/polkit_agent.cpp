// SPDX-License-Identifier: GPL-3.0-or-later
#include "polkit_agent.hpp"
#define POLKIT_AGENT_I_KNOW_API_IS_SUBJECT_TO_CHANGE
#include <polkitagent/polkitagent.h>
#include <QAbstractEventDispatcher>
#include <QPointer>
#include <cstring>
#include <iostream>
#include <pwd.h>
#include <unistd.h>
#include <vector>

namespace {
// Where the listener's object is exported on the system bus.
constexpr const char *objectPath = "/org/paw/PolicyKit1/AuthenticationAgent";

// A conversation through polkit's helper: PolkitAgentSession's signals as Qt's.
class PolkitConversation : public AuthConversation {
  public:
    PolkitConversation(PolkitIdentity *identity, const QString &cookie)
        : session_(polkit_agent_session_new(identity, cookie.toUtf8().constData())) {
        g_signal_connect(session_, "request", G_CALLBACK(onRequest), this);
        g_signal_connect(session_, "show-error", G_CALLBACK(onError), this);
        g_signal_connect(session_, "show-info", G_CALLBACK(onInfo), this);
        g_signal_connect(session_, "completed", G_CALLBACK(onCompleted), this);
    }
    ~PolkitConversation() override {
        cancel();
        g_object_unref(session_);
    }
    void start() override { polkit_agent_session_initiate(session_); }
    void respond(const QString &answer) override {
        QByteArray text = answer.toUtf8();
        polkit_agent_session_response(session_, text.constData());
        // Not left lying about in this copy, at least.
        std::memset(text.data(), 0, static_cast<size_t>(text.size()));
    }
    void cancel() override {
        if (over_)
            return;
        over_ = true;
        g_signal_handlers_disconnect_by_data(session_, this);
        polkit_agent_session_cancel(session_);
    }

  private:
    PolkitAgentSession *session_;
    bool over_ = false;
    static void onRequest(PolkitAgentSession *, const gchar *text, gboolean echo, gpointer self) {
        Q_EMIT static_cast<PolkitConversation *>(self)->request(QString::fromUtf8(text), echo);
    }
    static void onError(PolkitAgentSession *, const gchar *text, gpointer self) {
        Q_EMIT static_cast<PolkitConversation *>(self)->error(QString::fromUtf8(text));
    }
    static void onInfo(PolkitAgentSession *, const gchar *text, gpointer self) {
        Q_EMIT static_cast<PolkitConversation *>(self)->info(QString::fromUtf8(text));
    }
    static void onCompleted(PolkitAgentSession *, gboolean authorized, gpointer self) {
        auto *conversation = static_cast<PolkitConversation *>(self);
        conversation->over_ = true;
        g_signal_handlers_disconnect_by_data(conversation->session_, conversation);
        Q_EMIT conversation->completed(authorized);
    }
};

// What `polkit_agent_listener_initiate_authentication` needs a cancellable's callback to know.
struct CancelData {
    QPointer<PolkitAgent> agent;
    unsigned long long key;
};
} // namespace

// The listener polkitd talks to: a PolkitAgentListener whose requests go to its agent.
extern "C" {
struct PawPolkitListener {
    PolkitAgentListener parent_instance;
    PolkitAgent *agent;
};
struct PawPolkitListenerClass {
    PolkitAgentListenerClass parent_class;
};
}
G_DEFINE_TYPE(PawPolkitListener, paw_polkit_listener, POLKIT_AGENT_TYPE_LISTENER)

static void initiate(PolkitAgentListener *listener, const gchar *actionId, const gchar *message,
                     const gchar *iconName, PolkitDetails *details, const gchar *cookie,
                     GList *identities, GCancellable *cancellable, GAsyncReadyCallback callback,
                     gpointer data) {
    GTask *task = g_task_new(listener, cancellable, callback, data);
    auto *self = reinterpret_cast<PawPolkitListener *>(listener);
    if (!self->agent) {
        g_task_return_new_error(task, POLKIT_ERROR, POLKIT_ERROR_FAILED, "The agent is going");
        g_object_unref(task);
        return;
    }
    self->agent->begin(task, actionId, message, iconName, details, cookie, identities, cancellable);
}
static gboolean initiateFinish(PolkitAgentListener *, GAsyncResult *result, GError **error) {
    return g_task_propagate_boolean(G_TASK(result), error);
}
static void paw_polkit_listener_init(PawPolkitListener *self) {
    self->agent = nullptr;
}
static void paw_polkit_listener_class_init(PawPolkitListenerClass *type) {
    auto *listener = POLKIT_AGENT_LISTENER_CLASS(type);
    listener->initiate_authentication = initiate;
    listener->initiate_authentication_finish = initiateFinish;
}

PolkitAgent::PolkitAgent(Authentication &authentication, QObject *parent)
    : QObject(parent), authentication_(authentication) {
    authentication_.setConversations([](const AuthIdentity &identity, const QString &cookie)
                                         -> AuthConversation * {
        GError *error = nullptr;
        PolkitIdentity *polkitIdentity =
            polkit_identity_from_string(identity.id.toUtf8().constData(), &error);
        if (!polkitIdentity) {
            std::cerr << "paw polkit: " << identity.id.toStdString() << ": " << error->message
                      << '\n';
            g_error_free(error);
            return nullptr;
        }
        auto *conversation = new PolkitConversation(polkitIdentity, cookie);
        g_object_unref(polkitIdentity);
        return conversation;
    });
}

PolkitAgent::~PolkitAgent() {
    authentication_.withdrawAll();
    authentication_.setConversations({});
    if (registration_)
        polkit_agent_listener_unregister(registration_);
    if (listener_) {
        reinterpret_cast<PawPolkitListener *>(listener_)->agent = nullptr;
        g_object_unref(listener_);
    }
}

bool PolkitAgent::start() {
    if (registration_)
        return true;
    // polkit's listener and sessions are GObjects on GLib's main context; Qt dispatches it unless
    // it was built without GLib or QT_NO_GLIB is set.
    auto *dispatcher = QAbstractEventDispatcher::instance();
    if (!dispatcher || !dispatcher->inherits("QEventDispatcherGlib")) {
        error_ = "Qt's event loop is not GLib's (is QT_NO_GLIB set?), which polkit's library needs";
        return false;
    }
    GError *failure = nullptr;
    PolkitSubject *subject = polkit_unix_session_new_for_process_sync(getpid(), nullptr, &failure);
    if (!subject) {
        // The session the environment names, when logind cannot tell this process's.
        const QByteArray id = qgetenv("XDG_SESSION_ID");
        if (!id.isEmpty())
            subject = polkit_unix_session_new(id.constData());
    }
    if (!subject) {
        error_ = QString("this process is in no login session: ") +
                 (failure ? failure->message : "logind does not know it");
        g_clear_error(&failure);
        return false;
    }
    g_clear_error(&failure);
    listener_ = static_cast<PolkitAgentListener *>(
        g_object_new(paw_polkit_listener_get_type(), nullptr));
    reinterpret_cast<PawPolkitListener *>(listener_)->agent = this;
    registration_ = polkit_agent_listener_register(listener_, POLKIT_AGENT_REGISTER_FLAGS_NONE,
                                                   subject, objectPath, nullptr, &failure);
    g_object_unref(subject);
    if (!registration_) {
        error_ = QString::fromUtf8(failure ? failure->message : "polkit did not say why");
        taken_ = error_.contains("already exists");
        g_clear_error(&failure);
        reinterpret_cast<PawPolkitListener *>(listener_)->agent = nullptr;
        g_object_unref(listener_);
        listener_ = nullptr;
        return false;
    }
    return true;
}

void PolkitAgent::begin(GTask *task, const char *actionId, const char *message,
                        const char *iconName, PolkitDetails *details, const char *cookie,
                        GList *identities, GCancellable *cancellable) {
    AuthRequest request;
    request.actionId = QString::fromUtf8(actionId);
    request.message = QString::fromUtf8(message);
    request.iconName = QString::fromUtf8(iconName);
    request.cookie = QString::fromUtf8(cookie);
    if (details) {
        gchar **keys = polkit_details_get_keys(details);
        for (gchar **key = keys; key && *key; ++key)
            request.details.insert(QString::fromUtf8(*key),
                                   QString::fromUtf8(polkit_details_lookup(details, *key)));
        g_strfreev(keys);
    }
    // polkitd lists an administrator group's members as users; a group itself cannot answer.
    for (GList *item = identities; item; item = item->next) {
        auto *identity = static_cast<PolkitIdentity *>(item->data);
        if (!POLKIT_IS_UNIX_USER(identity))
            continue;
        const auto uid = static_cast<uid_t>(polkit_unix_user_get_uid(POLKIT_UNIX_USER(identity)));
        gchar *id = polkit_identity_to_string(identity);
        AuthIdentity entry{QString::fromUtf8(id), QString::number(uid), {}, uid};
        g_free(id);
        struct passwd record, *found = nullptr;
        std::vector<char> buffer(16384);
        if (getpwuid_r(uid, &record, buffer.data(), buffer.size(), &found) == 0 && found) {
            entry.name = QString::fromLocal8Bit(found->pw_name);
            // The GECOS field's first part is the full name.
            entry.fullName = QString::fromLocal8Bit(found->pw_gecos).section(',', 0, 0).trimmed();
        }
        request.identities << entry;
    }
    const unsigned long long key = ++lastKey_;
    pending_[key] = {task, cancellable ? static_cast<GCancellable *>(g_object_ref(cancellable))
                                       : nullptr,
                     0, 0};
    pending_[key].number = authentication_.add(std::move(request), [this, key](Authentication::Outcome outcome) {
        complete(key, outcome);
    });
    // polkitd withdraws a request through the cancellable (CancelAuthentication), maybe at once.
    if (cancellable && pending_.count(key))
        pending_[key].handler = g_cancellable_connect(
            cancellable, G_CALLBACK(cancelled), new CancelData{this, key},
            [](gpointer data) { delete static_cast<CancelData *>(data); });
}

void PolkitAgent::cancelled(GCancellable *, void *data) {
    // Not from inside the handler: completing disconnects it, which waits for the handler.
    auto *cancel = static_cast<CancelData *>(data);
    if (cancel->agent)
        QMetaObject::invokeMethod(
            cancel->agent.data(),
            [agent = cancel->agent, key = cancel->key] {
                if (agent)
                    agent->withdraw(key);
            },
            Qt::QueuedConnection);
}

void PolkitAgent::withdraw(unsigned long long key) {
    auto found = pending_.find(key);
    if (found != pending_.end())
        authentication_.withdraw(found->second.number);
}

void PolkitAgent::complete(unsigned long long key, Authentication::Outcome outcome) {
    auto found = pending_.find(key);
    if (found == pending_.end())
        return;
    Pending pending = found->second;
    pending_.erase(found);
    if (pending.cancellable && pending.handler)
        g_cancellable_disconnect(pending.cancellable, pending.handler);
    switch (outcome) {
    case Authentication::Outcome::Authorized:
        g_task_return_boolean(pending.task, TRUE);
        break;
    case Authentication::Outcome::Dismissed:
        g_task_return_new_error(pending.task, POLKIT_ERROR, POLKIT_ERROR_CANCELLED,
                                "The authentication dialog was dismissed by the user");
        break;
    case Authentication::Outcome::Withdrawn:
        g_task_return_new_error(pending.task, G_IO_ERROR, G_IO_ERROR_CANCELLED,
                                "The authentication request was withdrawn");
        break;
    }
    g_object_unref(pending.task);
    if (pending.cancellable)
        g_object_unref(pending.cancellable);
}
