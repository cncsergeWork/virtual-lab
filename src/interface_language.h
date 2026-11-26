#pragma once

#include <QObject>
#include <QTranslator>
#include <array>

class InterfaceLanguage final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString language READ language NOTIFY changed)

public:
    InterfaceLanguage();
    ~InterfaceLanguage() override;

    QString language() const { return language_; }
    Q_INVOKABLE void select(const QString &language);

signals:
    void changed();

private:
    void apply(const QString &language);
    void removeTranslators();

    QString language_;
    QTranslator application_;
    std::array<QTranslator, 4> modules_;
};
