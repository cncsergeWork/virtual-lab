#pragma once

#include "runtime.h"
#include <QUrl>
#include <memory>

class DemoServer;
class QTemporaryDir;

class Studio final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name NOTIFY projectChanged)
    Q_PROPERTY(QString projectId READ projectId NOTIFY projectChanged)
    Q_PROPERTY(QString path READ path NOTIFY projectChanged)
    Q_PROPERTY(bool modified READ modified NOTIFY projectChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY projectChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY projectChanged)
    Q_PROPERTY(QVariantList sources READ sources NOTIFY projectChanged)
    Q_PROPERTY(QVariantList tags READ tags NOTIFY projectChanged)
    Q_PROPERTY(QVariantList screens READ screens NOTIFY projectChanged)
    Q_PROPERTY(QVariantList templates READ templates NOTIFY projectChanged)
    Q_PROPERTY(QVariantList faceplates READ faceplates NOTIFY projectChanged)
    Q_PROPERTY(QVariantList items READ items NOTIFY canvasChanged)
    Q_PROPERTY(QVariantMap canvasSettings READ canvasSettings NOTIFY canvasChanged)
    Q_PROPERTY(int compositeDepth READ compositeDepth NOTIFY canvasChanged)
    Q_PROPERTY(QVariantList editorTags READ editorTags NOTIFY canvasChanged)
    Q_PROPERTY(QVariantMap widget READ widget NOTIFY selectionChanged)
    Q_PROPERTY(QString screenId READ screenId NOTIFY selectionChanged)
    Q_PROPERTY(QString selectedId READ selectedId NOTIFY selectionChanged)
    Q_PROPERTY(QStringList selectedIds READ selectedIds NOTIFY selectionChanged)
    Q_PROPERTY(bool running READ running NOTIFY stateChanged)
    Q_PROPERTY(bool simulator READ simulator NOTIFY projectChanged)
    Q_PROPERTY(bool simulatorPaused READ simulatorPaused NOTIFY stateChanged)
    Q_PROPERTY(QVariantMap readings READ readings NOTIFY liveChanged)
    Q_PROPERTY(QVariantMap statuses READ statuses NOTIFY liveChanged)
    Q_PROPERTY(QVariantList alarms READ alarms NOTIFY alarmsChanged)
public:
    explicit Studio(QObject *parent = nullptr);
    ~Studio() override;

    QString name() const { return project_.value("name").toString(); }
    QString projectId() const { return project_.value("id").toString(); }
    QString path() const { return path_; }
    bool modified() const { return project_ != saved_; }
    bool canUndo() const { return !undo_.isEmpty() && !running(); }
    bool canRedo() const { return !redo_.isEmpty() && !running(); }
    QVariantList sources() const;
    QVariantList tags() const;
    QVariantList screens() const;
    QVariantList templates() const;
    QVariantList faceplates() const;
    QVariantList items() const;
    QVariantMap canvasSettings() const;
    int compositeDepth() const { return faceplateId_.isEmpty() ? 0 : 1; }
    QVariantList editorTags() const;
    QVariantMap widget() const;
    QString screenId() const { return screenId_; }
    QString selectedId() const { return selectedId_; }
    QStringList selectedIds() const { return selectedIds_; }
    bool running() const { return runtime_.running(); }
    bool simulator() const;
    bool simulatorPaused() const { return simulatorPaused_; }
    QVariantMap readings() const { return runtime_.readings(); }
    QVariantMap statuses() const { return runtime_.statuses(); }
    QVariantList alarms() const { return runtime_.alarms(); }

    Q_INVOKABLE void newProject(bool demo);
    Q_INVOKABLE bool open(const QUrl &url);
    Q_INVOKABLE bool save(const QUrl &url = {});
    Q_INVOKABLE bool start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void toggleSimulator();
    Q_INVOKABLE void renameProject(const QString &name);
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE bool setSource(const QVariantMap &source);
    Q_INVOKABLE bool setTag(const QVariantMap &tag);
    Q_INVOKABLE bool removeSource(const QString &id);
    Q_INVOKABLE bool importEngineTags(const QString &sourceId, const QUrl &file = {});
    Q_INVOKABLE bool exportEngineTags(const QString &sourceId, const QUrl &file);
    Q_INVOKABLE bool removeTag(const QString &id);
    Q_INVOKABLE void selectScreen(const QString &id);
    Q_INVOKABLE void selectWidget(const QString &id, bool extend = false);
    Q_INVOKABLE void setScreen(const QString &id, const QString &name);
    Q_INVOKABLE bool setScreenProperties(const QString &id, const QVariantMap &properties, bool scaleItems,
                                        const QUrl &newBackground);
    Q_INVOKABLE void removeScreen();
    Q_INVOKABLE void addWidget(const QString &kind, double x, double y);
    Q_INVOKABLE void moveWidget(const QString &id, double x, double y, double w, double h);
    Q_INVOKABLE bool configureWidget(const QVariantMap &properties);
    Q_INVOKABLE QVariantMap widgetColors(const QVariantMap &appearance, const QVariantMap &reading) const;
    Q_INVOKABLE bool animationActive(const QVariantMap &animation, const QVariantMap &reading) const;
    Q_INVOKABLE void removeWidget();
    Q_INVOKABLE void duplicateWidget();
    Q_INVOKABLE void alignWidgets(bool vertical);
    Q_INVOKABLE void groupWidgets(bool group);
    Q_INVOKABLE bool createFaceplate(const QString &name, int width = 480, int height = 280);
    Q_INVOKABLE bool renameFaceplate(const QString &id, const QString &name);
    Q_INVOKABLE QString duplicateFaceplate(const QString &id);
    Q_INVOKABLE QString duplicateFaceplate(const QString &id, const QString &name);
    Q_INVOKABLE QString suggestFaceplateCopyName(const QString &typeId) const;
    Q_INVOKABLE bool duplicateFaceplateForInstance(const QString &instanceId, const QString &name);
    Q_INVOKABLE bool exportFaceplate(const QString &id, const QUrl &url);
    Q_INVOKABLE QVariantMap faceplateImportOptions(const QUrl &url);
    Q_INVOKABLE QVariantList faceplateReplacementFields(const QString &typeId, const QVariantMap &incoming) const;
    Q_INVOKABLE QString importFaceplate(const QUrl &url, const QString &name = {}, const QString &replaceId = {},
                                       const QVariantMap &mapping = {});
    Q_INVOKABLE QVariantMap previewFaceplateTypeChange(const QString &instanceId, const QString &typeId) const;
    Q_INVOKABLE bool changeFaceplateType(const QString &instanceId, const QString &typeId,
                                         const QVariantMap &mapping = {});
    Q_INVOKABLE void insertFaceplate(const QString &id);
    Q_INVOKABLE bool removeFaceplate(const QString &id);
    Q_INVOKABLE bool enterComposite(const QString &id);
    Q_INVOKABLE void leaveComposite();
    Q_INVOKABLE void closeComposite();
    Q_INVOKABLE bool setFaceplatePorts(const QVariantList &ports);
    Q_INVOKABLE bool setFaceplateProperties(const QVariantList &properties);
    Q_INVOKABLE bool setFaceplateSize(int width, int height);
    Q_INVOKABLE QVariantList resolveFaceplate(const QVariantMap &instance) const;
    Q_INVOKABLE QVariantMap faceplatePropertyValues(const QVariantMap &bindings, const QVariantMap &readings,
                                                   const QVariantList &alarms) const;
    Q_INVOKABLE bool saveTemplate(const QString &name);
    Q_INVOKABLE void insertTemplate(const QString &id);
    Q_INVOKABLE void removeTemplate(const QString &id);
    Q_INVOKABLE void layerWidget(bool front);
    Q_INVOKABLE void moveLayer(int direction);
    Q_INVOKABLE void setImage(const QUrl &url);
    Q_INVOKABLE QUrl imageUrl(const QString &relative) const;
    Q_INVOKABLE QVariantList series(const QString &tag, qint64 from, qint64 to, int buckets);
    Q_INVOKABLE QVariantMap archiveBounds(const QStringList &tags);
    Q_INVOKABLE QVariantMap archiveSamples(const QStringList &tags, qint64 time);
    Q_INVOKABLE QVariantList events();
    Q_INVOKABLE void acknowledge(const QString &tag);
    Q_INVOKABLE void write(const QString &tag, double value);
    Q_INVOKABLE void readTag(const QString &tag);
    Q_INVOKABLE void runWidgetActions(const QString &widgetId, const QVariantList &rows);
    Q_INVOKABLE void exportCsv(const QUrl &url, const QString &tag, qint64 from, qint64 to);

signals:
    void projectChanged();
    void selectionChanged();
    void canvasChanged();
    void stateChanged();
    void liveChanged();
    void alarmsChanged();
    void archiveChanged();
    void notice(const QString &message, bool error);

private:
    bool commit(const QJsonObject &next);
    bool acquisitionEdit();
    bool changeEntity(const QString &key, QJsonObject object);
    void replaceProject(const QJsonObject &object, const QString &path);
    void repairSelection();
    bool setItems(const QJsonArray &entries);
    bool setItems(const QJsonArray &entries, QJsonObject next);
    QJsonArray selectedItems() const;
    void insertCopies(const QJsonArray &items, double dx, double dy);
    QString importImage(const QUrl &url);
    QString imageDirectory();
    QJsonObject activeScreen() const;
    QJsonObject activeFaceplate() const;
    QJsonObject activeCanvas() const;
    QString archivePath(const QString &projectPath) const;
    bool fail(const QString &text);

    QJsonObject project_;
    QJsonObject saved_;
    QString path_;
    std::unique_ptr<QTemporaryDir> stagedFaceplateImages_;
    QString screenId_;
    QString faceplateId_;
    QString parentSelection_;
    QString selectedId_;
    QStringList selectedIds_;
    QList<QJsonObject> undo_;
    QList<QJsonObject> redo_;
    QList<DemoServer *> simulators_;
    Runtime runtime_;
    bool simulatorPaused_ = false;
};
