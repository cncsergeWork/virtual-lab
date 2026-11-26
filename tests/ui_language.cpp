#include "interface_language.h"
#include "studio.h"

#include <QLocale>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSettings>
#include <QtTest>

namespace {
QQuickItem *visualItem(QQuickItem *parent, const QString &name)
{
    if (parent->objectName() == name)
        return parent;
    for (auto *child : parent->childItems()) {
        if (auto *found = visualItem(child, name))
            return found;
    }
    return nullptr;
}
}

int runUiLanguage(QObject *root, Studio &studio, InterfaceLanguage &language)
{
    const auto fail = [](const char *step) {
        qCritical().noquote() << "Language test:" << step;
        return 2;
    };
    auto *window = qobject_cast<QQuickWindow *>(root);
    if (!window || !QTest::qWaitForWindowExposed(window))
        return fail("Show window");
    const auto property = [window](const char *id, const char *name) -> QVariant {
        const auto objectName = QString::fromLatin1(id);
        if (const auto *control = visualItem(window->contentItem(), objectName))
            return control->property(name);
        const auto *object = window->findChild<QObject *>(objectName);
        return object ? object->property(name) : QVariant{};
    };
    if (property("saveButton", "text") != QStringLiteral("Сохранить") ||
        property("tab2", "text") != QStringLiteral("Источники") ||
        language.language() != QStringLiteral("ru"))
        return fail("Russian is the default");

    studio.newProject(false);
    studio.renameProject(QStringLiteral("Проект с пользовательским именем"));
    const auto projectId = studio.projectId();
    const auto screenId = studio.screenId();
    root->setProperty("page", 4);
    language.select(QStringLiteral("uk"));
    if (!QTest::qWaitFor([&] {
        return property("tab2", "text") == QStringLiteral("Джерела") &&
               property("saveButton", "text") == QStringLiteral("Зберегти");
    }))
        return fail("Ukrainian menu choice retranslates the running UI");
    if (property("projectOpenPicker", "title") != QStringLiteral("Відкрити проєкт Virtual Lab") ||
        !property("projectOpenPicker", "nameFilters").toStringList().value(0).contains("*.scada.json") ||
        QSettings().value("ui/language") != QStringLiteral("uk") || QLocale().name() != QStringLiteral("uk_UA"))
        return fail("Ukrainian preference, locale and file picker");
    if (studio.projectId() != projectId || studio.screenId() != screenId || !studio.modified() || !studio.canUndo() ||
        studio.name() != QStringLiteral("Проект с пользовательским именем") || root->property("page") != 4)
        return fail("Changing language preserves the active project and page");

    QSignalSpy notices(&studio, &Studio::notice);
    if (studio.start() || notices.count() != 1 || notices.first().first() != QStringLiteral("Додайте щонайменше одне джерело й один тег."))
        return fail("Validation reports in Ukrainian");
    studio.newProject(true);
    const auto overview = studio.screens().value(0).toMap().value("name");
    if (overview != QStringLiteral("Огляд станції"))
        return fail("New demo data uses Ukrainian");

    language.select(QStringLiteral("ru"));
    if (!QTest::qWaitFor([&] {
        return property("saveButton", "text") == QStringLiteral("Сохранить") &&
               property("tab2", "text") == QStringLiteral("Источники");
    }) ||
        QSettings().value("ui/language") != QStringLiteral("ru") ||
        studio.screens().value(0).toMap().value("name") != overview)
        return fail("Russian switch preserves already created demo data");
    qInfo("Interface language switches immediately and is saved");
    return 0;
}
