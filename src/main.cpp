#include "interface_language.h"
#include "studio.h"

#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QPalette>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#ifdef SCADA_UI_TEST
#include <QSettings>
#include <QTemporaryDir>
#include <optional>

int runUiFlow(QObject *root, Studio &studio);
int runUiAudit(QObject *root, Studio &studio);
int runUiLanguage(QObject *root, Studio &studio, InterfaceLanguage &language);
#endif

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    app.setApplicationDisplayName("Virtual Lab");
    app.setWindowIcon(QIcon(QStringLiteral(":/assets/virtual-lab.ico")));
    // Keep the existing QSettings scope for editor preferences.
    app.setApplicationName("SCADA Studio");
    app.setApplicationVersion("0.1.0");
    app.setOrganizationName("SCADA Studio");
    const auto arguments = app.arguments();
#ifdef SCADA_UI_TEST
    std::optional<QTemporaryDir> testSettings;
    const bool audit = arguments.contains("--ui-audit");
    const bool languageTest = arguments.contains("--ui-language");
    if (audit || languageTest || arguments.contains("--ui-test")) {
        testSettings.emplace();
        if (!testSettings->isValid())
            return 1;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, testSettings->path());
    }
#endif
    QQuickStyle::setStyle("Basic");
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#edf1f5"));
    palette.setColor(QPalette::WindowText, QColor("#13263b"));
    palette.setColor(QPalette::Base, Qt::white);
    palette.setColor(QPalette::AlternateBase, QColor("#eff3f7"));
    palette.setColor(QPalette::Text, QColor("#13263b"));
    palette.setColor(QPalette::Button, QColor("#e5ebf1"));
    palette.setColor(QPalette::ButtonText, QColor("#13263b"));
    palette.setColor(QPalette::Highlight, QColor("#0b716a"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    app.setPalette(palette);
    InterfaceLanguage language;
    Studio studio;
    QQmlApplicationEngine engine;
    int warnings = 0;
    QObject::connect(&engine, &QQmlEngine::warnings, &app, [&warnings](const auto &list) { warnings += list.size(); });
    QObject::connect(&language, &InterfaceLanguage::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("interfaceLanguage", &language);
    engine.rootContext()->setContextProperty("studioApi", &studio);
    engine.load(QUrl("qrc:/qml/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;
#ifdef SCADA_UI_TEST
    if (testSettings) {
        auto *root = engine.rootObjects().first();
        const auto failed = languageTest ? runUiLanguage(root, studio, language)
                                         : audit ? runUiAudit(root, studio) : runUiFlow(root, studio);
        return failed != 0 ? failed : warnings == 0 ? 0 : 3;
    }
#endif
    if (arguments.size() == 2) {
        studio.open(QUrl::fromLocalFile(QDir::current().absoluteFilePath(arguments[1])));
    }
    return app.exec();
}
