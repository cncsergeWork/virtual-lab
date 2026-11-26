#include "interface_language.h"

#include <QCoreApplication>
#include <QLibraryInfo>
#include <QLocale>
#include <QSettings>

namespace {
constexpr std::array qtModules{"qtbase", "qtdeclarative", "qtserialport", "qtwebsockets"};

void loadQtTranslation(QTranslator &translator, const QString &file)
{
    const auto deployed = QCoreApplication::applicationDirPath() + QStringLiteral("/translations");
    if (translator.load(file, deployed) ||
        translator.load(file, QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        QCoreApplication::installTranslator(&translator);
}
}

InterfaceLanguage::InterfaceLanguage()
{
    const auto preferred = QSettings().value(QStringLiteral("ui/language")).toString();
    apply(preferred == QStringLiteral("uk") ? preferred : QStringLiteral("ru"));
}

InterfaceLanguage::~InterfaceLanguage()
{
    removeTranslators();
}

void InterfaceLanguage::select(const QString &language)
{
    if (language == language_ || (language != QStringLiteral("ru") && language != QStringLiteral("uk")))
        return;
    apply(language);
    QSettings().setValue(QStringLiteral("ui/language"), language_);
    emit changed();
}

void InterfaceLanguage::apply(const QString &language)
{
    removeTranslators();
    const bool ukrainian = language == QStringLiteral("uk") &&
        application_.load(QStringLiteral(":/translations/virtuallab_uk.qm"));
    language_ = ukrainian ? QStringLiteral("uk") : QStringLiteral("ru");
    QLocale::setDefault(QLocale(ukrainian ? QStringLiteral("uk_UA") : QStringLiteral("ru_RU")));
    for (std::size_t i = 0; i < qtModules.size(); ++i)
        loadQtTranslation(modules_[i], QString::fromLatin1(qtModules[i]) + u'_' + language_);
    if (ukrainian)
        QCoreApplication::installTranslator(&application_);
}

void InterfaceLanguage::removeTranslators()
{
    QCoreApplication::removeTranslator(&application_);
    for (auto &module : modules_)
        QCoreApplication::removeTranslator(&module);
}
