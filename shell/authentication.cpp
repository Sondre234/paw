// SPDX-License-Identifier: GPL-3.0-or-later
#include "authentication.hpp"
#include <algorithm>
#include <unistd.h>

Authentication::Authentication(QObject *parent) : QObject(parent) {}

Authentication::~Authentication() {
    // The agent withdraws its requests before it goes; nothing is left to tell.
    endConversation();
}

int Authentication::add(AuthRequest request, Done done) {
    const int number = next_++;
    queue_.push_back({number, std::move(request), std::move(done)});
    if (queue_.size() == 1)
        openFirst();
    return number;
}

void Authentication::withdraw(int number) {
    if (queue_.empty())
        return;
    if (queue_.front().number == number) {
        finish(Outcome::Withdrawn);
        return;
    }
    for (auto it = queue_.begin(); it != queue_.end(); ++it) {
        if (it->number != number)
            continue;
        Done done = std::move(it->done);
        queue_.erase(it);
        if (done)
            done(Outcome::Withdrawn);
        return;
    }
}

void Authentication::withdrawAll() {
    // The waiting ones first, so that none opens as the open one closes.
    while (queue_.size() > 1)
        withdraw(queue_.back().number);
    if (!queue_.empty())
        finish(Outcome::Withdrawn);
}

QString Authentication::message() const {
    return shown_.message;
}
QString Authentication::actionId() const {
    return shown_.actionId;
}
QString Authentication::iconName() const {
    return shown_.iconName;
}
QString Authentication::command() const {
    return shown_.details.value("command_line").toString();
}

QVariantList Authentication::identities() const {
    QVariantList list;
    for (const auto &identity : shown_.identities)
        list << QVariantMap{{"name", identity.name},
                            {"fullName", identity.fullName},
                            {"label", identity.fullName.isEmpty() || identity.fullName == identity.name
                                          ? identity.name
                                          : identity.fullName + " (" + identity.name + ")"}};
    return list;
}

void Authentication::setIdentity(int index) {
    if (queue_.empty() || index == identity_ || index < 0 || index >= shown_.identities.size())
        return;
    identity_ = index;
    error_.clear();
    info_.clear();
    checking_ = havePending_ = false;
    pending_.clear();
    converse();
    Q_EMIT changed();
}

void Authentication::submit(const QString &answer) {
    if (queue_.empty() || checking_)
        return;
    error_.clear();
    info_.clear();
    checking_ = true;
    if (conversation_ && asked_) {
        asked_ = false;
        conversation_->respond(answer);
    } else {
        pending_ = answer;
        havePending_ = true;
    }
    Q_EMIT changed();
}

void Authentication::cancel() {
    if (!queue_.empty())
        finish(Outcome::Dismissed);
}

void Authentication::openFirst() {
    shown_ = queue_.front().request;
    ++serial_;
    output_ = outputSource_ ? outputSource_() : QString();
    error_.clear();
    info_.clear();
    pending_.clear();
    havePending_ = checking_ = false;
    // The user logged in, else root, else the first who may answer.
    identity_ = shown_.identities.isEmpty() ? -1 : 0;
    const uint me = getuid();
    for (uint wanted : {me, 0u}) {
        auto found = std::find_if(shown_.identities.begin(), shown_.identities.end(),
                                  [wanted](const AuthIdentity &identity) { return identity.uid == wanted; });
        if (found != shown_.identities.end()) {
            identity_ = static_cast<int>(found - shown_.identities.begin());
            break;
        }
    }
    if (identity_ < 0)
        error_ = "No user may answer this request.";
    converse();
    Q_EMIT changed();
}

void Authentication::converse() {
    endConversation();
    asked_ = echo_ = false;
    prompt_ = "Password";
    if (queue_.empty() || identity_ < 0)
        return;
    const Entry &entry = queue_.front();
    AuthConversation *conversation =
        conversations_ ? conversations_(entry.request.identities[identity_], entry.request.cookie)
                       : nullptr;
    if (!conversation) {
        error_ = "polkit's helper cannot be asked.";
        return;
    }
    conversation->setParent(this);
    conversation_ = conversation;
    connect(conversation, &AuthConversation::request, this, [this](const QString &prompt, bool echo) {
        asked_ = true;
        echo_ = echo;
        prompt_ = prompt.trimmed();
        while (prompt_.endsWith(':'))
            prompt_.chop(1);
        prompt_ = prompt_.trimmed();
        if (prompt_.isEmpty())
            prompt_ = "Password";
        if (havePending_) {
            // Typed before the helper asked.
            havePending_ = false;
            asked_ = false;
            const QString answer = pending_;
            pending_.clear();
            conversation_->respond(answer);
        } else {
            checking_ = false;
        }
        Q_EMIT changed();
    });
    connect(conversation, &AuthConversation::error, this, [this](const QString &text) {
        error_ = text.trimmed();
        Q_EMIT changed();
    });
    connect(conversation, &AuthConversation::info, this, [this](const QString &text) {
        info_ = text.trimmed();
        Q_EMIT changed();
    });
    connect(conversation, &AuthConversation::completed, this, [this](bool authorized) {
        if (authorized) {
            finish(Outcome::Authorized);
            return;
        }
        // Wrong: another try, as the same user.
        checking_ = havePending_ = false;
        pending_.clear();
        error_ = "The password was not accepted. Try again.";
        converse();
        Q_EMIT rejected();
        Q_EMIT changed();
    });
    conversation->start();
}

void Authentication::endConversation() {
    if (!conversation_)
        return;
    AuthConversation *conversation = conversation_;
    conversation_ = nullptr;
    conversation->disconnect(this);
    conversation->cancel();
    conversation->deleteLater();
}

void Authentication::finish(Outcome outcome) {
    endConversation();
    Entry entry = std::move(queue_.front());
    queue_.pop_front();
    checking_ = havePending_ = asked_ = false;
    pending_.clear();
    if (!queue_.empty()) {
        openFirst();
    } else {
        // What it showed stays readable while the dialog fades out.
        error_.clear();
        info_.clear();
        Q_EMIT changed();
    }
    if (entry.done)
        entry.done(outcome);
}
