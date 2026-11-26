import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Item {
    id: dashboard
    required property var api
    required property bool editing
    property bool operatorFullscreen: false
    property var selectedScreen: api.screens.find(screen => screen.id === api.screenId) || {}
    readonly property bool typeEditor: editing && api.compositeDepth > 0
    readonly property var canvasSettings: typeEditor ? api.canvasSettings : selectedScreen
    property double zoom: 1
    property bool fit: true
    property bool snap: true
    property string hoverTitle: ""
    property point hoverPoint: Qt.point(0, 0)
    property int paletteWidth: 166
    property int inspectorWidth: 230
    property bool paletteCollapsed: false
    property bool inspectorCollapsed: false
    readonly property int gridStep: canvasSettings.gridStep ?? 10
    signal nameRequested(string id, string title)
    signal screenPropertiesRequested(var screen)
    signal deleteScreenRequested()
    onEditingChanged: if (!editing && api.compositeDepth) api.closeComposite()
    onTypeEditorChanged: {
        fit = true
        view.contentX = 0
        view.contentY = 0
    }
    Settings { id: editorSettings; category: "editor" }
    Component.onCompleted: {
        paletteWidth = editorSettings.value("paletteWidth", paletteWidth)
        inspectorWidth = editorSettings.value("inspectorWidth", inspectorWidth)
        paletteCollapsed = String(editorSettings.value("paletteCollapsed", false)) === "true"
        inspectorCollapsed = String(editorSettings.value("inspectorCollapsed", false)) === "true"
    }

    function saveEditorSetting(key, value) {
        editorSettings.setValue(key, value)
        editorSettings.sync()
    }

    component PanelWidthHandle: MouseArea {
        id: handle
        required property bool leftEdge
        required property int minWidth
        required property int maxWidth
        required property int storedWidth
        signal widthEdited(int value)
        signal widthCommitted(int value)
        property real startX
        property real startWidth
        property int previousWidth

        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.left: leftEdge ? parent.left : undefined
        anchors.right: leftEdge ? undefined : parent.right
        width: 12
        hoverEnabled: true
        cursorShape: Qt.SizeHorCursor
        onPressed: mouse => {
            startX = mapToItem(null, mouse.x, mouse.y).x
            startWidth = parent.width
            previousWidth = storedWidth
        }
        onPositionChanged: mouse => {
            if (!pressed) return
            const delta = (mapToItem(null, mouse.x, mouse.y).x - startX) * (leftEdge ? -1 : 1)
            const newWidth = Math.round(Math.max(minWidth, Math.min(maxWidth, startWidth + delta)))
            widthEdited(newWidth === startWidth ? previousWidth : newWidth)
        }
        onReleased: if (storedWidth !== previousWidth) widthCommitted(storedWidth)
        onCanceled: widthEdited(previousWidth)

        Rectangle {
            anchors.centerIn: parent
            width: 3; height: 34; radius: 2
            color: handle.containsMouse || handle.pressed ? "#0b776f" : "#ccd9e1"
        }
    }

    readonly property var tools: [
        { kind: "value", title: "Значение", glyph: "12" },
        { kind: "indicator", title: "Индикатор", glyph: "●" },
        { kind: "gauge", title: "Шкала", glyph: "▰" },
        { kind: "trend", title: "Тренд", glyph: "⌁" },
        { kind: "tank", title: "Бак", glyph: "▥" },
        { kind: "pump", title: "Насос", glyph: "→" },
        { kind: "label", title: "Текст", glyph: "T" },
        { kind: "image", title: "Изображение", glyph: "▧" },
        { kind: "shape", title: "Фигура", glyph: "◇", onlyType: true },
        { kind: "hitArea", title: "Зона команды", glyph: "⌖", onlyType: true },
        { kind: "command", title: "Команда", glyph: "↯" },
        { kind: "link", title: "Переход", glyph: "↗" },
        { kind: "dial", title: "Стрелочный прибор", glyph: "◴" },
        { kind: "column", title: "Термометр", glyph: "▕" },
        { kind: "ring", title: "Сегментная шкала", glyph: "◉" },
        { kind: "segments", title: "Цифровое табло", glyph: "88" },
        { kind: "signal", title: "Сигнальная лампа", glyph: "◈" },
        { kind: "compass", title: "Компас", glyph: "◇" },
        { kind: "switch", title: "Тумблер", glyph: "◉", section: "Управление" },
        { kind: "setpoint", title: "Ввод уставки", glyph: "±" },
        { kind: "slider", title: "Ползунок", glyph: "↔" },
        { kind: "knob", title: "Регулятор", glyph: "◴" },
        { kind: "selector", title: "Выбор режима", glyph: "≡" },
        { kind: "valve", title: "Клапан", glyph: "⋈", section: "Мнемосхема" },
        { kind: "motor", title: "Двигатель", glyph: "Ⓜ" },
        { kind: "pipe", title: "Трубопровод", glyph: "━" },
        { kind: "bargraph", title: "Линейный прибор", glyph: "▤" },
        { kind: "alarm", title: "Тревога", glyph: "!" }
    ].filter(tool => dashboard.typeEditor || !tool.onlyType)
    function onGrid(value) { return snap ? Math.round(value / gridStep) * gridStep : Math.round(value) }
    function revealScreen() {
        if (!screenTabs.visible || screenTabs.width <= 0) return
        const index = api.screens.findIndex(screen => screen.id === api.screenId)
        const button = screenRepeater.itemAt(index)
        if (!button) return
        const right = button.x + button.width
        let offset = screenTabs.contentX
        if (button.x < offset)
            offset = button.x
        else if (right > offset + screenTabs.width)
            offset = right - screenTabs.width
        screenTabs.contentX = Math.max(0, Math.min(offset, screenTabs.contentWidth - screenTabs.width))
    }
    Connections {
        target: dashboard.api
        function onSelectionChanged() { Qt.callLater(dashboard.revealScreen) }
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 6
        RowLayout {
            objectName: "screenToolbar"
            visible: !dashboard.operatorFullscreen && (dashboard.editing || dashboard.api.screens.length > 1)
            Layout.fillWidth: true
            spacing: 5
            Flickable {
                id: screenTabs
                objectName: "screenTabs"
                visible: !dashboard.typeEditor
                Layout.fillWidth: true; Layout.preferredHeight: 30
                clip: true
                contentWidth: screenButtons.width
                contentHeight: height
                interactive: contentWidth > width
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.HorizontalFlick
                onWidthChanged: Qt.callLater(dashboard.revealScreen)
                onVisibleChanged: if (visible) Qt.callLater(dashboard.revealScreen)
                onContentWidthChanged: Qt.callLater(dashboard.revealScreen)
                ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                Row {
                    id: screenButtons
                    width: implicitWidth
                    height: screenTabs.height
                    spacing: 5
                    onPositioningComplete: Qt.callLater(dashboard.revealScreen)
                    Repeater {
                        id: screenRepeater
                        model: dashboard.api.screens
                        onItemAdded: Qt.callLater(dashboard.revealScreen)
                        delegate: ActionButton {
                            required property var modelData
                            objectName: "screenTab_" + modelData.id
                            compact: true
                            width: Math.min(196, implicitWidth)
                            height: screenButtons.height
                            text: modelData.name
                            highlighted: modelData.id === dashboard.api.screenId
                            onClicked: dashboard.api.selectScreen(modelData.id)
                            ToolTip.visible: hovered && implicitWidth > width
                            ToolTip.text: text
                        }
                    }
                }
            }
            ActionButton {
                objectName: "addScreen"
                visible: dashboard.editing && !dashboard.typeEditor
                compact: true
                text: "+"
                Accessible.name: "Добавить экран"
                ToolTip.visible: hovered; ToolTip.text: Accessible.name
                onClicked: dashboard.nameRequested("", "")
            }
            ActionButton {
                visible: dashboard.editing && !dashboard.typeEditor
                compact: true
                text: "Экран ▾"
                objectName: "screenMenu"
                onClicked: screenMenu.open()
                Menu {
                    id: screenMenu
                    y: parent.height
                    MenuItem {
                        objectName: "openScreenProperties"
                        text: "Свойства экрана…"
                        onTriggered: dashboard.screenPropertiesRequested(dashboard.selectedScreen)
                    }
                    MenuItem {
                        objectName: "deleteScreen"
                        text: "Удалить…"
                        enabled: dashboard.api.screens.length > 1
                        onTriggered: dashboard.deleteScreenRequested()
                    }
                }
            }
            ActionButton {
                objectName: "faceplateBack"
                visible: dashboard.typeEditor
                compact: true
                text: "← На экран"
                onClicked: dashboard.api.leaveComposite()
            }
            LabelValue {
                objectName: "faceplateCanvasName"
                visible: dashboard.typeEditor
                Layout.fillWidth: true
                text: dashboard.canvasSettings.name || ""
                mode: "strong"
            }
            ActionButton {
                objectName: "faceplatePorts"
                visible: dashboard.typeEditor
                compact: true
                text: "Интерфейс тегов…"
                onClicked: faceplatePorts.edit(dashboard.api.faceplates.find(type => type.id === dashboard.canvasSettings.id))
            }
            ActionButton {
                objectName: "faceplateSize"
                visible: dashboard.typeEditor
                compact: true
                text: "Размер типа…"
                onClicked: {
                    typeWidth.text = String(dashboard.canvasSettings.width)
                    typeHeight.text = String(dashboard.canvasSettings.height)
                    faceplateSize.open()
                }
            }
            ActionButton {
                objectName: "faceplatePropertyInterface"
                visible: dashboard.typeEditor
                compact: true
                text: "Интерфейс свойств…"
                onClicked: faceplateProperties.edit(dashboard.api.faceplates.find(type => type.id === dashboard.canvasSettings.id))
            }
            ActionButton {
                objectName: "faceplatePreview"
                visible: dashboard.typeEditor
                compact: true
                text: "Предпросмотр"
                onClicked: widgetPreview.start()
            }
            ActionButton {
                objectName: "togglePalette"
                visible: dashboard.editing
                compact: true
                text: "Палитра"
                checkable: true
                checked: !dashboard.paletteCollapsed
                highlighted: checked
                Accessible.name: checked ? "Свернуть левую панель" : "Развернуть левую панель"
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onToggled: {
                    dashboard.paletteCollapsed = !checked
                    dashboard.saveEditorSetting("paletteCollapsed", dashboard.paletteCollapsed)
                }
            }
            ActionButton {
                objectName: "toggleInspector"
                visible: dashboard.editing
                compact: true
                text: "Свойства"
                checkable: true
                checked: !dashboard.inspectorCollapsed
                highlighted: checked
                Accessible.name: checked ? "Свернуть панель свойств" : "Развернуть панель свойств"
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onToggled: {
                    dashboard.inspectorCollapsed = !checked
                    dashboard.saveEditorSetting("inspectorCollapsed", dashboard.inspectorCollapsed)
                }
            }
            ActionButton { objectName: "editorUndo"; visible: dashboard.editing; compact: true; text: "↶"; enabled: dashboard.api.canUndo; onClicked: dashboard.api.undo(); ToolTip.visible: hovered; ToolTip.text: "Отменить" }
            ActionButton { objectName: "editorRedo"; visible: dashboard.editing; compact: true; text: "↷"; enabled: dashboard.api.canRedo; onClicked: dashboard.api.redo(); ToolTip.visible: hovered; ToolTip.text: "Повторить" }
        }
        RowLayout {
            id: editorRow
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 10
            readonly property int minPaletteWidth: 160
            readonly property int minInspectorWidth: 210
            readonly property int visiblePaletteMinimum: palettePanel.visible ? minPaletteWidth : 0
            readonly property int visibleInspectorMinimum: inspector.visible ? minInspectorWidth : 0
            readonly property int panelCount: Number(palettePanel.visible) + Number(inspector.visible)
            readonly property int panelSpace: Math.max(visiblePaletteMinimum + visibleInspectorMinimum,
                Math.floor(width - panelCount * spacing - 360))
            readonly property int maxPaletteWidth: Math.min(500,
                panelSpace - (inspector.visible ? inspector.Layout.preferredWidth : 0))
            readonly property int maxInspectorWidth: Math.min(500, panelSpace - visiblePaletteMinimum)
            Rectangle {
                id: palettePanel
                objectName: "palettePanel"
                visible: dashboard.editing && !dashboard.paletteCollapsed
                Layout.preferredWidth: Math.max(editorRow.minPaletteWidth,
                    Math.min(editorRow.maxPaletteWidth, dashboard.paletteWidth))
                Layout.fillHeight: true
                color: "#ffffff"; border.color: "#d7e0e8"; radius: 8
                ScrollView {
                    anchors.fill: parent; anchors.margins: 12
                    contentWidth: availableWidth
                    clip: true
                    ColumnLayout {
                        width: parent.width
                        spacing: 5
                        LabelValue {
                            visible: !dashboard.typeEditor
                            text: "Свои виджеты"; mode: "strong"; font.pixelSize: 15
                        }
                        ActionButton {
                            objectName: "faceplateLibraryNew"
                            visible: !dashboard.typeEditor
                            Layout.fillWidth: true
                            text: "Создать виджет…"
                            compact: true
                            onClicked: libraryNameDialog.edit("", "")
                        }
                        ActionButton {
                            objectName: "faceplateLibraryImport"
                            visible: !dashboard.typeEditor
                            Layout.fillWidth: true
                            text: "Импорт…"
                            compact: true
                            onClicked: faceplateImportPicker.open()
                        }
                        ComboBox {
                            id: faceplateBox
                            objectName: "faceplateLibraryType"
                            visible: !dashboard.typeEditor && count > 0
                            Layout.fillWidth: true
                            model: dashboard.api.faceplates
                            textRole: "name"; valueRole: "id"
                            popup.width: Math.max(faceplateBox.width, 560)
                            delegate: ItemDelegate {
                                id: typeOption
                                required property var model
                                required property int index
                                objectName: "faceplateOption_" + model.id
                                width: ListView.view.width
                                implicitHeight: Math.max(40, actions.implicitHeight + 8)
                                rightPadding: actions.width + 12
                                text: model.name
                                highlighted: faceplateBox.highlightedIndex === index
                                font.weight: faceplateBox.currentIndex === index ? Font.DemiBold : Font.Normal
                                Row {
                                    id: actions
                                    anchors.right: parent.right
                                    anchors.rightMargin: 6
                                    anchors.verticalCenter: parent.verticalCenter
                                    spacing: 5
                                    ActionButton {
                                        objectName: "faceplateDuplicate_" + typeOption.model.id
                                        text: "Дублировать"
                                        compact: true
                                        Accessible.name: "Дублировать " + typeOption.text
                                        onClicked: libraryNameDialog.duplicate(typeOption.model.id)
                                    }
                                    ActionButton {
                                        objectName: "faceplateRename_" + typeOption.model.id
                                        text: "Переименовать"
                                        compact: true
                                        Accessible.name: "Переименовать " + typeOption.text
                                        onClicked: libraryNameDialog.edit(typeOption.model.id, typeOption.text)
                                    }
                                    ActionButton {
                                        objectName: "faceplateExport_" + typeOption.model.id
                                        text: "Экспорт…"
                                        compact: true
                                        Accessible.name: "Сохранить тип «" + typeOption.text + "» в файл"
                                        onClicked: faceplateExportPicker.exportType(typeOption.model.id)
                                    }
                                    ActionButton {
                                        objectName: "faceplateDelete_" + typeOption.model.id
                                        text: "Удалить"
                                        compact: true
                                        Accessible.name: "Удалить " + typeOption.text
                                        onClicked: libraryDeleteDialog.ask(typeOption.model.id, typeOption.text)
                                    }
                                }
                            }
                        }
                        RowLayout {
                            visible: faceplateBox.visible
                            Layout.fillWidth: true
                            ActionButton {
                                objectName: "faceplateLibraryInsert"
                                text: "Вставить"; compact: true
                                highlighted: true; Layout.fillWidth: true
                                enabled: !!faceplateBox.currentValue
                                onClicked: dashboard.api.insertFaceplate(faceplateBox.currentValue)
                            }
                            ActionButton {
                                objectName: "faceplateLibraryEdit"
                                text: "Правка"; compact: true
                                enabled: !!faceplateBox.currentValue
                                onClicked: dashboard.api.enterComposite(faceplateBox.currentValue)
                            }
                        }
                        WidgetLayers {
                            visible: dashboard.typeEditor
                            api: dashboard.api
                            Layout.fillWidth: true
                        }
                        LabelValue { text: "Элементы"; mode: "strong"; font.pixelSize: 15; Layout.bottomMargin: 5 }
                        Repeater {
                            model: dashboard.tools
                            delegate: ColumnLayout {
                                id: entry
                                required property var modelData
                                Layout.fillWidth: true
                                LabelValue {
                                    visible: !!entry.modelData.section
                                    text: entry.modelData.section || ""
                                    mode: "strong"
                                    Layout.topMargin: 7
                                }
                                Rectangle {
                                    id: tile
                                    readonly property var modelData: entry.modelData
                                    objectName: "palette_" + modelData.kind
                                    Layout.fillWidth: true; implicitHeight: 31; radius: 5
                                    color: toolPointer.containsMouse ? "#dff0ed" : "#f0f4f7"
                                    RowLayout {
                                        anchors.fill: parent; anchors.leftMargin: 7; anchors.rightMargin: 5
                                        Label {
                                            text: tile.modelData.glyph
                                            Layout.preferredWidth: 23
                                            color: "#08766e"; font.pixelSize: 15; font.weight: Font.DemiBold
                                            horizontalAlignment: Text.AlignHCenter
                                        }
                                        LabelValue { text: tile.modelData.title; Layout.fillWidth: true }
                                    }
                                    MouseArea {
                                        id: toolPointer
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        preventStealing: true
                                        cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                        property point start
                                        property bool moved: false
                                        onPressed: mouse => { start = Qt.point(mouse.x, mouse.y); moved = false }
                                        onPositionChanged: mouse => {
                                            if (!pressed) return
                                            if (Math.abs(mouse.x - start.x) > 5 || Math.abs(mouse.y - start.y) > 5) moved = true
                                            if (moved) {
                                                dashboard.hoverTitle = tile.modelData.title
                                                dashboard.hoverPoint = mapToItem(dashboard, mouse.x, mouse.y)
                                            }
                                        }
                                        onReleased: mouse => {
                                            const local = mapToItem(canvasBoard, mouse.x, mouse.y)
                                            const visiblePoint = mapToItem(view, mouse.x, mouse.y)
                                            dashboard.hoverTitle = ""
                                            if (!moved) {
                                                const shift = 30 + (dashboard.api.items.length % 12) * 20
                                                dashboard.api.addWidget(tile.modelData.kind, shift, shift)
                                            } else if (local.x >= 0 && local.y >= 0 && local.x < canvasBoard.width && local.y < canvasBoard.height &&
                                                       visiblePoint.x >= 0 && visiblePoint.y >= 0 && visiblePoint.x < view.width && visiblePoint.y < view.height) {
                                                dashboard.api.addWidget(tile.modelData.kind, dashboard.onGrid(local.x), dashboard.onGrid(local.y))
                                            }
                                        }
                                        onCanceled: dashboard.hoverTitle = ""
                                    }
                                }
                            }
                        }
                        LabelValue {
                            visible: dashboard.api.templates.length > 0
                            text: "Шаблоны"; mode: "strong"; Layout.topMargin: 7
                        }
                        ComboBox {
                            id: templateBox
                            visible: dashboard.api.templates.length > 0
                            Layout.fillWidth: true
                            model: dashboard.api.templates
                            textRole: "name"; valueRole: "id"
                        }
                        RowLayout {
                            visible: templateBox.visible
                            Layout.fillWidth: true
                            ActionButton {
                                objectName: "insertTemplate"
                                text: "Вставить"; Layout.fillWidth: true; highlighted: true
                                onClicked: dashboard.api.insertTemplate(templateBox.currentValue)
                            }
                            ActionButton {
                                objectName: "removeTemplate"
                                text: "×"; onClicked: dashboard.api.removeTemplate(templateBox.currentValue)
                                ToolTip.visible: hovered; ToolTip.text: "Удалить шаблон"
                            }
                        }
                    }
                }
                PanelWidthHandle {
                    objectName: "paletteResize"
                    leftEdge: false
                    minWidth: editorRow.minPaletteWidth
                    maxWidth: editorRow.maxPaletteWidth
                    storedWidth: dashboard.paletteWidth
                    onWidthEdited: value => dashboard.paletteWidth = value
                    onWidthCommitted: value => dashboard.saveEditorSetting("paletteWidth", value)
                }
            }
            Rectangle {
                Layout.fillWidth: true; Layout.fillHeight: true
                radius: dashboard.operatorFullscreen ? 0 : 8
                color: dashboard.operatorFullscreen ? "#111d2a" : "#e1e8ef"
                border.color: "#d3dde6"
                border.width: dashboard.operatorFullscreen ? 0 : 1
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: dashboard.operatorFullscreen ? 0 : 1; spacing: 0
                    Rectangle {
                        visible: dashboard.editing
                        Layout.fillWidth: true; implicitHeight: 33
                        color: "#f8fafb"; radius: 7
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 8
                            LabelValue { text: canvasBoard.width + " × " + canvasBoard.height; mode: "muted" }
                            Item { Layout.fillWidth: true }
                            CheckBox {
                                implicitHeight: 30
                                topPadding: 2; bottomPadding: 2
                                objectName: "editorSnap"
                                text: "Привязка к сетке"; checked: dashboard.snap
                                onToggled: dashboard.snap = checked
                            }
                            ActionButton {
                                compact: true
                                objectName: "editorFit"
                                text: "Вписать"; highlighted: dashboard.fit
                                onClicked: { dashboard.fit = true; view.contentX = 0; view.contentY = 0 }
                            }
                            ActionButton { objectName: "editorZoomOut"; compact: true; text: "−"; onClicked: { dashboard.zoom = Math.max(0.025, canvasBoard.factor / 1.2); dashboard.fit = false } }
                            LabelValue { text: Math.round(canvasBoard.factor * 100) + "%"; horizontalAlignment: Text.AlignHCenter; Layout.preferredWidth: 42 }
                            ActionButton { objectName: "editorZoomIn"; compact: true; text: "+"; onClicked: { dashboard.zoom = Math.min(Math.max(2, canvasBoard.factor), canvasBoard.factor * 1.2); dashboard.fit = false } }
                        }
                    }
                    Flickable {
                        id: view
                        objectName: "dashboardView"
                        readonly property int padding: dashboard.operatorFullscreen ? 0 : dashboard.editing ? 14 : 3
                        readonly property real availableWidth: Math.max(0, width - 2 * padding)
                        readonly property real availableHeight: Math.max(0, height - 2 * padding)
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip: true; boundsBehavior: Flickable.StopAtBounds
                        interactive: !dashboard.operatorFullscreen
                        contentWidth: Math.max(width, canvasBoard.width * canvasBoard.factor + 2 * padding)
                        contentHeight: Math.max(height, canvasBoard.height * canvasBoard.factor + 2 * padding)
                        ScrollBar.horizontal: ScrollBar {}
                        ScrollBar.vertical: ScrollBar {}
                        Rectangle {
                            id: canvasBoard
                            objectName: "screenCanvas"
                            property string moving: ""
                            property real dx: 0
                            property real dy: 0
                            readonly property int gridStep: dashboard.gridStep
                            readonly property bool darkBackground: color.r * 0.2126 + color.g * 0.7152 + color.b * 0.0722 < 0.55
                            readonly property real designWidth: dashboard.canvasSettings.width ?? 1200
                            readonly property real designHeight: dashboard.canvasSettings.height ?? 692
                            readonly property bool fillWindow: !dashboard.editing && dashboard.selectedScreen.operatorFit !== "fit"
                            readonly property real factor: dashboard.editing && !dashboard.fit ? dashboard.zoom :
                                Math.max(0.001, Math.min(view.availableWidth / designWidth, view.availableHeight / designHeight))
                            readonly property real horizontalRatio: fillWindow ? width / designWidth : 1
                            readonly property real verticalRatio: fillWindow ? height / designHeight : 1
                            x: Math.max(view.padding, (view.width - width * factor) / 2)
                            y: Math.max(view.padding, (view.height - height * factor) / 2)
                            width: fillWindow ? view.availableWidth / factor : designWidth
                            height: fillWindow ? view.availableHeight / factor : designHeight
                            scale: factor; transformOrigin: Item.TopLeft
                            color: dashboard.canvasSettings.background || "#f5f8fa"
                            border.color: "#d5e0e8"; border.width: dashboard.operatorFullscreen ? 0 : 1
                            radius: dashboard.operatorFullscreen ? 0 : 4
                            clip: true
                            Image {
                                objectName: "screenBackgroundImage"
                                anchors.fill: parent
                                visible: !!dashboard.canvasSettings.backgroundImage
                                source: visible ? dashboard.api.imageUrl(dashboard.canvasSettings.backgroundImage) : ""
                                fillMode: dashboard.canvasSettings.backgroundFit === "contain" ? Image.PreserveAspectFit :
                                          dashboard.canvasSettings.backgroundFit === "stretch" ? Image.Stretch : Image.PreserveAspectCrop
                                asynchronous: true
                            }
                            Canvas {
                                id: grid
                                anchors.fill: parent
                                visible: dashboard.editing && dashboard.snap
                                onWidthChanged: requestPaint()
                                onHeightChanged: requestPaint()
                                onVisibleChanged: if (visible) requestPaint()
                                Connections {
                                    target: canvasBoard
                                    function onGridStepChanged() { grid.requestPaint() }
                                    function onColorChanged() { grid.requestPaint() }
                                }
                                onPaint: {
                                    const ctx = getContext("2d")
                                    const step = canvasBoard.gridStep
                                    ctx.clearRect(0, 0, width, height)
                                    ctx.strokeStyle = canvasBoard.darkBackground ? Qt.rgba(0.83, 0.91, 0.98, 0.22) :
                                        Qt.rgba(0.17, 0.28, 0.40, 0.14)
                                    ctx.lineWidth = 1
                                    ctx.beginPath()
                                    for (let x = step; x < width; x += step) { ctx.moveTo(x, 0); ctx.lineTo(x, height) }
                                    for (let y = step; y < height; y += step) { ctx.moveTo(0, y); ctx.lineTo(width, y) }
                                    ctx.stroke()
                                }
                            }
                            MouseArea { anchors.fill: parent; enabled: dashboard.editing; onClicked: dashboard.api.selectWidget("") }
                            Label {
                                anchors.centerIn: parent
                                visible: dashboard.api.items.length === 0
                                text: dashboard.editing ? "Перетащите элемент на экран" : "Этот экран пока пуст"
                                color: canvasBoard.darkBackground ? "#cbddeb" : "#607486"; font.pixelSize: 23
                            }
                            Repeater {
                                model: dashboard.api.items
                                delegate: ScreenItem {
                                    required property var modelData
                                    objectName: modelData.kind === "faceplate" ?
                                        "screenItem_faceplate_" + modelData.id : "screenItem_" + modelData.kind
                                    api: dashboard.api
                                    config: modelData
                                    board: canvasBoard
                                    editing: dashboard.editing
                                    snap: dashboard.snap
                                }
                            }
                        }
                    }
                }
            }
            Inspector {
                id: inspector
                objectName: "inspectorPanel"
                visible: dashboard.editing && !dashboard.inspectorCollapsed
                api: dashboard.api
                Layout.preferredWidth: Math.max(editorRow.minInspectorWidth,
                    Math.min(editorRow.maxInspectorWidth, dashboard.inspectorWidth))
                Layout.fillHeight: true
                PanelWidthHandle {
                    objectName: "inspectorResize"
                    leftEdge: true
                    minWidth: editorRow.minInspectorWidth
                    maxWidth: Math.min(editorRow.maxInspectorWidth,
                        editorRow.panelSpace - (palettePanel.visible ? palettePanel.Layout.preferredWidth : 0))
                    storedWidth: dashboard.inspectorWidth
                    onWidthEdited: value => dashboard.inspectorWidth = value
                    onWidthCommitted: value => dashboard.saveEditorSetting("inspectorWidth", value)
                }
            }
        }
    }
    FaceplatePortsDialog { id: faceplatePorts; api: dashboard.api }
    FaceplatePropertyInterfaceDialog { id: faceplateProperties; api: dashboard.api }
    WidgetPreview { id: widgetPreview; api: dashboard.api }
    FileDialog {
        id: faceplateExportPicker
        objectName: "faceplateExportPicker"
        title: "Сохранить тип фейсплейса в файл"
        fileMode: FileDialog.SaveFile
        nameFilters: ["Тип фейсплейса (*.faceplate.json)"]
        defaultSuffix: "faceplate.json"
        property string typeId: ""
        function exportType(id) {
            typeId = id
            faceplateBox.popup.close()
            open()
        }
        onAccepted: dashboard.api.exportFaceplate(typeId, selectedFile)
    }
    FileDialog {
        id: faceplateImportPicker
        objectName: "faceplateImportPicker"
        title: "Импортировать тип фейсплейса"
        nameFilters: ["Тип фейсплейса (*.faceplate.json)"]
        onAccepted: {
            const file = selectedFile
            Qt.callLater(() => faceplateImportDialog.edit(file))
        }
    }
    Dialog {
        id: faceplateImportDialog
        objectName: "faceplateImportOptionsDialog"
        title: "Импорт типа фейсплейса"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        focus: true
        width: Math.min(660, parent ? parent.width - 32 : 660)
        property url file
        property string sourceName: ""
        property string errorText: ""
        property bool applying: false
        property var incomingDefinition: ({})
        readonly property bool canApply: replaceMode.checked ?
            !!importTarget.currentValue && importMapping.problem === "" : importName.text.trim() !== ""

        onOpened: {
            importName.forceActiveFocus()
            importName.selectAll()
        }
        function edit(url) {
            const options = dashboard.api.faceplateImportOptions(url)
            if (!options.name) return
            file = url
            sourceName = options.name
            incomingDefinition = options.definition
            errorText = ""
            newMode.checked = true
            importName.text = options.suggestedName
            importTarget.currentIndex = 0
            refreshMapping()
            open()
        }
        function refreshMapping() {
            if (!importMapping) return
            const rows = replaceMode.checked && importTarget.currentValue ?
                dashboard.api.faceplateReplacementFields(importTarget.currentValue, incomingDefinition) : []
            importMapping.setFields(rows)
            errorText = ""
        }
        function applyImport() {
            if (!canApply) return
            errorText = ""
            applying = true
            const importedId = dashboard.api.importFaceplate(file, importName.text.trim(),
                replaceMode.checked ? importTarget.currentValue : "", replaceMode.checked ? importMapping.selection : ({}))
            applying = false
            if (!importedId) return
            close()
            faceplateBox.currentIndex = faceplateBox.indexOfValue(importedId)
            dashboard.api.enterComposite(importedId)
        }
        Connections {
            target: dashboard.api
            enabled: faceplateImportDialog.applying
            function onNotice(message, error) {
                if (error) faceplateImportDialog.errorText = message
            }
        }
        contentItem: ColumnLayout {
            spacing: 7
            LabelValue { text: "Тип в файле · " + faceplateImportDialog.sourceName; Layout.fillWidth: true }
            RadioButton {
                id: newMode
                objectName: "faceplateImportNewMode"
                text: "Создать новый тип"
                checked: true
            }
            TextField {
                id: importName
                objectName: "faceplateImportName"
                placeholderText: "Название нового типа"
                Accessible.name: placeholderText
                visible: newMode.checked
                Layout.fillWidth: true
                onTextEdited: faceplateImportDialog.errorText = ""
                onAccepted: faceplateImportDialog.applyImport()
            }
            RadioButton {
                id: replaceMode
                objectName: "faceplateImportReplaceMode"
                text: "Заменить существующий тип"
                enabled: dashboard.api.faceplates.length > 0
                onCheckedChanged: faceplateImportDialog.refreshMapping()
            }
            ComboBox {
                id: importTarget
                objectName: "faceplateImportTarget"
                Accessible.name: "Тип для замены"
                visible: replaceMode.checked
                Layout.fillWidth: true
                model: [{ id: "", name: "Выберите тип для замены" }].concat(dashboard.api.faceplates)
                textRole: "name"
                valueRole: "id"
                onCurrentValueChanged: faceplateImportDialog.refreshMapping()
            }
            Label {
                visible: replaceMode.checked
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                color: "#506479"
                text: "Замена обновит тип на всех экранах и в шаблонах."
            }
            FaceplateFieldMapping {
                id: importMapping
                choicePrefix: "faceplateImportMap_"
                destinationHeading: "Поле из файла"
                Layout.fillWidth: true
                onEdited: faceplateImportDialog.errorText = ""
            }
            Label {
                objectName: "faceplateImportError"
                visible: text !== ""
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                color: "#873831"
                text: faceplateImportDialog.errorText || (replaceMode.checked ? importMapping.problem : "")
            }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                ActionButton { text: "Отмена"; onClicked: faceplateImportDialog.close() }
                ActionButton {
                    objectName: "faceplateImportApply"
                    text: replaceMode.checked ? "Заменить" : "Импортировать"
                    enabled: faceplateImportDialog.canApply
                    highlighted: true
                    onClicked: faceplateImportDialog.applyImport()
                }
            }
        }
    }
    Dialog {
        id: faceplateSize
        title: "Размер типа"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 360
        contentItem: ColumnLayout {
            RowLayout {
                Field { id: typeWidth; objectName: "typeWidth"; caption: "Ширина, px" }
                Field { id: typeHeight; objectName: "typeHeight"; caption: "Высота, px" }
            }
            RowLayout {
                Item { Layout.fillWidth: true }
                ActionButton { text: "Отмена"; onClicked: faceplateSize.close() }
                ActionButton {
                    objectName: "typeSizeApply"
                    text: "Применить"; highlighted: true
                    enabled: /^[0-9]+$/.test(typeWidth.text.trim()) && /^[0-9]+$/.test(typeHeight.text.trim()) &&
                        Number(typeWidth.text) >= 80 && Number(typeWidth.text) <= 8192 &&
                        Number(typeHeight.text) >= 52 && Number(typeHeight.text) <= 8192
                    onClicked: if (dashboard.api.setFaceplateSize(Number(typeWidth.text), Number(typeHeight.text))) faceplateSize.close()
                }
            }
        }
    }
    Dialog {
        id: libraryNameDialog
        property string typeId: ""
        property bool copying: false
        title: copying ? "Создать копию типа" : typeId ? "Переименовать тип фейсплейса" : "Создать свой виджет"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        focus: true
        width: 410
        onOpened: {
            libraryName.forceActiveFocus()
            if (typeId) libraryName.selectAll()
        }
        function duplicate(id) {
            edit(id, dashboard.api.suggestFaceplateCopyName(id), true)
        }
        function edit(id, name, copy = false) {
            typeId = id
            copying = copy
            libraryName.text = name
            newWidgetWidth.text = "480"
            newWidgetHeight.text = "280"
            if (id) faceplateBox.popup.close()
            open()
        }
        function save() {
            const name = libraryName.text.trim()
            if (!name) return
            if (typeId) {
                let savedId = typeId
                if (copying)
                    savedId = dashboard.api.duplicateFaceplate(typeId, name)
                else if (!dashboard.api.renameFaceplate(typeId, name))
                    return
                if (!savedId) return
                faceplateBox.currentIndex = faceplateBox.indexOfValue(savedId)
                close()
                if (copying) dashboard.api.enterComposite(savedId)
                return
            }
            dashboard.api.selectWidget("")
            if (newWidgetWidth.acceptableInput && newWidgetHeight.acceptableInput &&
                dashboard.api.createFaceplate(name, Number(newWidgetWidth.text), Number(newWidgetHeight.text))) {
                faceplateBox.currentIndex = faceplateBox.indexOfValue(dashboard.api.canvasSettings.id)
                close()
            }
        }
        contentItem: ColumnLayout {
            LabelValue { text: "Название типа"; mode: "muted" }
            TextField {
                id: libraryName
                objectName: "faceplateLibraryName"
                Accessible.name: "Название типа"
                Layout.fillWidth: true
                implicitHeight: 36
                selectByMouse: true
                onAccepted: libraryNameDialog.save()
            }
            RowLayout {
                visible: !libraryNameDialog.typeId
                Field {
                    id: newWidgetWidth
                    objectName: "newWidgetWidth"
                    caption: "Ширина, px"
                    validator: IntValidator { bottom: 80; top: 8192 }
                    onAccepted: libraryNameDialog.save()
                }
                Field {
                    id: newWidgetHeight
                    objectName: "newWidgetHeight"
                    caption: "Высота, px"
                    validator: IntValidator { bottom: 52; top: 8192 }
                    onAccepted: libraryNameDialog.save()
                }
            }
            RowLayout {
                Item { Layout.fillWidth: true }
                ActionButton {
                    objectName: "faceplateLibraryNameCancel"
                    text: "Отмена"
                    onClicked: libraryNameDialog.close()
                }
                ActionButton {
                    objectName: "faceplateLibraryNameApply"
                    text: libraryNameDialog.copying ? "Создать копию" : libraryNameDialog.typeId ? "Сохранить" : "Создать"
                    enabled: libraryName.text.trim() !== "" && (!!libraryNameDialog.typeId ||
                        newWidgetWidth.acceptableInput && newWidgetHeight.acceptableInput)
                    highlighted: true
                    onClicked: libraryNameDialog.save()
                }
            }
        }
    }
    Dialog {
        id: libraryDeleteDialog
        property string typeId: ""
        property string typeName: ""
        title: "Удаление типа фейсплейса"
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        width: 410
        function ask(id, name) {
            typeId = id
            typeName = name
            faceplateBox.popup.close()
            open()
        }
        function remove() {
            const selectedType = faceplateBox.currentValue ?? ""
            close()
            if (!dashboard.api.removeFaceplate(typeId)) return
            const selectedIndex = faceplateBox.indexOfValue(selectedType)
            faceplateBox.currentIndex = selectedIndex >= 0 ? selectedIndex : faceplateBox.count > 0 ? 0 : -1
        }
        contentItem: ColumnLayout {
            Label {
                text: "Удалить тип «" + libraryDeleteDialog.typeName + "»?"
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
            RowLayout {
                Item { Layout.fillWidth: true }
                ActionButton {
                    objectName: "faceplateDeleteCancel"
                    text: "Отмена"
                    onClicked: libraryDeleteDialog.close()
                }
                ActionButton {
                    objectName: "faceplateDeleteConfirm"
                    text: "Удалить"
                    highlighted: true
                    onClicked: libraryDeleteDialog.remove()
                }
            }
        }
    }
    Rectangle {
        z: 10; visible: dashboard.hoverTitle.length > 0
        x: dashboard.hoverPoint.x + 8; y: dashboard.hoverPoint.y + 8
        width: ghostLabel.implicitWidth + 24; height: 33; radius: 5
        color: "#0b716a"
        Label { id: ghostLabel; anchors.centerIn: parent; text: dashboard.hoverTitle; color: "#ffffff" }
    }
}
