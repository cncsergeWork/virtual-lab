#include "studio.h"

#include "demo.h"
#include "project.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRect>
#include <QSaveFile>
#include <QSet>
#include <QTemporaryDir>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <utility>

namespace {
bool replace(QJsonArray &entries, const QJsonObject &object)
{
    for (qsizetype i = 0; i < entries.size(); ++i) {
        if (entries[i].toObject().value("id") == object.value("id")) {
            entries.replace(i, object);
            return true;
        }
    }
    return false;
}

bool erase(QJsonArray &entries, const QString &id)
{
    for (qsizetype i = 0; i < entries.size(); ++i) {
        if (entries[i].toObject().value("id").toString() == id) {
            entries.removeAt(i);
            return true;
        }
    }
    return false;
}

void removeFiles(const QStringList &paths)
{
    for (const auto &path : paths)
        QFile::remove(path);
}

struct Bounds
{
    double left = std::numeric_limits<double>::infinity();
    double top = std::numeric_limits<double>::infinity();
    double right = 0;
    double bottom = 0;
};

Bounds bounds(const QJsonArray &entries)
{
    Bounds result;
    for (const auto &entry : entries) {
        const auto item = entry.toObject();
        const auto x = item.value("x").toDouble();
        const auto y = item.value("y").toDouble();
        result.left = std::min(result.left, x);
        result.top = std::min(result.top, y);
        result.right = std::max(result.right, x + item.value("w").toDouble());
        result.bottom = std::max(result.bottom, y + item.value("h").toDouble());
    }
    return result;
}

void place(QJsonObject &item, const QSize &canvas, double x, double y, double w, double h)
{
    const auto minimum = Project::minimumWidgetSize(item.value("kind").toString());
    const auto width = std::clamp(w, double(minimum.width()), double(canvas.width()));
    const auto height = std::clamp(h, double(minimum.height()), double(canvas.height()));
    item.insert("x", std::clamp(x, 0.0, canvas.width() - width));
    item.insert("y", std::clamp(y, 0.0, canvas.height() - height));
    item.insert("w", width);
    item.insert("h", height);
}

void clearTrendBinding(QJsonObject &item, const QString &tagId = {})
{
    auto trend = item.value("trend").toObject();
    if (trend.isEmpty()) return;
    auto traces = trend.value("series").toArray();
    for (qsizetype index = 0; index < traces.size(); ++index) {
        auto trace = traces[index].toObject();
        if (tagId.isEmpty() || trace.value("tag").toString() == tagId) {
            trace.insert("tag", "");
            traces.replace(index, trace);
        }
    }
    trend.insert("series", traces);
    item.insert("trend", trend);
}

void clearVisualBindings(QJsonObject &item, const QString &tagId = {})
{
    auto appearance = item.value("appearance").toObject();
    if (appearance.contains("tag") && (tagId.isEmpty() || appearance.value("tag").toString() == tagId)) {
        appearance.remove("tag");
        item.insert("appearance", appearance);
    }
    auto animation = item.value("animation").toObject();
    if (animation.contains("tag") && (tagId.isEmpty() || animation.value("tag").toString() == tagId)) {
        animation.insert("tag", "");
        item.insert("animation", animation);
    }
}

void clearActionBindings(QJsonObject &item, const QString &tagId = {})
{
    if (!item.value("actions").isArray())
        return;
    auto actions = item.value("actions").toArray();
    for (qsizetype index = 0; index < actions.size(); ++index) {
        auto row = actions[index].toObject();
        for (const auto *key : {"tag", "valueTag"}) {
            if (row.contains(key) && (tagId.isEmpty() || row.value(key).toString() == tagId))
                row.insert(key, "");
        }
        actions.replace(index, row);
    }
    item.insert("actions", actions);
}

template <typename Mapper>
void remapTagReferences(QJsonObject &item, const Mapper &map)
{
    const auto mapField = [&map](QJsonObject &object, const char *key) {
        const auto id = object.value(key).toString();
        if (!id.isEmpty())
            object.insert(key, map(id));
    };
    const auto mapRows = [&mapField](QJsonArray rows) {
        for (qsizetype i = 0; i < rows.size(); ++i) {
            auto row = rows[i].toObject();
            mapField(row, "tag");
            mapField(row, "valueTag");
            rows.replace(i, row);
        }
        return rows;
    };
    mapField(item, "tag");
    mapField(item, "feedback");
    for (const auto *key : {"appearance", "animation"}) {
        auto options = item.value(key).toObject();
        if (options.contains("tag")) {
            mapField(options, "tag");
            item.insert(key, options);
        }
    }
    auto trend = item.value("trend").toObject();
    if (trend.contains("series")) {
        trend.insert("series", mapRows(trend.value("series").toArray()));
        item.insert("trend", trend);
    }
    if (item.contains("actions"))
        item.insert("actions", mapRows(item.value("actions").toArray()));
}

template <typename Editor>
void editLayoutItems(QJsonObject &project, const char *key, const Editor &edit)
{
    if (!project.contains(key))
        return;
    auto layouts = project.value(key).toArray();
    for (qsizetype i = 0; i < layouts.size(); ++i) {
        auto layout = layouts[i].toObject();
        auto items = layout.value("items").toArray();
        for (qsizetype index = 0; index < items.size(); ++index) {
            auto item = items[index].toObject();
            edit(item);
            items.replace(index, item);
        }
        layout.insert("items", items);
        layouts.replace(i, layout);
    }
    project.insert(key, layouts);
}

void clearFaceplatePropertyBindings(QJsonObject &item, const QString &tagId = {})
{
    auto bindings = item.value("propertyBindings").toObject();
    if (bindings.isEmpty())
        return;
    for (const auto &propertyId : bindings.keys()) {
        auto binding = bindings.value(propertyId).toObject();
        if (tagId.isEmpty() || binding.value("tag").toString() == tagId) {
            binding.insert("tag", "");
            bindings.insert(propertyId, binding);
        }
    }
    item.insert("propertyBindings", bindings);
}

void clearFaceplateProperties(QJsonObject &project, const QJsonValue &typeId, const QSet<QString> &removed)
{
    if (removed.isEmpty())
        return;
    const auto clear = [&typeId, &removed](QJsonObject &item) {
        if (item.value("kind").toString() != "faceplate" || item.value("faceplate") != typeId)
            return;
        for (const auto *key : {"overrides", "propertyBindings"}) {
            auto values = item.value(key).toObject();
            for (const auto &id : removed)
                values.remove(id);
            if (values.isEmpty())
                item.remove(key);
            else
                item.insert(key, values);
        }
    };
    editLayoutItems(project, "screens", clear);
    editLayoutItems(project, "templates", clear);
}

QJsonObject faceplateInstance(const QJsonObject &type, const QJsonObject &bindings = {})
{
    return {{"id", Project::id()}, {"kind", "faceplate"}, {"faceplate", type.value("id")},
            {"title", type.value("name")}, {"bindings", bindings}};
}

QString faceplateOrigin(const QJsonObject &object)
{
    const auto value = object.value("originId").toString(object.value("id").toString());
    return QUuid::fromString(value).toString(QUuid::WithoutBraces);
}

QString faceplateFieldKey(const QJsonObject &field, bool related)
{
    return related ? faceplateOrigin(field) : field.value("name").toString().trimmed().toCaseFolded();
}

struct FaceplateFieldUse
{
    QSet<QString> tags;
    QSet<QString> properties;

    void include(const QJsonObject &item)
    {
        const auto bindings = item.value("bindings").toObject();
        for (auto it = bindings.constBegin(); it != bindings.constEnd(); ++it) {
            if (!it.value().toString().isEmpty())
                tags.insert(it.key());
        }
        for (const auto *field : {"overrides", "propertyBindings"}) {
            for (const auto &id : item.value(field).toObject().keys())
                properties.insert(id);
        }
    }
};

QVariantList faceplateFieldRows(const QJsonObject &previous, const QJsonObject &incoming, const FaceplateFieldUse &used);
QString buildFaceplateMapping(const QVariantList &rows, const QVariantMap &requested, QHash<QString, QString> &mapping);

void applyRememberedFaceplateFields(QVariantList &rows, const QJsonObject &project, const QString &sourceId,
                                   const QString &targetId)
{
    const auto presets = project.value("faceplateMappings").toObject();
    const auto direct = presets.value(sourceId).toObject().value(targetId).toObject();
    const auto reverse = presets.value(targetId).toObject().value(sourceId).toObject();
    for (auto &entry : rows) {
        auto row = entry.toMap();
        const auto id = row.value("id").toString();
        const auto choices = row.value("choices").toList();
        const auto compatible = [&choices](const QString &choiceId) {
            return std::any_of(choices.cbegin(), choices.cend(), [&choiceId](const QVariant &choice) {
                return choice.toMap().value("id").toString() == choiceId;
            });
        };
        if (direct.contains(id)) {
            const auto remembered = direct.value(id).toString();
            row["automatic"] = compatible(remembered) ? remembered : QString{};
        } else {
            QStringList candidates;
            for (auto it = reverse.constBegin(); it != reverse.constEnd(); ++it) {
                if (it.value().toString() == id && compatible(it.key()))
                    candidates.append(it.key());
            }
            if (candidates.size() == 1)
                row["automatic"] = candidates.first();
        }
        entry = row;
    }
}

void rememberFaceplateFields(QJsonObject &project, const QString &sourceId, const QString &targetId,
                             const QVariantMap &mapping)
{
    if (mapping.isEmpty())
        return;
    auto presets = project.value("faceplateMappings").toObject();
    auto destinations = presets.value(sourceId).toObject();
    auto fields = destinations.value(targetId).toObject();
    for (auto it = mapping.cbegin(); it != mapping.cend(); ++it)
        fields.insert(it.key(), it.value().toString());
    destinations.insert(targetId, fields);
    presets.insert(sourceId, destinations);
    project.insert("faceplateMappings", presets);
}

void forgetFaceplateFields(QJsonObject &project, const QString &id)
{
    auto presets = project.value("faceplateMappings").toObject();
    presets.remove(id);
    for (const auto &source : presets.keys()) {
        auto destinations = presets.value(source).toObject();
        destinations.remove(id);
        if (destinations.isEmpty())
            presets.remove(source);
        else
            presets.insert(source, destinations);
    }
    if (presets.isEmpty())
        project.remove("faceplateMappings");
    else
        project.insert("faceplateMappings", presets);

    auto importPresets = project.value("faceplateImportMappings").toObject();
    importPresets.remove(id);
    if (importPresets.isEmpty())
        project.remove("faceplateImportMappings");
    else
        project.insert("faceplateImportMappings", importPresets);
}

struct FaceplateTypeChange
{
    QJsonObject item;
    QString error;
    bool related = false;
    qsizetype tags = 0;
    qsizetype properties = 0;
};

FaceplateTypeChange remapFaceplateInstance(const QJsonObject &instance, const QJsonObject &source,
                                         const QJsonObject &target, const QHash<QString, QString> *mapping = nullptr)
{
    FaceplateTypeChange change;
    change.item = instance;
    change.item.insert("faceplate", target.value("id"));
    change.related = faceplateOrigin(source) == faceplateOrigin(target);
    QStringList unavailable;
    const auto transfer = [&](const char *interfaceKey, const char *kindKey,
                              std::initializer_list<const char *> fields, const QString &label) {
        QHash<QString, QJsonObject> sourceById;
        for (const auto &entry : source.value(interfaceKey).toArray()) {
            const auto definition = entry.toObject();
            sourceById.insert(definition.value("id").toString(), definition);
        }
        QHash<QString, QJsonObject> targetByKey;
        for (const auto &entry : target.value(interfaceKey).toArray()) {
            const auto definition = entry.toObject();
            const auto key = mapping ? definition.value("id").toString() : faceplateFieldKey(definition, change.related);
            targetByKey.insert(key, definition);
        }

        QSet<QString> transferred;
        for (const auto *field : fields) {
            if (!instance.contains(field))
                continue;
            const bool tags = QLatin1StringView(field) == "bindings";
            const auto settings = instance.value(field).toObject();
            QJsonObject remapped;
            for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
                const auto definition = sourceById.value(it.key());
                const bool used = !tags || !it.value().toString().isEmpty();
                if (mapping && !used)
                    continue;
                const auto key = mapping ? mapping->value(it.key()) : faceplateFieldKey(definition, change.related);
                const auto destination = targetByKey.value(key);
                if (destination.isEmpty() || destination.value(kindKey) != definition.value(kindKey)) {
                    const auto name = label.arg(definition.value("name").toString());
                    if (used && !unavailable.contains(name))
                        unavailable.append(name);
                    continue;
                }
                const auto newId = destination.value("id").toString();
                remapped.insert(newId, it.value());
                if (used)
                    transferred.insert(newId);
            }
            change.item.insert(field, remapped);
        }
        return transferred.size();
    };
    change.tags = transfer("ports", "type", {"bindings"}, "тег «%1»");
    change.properties = transfer("properties", "kind", {"overrides", "propertyBindings"}, "свойство «%1»");
    if (!unavailable.isEmpty()) {
        constexpr qsizetype shown = 5;
        auto list = unavailable.mid(0, shown).join(", ");
        if (unavailable.size() > shown)
            list += QString(" и ещё %1").arg(unavailable.size() - shown);
        change.error = QString("В выбранном типе нет совместимых полей: %1.").arg(list);
    }
    return change;
}

FaceplateTypeChange prepareFaceplateTypeChange(const QJsonObject &project, const QJsonObject &canvas,
                                             const QString &instanceId, const QString &typeId,
                                             const QVariantMap &mapping = {}, QVariantList *fields = nullptr)
{
    const auto instance = Project::find(canvas, "items", instanceId);
    const auto sourceId = instance.value("faceplate").toString();
    const auto source = Project::find(project, "faceplates", sourceId);
    const auto target = Project::find(project, "faceplates", typeId);
    if (instance.value("kind") != "faceplate" || source.isEmpty() || target.isEmpty() || sourceId == typeId) {
        FaceplateTypeChange change;
        change.error = "Выберите другой тип для экземпляра фейсплейса на экране.";
        return change;
    }
    FaceplateFieldUse used;
    used.include(instance);
    auto rows = faceplateFieldRows(source, target, used);
    applyRememberedFaceplateFields(rows, project, sourceId, typeId);
    if (fields)
        *fields = rows;
    if (rows.isEmpty() && mapping.isEmpty())
        return remapFaceplateInstance(instance, source, target);
    QHash<QString, QString> mapped;
    FaceplateTypeChange change;
    change.related = faceplateOrigin(source) == faceplateOrigin(target);
    change.error = buildFaceplateMapping(rows, mapping, mapped);
    if (!change.error.isEmpty())
        return change;
    return remapFaceplateInstance(instance, source, target, &mapped);
}
}

Studio::Studio(QObject *parent) : QObject(parent)
{
    connect(&runtime_, &Runtime::updated, this, &Studio::liveChanged);
    connect(&runtime_, &Runtime::alarmsChanged, this, &Studio::alarmsChanged);
    connect(&runtime_, &Runtime::archiveChanged, this, &Studio::archiveChanged);
    connect(&runtime_, &Runtime::warning, this, [this](const QString &error) { fail(error); });
    connect(&runtime_, &Runtime::writeResult, this,
            [this](const QString &, bool accepted, const QString &text) { emit notice(text, !accepted); });
    newProject(true);
}

Studio::~Studio()
{
    stop();
}

QVariantList Studio::sources() const { return project_.value("sources").toArray().toVariantList(); }
QVariantList Studio::tags() const { return project_.value("tags").toArray().toVariantList(); }
QVariantList Studio::screens() const { return project_.value("screens").toArray().toVariantList(); }
QVariantList Studio::templates() const { return project_.value("templates").toArray().toVariantList(); }
QVariantList Studio::faceplates() const { return project_.value("faceplates").toArray().toVariantList(); }
QJsonObject Studio::activeScreen() const { return Project::find(project_, "screens", screenId_); }
QJsonObject Studio::activeFaceplate() const { return Project::find(project_, "faceplates", faceplateId_); }
QJsonObject Studio::activeCanvas() const { return compositeDepth() ? activeFaceplate() : activeScreen(); }
QVariantList Studio::items() const { return activeCanvas().value("items").toArray().toVariantList(); }

QVariantMap Studio::canvasSettings() const
{
    auto canvas = activeCanvas();
    const auto size = Project::screenSize(canvas);
    canvas.remove("items");
    canvas.remove("ports");
    canvas.remove("properties");
    canvas.insert("width", size.width());
    canvas.insert("height", size.height());
    if (compositeDepth())
        canvas.insert("gridStep", activeScreen().value("gridStep").toInt(10));
    return canvas.toVariantMap();
}

QVariantList Studio::editorTags() const
{
    if (!compositeDepth())
        return tags();
    QVariantList result;
    for (const auto &entry : activeFaceplate().value("ports").toArray()) {
        const auto port = entry.toObject();
        const auto type = port.value("type").toString();
        result.append(QVariantMap{{"id", port.value("id").toString()}, {"name", port.value("name").toString()},
                                  {"address", QVariantMap{{"type", type}}}, {"interfacePort", true},
                                  {"writable", type != "text"}});
    }
    return result;
}

QVariantMap Studio::widget() const
{
    return Project::find(activeCanvas(), "items", selectedId_).toVariantMap();
}

bool Studio::simulator() const
{
    for (const auto &entry : project_.value("sources").toArray()) {
        if (entry.toObject().value("settings").toObject().value("simulator").toBool())
            return true;
    }
    return false;
}

bool Studio::fail(const QString &text)
{
    emit notice(text, true);
    return false;
}

QString Studio::archivePath(const QString &projectPath) const
{
    return projectPath.isEmpty() ? QString{} : QFileInfo(projectPath).absoluteDir().filePath("history.sqlite");
}

void Studio::replaceProject(const QJsonObject &object, const QString &path)
{
    stagedFaceplateImages_.reset();
    project_ = saved_ = object;
    path_ = path;
    undo_.clear();
    redo_.clear();
    screenId_.clear();
    faceplateId_.clear();
    parentSelection_.clear();
    selectedId_.clear();
    selectedIds_.clear();
    repairSelection();
    emit projectChanged();
    emit selectionChanged();
    emit canvasChanged();
}

void Studio::newProject(bool demo)
{
    stop();
    const auto next = demo ? DemoServer::project() : Project::empty();
    QString error;
    if (!runtime_.openArchive({}, next.value("id").toString(), error)) {
        fail(error);
        return;
    }
    replaceProject(next, {});
}

bool Studio::open(const QUrl &url)
{
    if (!url.isLocalFile())
        return fail("Выберите локальный файл проекта.");
    QJsonObject next;
    QString error;
    const auto path = QFileInfo(url.toLocalFile()).absoluteFilePath();
    if (!Project::load(path, next, error))
        return fail(QString("Не удалось открыть проект. %1").arg(error));
    stop();
    const auto archive = archivePath(path);
    if (!runtime_.openArchive(QFileInfo::exists(archive) ? archive : QString{}, next.value("id").toString(), error))
        return fail(QString("Не удалось открыть архив. %1").arg(error));
    replaceProject(next, path);
    return true;
}

bool Studio::save(const QUrl &url)
{
    if (!url.isEmpty() && !url.isLocalFile())
        return fail("Выберите локальный файл проекта.");
    auto path = url.isEmpty() ? path_ : QFileInfo(url.toLocalFile()).absoluteFilePath();
    if (path.isEmpty())
        return fail("Выберите файл проекта.");
    if (!path.endsWith(".scada.json", Qt::CaseInsensitive))
        path += ".scada.json";
    if (running() && path != path_)
        return fail("Остановите опрос перед сохранением проекта в новый файл.");
    auto next = project_;
    QStringList copiedImages;
    const bool fork = !path_.isEmpty() && path != path_;
    const bool fromStaging = path_.isEmpty() && stagedFaceplateImages_;
    if (fork)
        next.insert("id", Project::id());
    if (fork || fromStaging) {
        const auto sourceDir = fromStaging ? QDir(stagedFaceplateImages_->path()) : QFileInfo(path_).absoluteDir();
        const auto targetDir = QFileInfo(path).absoluteDir();
        if (sourceDir != targetDir) {
            QSet<QString> images;
            if (fromStaging) {
                // Keep images reachable through undo and redo after the first save.
                const QDir staging(sourceDir.filePath("images"));
                for (const auto &file : staging.entryList(QDir::Files))
                    images.insert("images/" + file);
            } else {
                for (const auto *key : {"screens", "templates", "faceplates"}) {
                    for (const auto &entry : next.value(key).toArray()) {
                        const auto screen = entry.toObject();
                        const auto background = screen.value("backgroundImage").toString();
                        if (!background.isEmpty() && !Project::isDemoImage(background))
                            images.insert(background);
                        for (const auto &item : screen.value("items").toArray()) {
                            const auto image = item.toObject().value("image").toString();
                            if (!image.isEmpty() && !Project::isDemoImage(image))
                                images.insert(image);
                        }
                    }
                }
            }
            if (!images.isEmpty() && !targetDir.mkpath("images"))
                return fail("Не удалось создать папку изображений.");
            for (const auto &image : images) {
                const auto destination = targetDir.filePath(image);
                if (QFile::exists(destination))
                    continue;
                if (!QFile::copy(sourceDir.filePath(image), destination)) {
                    removeFiles(copiedImages);
                    return fail(QString("Не удалось скопировать %1.").arg(image));
                }
                copiedImages.append(destination);
            }
        }
    }
    QString error;
    if (!Project::save(path, next, error)) {
        removeFiles(copiedImages);
        return fail(QString("Не удалось сохранить проект. %1").arg(error));
    }
    if (fork) {
        undo_.clear();
        redo_.clear();
        const auto archive = archivePath(path);
        if (!runtime_.openArchive(QFileInfo::exists(archive) ? archive : QString{}, next.value("id").toString(), error))
            fail(QString("Проект сохранён. Архив недоступен. %1").arg(error));
    }
    project_ = saved_ = next;
    path_ = path;
    if (fromStaging)
        stagedFaceplateImages_.reset();
    emit projectChanged();
    emit selectionChanged();
    emit canvasChanged();
    emit notice("Проект сохранён.", false);
    return true;
}

bool Studio::start()
{
    if (running())
        return true;
    const auto entries = project_.value("sources").toArray();
    if (entries.isEmpty() || tags().isEmpty())
        return fail("Добавьте хотя бы один источник и тег.");
    if (path_.isEmpty()) {
        for (const auto &entry : entries) {
            if (!entry.toObject().value("settings").toObject().value("simulator").toBool())
                return fail("Сохраните проект перед запуском опроса устройства.");
        }
    } else {
        if (modified() && !save())
            return false;
        QString error;
        if (!runtime_.openArchive(archivePath(path_), project_.value("id").toString(), error))
            return fail(QString("Не удалось открыть архив. %1").arg(error));
    }
    for (const auto &entry : entries) {
        const auto settings = entry.toObject().value("settings").toObject();
        if (!settings.value("simulator").toBool())
            continue;
        QString error;
        auto *server = new DemoServer(this);
        if (!server->listen(settings.value("port").toInt(), error)) {
            delete server;
            qDeleteAll(std::exchange(simulators_, {}));
            return fail(QString("Не удалось запустить симулятор. %1").arg(error));
        }
        simulators_.append(server);
    }
    simulatorPaused_ = false;
    runtime_.start(project_);
    emit stateChanged();
    emit projectChanged();
    return true;
}

void Studio::stop()
{
    if (!running())
        return;
    runtime_.stop();
    qDeleteAll(std::exchange(simulators_, {}));
    simulatorPaused_ = false;
    emit stateChanged();
    emit projectChanged();
}

void Studio::toggleSimulator()
{
    if (simulators_.isEmpty())
        return;
    simulatorPaused_ = !simulatorPaused_;
    for (auto *server : simulators_)
        server->setPaused(simulatorPaused_);
    emit stateChanged();
}

bool Studio::acquisitionEdit()
{
    return !running() || fail("Остановите опрос для изменения источников и тегов.");
}

bool Studio::commit(const QJsonObject &next)
{
    const auto error = Project::validate(next);
    if (!error.isEmpty())
        return fail(error);
    if (next == project_)
        return true;
    undo_.append(project_);
    if (undo_.size() > 75)
        undo_.removeFirst();
    redo_.clear();
    project_ = next;
    repairSelection();
    emit projectChanged();
    emit selectionChanged();
    emit canvasChanged();
    return true;
}

void Studio::undo()
{
    if (!canUndo())
        return;
    redo_.append(project_);
    project_ = undo_.takeLast();
    repairSelection();
    emit projectChanged();
    emit selectionChanged();
    emit canvasChanged();
}

void Studio::redo()
{
    if (!canRedo())
        return;
    undo_.append(project_);
    project_ = redo_.takeLast();
    repairSelection();
    emit projectChanged();
    emit selectionChanged();
    emit canvasChanged();
}

void Studio::repairSelection()
{
    if (activeScreen().isEmpty()) {
        screenId_ = project_.value("screens").toArray().first().toObject().value("id").toString();
        parentSelection_.clear();
    }
    if (compositeDepth() && activeFaceplate().isEmpty()) {
        faceplateId_.clear();
        parentSelection_.clear();
    }
    const auto screen = activeCanvas();
    selectedIds_.removeIf([&screen](const auto &id) { return Project::find(screen, "items", id).isEmpty(); });
    if (!selectedIds_.contains(selectedId_))
        selectedId_ = selectedIds_.isEmpty() ? QString{} : selectedIds_.last();
}

void Studio::renameProject(const QString &name)
{
    auto next = project_;
    next.insert("name", name.trimmed());
    commit(next);
}

bool Studio::changeEntity(const QString &key, QJsonObject object)
{
    auto next = project_;
    auto entries = next.value(key).toArray();
    if (object.value("id").toString().isEmpty()) {
        object.insert("id", Project::id());
        entries.append(object);
    } else if (!replace(entries, object)) {
        return fail("Эта запись уже удалена.");
    }
    object.insert("name", object.value("name").toString().trimmed());
    replace(entries, object);
    next.insert(key, entries);
    return commit(next);
}

bool Studio::setSource(const QVariantMap &source)
{
    return acquisitionEdit() && changeEntity("sources", QJsonObject::fromVariantMap(source));
}

bool Studio::setTag(const QVariantMap &tag)
{
    return acquisitionEdit() && changeEntity("tags", QJsonObject::fromVariantMap(tag));
}

bool Studio::importEngineTags(const QString &sourceId, const QUrl &url)
{
    if (!acquisitionEdit()) return false;
    const auto source = Project::find(project_, "sources", sourceId);
    if (source.value("driver").toString() != "engine-json")
        return fail("Выберите источник JSON-телеметрии.");
    if (!url.isEmpty() && !url.isLocalFile())
        return fail("Выберите локальную таблицу тегов.");
    QFile file(url.isEmpty() ? QString(":/engine-tags.json") : url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024)
        return fail("Не удалось прочитать таблицу тегов JSON.");
    const auto catalog = QJsonDocument::fromJson(file.readAll()).object();
    const auto rows = catalog.value("tags").toArray();
    if (catalog.value("version").toInt() != 1 || rows.isEmpty())
        return fail("Ожидается JSON версии 1 со списком tags.");
    auto next = project_;
    auto tags = next.value("tags").toArray();
    QHash<QString, QJsonObject> owned;
    for (const auto &entry : tags) {
        const auto tag = entry.toObject();
        if (tag.value("source") == sourceId) owned.insert(tag.value("name").toString().toCaseFolded(), tag);
    }
    int added = 0;
    for (const auto &entry : rows) {
        const auto row = entry.toObject();
        const auto name = row.value("name").toString().trimmed();
        const auto path = row.value("path").toString();
        const auto type = row.value("type").toString();
        if (name.isEmpty() || path.isEmpty())
            return fail("Каждой строке таблицы нужны name, path и type.");
        const auto interval = row.value("archiveMs");
        if ((!interval.isUndefined() && (!interval.isDouble() || interval.toDouble() != interval.toInt())) ||
            (!row.value("scale").isUndefined() && !row.value("scale").isDouble()) ||
            (!row.value("bias").isUndefined() && !row.value("bias").isDouble()))
            return fail(QString("В строке «%1» проверьте период, масштаб и смещение.").arg(name));
        const auto fullName = source.value("name").toString() + "." + name;
        const auto existing = owned.value(fullName.toCaseFolded());
        const QJsonObject address{{"path", path}, {"type", type}};
        if (!existing.isEmpty()) {
            if (existing.value("address").toObject() != address)
                return fail(QString("Тег «%1» уже привязан к другому полю или типу.").arg(fullName));
            continue;
        }
        QJsonObject tag{{"id", Project::id()}, {"source", sourceId}, {"name", fullName}, {"address", address},
                        {"units", row.value("units").toString()}, {"writable", false},
                        {"archiveMs", row.value("archiveMs").toInt(type == "text" ? 0 : 200)},
                        {"scale", row.value("scale").toDouble(1)}, {"bias", row.value("bias").toDouble(0)}};
        if (row.contains("alarm")) tag.insert("alarm", row.value("alarm"));
        owned.insert(fullName.toCaseFolded(), tag);
        tags.append(tag);
        ++added;
    }
    next.insert("tags", tags);
    if (!commit(next)) return false;
    emit notice(QString("Добавлено тегов из таблицы JSON: %1.").arg(added), false);
    return true;
}

bool Studio::exportEngineTags(const QString &sourceId, const QUrl &url)
{
    const auto source = Project::find(project_, "sources", sourceId);
    if (source.value("driver").toString() != "engine-json" || !url.isLocalFile())
        return fail("Выберите источник JSON-телеметрии и локальный файл.");
    const auto prefix = source.value("name").toString() + ".";
    QJsonArray rows;
    for (const auto &entry : project_.value("tags").toArray()) {
        const auto tag = entry.toObject();
        if (tag.value("source") != sourceId) continue;
        const auto addr = tag.value("address").toObject();
        auto name = tag.value("name").toString();
        if (name.startsWith(prefix)) name.remove(0, prefix.size());
        QJsonObject row{{"path", addr.value("path")}, {"type", addr.value("type")}, {"name", name},
                        {"units", tag.value("units")}, {"archiveMs", tag.value("archiveMs")},
                        {"scale", tag.value("scale")}, {"bias", tag.value("bias")}};
        if (tag.contains("alarm")) row.insert("alarm", tag.value("alarm"));
        rows.append(row);
    }
    if (rows.isEmpty()) return fail("У источника пока нет тегов.");
    auto path = url.toLocalFile();
    if (!path.endsWith(".json", Qt::CaseInsensitive)) path += ".json";
    QSaveFile file(path);
    const auto data = QJsonDocument(QJsonObject{{"version",1}, {"tags",rows}}).toJson();
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit())
        return fail(file.errorString());
    emit notice("Таблица тегов сохранена.", false);
    return true;
}

bool Studio::removeSource(const QString &id)
{
    if (!acquisitionEdit())
        return false;
    for (const auto &value : project_.value("tags").toArray()) {
        if (value.toObject().value("source").toString() == id)
            return fail("Перенесите или удалите теги этого источника.");
    }
    auto next = project_;
    auto entries = next.value("sources").toArray();
    if (!erase(entries, id))
        return false;
    next.insert("sources", entries);
    return commit(next);
}

bool Studio::removeTag(const QString &id)
{
    if (!acquisitionEdit())
        return false;
    auto next = project_;
    auto entries = next.value("tags").toArray();
    if (!erase(entries, id))
        return false;
    editLayoutItems(next, "screens", [&id](QJsonObject &item) {
        if (item.value("kind").toString() == "faceplate") {
            auto bindings = item.value("bindings").toObject();
            for (const auto &port : bindings.keys()) {
                if (bindings.value(port).toString() == id)
                    bindings.remove(port);
            }
            item.insert("bindings", bindings);
            clearFaceplatePropertyBindings(item, id);
        }
        if (item.value("tag").toString() == id)
            item.insert("tag", "");
        if (item.value("feedback").toString() == id)
            item.remove("feedback");
        clearTrendBinding(item, id);
        clearVisualBindings(item, id);
        clearActionBindings(item, id);
    });
    next.insert("tags", entries);
    return commit(next);
}

void Studio::selectScreen(const QString &id)
{
    if ((screenId_ == id && !compositeDepth()) || Project::find(project_, "screens", id).isEmpty())
        return;
    screenId_ = id;
    faceplateId_.clear();
    parentSelection_.clear();
    selectedId_.clear();
    selectedIds_.clear();
    emit selectionChanged();
    emit canvasChanged();
}

void Studio::selectWidget(const QString &id, bool extend)
{
    const auto screen = activeCanvas();
    const auto item = Project::find(screen, "items", id);
    if (item.isEmpty()) {
        selectedIds_.clear();
        selectedId_.clear();
        emit selectionChanged();
        return;
    }
    QStringList members{id};
    const auto group = item.value("group").toString();
    if (!group.isEmpty()) {
        for (const auto &entry : screen.value("items").toArray()) {
            const auto sibling = entry.toObject();
            const auto other = sibling.value("id").toString();
            if (sibling.value("group").toString() == group && other != id)
                members.append(other);
        }
    }
    if (extend) {
        if (selectedIds_.contains(id)) {
            for (const auto &member : members)
                selectedIds_.removeAll(member);
        } else {
            for (const auto &member : members) {
                if (!selectedIds_.contains(member))
                    selectedIds_.append(member);
            }
        }
    } else if (!selectedIds_.contains(id)) {
        selectedIds_ = members;
    }
    selectedId_ = id;
    repairSelection();
    emit selectionChanged();
}

void Studio::setScreen(const QString &id, const QString &name)
{
    auto next = project_;
    auto screens = next.value("screens").toArray();
    auto screen = Project::find(next, "screens", id);
    if (id.isEmpty()) {
        screen = {{"id", Project::id()}, {"items", QJsonArray{}}};
    } else if (screen.isEmpty()) {
        return;
    }
    screen.insert("name", name.trimmed());
    if (id.isEmpty())
        screens.append(screen);
    else
        replace(screens, screen);
    next.insert("screens", screens);
    if (commit(next))
        selectScreen(screen.value("id").toString());
}

bool Studio::setScreenProperties(const QString &id, const QVariantMap &properties, bool scaleItems,
                                 const QUrl &newBackground)
{
    auto next = project_;
    auto screens = next.value("screens").toArray();
    auto screen = Project::find(next, "screens", id);
    if (screen.isEmpty())
        return fail("Экран не найден.");
    const auto previous = Project::screenSize(screen);
    for (const auto *field : {"name", "width", "height", "background", "backgroundFit", "backgroundImage", "gridStep", "operatorFit"}) {
        if (properties.contains(field))
            screen.insert(field, QJsonValue::fromVariant(properties.value(field)));
    }
    screen.insert("name", screen.value("name").toString().trimmed());
    if (const auto error = Project::validateScreenSettings(screen); !error.isEmpty())
        return fail(error);

    const auto canvas = Project::screenSize(screen);
    if (canvas != previous) {
        const double sx = scaleItems ? double(canvas.width()) / previous.width() : 1.0;
        const double sy = scaleItems ? double(canvas.height()) / previous.height() : 1.0;
        QJsonArray placed;
        for (const auto &entry : screen.value("items").toArray()) {
            auto item = entry.toObject();
            place(item, canvas, item.value("x").toDouble() * sx, item.value("y").toDouble() * sy,
                  item.value("w").toDouble() * sx, item.value("h").toDouble() * sy);
            placed.append(item);
        }
        screen.insert("items", placed);
    }
    QString imported;
    if (!newBackground.isEmpty()) {
        imported = importImage(newBackground);
        if (imported.isEmpty())
            return false;
        screen.insert("backgroundImage", imported);
    }
    replace(screens, screen);
    next.insert("screens", screens);
    if (commit(next))
        return true;
    if (!imported.isEmpty())
        QFile::remove(imageUrl(imported).toLocalFile());
    return false;
}

void Studio::removeScreen()
{
    auto next = project_;
    auto screens = next.value("screens").toArray();
    if (screens.size() == 1) {
        fail("В проекте должен остаться хотя бы один экран.");
        return;
    }
    erase(screens, screenId_);
    next.insert("screens", screens);
    const auto clearScreen = [this](QJsonObject &item) {
        if (item.value("screen").toString() == screenId_)
            item.insert("screen", "");
    };
    editLayoutItems(next, "screens", clearScreen);
    editLayoutItems(next, "faceplates", clearScreen);
    commit(next);
}

bool Studio::setItems(const QJsonArray &entries)
{
    return setItems(entries, project_);
}

bool Studio::setItems(const QJsonArray &entries, QJsonObject next)
{
    const auto insideFaceplate = compositeDepth() != 0;
    const auto *key = insideFaceplate ? "faceplates" : "screens";
    auto layouts = next.value(key).toArray();
    auto canvas = activeCanvas();
    if (insideFaceplate && canvas.contains("properties")) {
        QSet<QString> childIds;
        for (const auto &entry : entries)
            childIds.insert(entry.toObject().value("id").toString());
        QSet<QString> removed;
        QJsonArray properties;
        for (const auto &entry : canvas.value("properties").toArray()) {
            const auto property = entry.toObject();
            if (childIds.contains(property.value("item").toString()))
                properties.append(entry);
            else
                removed.insert(property.value("id").toString());
        }
        if (!removed.isEmpty()) {
            canvas.insert("properties", properties);
            clearFaceplateProperties(next, canvas.value("id"), removed);
        }
    }
    canvas.insert("items", entries);
    if (!replace(layouts, canvas))
        return false;
    next.insert(key, layouts);
    return commit(next);
}

void Studio::addWidget(const QString &kind, double x, double y)
{
    const QHash<QString, QString> names{{"value", "Значение"}, {"indicator", "Индикатор"}, {"trend", "Тренд"},
                                       {"label", "Заголовок"}, {"gauge", "Шкала"}, {"command", "Команда"},
                                       {"link", "Переход"}, {"image", "Изображение"}, {"tank", "Бак"}, {"pump", "Насос"},
                                       {"dial", "Стрелочный прибор"}, {"column", "Термометр"}, {"ring", "Сегментная шкала"},
                                       {"segments", "Цифровое табло"}, {"signal", "Сигнальная лампа"}, {"compass", "Компас"},
                                       {"switch", "Тумблер"}, {"setpoint", "Ввод уставки"}, {"slider", "Ползунок"},
                                       {"knob", "Регулятор"}, {"selector", "Выбор режима"}, {"valve", "Клапан"},
                                       {"bargraph", "Линейный прибор"}, {"motor", "Двигатель"}, {"pipe", "Трубопровод"},
                                       {"alarm", "Тревога"}, {"shape", "Фигура"}, {"hitArea", "Зона команды"}};
    const auto id = Project::id();
    const QSet<QString> wide{"segments", "bargraph", "setpoint", "slider", "selector", "alarm"};
    const int width = kind == "trend" ? 520 : wide.contains(kind) ? 310 : 224;
    const QHash<QString, int> heights{{"trend", 310}, {"shape", 80}, {"hitArea", 70}, {"label", 64}, {"pipe", 64}, {"tank", 230}, {"dial", 230},
        {"column", 230}, {"ring", 230}, {"compass", 230}, {"knob", 215}, {"valve", 190}, {"selector", 175}};
    const int height = heights.value(kind, 132);
    const auto screen = activeCanvas();
    auto items = screen.value("items").toArray();
    QJsonObject item{{"id", id}, {"title", names.value(kind)}, {"kind", kind}, {"tag", ""},
                     {"min", 0}, {"max", 100}, {"command", 1}};
    place(item, Project::screenSize(screen), x, y, width, height);
    items.append(item);
    if (setItems(items))
        selectWidget(id);
}

void Studio::moveWidget(const QString &id, double x, double y, double w, double h)
{
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h))
        return;
    const auto screen = activeCanvas();
    const auto canvas = Project::screenSize(screen);
    auto entries = screen.value("items").toArray();
    auto item = Project::find(screen, "items", id);
    if (item.isEmpty() || item.value("locked").toBool())
        return;
    if (selectedIds_.size() > 1 && selectedIds_.contains(id)
        && w == item.value("w").toDouble() && h == item.value("h").toDouble()) {
        QJsonArray selected;
        for (const auto &entry : selectedItems()) {
            if (!entry.toObject().value("locked").toBool())
                selected.append(entry);
        }
        const auto area = bounds(selected);
        const auto dx = std::clamp(x - item.value("x").toDouble(), -area.left, std::max(-area.left, canvas.width() - area.right));
        const auto dy = std::clamp(y - item.value("y").toDouble(), -area.top, std::max(-area.top, canvas.height() - area.bottom));
        for (const auto &entry : selected) {
            auto member = entry.toObject();
            place(member, canvas, member.value("x").toDouble() + dx, member.value("y").toDouble() + dy,
                  member.value("w").toDouble(), member.value("h").toDouble());
            replace(entries, member);
        }
        setItems(entries);
        return;
    }
    place(item, canvas, x, y, w, h);
    if (replace(entries, item))
        setItems(entries);
}

bool Studio::configureWidget(const QVariantMap &properties)
{
    const auto screen = activeCanvas();
    auto entries = screen.value("items").toArray();
    auto item = Project::find(screen, "items", selectedId_);
    if (item.isEmpty())
        return false;
    for (const auto *key : {"title", "tag", "min", "max", "command", "screen", "image", "decimals", "accent", "trend", "appearance",
                           "step", "feedback", "positions", "vertical", "confirmWrite", "actions", "bindings",
                           "overrides", "propertyBindings", "shape", "visible", "locked", "animation"}) {
        if (!properties.contains(key))
            continue;
        const auto value = QJsonValue::fromVariant(properties.value(key));
        if (value.isNull() && (QLatin1StringView(key) == "step" || QLatin1StringView(key) == "animation"))
            item.remove(key);
        else
            item.insert(key, value);
    }
    return replace(entries, item) && setItems(entries);
}

QVariantMap Studio::widgetColors(const QVariantMap &appearance, const QVariantMap &reading) const
{
    return Project::widgetColors(appearance, reading);
}

bool Studio::animationActive(const QVariantMap &animation, const QVariantMap &reading) const
{
    return Project::animationActive(animation, reading);
}

void Studio::removeWidget()
{
    auto entries = activeCanvas().value("items").toArray();
    for (const auto &id : selectedIds_)
        erase(entries, id);
    if (!selectedIds_.isEmpty())
        setItems(entries);
}

void Studio::duplicateWidget()
{
    insertCopies(selectedItems(), 20, 20);
}

QJsonArray Studio::selectedItems() const
{
    QJsonArray result;
    for (const auto &item : activeCanvas().value("items").toArray()) {
        if (selectedIds_.contains(item.toObject().value("id").toString()))
            result.append(item);
    }
    return result;
}

void Studio::insertCopies(const QJsonArray &items, double dx, double dy)
{
    if (items.isEmpty())
        return;
    const auto screen = activeCanvas();
    const auto canvas = Project::screenSize(screen);
    auto copies = items;
    auto area = bounds(copies);
    const auto scale = std::min({1.0, canvas.width() / (area.right - area.left),
                                 canvas.height() / (area.bottom - area.top)});
    if (scale < 1.0) {
        dx += area.left;
        dy += area.top;
        for (qsizetype index = 0; index < copies.size(); ++index) {
            auto item = copies[index].toObject();
            place(item, canvas, (item.value("x").toDouble() - area.left) * scale,
                  (item.value("y").toDouble() - area.top) * scale,
                  item.value("w").toDouble() * scale, item.value("h").toDouble() * scale);
            copies.replace(index, item);
        }
        area = bounds(copies);
    }
    dx = std::clamp(dx, -area.left, std::max(-area.left, canvas.width() - area.right));
    dy = std::clamp(dy, -area.top, std::max(-area.top, canvas.height() - area.bottom));
    const auto group = items.size() > 1 ? Project::id() : QString{};
    auto entries = screen.value("items").toArray();
    QString first;
    for (const auto &entry : copies) {
        auto copy = entry.toObject();
        const auto id = Project::id();
        if (first.isEmpty())
            first = id;
        copy.insert("id", id);
        place(copy, canvas, copy.value("x").toDouble() + dx, copy.value("y").toDouble() + dy,
              copy.value("w").toDouble(), copy.value("h").toDouble());
        copy.remove("group");
        if (!group.isEmpty())
            copy.insert("group", group);
        entries.append(copy);
    }
    if (setItems(entries))
        selectWidget(first);
}

void Studio::alignWidgets(bool vertical)
{
    const auto selected = selectedItems();
    if (selected.size() < 2)
        return;
    const auto area = bounds(selected);
    auto entries = activeCanvas().value("items").toArray();
    for (const auto &entry : selected) {
        auto item = entry.toObject();
        if (item.value("locked").toBool())
            continue;
        item.insert(vertical ? "y" : "x", vertical ? area.top : area.left);
        replace(entries, item);
    }
    setItems(entries);
}

void Studio::groupWidgets(bool group)
{
    if (group && selectedIds_.size() < 2)
        return;
    const auto groupId = group ? Project::id() : QString{};
    auto entries = activeCanvas().value("items").toArray();
    for (const auto &entry : selectedItems()) {
        auto item = entry.toObject();
        if (group)
            item.insert("group", groupId);
        else
            item.remove("group");
        replace(entries, item);
    }
    setItems(entries);
}

bool Studio::createFaceplate(const QString &name, int width, int height)
{
    if (compositeDepth())
        return fail("Завершите редактирование текущего типа перед созданием нового.");
    if (width < 80 || width > 8192 || height < 52 || height > 8192)
        return fail("Ширина типа должна быть от 80 до 8192 px, высота — от 52 до 8192 px.");
    const auto selected = selectedItems();
    const auto typeId = Project::id();
    QJsonObject type{{"id", typeId}, {"name", name.trimmed()}, {"width", width}, {"height", height},
                     {"ports", QJsonArray{}}, {"items", QJsonArray{}}};
    auto next = project_;
    auto types = next.value("faceplates").toArray();
    QString instanceId;
    if (!selected.isEmpty()) {
        for (const auto &entry : selected) {
            if (entry.toObject().value("kind").toString() == "faceplate")
                return fail("Экземпляр составного виджета нельзя вложить в другой тип.");
        }
        const auto area = bounds(selected);
        const auto x = std::floor(area.left);
        const auto y = std::floor(area.top);
        const auto width = int(std::ceil(area.right) - x);
        const auto height = int(std::ceil(area.bottom) - y);
        type.insert("width", std::max(width, 80));
        type.insert("height", std::max(height, 52));

        QJsonArray ports;
        QJsonObject bindings;
        QHash<QString, QString> portForTag;
        const auto portId = [&](const QString &tagId) {
            if (const auto existing = portForTag.value(tagId); !existing.isEmpty())
                return existing;
            const auto tag = Project::find(project_, "tags", tagId);
            const auto addressType = tag.value("address").toObject().value("type").toString();
            const auto portType = addressType == "bool" || addressType == "text" ? addressType : QString("number");
            const auto id = Project::id();
            ports.append(QJsonObject{{"id", id}, {"name", tag.value("name")}, {"type", portType}});
            bindings.insert(id, tagId);
            portForTag.insert(tagId, id);
            return id;
        };
        QJsonArray children;
        for (const auto &entry : selected) {
            auto item = entry.toObject();
            item.insert("x", item.value("x").toDouble() - x);
            item.insert("y", item.value("y").toDouble() - y);
            remapTagReferences(item, portId);
            children.append(item);
        }
        type.insert("items", children);
        type.insert("ports", ports);

        auto instance = faceplateInstance(type, bindings);
        instanceId = instance.value("id").toString();
        auto screen = activeScreen();
        place(instance, Project::screenSize(screen), x, y, width, height);
        QJsonArray entries;
        const auto last = selected.last().toObject().value("id").toString();
        for (const auto &entry : screen.value("items").toArray()) {
            const auto id = entry.toObject().value("id").toString();
            if (id == last)
                entries.append(instance);
            else if (!selectedIds_.contains(id))
                entries.append(entry);
        }
        screen.insert("items", entries);
        auto screens = next.value("screens").toArray();
        replace(screens, screen);
        next.insert("screens", screens);
    }
    types.append(type);
    next.insert("faceplates", types);
    if (!commit(next))
        return false;
    if (instanceId.isEmpty())
        return enterComposite(typeId);
    selectWidget(instanceId);
    return true;
}

bool Studio::renameFaceplate(const QString &id, const QString &name)
{
    auto type = Project::find(project_, "faceplates", id);
    if (type.isEmpty())
        return fail("Тип фейсплейса не найден.");
    type.insert("name", name.trimmed());
    auto next = project_;
    auto types = next.value("faceplates").toArray();
    replace(types, type);
    next.insert("faceplates", types);
    return commit(next);
}

namespace {
QString availableFaceplateCopyName(const QJsonObject &type, const QJsonArray &types)
{
    QSet<QString> names;
    for (const auto &entry : types)
        names.insert(entry.toObject().value("name").toString().trimmed().toCaseFolded());
    const auto originalName = type.value("name").toString().trimmed();
    auto name = originalName + " (копия)";
    for (int number = 2; names.contains(name.toCaseFolded()); ++number)
        name = QString("%1 (копия %2)").arg(originalName).arg(number);
    return name;
}

QJsonObject makeFaceplateCopy(QJsonObject copy, const QString &name,
                              QHash<QString, QString> *copiedFields = nullptr)
{
    copy.insert("originId", faceplateOrigin(copy));
    copy.insert("id", Project::id());
    copy.insert("name", name);

    QHash<QString, QString> portIds;
    QJsonArray ports;
    for (const auto &entry : copy.value("ports").toArray()) {
        auto port = entry.toObject();
        const auto newId = Project::id();
        portIds.insert(port.value("id").toString(), newId);
        port.insert("originId", faceplateOrigin(port));
        port.insert("id", newId);
        ports.append(port);
    }
    copy.insert("ports", ports);
    if (copiedFields)
        *copiedFields = portIds;

    QHash<QString, QString> itemIds;
    QHash<QString, QString> groupIds;
    QJsonArray items;
    for (const auto &entry : copy.value("items").toArray()) {
        auto item = entry.toObject();
        const auto newId = Project::id();
        itemIds.insert(item.value("id").toString(), newId);
        item.insert("id", newId);
        const auto group = item.value("group").toString();
        if (!group.isEmpty()) {
            auto &newGroup = groupIds[group];
            if (newGroup.isEmpty())
                newGroup = Project::id();
            item.insert("group", newGroup);
        }
        remapTagReferences(item, [&portIds](const QString &port) { return portIds.value(port); });
        items.append(item);
    }
    copy.insert("items", items);

    if (copy.contains("properties")) {
        QJsonArray properties;
        for (const auto &entry : copy.value("properties").toArray()) {
            auto property = entry.toObject();
            const auto newId = Project::id();
            if (copiedFields)
                copiedFields->insert(property.value("id").toString(), newId);
            property.insert("originId", faceplateOrigin(property));
            property.insert("id", newId);
            property.insert("item", itemIds.value(property.value("item").toString()));
            properties.append(property);
        }
        copy.insert("properties", properties);
    }
    return copy;
}

bool writeFaceplateData(const QString &path, const QByteArray &data, QString &error)
{
    QSaveFile file(path);
    if (file.open(QIODevice::WriteOnly) && file.write(data) == data.size() && file.commit())
        return true;
    error = file.errorString();
    return false;
}

QJsonObject readFaceplatePackage(const QUrl &url, QString &error)
{
    if (!url.isLocalFile()) {
        error = "Выберите локальный файл фейсплейса.";
        return {};
    }
    QFile file(url.toLocalFile());
    if (!file.open(QIODevice::ReadOnly)) {
        error = QString("Не удалось открыть файл фейсплейса. %1").arg(file.errorString());
        return {};
    }
    const auto package = QJsonDocument::fromJson(file.readAll()).object();
    if (package.value("format") != "scada-faceplate" || package.value("version") != QJsonValue(1) ||
        !package.value("faceplate").isObject() || !package.value("images").isObject() ||
        !package.value("screens").isObject()) {
        error = "Ожидается файл типа фейсплейса версии 1.";
        return {};
    }

    auto validation = Project::empty();
    validation.insert("faceplates", QJsonArray{package.value("faceplate")});
    const auto sourceScreens = package.value("screens").toObject();
    if (!sourceScreens.isEmpty()) {
        QJsonArray screens;
        for (auto it = sourceScreens.constBegin(); it != sourceScreens.constEnd(); ++it)
            screens.append(QJsonObject{{"id", it.key()}, {"name", it.value()}, {"items", QJsonArray{}}});
        validation.insert("screens", screens);
    }
    if (const auto problem = Project::validate(validation); !problem.isEmpty()) {
        error = QString("Не удалось прочитать тип фейсплейса. %1").arg(problem);
        return {};
    }
    return package;
}

bool hasFaceplateName(const QJsonArray &types, const QString &name)
{
    const auto folded = name.trimmed().toCaseFolded();
    return std::any_of(types.constBegin(), types.constEnd(), [&folded](const QJsonValue &type) {
        return type.toObject().value("name").toString().trimmed().toCaseFolded() == folded;
    });
}

QString suggestedFaceplateImportName(const QJsonObject &source, const QJsonArray &types)
{
    const auto name = source.value("name").toString().trimmed();
    return hasFaceplateName(types, name) ? availableFaceplateCopyName(source, types) : name;
}

QVariantList replacementFields(const QJsonObject &project, const QJsonObject &previous, const QJsonObject &incoming)
{
    const auto typeId = previous.value("id");
    FaceplateFieldUse used;
    for (const auto *key : {"screens", "templates"}) {
        for (const auto &layout : project.value(key).toArray()) {
            for (const auto &entry : layout.toObject().value("items").toArray()) {
                const auto item = entry.toObject();
                if (item.value("faceplate") == typeId)
                    used.include(item);
            }
        }
    }
    return faceplateFieldRows(previous, incoming, used);
}

QVariantList faceplateFieldRows(const QJsonObject &previous, const QJsonObject &incoming, const FaceplateFieldUse &used)
{
    QVariantList rows;
    const bool related = faceplateOrigin(previous) == faceplateOrigin(incoming);
    const auto appendRows = [&](const char *array, const char *kindKey, const QSet<QString> &used) {
        const auto choices = incoming.value(array).toArray();
        for (const auto &entry : previous.value(array).toArray()) {
            const auto record = entry.toObject();
            const auto id = record.value("id").toString();
            if (!used.contains(id))
                continue;
            const auto key = faceplateFieldKey(record, related);
            const auto kind = record.value(kindKey);
            QVariantList compatible;
            QString automatic;
            for (const auto &candidate : choices) {
                const auto field = candidate.toObject();
                if (field.value(kindKey) != kind)
                    continue;
                const auto candidateId = field.value("id").toString();
                compatible.append(QVariantMap{{"id", candidateId}, {"name", field.value("name").toString()}});
                if (faceplateFieldKey(field, related) == key)
                    automatic = candidateId;
            }
            rows.append(QVariantMap{{"id", id}, {"name", record.value("name").toString()}, {"kind", kind.toString()},
                                    {"choices", compatible}, {"automatic", automatic}});
        }
    };
    appendRows("ports", "type", used.tags);
    appendRows("properties", "kind", used.properties);
    return rows;
}

QString buildFaceplateMapping(const QVariantList &rows, const QVariantMap &requested, QHash<QString, QString> &mapping)
{
    QSet<QString> selected;
    for (const auto &entry : rows) {
        const auto row = entry.toMap();
        const auto id = row.value("id").toString();
        const auto name = row.value("name").toString();
        const auto choiceId = requested.value(id, row.value("automatic")).toString();
        const auto choices = row.value("choices").toList();
        const bool compatible = std::any_of(choices.cbegin(), choices.cend(), [&choiceId](const QVariant &choice) {
            return choice.toMap().value("id").toString() == choiceId;
        });
        if (!compatible)
            return QString("Выберите совместимое поле для «%1».").arg(name);
        if (selected.contains(choiceId))
            return QString("Поле для «%1» уже назначено. Выберите разные поля назначения.").arg(name);
        selected.insert(choiceId);
        mapping.insert(id, choiceId);
    }
    for (auto it = requested.cbegin(); it != requested.cend(); ++it) {
        if (!mapping.contains(it.key()))
            return "Список используемых полей изменился. Откройте диалог заново.";
    }
    return {};
}

QHash<QString, QString> faceplateFieldOrigins(const QJsonObject &type)
{
    QHash<QString, QString> origins;
    for (const auto *group : {"ports", "properties"}) {
        for (const auto &entry : type.value(group).toArray()) {
            const auto field = entry.toObject();
            origins.insert(field.value("id").toString(), faceplateOrigin(field));
        }
    }
    return origins;
}

QVariantList importReplacementFields(const QJsonObject &project, const QJsonObject &previous, const QJsonObject &incoming)
{
    auto rows = replacementFields(project, previous, incoming);
    const auto remembered = project.value("faceplateImportMappings").toObject()
        .value(previous.value("id").toString()).toObject().value(faceplateOrigin(incoming)).toObject();
    if (remembered.isEmpty())
        return rows;

    const auto origins = faceplateFieldOrigins(incoming);
    for (auto &entry : rows) {
        auto row = entry.toMap();
        const auto origin = remembered.value(row.value("id").toString()).toString();
        if (origin.isEmpty())
            continue;
        const auto choices = row.value("choices").toList();
        const auto match = std::find_if(choices.cbegin(), choices.cend(), [&](const QVariant &choice) {
            return origins.value(choice.toMap().value("id").toString()) == origin;
        });
        row["automatic"] = match == choices.cend() ? QString{} : match->toMap().value("id").toString();
        entry = row;
    }
    return rows;
}

void rememberFaceplateImport(QJsonObject &project, const QJsonObject &previous, const QJsonObject &incoming,
                             const QHash<QString, QString> &assignments, const QHash<QString, QString> &copiedFields)
{
    const auto typeId = previous.value("id").toString();
    auto presets = project.value("faceplateImportMappings").toObject();
    const auto previousPresets = presets.value(typeId).toObject();
    QJsonObject carried;
    for (auto source = previousPresets.constBegin(); source != previousPresets.constEnd(); ++source) {
        const auto remembered = source.value().toObject();
        QJsonObject fields;
        for (auto field = remembered.constBegin(); field != remembered.constEnd(); ++field) {
            const auto newField = copiedFields.value(assignments.value(field.key()));
            if (!newField.isEmpty())
                fields.insert(newField, field.value());
        }
        if (!fields.isEmpty())
            carried.insert(source.key(), fields);
    }

    const auto oldOrigin = faceplateOrigin(previous);
    const auto incomingOrigin = faceplateOrigin(incoming);
    const auto oldFields = faceplateFieldOrigins(previous);
    const auto incomingFields = faceplateFieldOrigins(incoming);
    auto oldChoices = carried.value(oldOrigin).toObject();
    QJsonObject incomingChoices;
    for (auto field = assignments.cbegin(); field != assignments.cend(); ++field) {
        const auto localId = copiedFields.value(field.value());
        oldChoices.insert(localId, oldFields.value(field.key()));
        incomingChoices.insert(localId, incomingFields.value(field.value()));
    }
    if (oldOrigin != incomingOrigin && !oldChoices.isEmpty())
        carried.insert(oldOrigin, oldChoices);
    if (!incomingChoices.isEmpty())
        carried.insert(incomingOrigin, incomingChoices);
    if (carried.isEmpty())
        presets.remove(typeId);
    else
        presets.insert(typeId, carried);
    if (presets.isEmpty())
        project.remove("faceplateImportMappings");
    else
        project.insert("faceplateImportMappings", presets);
}
}

QString Studio::duplicateFaceplate(const QString &id)
{
    return duplicateFaceplate(id, suggestFaceplateCopyName(id));
}

QString Studio::duplicateFaceplate(const QString &id, const QString &name)
{
    const auto type = Project::find(project_, "faceplates", id);
    if (type.isEmpty()) {
        fail("Тип фейсплейса не найден.");
        return {};
    }
    const auto copyName = name.trimmed();
    if (copyName.isEmpty()) {
        fail("Введите название копии типа.");
        return {};
    }
    auto next = project_;
    auto types = next.value("faceplates").toArray();
    const auto copy = makeFaceplateCopy(type, copyName);
    types.append(copy);
    next.insert("faceplates", types);
    return commit(next) ? copy.value("id").toString() : QString{};
}

QString Studio::suggestFaceplateCopyName(const QString &typeId) const
{
    const auto type = Project::find(project_, "faceplates", typeId);
    return type.isEmpty() ? QString{} : availableFaceplateCopyName(type, project_.value("faceplates").toArray());
}

bool Studio::duplicateFaceplateForInstance(const QString &instanceId, const QString &name)
{
    const auto copyName = name.trimmed();
    if (copyName.isEmpty())
        return fail("Введите название копии типа.");
    auto canvas = activeCanvas();
    const auto instance = Project::find(canvas, "items", instanceId);
    if (instance.value("kind") != "faceplate")
        return fail("Выберите экземпляр фейсплейса на экране.");
    const auto type = Project::find(project_, "faceplates", instance.value("faceplate").toString());
    if (type.isEmpty())
        return fail("Тип фейсплейса не найден.");

    auto next = project_;
    auto types = next.value("faceplates").toArray();
    const auto copy = makeFaceplateCopy(type, copyName);
    types.append(copy);
    next.insert("faceplates", types);
    const auto change = prepareFaceplateTypeChange(next, canvas, instanceId, copy.value("id").toString());
    if (!change.error.isEmpty())
        return fail(change.error);

    auto items = canvas.value("items").toArray();
    if (!replace(items, change.item))
        return false;
    canvas.insert("items", items);
    auto screens = next.value("screens").toArray();
    if (!replace(screens, canvas))
        return false;
    next.insert("screens", screens);
    if (!commit(next))
        return false;
    enterComposite(instanceId);
    emit notice(QString("Создан тип «%1». На него переключён выбранный фейсплейс.")
        .arg(copyName), false);
    return true;
}

bool Studio::exportFaceplate(const QString &id, const QUrl &url)
{
    if (!url.isLocalFile())
        return fail("Выберите локальный файл фейсплейса.");
    const auto type = Project::find(project_, "faceplates", id);
    if (type.isEmpty())
        return fail("Тип фейсплейса не найден.");

    QJsonObject images;
    QJsonObject screens;
    for (const auto &entry : type.value("items").toArray()) {
        const auto item = entry.toObject();
        if (item.value("kind") == "link") {
            const auto target = item.value("screen").toString();
            if (!target.isEmpty())
                screens.insert(target, Project::find(project_, "screens", target).value("name"));
        }
        const auto relative = item.value("image").toString();
        if (item.value("kind") != "image" || relative.isEmpty() || images.contains(relative))
            continue;
        QFile image(Project::isDemoImage(relative) ? relative : imageUrl(relative).toLocalFile());
        if (!image.open(QIODevice::ReadOnly))
            return fail(QString("Не удалось прочитать изображение «%1».").arg(item.value("title").toString()));
        const auto content = image.readAll();
        if (content.isEmpty() || image.error() != QFileDevice::NoError)
            return fail(QString("Изображение «%1» пустое или повреждено.").arg(item.value("title").toString()));
        images.insert(relative, QString::fromLatin1(content.toBase64()));
    }
    const QJsonObject package{{"format", "scada-faceplate"}, {"version", 1}, {"faceplate", type},
                              {"images", images}, {"screens", screens}};
    auto path = url.toLocalFile();
    if (!path.endsWith(".faceplate.json", Qt::CaseInsensitive))
        path += ".faceplate.json";
    QString error;
    if (!writeFaceplateData(path, QJsonDocument(package).toJson(QJsonDocument::Indented), error))
        return fail(QString("Не удалось сохранить тип фейсплейса. %1").arg(error));
    emit notice(QString("Тип «%1» сохранён в файл.").arg(type.value("name").toString()), false);
    return true;
}

QVariantMap Studio::faceplateImportOptions(const QUrl &url)
{
    QString error;
    const auto package = readFaceplatePackage(url, error);
    if (package.isEmpty()) {
        fail(error);
        return {};
    }
    const auto source = package.value("faceplate").toObject();
    const QJsonObject definition{{"id", source.value("id")}, {"originId", faceplateOrigin(source)},
                                 {"ports", source.value("ports").toArray()}, {"properties", source.value("properties").toArray()}};
    return {{"name", source.value("name").toString()},
            {"definition", definition.toVariantMap()},
            {"suggestedName", suggestedFaceplateImportName(source, project_.value("faceplates").toArray())}};
}

QVariantList Studio::faceplateReplacementFields(const QString &typeId, const QVariantMap &incoming) const
{
    const auto type = Project::find(project_, "faceplates", typeId);
    return type.isEmpty() || incoming.isEmpty() ? QVariantList{} :
        importReplacementFields(project_, type, QJsonObject::fromVariantMap(incoming));
}

QString Studio::importFaceplate(const QUrl &url, const QString &requestedName, const QString &replaceId,
                               const QVariantMap &mapping)
{
    const auto reject = [this](const QString &error) -> QString {
        fail(error);
        return {};
    };
    QString readError;
    const auto package = readFaceplatePackage(url, readError);
    if (package.isEmpty())
        return reject(readError);
    const auto source = package.value("faceplate").toObject();
    const auto sourceScreens = package.value("screens").toObject();

    auto next = project_;
    auto types = next.value("faceplates").toArray();
    const bool replacing = !replaceId.isEmpty();
    const auto previous = replacing ? Project::find(project_, "faceplates", replaceId) : QJsonObject{};
    if (replacing && previous.isEmpty())
        return reject("Тип для замены не найден.");
    auto name = requestedName.trimmed();
    if (replacing) {
        name = previous.value("name").toString();
    } else if (requestedName.isNull()) {
        name = suggestedFaceplateImportName(source, types);
    } else if (name.isEmpty() || hasFaceplateName(types, name)) {
        return reject("Введите свободное имя для нового типа фейсплейса.");
    }
    QHash<QString, QString> copiedFields;
    auto imported = makeFaceplateCopy(source, name, replacing ? &copiedFields : nullptr);
    if (replacing) {
        imported.insert("id", replaceId);
        QHash<QString, QString> mapped;
        if (const auto error = buildFaceplateMapping(importReplacementFields(project_, previous, source),
                                                     mapping, mapped); !error.isEmpty())
            return reject(error);
        rememberFaceplateImport(next, previous, source, mapped, copiedFields);
        for (auto it = mapped.begin(); it != mapped.end(); ++it)
            it.value() = copiedFields.value(it.value());
        QString conflict;
        const auto update = [&](QJsonObject &item) {
            if (!conflict.isEmpty() || item.value("faceplate").toString() != replaceId)
                return;
            const auto change = remapFaceplateInstance(item, previous, imported, &mapped);
            if (change.error.isEmpty())
                item = change.item;
            else
                conflict = QString("Фейсплейс «%1». %2").arg(item.value("title").toString(), change.error);
        };
        editLayoutItems(next, "screens", update);
        editLayoutItems(next, "templates", update);
        if (!conflict.isEmpty())
            return reject(conflict);
    }

    QSet<QString> screenIds;
    QHash<QString, QString> screenByName;
    for (const auto &entry : next.value("screens").toArray()) {
        const auto screen = entry.toObject();
        const auto id = screen.value("id").toString();
        screenIds.insert(id);
        screenByName.insert(screen.value("name").toString().trimmed().toCaseFolded(), id);
    }
    const auto images = package.value("images").toObject();
    QHash<QString, QString> relocatedImages;
    QHash<QString, QByteArray> imageData;
    qsizetype unresolvedLinks = 0;
    auto items = imported.value("items").toArray();
    for (qsizetype i = 0; i < items.size(); ++i) {
        auto item = items.at(i).toObject();
        const auto kind = item.value("kind");
        const auto link = item.value("screen").toString();
        if (kind == "link" && !link.isEmpty() && !screenIds.contains(link)) {
            const auto label = sourceScreens.value(link).toString().trimmed().toCaseFolded();
            const auto target = screenByName.value(label);
            item.insert("screen", target);
            if (target.isEmpty())
                ++unresolvedLinks;
        }
        const auto image = item.value("image").toString();
        if (kind == "image" && !image.isEmpty()) {
            auto relative = relocatedImages.value(image);
            if (relative.isEmpty()) {
                auto decoded = QByteArray::fromBase64Encoding(images.value(image).toString().toLatin1(),
                                                              QByteArray::AbortOnBase64DecodingErrors);
                if (!decoded || decoded.decoded.isEmpty())
                    return reject(QString("В файле отсутствует или повреждено изображение «%1».")
                        .arg(item.value("title").toString()));
                relative = "images/" + Project::id() + "." + QFileInfo(image).suffix();
                relocatedImages.insert(image, relative);
                imageData.insert(relative, std::move(decoded.decoded));
            }
            item.insert("image", relative);
        }
        items.replace(i, item);
    }
    imported.insert("items", items);
    if (replacing)
        replace(types, imported);
    else
        types.append(imported);
    next.insert("faceplates", types);

    QStringList written;
    if (!imageData.isEmpty()) {
        const auto directory = imageDirectory();
        if (directory.isEmpty())
            return reject("Не удалось подготовить временную папку для изображений.");
        const QDir dir(directory);
        if (!dir.mkpath("images"))
            return reject("Не удалось создать папку изображений.");
        for (auto it = imageData.cbegin(); it != imageData.cend(); ++it) {
            const auto destination = dir.filePath(it.key());
            if (QFile::exists(destination)) {
                removeFiles(written);
                return reject("В папке проекта уже есть изображение с таким именем.");
            }
            QString error;
            if (!writeFaceplateData(destination, it.value(), error)) {
                removeFiles(written);
                return reject(QString("Не удалось сохранить изображение фейсплейса. %1").arg(error));
            }
            written.append(destination);
        }
    }
    if (!commit(next)) {
        removeFiles(written);
        return {};
    }
    auto noticeText = replacing ? QString("Тип «%1» заменён. Настройки экземпляров сохранены.").arg(name)
                                : QString("Тип «%1» импортирован.").arg(name);
    if (unresolvedLinks)
        noticeText += QString(" Переходов без подходящего экрана: %1. Укажите экран в их свойствах.").arg(unresolvedLinks);
    emit notice(noticeText, false);
    return imported.value("id").toString();
}

QVariantMap Studio::previewFaceplateTypeChange(const QString &instanceId, const QString &typeId) const
{
    QVariantList fields;
    const auto change = prepareFaceplateTypeChange(project_, activeCanvas(), instanceId, typeId, {}, &fields);
    return {{"allowed", change.error.isEmpty()}, {"error", change.error}, {"related", change.related},
            {"tags", change.tags}, {"properties", change.properties}, {"fields", fields}};
}

bool Studio::changeFaceplateType(const QString &instanceId, const QString &typeId, const QVariantMap &mapping)
{
    const auto canvas = activeCanvas();
    const auto change = prepareFaceplateTypeChange(project_, canvas, instanceId, typeId, mapping);
    if (!change.error.isEmpty())
        return fail(change.error);
    auto items = canvas.value("items").toArray();
    if (!replace(items, change.item))
        return false;
    auto next = project_;
    const auto sourceId = Project::find(canvas, "items", instanceId).value("faceplate").toString();
    rememberFaceplateFields(next, sourceId, typeId, mapping);
    return setItems(items, std::move(next));
}

void Studio::insertFaceplate(const QString &id)
{
    if (compositeDepth()) {
        fail("Составные виджеты вставляются на экран.");
        return;
    }
    const auto type = Project::find(project_, "faceplates", id);
    if (type.isEmpty())
        return;
    const auto screen = activeScreen();
    const auto canvas = Project::screenSize(screen);
    const auto design = Project::screenSize(type);
    const double scale = std::min({1.0, double(canvas.width()) / design.width(),
                                  double(canvas.height()) / design.height()});
    auto instance = faceplateInstance(type);
    place(instance, canvas, 40, 40, design.width() * scale, design.height() * scale);
    auto entries = screen.value("items").toArray();
    entries.append(instance);
    if (setItems(entries))
        selectWidget(instance.value("id").toString());
}

bool Studio::removeFaceplate(const QString &id)
{
    auto next = project_;
    auto types = next.value("faceplates").toArray();
    if (!erase(types, id))
        return fail("Тип фейсплейса не найден.");
    for (const auto *key : {"screens", "templates"}) {
        const bool screen = QLatin1StringView(key) == "screens";
        for (const auto &entry : project_.value(key).toArray()) {
            const auto layout = entry.toObject();
            for (const auto &item : layout.value("items").toArray()) {
                if (item.toObject().value("faceplate").toString() == id) {
                    const auto name = layout.value("name").toString();
                    return fail(screen ? QString("Тип используется на экране «%1». Сначала удалите его экземпляры.").arg(name)
                                       : QString("Тип используется в шаблоне «%1». Сначала удалите этот шаблон.").arg(name));
                }
            }
        }
    }
    next.insert("faceplates", types);
    forgetFaceplateFields(next, id);
    return commit(next);
}

bool Studio::enterComposite(const QString &id)
{
    auto type = Project::find(project_, "faceplates", id);
    QString instanceId;
    if (type.isEmpty()) {
        const auto instance = Project::find(activeScreen(), "items", id);
        if (instance.value("kind").toString() != "faceplate")
            return false;
        type = Project::find(project_, "faceplates", instance.value("faceplate").toString());
        instanceId = id;
    }
    if (type.isEmpty())
        return false;
    const auto typeId = type.value("id").toString();
    if (typeId == faceplateId_)
        return true;
    faceplateId_ = typeId;
    parentSelection_ = instanceId;
    selectedId_.clear();
    selectedIds_.clear();
    emit selectionChanged();
    emit canvasChanged();
    return true;
}

void Studio::leaveComposite()
{
    if (!compositeDepth())
        return;
    faceplateId_.clear();
    selectedId_.clear();
    selectedIds_.clear();
    selectWidget(std::exchange(parentSelection_, {}));
    emit canvasChanged();
}

void Studio::closeComposite()
{
    leaveComposite();
}

bool Studio::setFaceplatePorts(const QVariantList &ports)
{
    auto type = activeFaceplate();
    if (type.isEmpty())
        return fail("Откройте тип составного виджета.");
    QHash<QString, QJsonObject> removed;
    for (const auto &entry : type.value("ports").toArray()) {
        const auto port = entry.toObject();
        removed.insert(port.value("id").toString(), port);
    }
    QJsonArray updated;
    for (const auto &entry : ports) {
        const auto row = entry.toMap();
        auto id = row.value("id").toString();
        QJsonObject previous;
        if (id.isEmpty()) {
            id = Project::id();
        } else {
            previous = removed.take(id);
            if (previous.isEmpty())
                return fail("Порт не найден или указан повторно.");
        }
        QJsonObject port{{"id", id}, {"name", row.value("name").toString().trimmed()},
                         {"type", row.value("type").toString()}};
        if (previous.contains("originId"))
            port.insert("originId", previous.value("originId"));
        updated.append(port);
    }
    type.insert("ports", updated);
    auto next = project_;
    if (!removed.isEmpty()) {
        auto children = type.value("items").toArray();
        for (qsizetype i = 0; i < children.size(); ++i) {
            auto item = children[i].toObject();
            remapTagReferences(item, [&removed](const QString &id) {
                return removed.contains(id) ? QString{} : id;
            });
            children.replace(i, item);
        }
        type.insert("items", children);
        const auto clearBindings = [this, &removed](QJsonObject &item) {
            if (item.value("faceplate").toString() != faceplateId_)
                return;
            auto bindings = item.value("bindings").toObject();
            for (auto it = removed.cbegin(); it != removed.cend(); ++it)
                bindings.remove(it.key());
            item.insert("bindings", bindings);
        };
        editLayoutItems(next, "screens", clearBindings);
        editLayoutItems(next, "templates", clearBindings);
    }
    auto types = next.value("faceplates").toArray();
    replace(types, type);
    next.insert("faceplates", types);
    return commit(next);
}

bool Studio::setFaceplateProperties(const QVariantList &properties)
{
    auto type = activeFaceplate();
    if (type.isEmpty())
        return fail("Откройте тип составного виджета.");

    QHash<QString, QJsonObject> previousProperties;
    for (const auto &entry : type.value("properties").toArray()) {
        const auto property = entry.toObject();
        previousProperties.insert(property.value("id").toString(), property);
    }
    QSet<QString> removed;
    QJsonArray updated;
    for (const auto &entry : properties) {
        const auto row = QJsonObject::fromVariantMap(entry.toMap());
        auto id = row.value("id").toString();
        const auto kind = row.value("kind");
        QJsonObject previous;
        if (!row.value("id").isUndefined() && !row.value("id").isString())
            return fail("Неверный идентификатор свойства интерфейса.");
        if (id.isEmpty()) {
            id = Project::id();
        } else {
            previous = previousProperties.take(id);
            if (previous.isEmpty())
                return fail("Свойство интерфейса не найдено или указано повторно.");
            if (previous.value("kind") != kind)
                removed.insert(id);
        }
        const auto name = row.value("name");
        QJsonObject property{{"id", id}, {"name", name.isString() ? QJsonValue(name.toString().trimmed()) : name},
                             {"item", row.value("item")}, {"kind", kind}};
        if (previous.contains("originId"))
            property.insert("originId", previous.value("originId"));
        if (row.contains("defaultVisible"))
            property.insert("defaultVisible", row.value("defaultVisible"));
        updated.append(property);
    }
    for (auto it = previousProperties.cbegin(); it != previousProperties.cend(); ++it)
        removed.insert(it.key());

    auto next = project_;
    type.insert("properties", updated);
    auto types = next.value("faceplates").toArray();
    replace(types, type);
    next.insert("faceplates", types);
    clearFaceplateProperties(next, type.value("id"), removed);
    return commit(next);
}

bool Studio::setFaceplateSize(int width, int height)
{
    auto type = activeFaceplate();
    if (type.isEmpty())
        return fail("Откройте тип составного виджета.");
    if (width < 80 || width > 8192 || height < 52 || height > 8192)
        return fail("Ширина типа должна быть от 80 до 8192 px, высота — от 52 до 8192 px.");
    const auto area = bounds(type.value("items").toArray());
    if (area.right > width || area.bottom > height)
        return fail("Переместите элементы внутрь нового размера типа.");
    type.insert("width", width);
    type.insert("height", height);
    auto next = project_;
    auto types = next.value("faceplates").toArray();
    replace(types, type);
    next.insert("faceplates", types);
    return commit(next);
}

QVariantList Studio::resolveFaceplate(const QVariantMap &instance) const
{
    const auto type = Project::find(project_, "faceplates", instance.value("faceplate").toString());
    const auto bindings = instance.value("bindings").toMap();
    const auto overrides = QJsonObject::fromVariantMap(instance.value("overrides").toMap());
    const auto propertyBindings = QJsonObject::fromVariantMap(instance.value("propertyBindings").toMap());
    QHash<QString, QList<QJsonObject>> propertiesByItem;
    for (const auto &entry : type.value("properties").toArray()) {
        const auto property = entry.toObject();
        propertiesByItem[property.value("item").toString()].append(property);
    }
    const auto instanceId = instance.value("id").toString();
    QVariantList resolved;
    for (const auto &entry : type.value("items").toArray()) {
        auto item = entry.toObject();
        const auto childId = item.value("id").toString();
        if (const auto properties = propertiesByItem.constFind(childId); properties != propertiesByItem.cend()) {
            QJsonObject childBindings;
            for (const auto &property : *properties) {
                const auto kind = property.value("kind").toString();
                const auto propertyId = property.value("id").toString();
                const auto value = overrides.value(propertyId);
                const auto binding = propertyBindings.value(propertyId);
                if (!binding.isUndefined())
                    childBindings.insert(kind, binding);
                if (kind == "visible") {
                    item.insert("visible", value.toBool(property.value("defaultVisible").toBool(true)));
                } else if (!value.isUndefined()) {
                    if (kind == "title" || kind == "captionColor") {
                        item.insert(kind, value);
                    } else if (kind == "background" || kind == "value") {
                        auto appearance = item.value("appearance").toObject();
                        appearance.insert(kind, value);
                        item.insert("appearance", appearance);
                    }
                }
            }
            if (!childBindings.isEmpty())
                item.insert("propertyBindings", childBindings);
        }
        const auto tagPort = item.value("tag").toString();
        if (!tagPort.isEmpty() && bindings.value(tagPort).toString().isEmpty())
            item.insert("tagUnbound", true);
        item.insert("id", instanceId + "/" + childId);
        const auto feedbackPort = item.value("feedback").toString();
        const auto appearancePort = item.value("appearance").toObject().value("tag").toString();
        remapTagReferences(item, [&bindings](const QString &portId) { return bindings.value(portId).toString(); });
        if (!feedbackPort.isEmpty() && item.value("feedback").toString().isEmpty())
            item.insert("feedbackUnbound", true);
        if (!appearancePort.isEmpty()) {
            auto appearance = item.value("appearance").toObject();
            if (appearance.value("tag").toString().isEmpty()) {
                appearance.remove("rules");
                item.insert("appearance", appearance);
            }
        }
        resolved.append(item.toVariantMap());
    }
    return resolved;
}

QVariantMap Studio::faceplatePropertyValues(const QVariantMap &bindings, const QVariantMap &readings,
                                           const QVariantList &alarms) const
{
    return Project::faceplatePropertyValues(bindings, readings, alarms);
}

bool Studio::saveTemplate(const QString &name)
{
    QJsonArray entries;
    for (const auto &entry : selectedItems()) {
        auto item = entry.toObject();
        item.insert("id", Project::id());
        if (item.value("kind").toString() == "faceplate") {
            item.insert("bindings", QJsonObject{});
            clearFaceplatePropertyBindings(item);
        } else {
            item.insert("tag", "");
        }
        item.remove("feedback");
        clearTrendBinding(item);
        clearVisualBindings(item);
        clearActionBindings(item);
        item.remove("screen");
        item.remove("group");
        entries.append(item);
    }
    if (entries.isEmpty())
        return fail("Выберите элементы для шаблона.");
    auto next = project_;
    auto templates = next.value("templates").toArray();
    const auto canvas = Project::screenSize(activeCanvas());
    templates.append(QJsonObject{{"id", Project::id()}, {"name", name.trimmed()}, {"items", entries},
                                 {"width", std::max(240, canvas.width())}, {"height", std::max(160, canvas.height())}});
    next.insert("templates", templates);
    return commit(next);
}

void Studio::insertTemplate(const QString &id)
{
    const auto entries = Project::find(project_, "templates", id).value("items").toArray();
    if (entries.isEmpty())
        return;
    const auto area = bounds(entries);
    insertCopies(entries, 40 - area.left, 40 - area.top);
}

void Studio::removeTemplate(const QString &id)
{
    auto next = project_;
    auto entries = next.value("templates").toArray();
    if (erase(entries, id)) {
        next.insert("templates", entries);
        commit(next);
    }
}

void Studio::layerWidget(bool front)
{
    const auto screen = activeCanvas();
    const auto item = Project::find(screen, "items", selectedId_);
    auto entries = screen.value("items").toArray();
    if (!erase(entries, selectedId_))
        return;
    if (front) entries.append(item);
    else entries.prepend(item);
    setItems(entries);
}

void Studio::moveLayer(int direction)
{
    auto entries = activeCanvas().value("items").toArray();
    for (qsizetype index = 0; index < entries.size(); ++index) {
        const auto entry = entries.at(index);
        if (entry.toObject().value("id").toString() != selectedId_)
            continue;
        const auto adjacent = index + direction;
        if ((direction == -1 || direction == 1) && adjacent >= 0 && adjacent < entries.size()) {
            entries.removeAt(index);
            entries.insert(adjacent, entry);
            setItems(entries);
        }
        return;
    }
}

void Studio::setImage(const QUrl &url)
{
    if (widget().value("kind").toString() != "image")
        return;
    const auto relative = importImage(url);
    if (!relative.isEmpty() && !configureWidget({{"image", relative}}))
        QFile::remove(imageUrl(relative).toLocalFile());
}

QString Studio::imageDirectory()
{
    if (!path_.isEmpty())
        return QFileInfo(path_).absolutePath();
    if (!stagedFaceplateImages_) {
        auto staging = std::make_unique<QTemporaryDir>();
        if (!staging->isValid())
            return {};
        stagedFaceplateImages_ = std::move(staging);
    }
    return stagedFaceplateImages_->path();
}

QString Studio::importImage(const QUrl &url)
{
    const QFileInfo file(url.toLocalFile());
    const auto ext = file.suffix().toLower();
    const QSet<QString> supported{"png", "jpg", "jpeg", "bmp", "svg"};
    if (!url.isLocalFile() || !file.isFile() || !supported.contains(ext)) {
        fail("Выберите PNG, JPEG, BMP или SVG.");
        return {};
    }
    const auto directory = imageDirectory();
    if (directory.isEmpty()) {
        fail("Не удалось подготовить временную папку для изображений.");
        return {};
    }
    const QDir dir(directory);
    const auto relative = "images/" + Project::id() + "." + ext;
    if (!dir.mkpath("images") || !QFile::copy(file.absoluteFilePath(), dir.filePath(relative))) {
        fail("Не удалось скопировать изображение в проект.");
        return {};
    }
    return relative;
}

QUrl Studio::imageUrl(const QString &relative) const
{
    if (Project::isDemoImage(relative))
        return QUrl("qrc" + relative);
    if (relative.isEmpty())
        return {};
    if (path_.isEmpty())
        return stagedFaceplateImages_ ? QUrl::fromLocalFile(QDir(stagedFaceplateImages_->path()).filePath(relative)) : QUrl{};
    return QUrl::fromLocalFile(QFileInfo(path_).absoluteDir().filePath(relative));
}

QVariantList Studio::series(const QString &tag, qint64 from, qint64 to, int buckets)
{
    return runtime_.series(tag, from, to, buckets);
}

QVariantMap Studio::archiveBounds(const QStringList &tags) { return runtime_.archiveBounds(tags); }
QVariantMap Studio::archiveSamples(const QStringList &tags, qint64 time) { return runtime_.archiveSamples(tags, time); }

QVariantList Studio::events() { return runtime_.events(); }

void Studio::acknowledge(const QString &tag)
{
    if (const auto error = runtime_.acknowledge(tag); !error.isEmpty())
        fail(error);
}

void Studio::write(const QString &tag, double value)
{
    if (const auto error = runtime_.requestWrite(tag, value); !error.isEmpty())
        fail(error);
}

void Studio::readTag(const QString &tag)
{
    if (const auto error = runtime_.requestRead(tag); !error.isEmpty())
        fail(error);
}

void Studio::runWidgetActions(const QString &widgetId, const QVariantList &rows)
{
    runtime_.runWidgetActions(widgetId, rows);
}

void Studio::exportCsv(const QUrl &url, const QString &tag, qint64 from, qint64 to)
{
    if (!url.isLocalFile()) {
        fail("Выберите локальный файл для экспорта.");
        return;
    }
    auto path = url.toLocalFile();
    if (!path.endsWith(".csv", Qt::CaseInsensitive))
        path += ".csv";
    const auto error = runtime_.exportCsv(path, tag, from, to);
    if (error.isEmpty())
        emit notice("Архив экспортирован.", false);
    else
        fail(error);
}
