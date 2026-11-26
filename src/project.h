#pragma once

#include <QJsonObject>
#include <QSize>
#include <QString>
#include <QVariant>

namespace Project {
inline constexpr char demoImage[] = ":/assets/demo-loop.png";
inline constexpr char demoPumpIdle[] = ":/assets/pump-idle.svg";
inline constexpr char demoPumpRunning[] = ":/assets/pump-running.svg";
inline constexpr char demoPumpImpeller[] = ":/assets/pump-impeller.svg";
bool isDemoImage(const QString &path);
QSize minimumWidgetSize(const QString &kind);

QString id();
QJsonObject empty();
QJsonObject find(const QJsonObject &project, const QString &array, const QString &id);
QSize screenSize(const QJsonObject &screen);
QString validateScreenSettings(const QJsonObject &screen);
QString validate(const QJsonObject &project);
QVariantMap widgetColors(const QVariantMap &appearance, const QVariantMap &reading);
bool animationActive(const QVariantMap &animation, const QVariantMap &reading);
QVariantMap faceplatePropertyValues(const QVariantMap &bindings, const QVariantMap &readings,
                                   const QVariantList &alarms);
bool load(const QString &path, QJsonObject &project, QString &error);
bool save(const QString &path, const QJsonObject &project, QString &error);
}
