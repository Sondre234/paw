// SPDX-License-Identifier: GPL-3.0-or-later
#include "notification_service.hpp"
#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QUrl>
#include <iostream>

namespace {
constexpr auto objectPath = "/org/freedesktop/Notifications";
constexpr auto serviceName = "org.freedesktop.Notifications";

// The image-data hint: (width, height, rowstride, has_alpha, bits_per_sample, channels, data).
QImage imageFromHint(const QVariant &value) {
    if (!value.canConvert<QDBusArgument>())
        return {};
    const auto argument = value.value<QDBusArgument>();
    int width = 0, height = 0, stride = 0, bits = 0, channels = 0;
    bool alpha = false;
    QByteArray data;
    argument.beginStructure();
    argument >> width >> height >> stride >> alpha >> bits >> channels >> data;
    argument.endStructure();
    if (bits != 8 || (channels != 3 && channels != 4) || width < 1 || height < 1 || width > 2048 ||
        height > 2048 || stride < width * channels ||
        qsizetype(stride) * (height - 1) + qsizetype(width) * channels > data.size())
        return {};
    QImage view(reinterpret_cast<const uchar *>(data.constData()), width, height, stride,
                channels == 4 ? QImage::Format_RGBA8888 : QImage::Format_RGB888);
    QImage image = view.copy(); // the bytes belong to `data`
    // The history keeps what a card showed, so a picture is kept no larger than a card can use.
    if (image.width() > 128 || image.height() > 128)
        image = image.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image;
}
int intHint(const QVariant &value, int fallback) {
    bool ok = false;
    const int result = value.toInt(&ok);
    return ok ? result : fallback;
}
} // namespace

NotificationsAdaptor::NotificationsAdaptor(NotificationCenter &center, QObject *parent)
    : QDBusAbstractAdaptor(parent), center_(center) {
    connect(&center, &NotificationCenter::closed, this, &NotificationsAdaptor::NotificationClosed);
    connect(&center, &NotificationCenter::actionInvoked, this, &NotificationsAdaptor::ActionInvoked);
}
QStringList NotificationsAdaptor::GetCapabilities() {
    return {"actions", "body", "body-markup", "body-hyperlinks", "icon-static", "persistence"};
}
QString NotificationsAdaptor::GetServerInformation(QString &vendor, QString &version,
                                                   QString &spec_version) {
    vendor = "paw";
    version = "1";
    spec_version = "1.2";
    return "paw";
}
void NotificationsAdaptor::CloseNotification(uint id) { center_.closeFromApplication(id); }
Notification NotificationsAdaptor::parse(const QString &app, const QString &icon,
                                         const QString &summary, const QString &body,
                                         const QStringList &actions, const QVariantMap &hints,
                                         int timeout) {
    Notification n;
    n.app = app.left(200);
    n.summary = summary.left(500);
    n.body = notificationMarkup(body.left(8000));
    n.timeout = timeout < 0 ? -1 : timeout;
    for (qsizetype i = 0; i + 1 < actions.size() && n.actions.size() < 8; i += 2)
        n.actions.emplace_back(actions[i], actions[i + 1].left(100));
    n.urgency = std::clamp(intHint(hints.value("urgency"), Notification::Normal), 0, 2);
    n.category = hints.value("category").toString();
    n.desktopEntry = hints.value("desktop-entry").toString();
    n.resident = hints.value("resident").toBool();
    n.transient = hints.value("transient").toBool();
    n.progress = std::clamp(intHint(hints.value("value"), -1), -1, 100);
    for (const char *key : {"x-canonical-private-synchronous", "synchronous", "x-dunst-stack-tag"})
        if (const auto tag = hints.value(key).toString(); !tag.isEmpty()) {
            n.tag = tag;
            break;
        }
    // The icon: the image hints override the icon parameter, as the spec orders them.
    QString path = hints.value("image-path", hints.value("image_path")).toString();
    for (const char *key : {"image-data", "image_data", "icon_data"})
        if (hints.contains(key) && n.image.isNull())
            n.image = imageFromHint(hints.value(key));
    QString name = !path.isEmpty() ? path : icon;
    if (name.startsWith("file://"))
        name = QUrl(name).toLocalFile();
    n.icon = name.left(1024);
    return n;
}
uint NotificationsAdaptor::Notify(const QString &app_name, uint replaces_id, const QString &app_icon,
                                  const QString &summary, const QString &body,
                                  const QStringList &actions, const QVariantMap &hints,
                                  int expire_timeout) {
    return center_.notify(parse(app_name, app_icon, summary, body, actions, hints, expire_timeout),
                          replaces_id);
}

NotificationService::NotificationService(NotificationCenter &center, QObject *parent)
    : QObject(parent), center_(center) {}
NotificationService::~NotificationService() {
    if (registered_) {
        bus_.unregisterService(serviceName);
        bus_.unregisterObject(objectPath);
    }
}
bool NotificationService::start(const QDBusConnection &bus) {
    bus_ = bus;
    if (!bus_.isConnected()) {
        error_ = "no session bus: " + bus_.lastError().message();
        return false;
    }
    new NotificationsAdaptor(center_, this);
    if (!bus_.registerObject(objectPath, this, QDBusConnection::ExportAdaptors)) {
        error_ = "cannot export the notification object: " + bus_.lastError().message();
        return false;
    }
    if (!bus_.registerService(serviceName)) {
        bus_.unregisterObject(objectPath);
        error_ = "another notification daemon owns " + QString(serviceName);
        return false;
    }
    registered_ = true;
    center_.setServing(true);
    return true;
}
