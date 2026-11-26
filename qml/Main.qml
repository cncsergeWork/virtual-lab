import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window

ApplicationWindow {
    id: window
    width: 1460
    height: 880
    minimumWidth: 990
    minimumHeight: 650
    visible: true
    title: api.name + (api.modified ? " *" : "") + " — " + Qt.application.displayName
    color: "#edf1f5"
    readonly property var api: studioApi
    property int page: 0
    property bool allowClose: false
    property var afterSave: null
    property string catalogSource: ""
    property bool operatorFullscreen: false
    property int pageBeforeFullscreen: 1
    property bool wasMaximized: false
    readonly property var pages: ["Редактор", "Оператор", "Источники", "Теги", "Архив", "События"]

    function saveProject(force, action) {
        if (!force && api.path) {
            if (api.save() && action) action()
        } else {
            afterSave = action || null
            savePicker.open()
        }
    }
    function guard(action) {
        if (!api.modified) { action(); return }
        unsaved.action = action
        unsaved.open()
    }
    function startAcquisition() {
        if (!api.path && !api.simulator) {
            saveProject(false, () => { if (api.start()) page = 1 })
        } else if (api.start()) {
            page = 1
        }
    }
    function sourceStatus(source) {
        const status = api.statuses[source] || {}
        return status.state === "connected" ? "Подключено" : status.state === "connecting" ? "Подключение" :
              status.state === "disconnected" ? "Нет связи" : "Остановлено"
    }
    function quality(tag) {
        const reading = api.readings[tag] || {}
        return reading.quality === "good" ? "Достоверно" : reading.quality === "stale" ?
            "Устарело" : reading.quality === "error" ? "Ошибка" : "Ожидание"
    }
    function toggleOperatorFullscreen() {
        if (operatorFullscreen) {
            operatorFullscreen = false
            page = pageBeforeFullscreen
            if (wasMaximized) showMaximized()
            else showNormal()
            return
        }
        pageBeforeFullscreen = page
        wasMaximized = visibility === Window.Maximized
        page = 1
        operatorFullscreen = true
        showFullScreen()
    }
    onClosing: event => {
        if (!allowClose && api.modified) {
            event.accepted = false
            guard(() => { allowClose = true; close() })
        }
    }
    Shortcut { sequences: [StandardKey.Save]; enabled: !window.operatorFullscreen; onActivated: window.saveProject(false, null) }
    Shortcut { sequences: [StandardKey.Open]; enabled: !window.operatorFullscreen; onActivated: window.guard(() => openPicker.open()) }
    Shortcut { sequences: [StandardKey.Undo]; enabled: window.page === 0 && window.api.canUndo; onActivated: window.api.undo() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: window.page === 0 && window.api.canRedo; onActivated: window.api.redo() }
    Shortcut { sequence: "F11"; onActivated: window.toggleOperatorFullscreen() }
    Shortcut { sequence: "Esc"; enabled: window.operatorFullscreen && !confirm.visible; onActivated: window.toggleOperatorFullscreen() }

    header: Rectangle {
        objectName: "mainToolbar"
        visible: !window.operatorFullscreen
        implicitHeight: window.operatorFullscreen ? 0 : 44
        color: "#13263b"
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12; anchors.rightMargin: 12
            spacing: 6
            Image {
                Layout.preferredWidth: 27; Layout.preferredHeight: 27
                source: "qrc:/assets/virtual-lab.svg"
                fillMode: Image.PreserveAspectFit
            }
            Label {
                visible: window.width >= 1240
                text: Qt.application.displayName; color: "#f2f7fb"; font.pixelSize: 15; font.weight: Font.DemiBold
                Layout.rightMargin: 5
            }
            ActionButton {
                objectName: "projectMenuButton"
                compact: true
                text: "Проект ▾"; fillTone: "#263c53"; textTone: "#f2f7fb"
                onClicked: projectMenu.open()
                Menu {
                    id: projectMenu
                    y: parent.height
                    MenuItem { objectName: "projectNew"; text: "Новый проект"; onTriggered: window.guard(() => { window.api.newProject(false); window.page = 2 }) }
                    MenuItem { objectName: "projectOpen"; text: "Открыть…"; onTriggered: window.guard(() => openPicker.open()) }
                    MenuItem { objectName: "projectSave"; text: "Сохранить"; onTriggered: window.saveProject(false, null) }
                    MenuItem { objectName: "projectSaveAs"; text: "Сохранить как…"; onTriggered: window.saveProject(true, null) }
                    MenuItem { objectName: "projectRename"; text: "Переименовать проект…"; onTriggered: nameDialog.edit("project", "", window.api.name) }
                    MenuSeparator {}
                    MenuItem { objectName: "projectDemo"; text: "Открыть демопроект"; onTriggered: window.guard(() => { window.api.newProject(true); window.page = 0 }) }
                }
            }
            ActionButton {
                objectName: "saveButton"
                compact: true
                text: "Сохранить"
                onClicked: window.saveProject(false, null)
            }
            Rectangle {
                Layout.preferredWidth: 1; Layout.preferredHeight: 23
                Layout.leftMargin: 2; Layout.rightMargin: 2
                color: "#385068"
            }
            RowLayout {
                Layout.fillHeight: true
                spacing: 2
                Repeater {
                    model: window.pages
                    delegate: Button {
                        id: tab
                        required property int index
                        required property string modelData
                        objectName: "tab" + index
                        readonly property bool active: window.page === index
                        text: modelData + (index === 5 && window.api.alarms.length ? " · " + window.api.alarms.length : "")
                        Accessible.role: Accessible.PageTab
                        Accessible.selected: active
                        leftPadding: 9; rightPadding: 9
                        Layout.fillHeight: true
                        implicitWidth: contentItem.implicitWidth + leftPadding + rightPadding
                        contentItem: Label {
                            text: tab.text
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            color: tab.active ? "#ffffff" : "#d7e6f2"
                            font.pixelSize: 13
                            font.weight: tab.active ? Font.DemiBold : Font.Normal
                        }
                        background: Rectangle {
                            color: tab.active ? "#294761" : tab.hovered || tab.visualFocus ? "#213b55" : "transparent"
                            Rectangle {
                                anchors.bottom: parent.bottom; width: parent.width; height: 2
                                color: "#66dbc4"; visible: tab.active
                            }
                        }
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        onClicked: window.page = index
                    }
                }
            }
            Item { Layout.fillWidth: true }
            Label {
                visible: window.api.simulator && window.width >= 1140
                text: "Симулятор"; color: "#a9cec9"; font.pixelSize: 12
                Layout.rightMargin: 4
            }
            Rectangle {
                Layout.preferredWidth: 7; Layout.preferredHeight: 7; radius: 4
                color: window.api.running ? "#5ee0af" : "#9daebc"
                ToolTip.visible: statusHover.hovered
                ToolTip.text: window.api.running ? "Опрос запущен" : "Опрос остановлен"
                HoverHandler { id: statusHover }
            }
            ActionButton {
                visible: window.api.simulator && window.api.running
                objectName: "pauseDemo"
                compact: true
                text: window.api.simulatorPaused ? "Вернуть связь" : "Прервать связь"
                onClicked: window.api.toggleSimulator()
            }
            ActionButton {
                objectName: "runButton"
                compact: true
                text: window.api.running ? "Остановить" : "Запустить опрос"
                highlighted: !window.api.running
                onClicked: window.api.running ? window.api.stop() : window.startAcquisition()
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: window.operatorFullscreen ? 0 : 12
        anchors.rightMargin: anchors.leftMargin
        anchors.topMargin: window.operatorFullscreen ? 0 : 8
        anchors.bottomMargin: anchors.topMargin
        spacing: 6
        RowLayout {
            visible: window.page === 2 || window.page === 3
            Layout.fillWidth: true
            LabelValue {
                text: window.page === 2 ? "Источники данных" : "Теги"
                mode: "strong"; font.pixelSize: 16
                Layout.fillWidth: true
            }
            ActionButton {
                visible: window.page === 2
                objectName: "addSource"
                compact: true
                text: "+ Источник"; highlighted: true
                enabled: !window.api.running
                onClicked: sourceForm.edit({})
            }
            ActionButton {
                visible: window.page === 3
                objectName: "addTag"
                compact: true
                text: "+ Тег"; highlighted: true
                enabled: !window.api.running && window.api.sources.length > 0
                onClicked: tagForm.edit({})
            }
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: Math.max(0, window.page - 1)
            Dashboard {
                objectName: "dashboard"
                api: window.api
                editing: window.page === 0
                operatorFullscreen: window.operatorFullscreen
                onNameRequested: (id, name) => nameDialog.edit("screen", id, name)
                onScreenPropertiesRequested: screen => screenProperties.edit(screen)
                onDeleteScreenRequested: confirm.ask("Удалить этот экран?", "Удалить", () => window.api.removeScreen())
            }
            Rectangle {
                color: "#ffffff"; radius: 8; border.color: "#d7e0e8"
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 13; spacing: 5
                    Rectangle {
                        Layout.fillWidth: true; height: 39; radius: 4; color: "#eaf0f5"
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                            LabelValue { text: "Подключение"; mode: "strong"; Layout.preferredWidth: 235 }
                            LabelValue { text: "Адрес"; mode: "strong"; Layout.fillWidth: true }
                            LabelValue { objectName: "sourceExchangeHeading"; text: "Обмен"; mode: "strong"; Layout.preferredWidth: 115 }
                            LabelValue { text: "Соединение"; mode: "strong"; Layout.preferredWidth: 129 }
                            Item { Layout.preferredWidth: 245 }
                        }
                    }
                    ListView {
                        id: sources
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true; model: window.api.sources
                        delegate: Rectangle {
                            id: row
                            required property var modelData
                            required property int index
                            readonly property bool engine: modelData.driver === "engine-json"
                            width: ListView.view.width; height: 47
                            color: index % 2 ? "#ffffff" : "#f6f8fa"
                            RowLayout {
                                anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                                LabelValue { text: row.modelData.name; mode: "strong"; Layout.preferredWidth: 235 }
                                LabelValue {
                                    text: row.engine ? (row.modelData.settings.transport === "serial" ? row.modelData.settings.portName :
                                          row.modelData.settings.url) : row.modelData.settings.host + ":" + row.modelData.settings.port
                                    Layout.fillWidth: true
                                }
                                LabelValue {
                                    objectName: "sourceExchange"
                                    text: row.engine ? (row.modelData.settings.transport === "serial" ? "JSON · USB COM" : "JSON · WebSocket") :
                                        row.modelData.settings.pollMs + " мс"
                                    Layout.preferredWidth: 115
                                    mode: "muted"
                                }
                                LabelValue {
                                    text: window.sourceStatus(row.modelData.id); Layout.preferredWidth: 129
                                    color: text === "Подключено" ? "#08766e" : "#5d6d80"
                                    ToolTip.visible: statusHover.hovered && !!(window.api.statuses[row.modelData.id] || {}).detail
                                    ToolTip.text: (window.api.statuses[row.modelData.id] || {}).detail || ""
                                    HoverHandler { id: statusHover }
                                }
                                RowLayout {
                                    Layout.fillWidth: false
                                    Layout.preferredWidth: 245
                                    Item { Layout.fillWidth: true }
                                    ActionButton {
                                        visible: row.engine
                                        objectName: "engineCatalog"
                                        text: "Теги JSON ▾"
                                        onClicked: { window.catalogSource = row.modelData.id; catalogMenu.open() }
                                        Menu {
                                            id: catalogMenu
                                            y: parent.height
                                            MenuItem {
                                                objectName: "jsonLoadSampleTags"
                                                text: "Загрузить пример тегов"
                                                enabled: !window.api.running
                                                onTriggered: window.api.importEngineTags(window.catalogSource)
                                            }
                                            MenuItem { objectName: "jsonImportTags"; text: "Импортировать JSON…"; enabled: !window.api.running; onTriggered: tagImport.open() }
                                            MenuItem { objectName: "jsonExportTags"; text: "Экспортировать JSON…"; onTriggered: tagExport.open() }
                                        }
                                    }
                                    ActionButton { objectName: "sourceEdit_" + row.modelData.id; text: "Правка"; enabled: !window.api.running; onClicked: sourceForm.edit(row.modelData) }
                                    ActionButton {
                                        objectName: "sourceRemove_" + row.modelData.id
                                        text: "Удалить"; enabled: !window.api.running
                                        onClicked: {
                                            const id = row.modelData.id
                                            confirm.ask("Удалить источник «" + row.modelData.name + "»?", "Удалить", () => window.api.removeSource(id))
                                        }
                                    }
                                }
                            }
                        }
                        LabelValue { anchors.centerIn: parent; visible: sources.count === 0; text: "Добавьте Modbus TCP или источник JSON-телеметрии."; mode: "muted" }
                        ScrollBar.vertical: ScrollBar {}
                    }
                }
            }
            Rectangle {
                color: "#ffffff"; radius: 8; border.color: "#d7e0e8"
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 13; spacing: 6
                    TextField {
                        id: filter
                        objectName: "tagSearch"
                        Layout.preferredWidth: 300; placeholderText: "Поиск по имени тега"
                        selectByMouse: true
                    }
                    Rectangle {
                        Layout.fillWidth: true; height: 39; radius: 4; color: "#eaf0f5"
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                            LabelValue { text: "Имя тега"; mode: "strong"; Layout.fillWidth: true }
                            LabelValue { text: "Источник"; mode: "strong"; Layout.preferredWidth: 155 }
                            LabelValue { text: "Адрес / JSON-путь"; mode: "strong"; Layout.preferredWidth: 153 }
                            LabelValue { text: "Значение"; mode: "strong"; Layout.preferredWidth: 100 }
                            LabelValue { text: "Качество / Время"; mode: "strong"; Layout.preferredWidth: 149 }
                            Item { Layout.preferredWidth: 159 }
                        }
                    }
                    ListView {
                        id: tagList
                        objectName: "tagList"
                        Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                        model: window.api.tags.filter(tag => tag.name.toLowerCase().includes(filter.text.toLowerCase().trim()))
                        delegate: Rectangle {
                            id: row
                            required property var modelData
                            required property int index
                            readonly property var reading: window.api.readings[modelData.id] || {}
                            readonly property var source: window.api.sources.find(s => s.id === modelData.source) || {}
                            width: ListView.view.width; height: 47
                            color: index % 2 ? "#ffffff" : "#f6f8fa"
                            RowLayout {
                                anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                                LabelValue { text: row.modelData.name; mode: "strong"; Layout.fillWidth: true }
                                LabelValue { text: row.source.name || ""; Layout.preferredWidth: 155 }
                                LabelValue {
                                    text: row.modelData.address.path || (row.modelData.address.table + " / " + row.modelData.address.unit + " / " + row.modelData.address.offset)
                                    mode: "muted"; Layout.preferredWidth: 153
                                }
                                LabelValue {
                                    text: row.reading.hasValue ? (typeof row.reading.value === "number" ?
                                          Number(row.reading.value.toPrecision(9)) : row.reading.value) + " " + (row.modelData.units || "") : "—"
                                    Layout.preferredWidth: 100; mode: "strong"
                                }
                                LabelValue {
                                    text: window.quality(row.modelData.id) + (row.reading.time ? " · " + Qt.formatTime(new Date(row.reading.time), "HH:mm:ss") : "")
                                    Layout.preferredWidth: 149; mode: "muted"
                                    ToolTip.visible: qualityHover.hovered && !!row.reading.detail
                                    ToolTip.text: row.reading.detail || ""
                                    HoverHandler { id: qualityHover }
                                }
                                ActionButton { objectName: "tagEdit_" + row.modelData.id; text: "Правка"; enabled: !window.api.running; onClicked: tagForm.edit(row.modelData) }
                                ActionButton {
                                    objectName: "tagRemove_" + row.modelData.id
                                    text: "Удалить"; enabled: !window.api.running
                                    onClicked: {
                                        const id = row.modelData.id
                                        confirm.ask("Удалить тег «" + row.modelData.name + "» и его привязки?", "Удалить", () => window.api.removeTag(id))
                                    }
                                }
                            }
                        }
                        LabelValue {
                            anchors.centerIn: parent; visible: tagList.count === 0; mode: "muted"
                            text: window.api.sources.length ? "Нет тегов для показа." : "Сначала добавьте источник."
                        }
                        ScrollBar.vertical: ScrollBar {}
                    }
                }
            }
            History { api: window.api }
            Events { api: window.api }
        }
    }
    footer: Rectangle {
        objectName: "mainFooter"
        visible: !window.operatorFullscreen
        implicitHeight: window.operatorFullscreen ? 0 : 25; color: "#e4ebf1"
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
            LabelValue {
                text: window.api.path || (window.api.simulator ? "Демопроект · архив в памяти" : "Проект ещё не сохранён")
                mode: "muted"; Layout.fillWidth: true
            }
            LabelValue { text: "Источники  " + window.api.sources.length + "     Теги  " + window.api.tags.length; mode: "muted" }
        }
    }
    FileDialog {
        id: tagImport
        objectName: "jsonImportPicker"
        title: "Импорт таблицы тегов JSON"
        nameFilters: ["Таблица JSON (*.json)"]
        onAccepted: window.api.importEngineTags(window.catalogSource, selectedFile)
    }
    FileDialog {
        id: tagExport
        objectName: "jsonExportPicker"
        title: "Экспорт таблицы тегов JSON"
        fileMode: FileDialog.SaveFile
        nameFilters: ["Таблица JSON (*.json)"]
        onAccepted: window.api.exportEngineTags(window.catalogSource, selectedFile)
    }
    FileDialog {
        id: openPicker
        objectName: "projectOpenPicker"
        title: "Открыть проект " + Qt.application.displayName
        nameFilters: ["Проект " + Qt.application.displayName + " (*.scada.json)", "JSON (*.json)"]
        onAccepted: { if (window.api.open(selectedFile)) window.page = 0 }
    }
    FileDialog {
        id: savePicker
        objectName: "projectSavePicker"
        title: "Сохранить проект " + Qt.application.displayName
        fileMode: FileDialog.SaveFile
        nameFilters: ["Проект " + Qt.application.displayName + " (*.scada.json)"]
        onAccepted: {
            const action = window.afterSave
            window.afterSave = null
            if (window.api.save(selectedFile) && action) action()
        }
        onRejected: window.afterSave = null
    }
    SourceDialog { id: sourceForm; objectName: "sourceForm"; api: window.api; anchors.centerIn: parent }
    TagDialog { id: tagForm; objectName: "tagForm"; api: window.api; anchors.centerIn: parent }
    ScreenProperties { id: screenProperties; api: window.api }
    Dialog {
        id: confirm
        objectName: "actionConfirmation"
        anchors.centerIn: parent; modal: true; focus: true; width: 460
        title: "Подтверждение"
        property var action
        function ask(question, button, callback) {
            questionLabel.text = question
            execute.text = button
            action = callback
            open()
        }
        contentItem: ColumnLayout {
            spacing: 13
            Label { id: questionLabel; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { objectName: "cancelAction"; text: "Отмена"; onClicked: confirm.close() }
                ActionButton {
                    id: execute; objectName: "confirmAction"; highlighted: true
                    onClicked: { const action = confirm.action; confirm.close(); if (action) action() }
                }
            }
        }
    }
    Dialog {
        id: nameDialog
        anchors.centerIn: parent; modal: true; width: 420
        property string kind
        property string entity
        function edit(type, entityId, value) {
            kind = type
            entity = entityId
            newName.text = value
            open()
            newName.forceActiveFocus()
            newName.selectAll()
        }
        function apply() {
            if (!newName.text.trim()) return
            if (kind === "project") window.api.renameProject(newName.text)
            else window.api.setScreen(entity, newName.text)
            close()
        }
        title: kind === "project" ? "Название проекта" : entity ? "Название экрана" : "Новый экран"
        contentItem: ColumnLayout {
            spacing: 9
            TextField {
                id: newName; objectName: "screenName"; Layout.fillWidth: true
                placeholderText: "Название"; onAccepted: nameDialog.apply()
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { objectName: "cancelName"; text: "Отмена"; onClicked: nameDialog.close() }
                ActionButton { objectName: "saveName"; text: "Сохранить"; highlighted: true; onClicked: nameDialog.apply() }
            }
        }
    }
    Dialog {
        id: unsaved
        anchors.centerIn: parent; modal: true; width: 460
        title: "Есть несохранённые изменения"
        property var action
        contentItem: RowLayout {
            Item { Layout.fillWidth: true }
            ActionButton { objectName: "unsavedCancel"; text: "Отмена"; onClicked: unsaved.close() }
            ActionButton { objectName: "unsavedDiscard"; text: "Не сохранять"; onClicked: { const action = unsaved.action; unsaved.close(); if (action) action() } }
            ActionButton {
                objectName: "unsavedSave"
                text: "Сохранить"; highlighted: true
                onClicked: { const action = unsaved.action; unsaved.close(); window.saveProject(false, action) }
            }
        }
    }
    Rectangle {
        id: toast
        objectName: "studioToast"
        parent: window.contentItem
        x: Math.max(14, (window.width - width) / 2)
        y: Math.max(6, parent.height - height - 22)
        width: Math.min(540, window.width - 28)
        height: toastText.implicitHeight + 24
        z: 30
        visible: false
        property bool error
        color: error ? "#873831" : "#174b4b"
        radius: 8
        function open() { visible = true }
        function close() { visible = false }
        Label {
            id: toastText
            anchors.fill: parent
            anchors.margins: 12
            color: "#ffffff"; wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        Timer { id: dismiss; interval: 5400; onTriggered: toast.close() }
    }
    Connections {
        target: window.api
        function onNotice(message, error) {
            toastText.text = message
            toast.error = error
            toast.open()
            dismiss.restart()
        }
    }
}
