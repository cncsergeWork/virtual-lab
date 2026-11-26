import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: widget
    required property var api
    required property var config
    required property bool editing
    required property bool snap
    required property var board
    property bool preview: false
    property var simulation: null
    readonly property bool operating: visible && !editing && !preview
    readonly property bool layered: (editing && api.compositeDepth > 0) || board.faceplateLayer === true
    readonly property bool locked: !!config.locked
    readonly property bool shapeLayer: config.kind === "shape"
    readonly property bool hitArea: config.kind === "hitArea"
    readonly property bool imageLayer: layered && config.kind === "image"
    readonly property var liveReadings: simulation ? simulation.samples : api.readings
    readonly property bool faceplate: config.kind === "faceplate"
    readonly property var faceplateDefinition: faceplate ? api.faceplates.find(type => type.id === config.faceplate) : null
    property real localX: config.x
    property real localY: config.y
    property real localWidth: config.w
    property real localHeight: config.h
    readonly property var reading: liveReadings[config.tag] || {}
    readonly property var availableTags: simulation || (editing && api.compositeDepth) ? api.editorTags : api.tags
    readonly property var tag: availableTags.find(t => t.id === config.tag) || {}
    readonly property var source: api.sources.find(s => s.id === tag.source) || {}
    readonly property bool good: reading.quality === "good"
    readonly property bool selected: editing && api.selectedIds.includes(config.id)
    readonly property bool moveWithGroup: !locked && board.moving !== "" && board.moving !== config.id && selected
    readonly property bool compact: height < 100
    readonly property bool shallow: height < 76
    readonly property string word: good ? "В работе" : reading.hasValue ? "Данные устарели" : config.tag ? "Нет данных" : "Тег не выбран"
    readonly property var appearance: config.appearance || ({})
    readonly property string appearanceTag: appearance.tag || config.tag ||
        config.trend?.series?.find(row => row.tag)?.tag || ""
    readonly property var colors: config.appearance ? api.widgetColors(appearance, liveReadings[appearanceTag] || {}) : ({})
    readonly property var propertyValues: config.propertyBindings ?
        api.faceplatePropertyValues(config.propertyBindings, liveReadings, simulation ? [] : api.alarms) : ({})
    readonly property bool displayed: propertyValues.visible ?? colors.visible ?? (config.visible !== false)
    readonly property var motion: config.animation || ({})
    readonly property bool motionActive: visible && !editing && (!preview || simulation !== null) &&
        !!motion.tag && api.animationActive(motion, liveReadings[motion.tag] || {})
    property real motionOpacity: 1
    readonly property string displayTitle: colors.text ?? config.title
    readonly property string surfaceOverride: propertyValues.background ?? colors.background ?? ""
    readonly property string valueOverride: propertyValues.value ?? colors.value ?? ""
    readonly property string captionTone: propertyValues.captionColor ?? config.captionColor ?? ""
    readonly property color surfaceColor: surfaceOverride ||
        (shapeLayer ? "#c9e0e8" : ["label", "pipe", "faceplate"].includes(config.kind) || imageLayer || hitArea ? "transparent" : "#ffffff")
    readonly property bool darkSurface: surfaceOverride ?
        (0.2126 * surfaceColor.r + 0.7152 * surfaceColor.g + 0.0722 * surfaceColor.b < 0.55) :
        (config.kind === "label" || faceplate) && board.darkBackground
    readonly property color ink: valueOverride || (darkSurface ? "#f4f9fc" : "#13263b")
    readonly property color secondary: darkSurface ? "#ceddeb" : "#506479"
    readonly property color accent: valueOverride || (darkSurface ? "#65d8c1" : "#087c70")
    readonly property bool industrial: ["dial", "column", "ring", "segments", "signal", "compass", "bargraph"].includes(config.kind)
    readonly property bool control: ["switch", "setpoint", "slider", "knob", "selector", "valve"].includes(config.kind)
    readonly property var actions: config.actions || []
    readonly property bool customActions: actions.length > 0
    readonly property bool canOperate: simulation ? visible && (customActions || (!!config.tag && !hitArea && tag.address?.type !== "text")) :
        operating && api.running && (customActions ?
        actions.every(row => actionReady(row)) :
        good && !!reading.hasValue && tag.writable === true && source.settings?.allowWrite === true &&
            api.statuses[source.id]?.state === "connected")
    readonly property var feedbackTag: config.feedbackUnbound ? ({}) :
        config.feedback ? availableTags.find(t => t.id === config.feedback) || {} : tag
    readonly property var feedbackReading: config.feedbackUnbound ? ({}) :
        config.feedback ? liveReadings[config.feedback] || {} : reading
    readonly property var activeAlarm: !simulation && config.kind === "alarm" && !config.tagUnbound ?
        (config.tag ? api.alarms.find(alarm => alarm.tag === config.tag) : api.alarms[0]) : null
    x: localX * board.horizontalRatio + (moveWithGroup ? board.dx : 0)
    y: localY * board.verticalRatio + (moveWithGroup ? board.dy : 0)
    width: localWidth * board.horizontalRatio
    height: localHeight * board.verticalRatio
    visible: editing || displayed
    opacity: editing && layered && !displayed && !selected ? 0.4 : motionOpacity
    clip: true
    NumberAnimation on rotation {
        running: widget.motionActive && widget.motion.kind === "rotate"
        from: 0
        to: widget.motion.reverse ? -360 : 360
        duration: widget.motion.periodMs ?? 1000
        loops: Animation.Infinite
        onStopped: widget.rotation = 0
    }
    SequentialAnimation {
        running: widget.motionActive && widget.motion.kind === "blink"
        loops: Animation.Infinite
        PauseAnimation { duration: Math.floor((widget.motion.periodMs ?? 1000) / 2) }
        PropertyAction { target: widget; property: "motionOpacity"; value: (widget.motion.dimPercent ?? 25) / 100 }
        PauseAnimation { duration: Math.ceil((widget.motion.periodMs ?? 1000) / 2) }
        PropertyAction { target: widget; property: "motionOpacity"; value: 1 }
        onStopped: widget.motionOpacity = 1
    }
    function format(value) { return Number(Number(value).toPrecision(8)).toLocaleString(Qt.locale(), "f", Math.abs(value - Math.round(value)) < 0.0000001 ? 0 : 2) }
    function actionReady(row) {
        if (!row.tag) return false
        const targetTag = api.tags.find(t => t.id === row.tag)
        const targetSource = api.sources.find(s => s.id === targetTag?.source)
        if (!targetSource || api.statuses[targetSource.id]?.state !== "connected") return false
        if (row.action === "read") return targetSource.driver === "modbus-tcp"
        const writable = targetTag.writable === true && targetSource.settings?.allowWrite === true &&
            api.readings[targetTag.id]?.quality === "good"
        if (!writable || row.valueMode !== "tag") return writable
        const inputTag = api.tags.find(t => t.id === row.valueTag)
        const inputSource = api.sources.find(s => s.id === inputTag?.source)
        if (!inputSource || api.statuses[inputSource.id]?.state !== "connected") return false
        const sample = api.readings[row.valueTag]
        return inputSource.driver === "modbus-tcp" || (sample?.quality === "good" && !!sample.hasValue)
    }
    function dispatchEvent(eventName, value) {
        if ((!simulation && !api.running) || (eventName !== "release" && !canOperate)) return
        if (!customActions) {
            if (eventName === "click" && !hitArea) {
                if (simulation) simulation.record(displayTitle, eventName, [{ action: "write", tag: config.tag, value: value }])
                else api.write(config.tag, value)
            }
            return
        }
        const requests = []
        for (const row of actions) {
            if (row.event !== eventName || !row.tag) continue
            if (row.action === "read") {
                requests.push({ action: "read", tag: row.tag })
            } else if (row.valueMode === "tag") {
                requests.push({ action: "write", tag: row.tag, valueTag: row.valueTag })
            } else {
                const output = row.valueMode === "widget" ? value : row.value
                if (Number.isFinite(output)) requests.push({ action: "write", tag: row.tag, value: output })
            }
        }
        if (requests.length) {
            if (simulation) simulation.record(displayTitle, eventName, requests)
            else api.runWidgetActions(config.id, requests)
        }
    }
    function grid(value) { return snap ? Math.round(value / board.gridStep) * board.gridStep : Math.round(value) }
    function fraction() {
        const min = config.min ?? 0, max = config.max ?? 100
        return Math.max(0, Math.min(1, (Number(reading.value ?? min) - min) / Math.max(1e-100, max - min)))
    }
    Rectangle {
        anchors.fill: parent
        visible: !widget.shapeLayer && !widget.hitArea
        radius: 12
        color: widget.surfaceColor
        border.width: widget.faceplate ? 0 : widget.selected ? 2 :
            widget.imageLayer || ["label", "pipe"].includes(widget.config.kind) ? 0 : 1
        border.color: widget.selected ? "#078375" : "#d9e4ed"
    }
    Loader {
        anchors.fill: parent
        active: widget.shapeLayer
        sourceComponent: Item {
            id: figure
            readonly property bool line: widget.config.shape === "line"
            readonly property bool ellipse: widget.config.shape === "ellipse"
            Rectangle {
                id: outline
                x: parent.line && widget.config.vertical ? (parent.width - width) / 2 : 0
                y: parent.line && !widget.config.vertical ? (parent.height - height) / 2 : 0
                width: parent.line && widget.config.vertical ? 3 : parent.ellipse ? Math.min(parent.width, parent.height) : parent.width
                height: parent.line && !widget.config.vertical ? 3 : parent.ellipse ? width : parent.height
                radius: parent.ellipse ? width / 2 : 0
                color: parent.line ? widget.valueOverride || widget.surfaceColor : widget.surfaceColor
                border.width: !parent.line && widget.valueOverride ? 2 : 0
                border.color: widget.valueOverride || "transparent"
                transform: Scale {
                    xScale: figure.ellipse ? figure.width / Math.max(1, outline.width) : 1
                    yScale: figure.ellipse ? figure.height / Math.max(1, outline.height) : 1
                }
            }
        }
    }
    Loader {
        anchors.fill: parent
        active: widget.hitArea
        sourceComponent: ActionEventButton {
            objectName: widget.simulation ? "previewHitAreaAction" : "hitAreaAction"
            text: widget.displayTitle
            enabled: widget.canOperate
            Accessible.name: widget.displayTitle
            background: Rectangle {
                color: widget.editing ? "#2387a31c" : "transparent"
                border.width: widget.editing ? 1 : 0
                border.color: "#5587a3"
            }
            contentItem: Label {
                visible: widget.editing
                text: widget.displayTitle
                color: "#285473"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
            }
            HoverHandler {
                enabled: widget.canOperate
                cursorShape: Qt.PointingHandCursor
            }
            onOccurred: eventName => widget.dispatchEvent(eventName, widget.config.command ?? 1)
        }
    }
    Loader {
        anchors.fill: parent
        property var instanceWidget: widget
        active: widget.faceplate
        source: "FaceplateContent.qml"
    }
    Loader {
        anchors.fill: parent
        active: widget.config.kind === "pipe"
        sourceComponent: ProcessSymbol {
            mode: "pipe"
            vertical: !!widget.config.vertical
            known: widget.good
            active: widget.good && Number(widget.reading.value) !== 0
            accent: widget.accent
            customInk: !!widget.valueOverride
            Accessible.name: widget.displayTitle
            ToolTip.visible: pipeHover.hovered && widget.operating && !!widget.displayTitle
            ToolTip.text: widget.displayTitle + (widget.config.tag && !widget.good ? " · " + widget.word : "")
            HoverHandler { id: pipeHover }
        }
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: widget.imageLayer ? 0 : widget.config.kind === "trend" || widget.control ? 10 : widget.shallow ? 8 : 16
        spacing: widget.shallow ? 1 : 7
        visible: widget.config.kind !== "pipe" && !widget.faceplate && !widget.shapeLayer && !widget.hitArea
        Label {
            objectName: "widgetCaption"
            visible: !["label", "image", "link", "command", "trend"].includes(widget.config.kind)
            text: widget.displayTitle
            font.pixelSize: widget.shallow ? 14 : 16
            font.weight: Font.Medium
            color: widget.captionTone || widget.secondary
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
        RowLayout {
            visible: widget.config.kind === "value" || widget.config.kind === "gauge"
            Layout.fillWidth: true
            Layout.fillHeight: widget.config.kind === "value"
            Label {
                objectName: "widgetValue"
                text: widget.reading.hasValue ? (typeof widget.reading.value === "string" ? (widget.reading.value || "—") :
                                               widget.format(widget.reading.value)) : "—"
                color: widget.good || widget.valueOverride ? widget.ink : widget.darkSurface ? "#c3d2de" : "#6c7f8c"
                font.pixelSize: widget.shallow ? 20 : widget.compact ? 27 : Math.min(48, widget.width * 0.20)
                font.weight: Font.DemiBold
                font.family: "Segoe UI"
                elide: Text.ElideRight
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
            }
            Label {
                objectName: "widgetUnits"
                text: widget.tag.units || ""
                color: widget.secondary; font.pixelSize: widget.shallow ? 12 : 16
                Layout.alignment: Qt.AlignVCenter
            }
        }
        Rectangle {
            visible: widget.config.kind === "gauge"
            Layout.fillWidth: true; Layout.preferredHeight: widget.shallow ? 3 : 7
            color: widget.darkSurface ? "#546a7d" : "#e7eef2"; radius: 4
            Rectangle {
                width: parent.width * widget.fraction()
                height: parent.height; radius: 4; color: widget.good ? widget.accent : "#9babb5"
            }
        }
        RowLayout {
            visible: widget.config.kind === "indicator" || widget.config.kind === "pump"
            Layout.fillWidth: true; Layout.fillHeight: true
            Rectangle {
                width: widget.config.kind === "pump" ? 36 : 13
                height: width; radius: width / 2
                color: widget.good && widget.valueOverride ? widget.valueOverride :
                    widget.good && widget.reading.value !== 0 ? "#078375" : "#e3eaf0"
                border.color: widget.good && widget.valueOverride ? widget.valueOverride : widget.good ? "#419b90" : "#aab8c3"
                Label {
                    visible: widget.config.kind === "pump"
                    anchors.centerIn: parent
                    text: "→"; color: widget.good && widget.reading.value !== 0 ? "#ffffff" : "#526778"
                    font.pixelSize: 22
                }
            }
            Label {
                Layout.fillWidth: true; elide: Text.ElideRight
                text: widget.good ? (widget.reading.value !== 0 ? "Включён" : "Выключен") : "Нет данных"
                font.pixelSize: widget.shallow ? 15 : 20; font.weight: Font.DemiBold; color: widget.ink
            }
        }
        Rectangle {
            visible: widget.config.kind === "tank"
            Layout.fillWidth: true; Layout.fillHeight: true; Layout.margins: 4
            radius: 8; color: widget.darkSurface ? "#293e52" : "#edf3f5"; border.color: "#acbecb"; clip: true
            Rectangle {
                anchors.bottom: parent.bottom
                width: parent.width
                height: parent.height * widget.fraction()
                color: widget.good ? (widget.valueOverride ? Qt.tint(parent.color, Qt.rgba(widget.ink.r, widget.ink.g, widget.ink.b, 0.28)) :
                                     "#9bd6c9") : "#c7d4dc"
            }
            Label {
                anchors.centerIn: parent
                text: widget.reading.hasValue ? widget.format(widget.reading.value) + " " + (widget.tag.units || "") : "—"
                font.pixelSize: 28; font.weight: Font.DemiBold; color: widget.ink
            }
        }
        Loader {
            visible: widget.control
            active: visible
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: ControlFace {
                objectName: "controlFace_" + widget.config.kind
                config: widget.config
                tag: widget.tag
                reading: widget.reading
                feedbackTag: widget.feedbackTag
                feedbackReading: widget.feedbackReading
                canAdjust: widget.canOperate
                customActions: widget.customActions
                ink: widget.ink
                secondary: widget.secondary
                accent: widget.accent
                darkSurface: widget.darkSurface
                onOccurred: (eventName, value) => widget.dispatchEvent(eventName, value)
            }
        }
        Loader {
            visible: widget.config.kind === "motor"
            active: visible
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: ColumnLayout {
                ProcessSymbol {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    mode: "motor"
                    known: widget.good
                    active: widget.good && Number(widget.reading.value) !== 0
                    accent: widget.accent
                    customInk: !!widget.valueOverride
                }
                LabelValue {
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                    text: widget.good ? (Number(widget.reading.value) !== 0 ? "Работает" : "Остановлен") : widget.word
                    color: widget.ink
                }
            }
        }
        Loader {
            visible: widget.config.kind === "alarm"
            active: visible
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: ColumnLayout {
                spacing: 5
                LabelValue {
                    Layout.fillWidth: true
                    text: widget.activeAlarm ? (widget.activeAlarm.ack ? "Квитирована" : "Тревога") :
                        !widget.api.running ? "Опрос остановлен" : widget.config.tagUnbound ? "Тег тревоги не назначен" :
                        widget.config.tag && !widget.good ? widget.word : "Активных тревог нет"
                    font.weight: Font.DemiBold
                    color: widget.valueOverride || (widget.activeAlarm ? (widget.darkSurface ? "#ffad86" : "#a6442b") : widget.ink)
                }
                LabelValue {
                    Layout.fillWidth: true
                    visible: !!widget.activeAlarm && widget.height > 100
                    text: widget.activeAlarm ? widget.activeAlarm.name + " · " +
                        (widget.activeAlarm.high ? "Верхний порог " : "Нижний порог ") + widget.activeAlarm.limit : ""
                    color: widget.secondary
                }
                ActionButton {
                    objectName: "ackWidgetAlarm"
                    compact: true
                    text: "Квитировать"
                    visible: !!widget.activeAlarm && !widget.activeAlarm.ack
                    enabled: widget.operating
                    onClicked: if (widget.activeAlarm) widget.api.acknowledge(widget.activeAlarm.tag)
                }
                Item { Layout.fillHeight: true }
            }
        }
        Loader {
            visible: widget.config.kind === "trend"
            active: visible
            Layout.fillWidth: true; Layout.fillHeight: true
            sourceComponent: DashboardTrend {
                api: widget.api; config: widget.config; editing: !widget.operating
                titleText: widget.displayTitle
                textTone: widget.valueOverride
                captionTone: widget.captionTone
                darkSurface: widget.darkSurface
            }
        }
        Loader {
            visible: widget.industrial
            active: visible
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: InstrumentFace {
                objectName: "instrumentFace_" + widget.config.kind
                mode: widget.config.kind
                lower: widget.config.min ?? 0
                upper: widget.config.max ?? 100
                precision: widget.config.decimals ?? (mode === "compass" ? 0 : 1)
                faceColor: widget.valueOverride || widget.config.accent || (lightPanel ? "#076f64" : "#52d6bf")
                customInk: !!widget.valueOverride
                surface: widget.surfaceOverride
                units: widget.tag.units || ""
                reading: widget.reading
                alarm: widget.good && widget.api.alarms.some(active => active.tag === widget.config.tag)
                bound: !!widget.config.tag
            }
        }
        Label {
            objectName: "widgetCaption"
            visible: widget.config.kind === "label"
            text: widget.displayTitle; color: widget.captionTone || widget.ink
            Layout.fillWidth: true; Layout.fillHeight: true
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            font.pixelSize: Math.min(36, widget.height * 0.52)
            font.weight: Font.DemiBold
        }
        Image {
            objectName: "widgetImage"
            visible: widget.config.kind === "image" && !!widget.config.image
            Layout.fillWidth: true; Layout.fillHeight: true
            fillMode: Image.PreserveAspectFit
            source: widget.config.kind === "image" && widget.config.image ? widget.api.imageUrl(widget.config.image) : ""
            asynchronous: true
        }
        LabelValue {
            visible: widget.config.kind === "image" && !widget.config.image
            Layout.fillWidth: true; Layout.fillHeight: true
            horizontalAlignment: Text.AlignHCenter
            text: "Выберите файл изображения"; color: widget.ink
        }
        ActionEventButton {
            objectName: widget.config.kind + "Action"
            visible: widget.config.kind === "link" || widget.config.kind === "command"
            Layout.fillWidth: true; Layout.fillHeight: true
            text: widget.displayTitle + (widget.config.kind === "link" ? "  →" : "")
            font.pixelSize: widget.width < 175 ? 13 : 17
            highlighted: widget.config.kind === "link"
            fillTone: widget.surfaceOverride
            textTone: widget.captionTone || widget.valueOverride || (widget.surfaceOverride ? widget.ink.toString() : "")
            enabled: widget.config.kind === "link" ? !widget.simulation && widget.operating && !!widget.config.screen :
                widget.canOperate
            onOccurred: eventName => {
                if (widget.config.kind === "link") {
                    if (eventName === "click") widget.api.selectScreen(widget.config.screen)
                } else {
                    widget.dispatchEvent(eventName, widget.config.command)
                }
            }
        }
        LabelValue {
            visible: !widget.shallow && !widget.good && ["value", "gauge", "indicator", "tank", "pump"].includes(widget.config.kind)
            Layout.fillWidth: true
            text: widget.word; color: widget.secondary
        }
    }
    Rectangle {
        anchors.fill: parent
        visible: widget.editing && (widget.faceplate || (widget.shapeLayer || widget.hitArea) && widget.selected)
        color: "transparent"
        radius: 6
        border.width: widget.selected ? 2 : 1
        border.color: widget.selected ? "#078375" : "#bacdd8"
    }
    MouseArea {
        id: dragArea
        anchors.fill: parent
        enabled: widget.editing
        hoverEnabled: true
        cursorShape: widget.locked ? Qt.ArrowCursor : pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
        preventStealing: true
        property point origin
        property point start
        property rect limits
        property bool extend
        onPressed: mouse => {
            origin = mapToItem(widget.board, mouse.x, mouse.y)
            start = Qt.point(widget.x, widget.y)
            extend = (mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)) !== 0
            widget.api.selectWidget(widget.config.id, extend)
            let minX = widget.config.x, minY = widget.config.y
            let maxX = minX + widget.width, maxY = minY + widget.height
            for (const instance of widget.api.items) {
                if (instance.locked || !widget.api.selectedIds.includes(instance.id)) continue
                minX = Math.min(minX, instance.x); minY = Math.min(minY, instance.y)
                maxX = Math.max(maxX, instance.x + instance.w); maxY = Math.max(maxY, instance.y + instance.h)
            }
            limits = Qt.rect(minX, minY, maxX - minX, maxY - minY)
        }
        onPositionChanged: mouse => {
            if (!pressed || extend || widget.locked) return
            const position = mapToItem(widget.board, mouse.x, mouse.y)
            if (Math.abs(position.x - origin.x) < 2 && Math.abs(position.y - origin.y) < 2) return
            const maxX = start.x + widget.board.width - limits.x - limits.width
            const maxY = start.y + widget.board.height - limits.y - limits.height
            widget.localX = Math.min(maxX, Math.max(start.x - limits.x, widget.grid(start.x + position.x - origin.x)))
            widget.localY = Math.min(maxY, Math.max(start.y - limits.y, widget.grid(start.y + position.y - origin.y)))
            widget.board.dx = widget.localX - start.x
            widget.board.dy = widget.localY - start.y
            widget.board.moving = widget.config.id
        }
        onReleased: {
            widget.board.moving = ""
            if (widget.localX !== widget.config.x || widget.localY !== widget.config.y)
                widget.api.moveWidget(widget.config.id, widget.localX, widget.localY, widget.width, widget.height)
        }
        onDoubleClicked: mouse => {
            if (widget.faceplate && !(mouse.modifiers & (Qt.ShiftModifier | Qt.ControlModifier)))
                widget.api.enterComposite(widget.config.id)
        }
        onCanceled: { widget.board.moving = ""; widget.localX = widget.config.x; widget.localY = widget.config.y }
    }
    Rectangle {
        objectName: "resizeGrip"
        visible: !widget.locked && widget.selected && widget.api.selectedId === widget.config.id
        anchors.right: parent.right; anchors.bottom: parent.bottom
        width: 15; height: 15; radius: 3; color: "#078375"
        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.SizeFDiagCursor
            preventStealing: true
            property point origin
            property size initial
            onPressed: mouse => { origin = mapToItem(widget.board, mouse.x, mouse.y); initial = Qt.size(widget.width, widget.height) }
            onPositionChanged: mouse => {
                if (!pressed) return
                const position = mapToItem(widget.board, mouse.x, mouse.y)
                const small = widget.shapeLayer || widget.hitArea
                widget.localWidth = Math.min(widget.board.width - widget.x, Math.max(small ? 8 : 80, widget.grid(initial.width + position.x - origin.x)))
                widget.localHeight = Math.min(widget.board.height - widget.y, Math.max(small ? 8 : 52, widget.grid(initial.height + position.y - origin.y)))
            }
            onReleased: widget.api.moveWidget(widget.config.id, widget.x, widget.y, widget.width, widget.height)
            onCanceled: { widget.localWidth = widget.config.w; widget.localHeight = widget.config.h }
        }
    }
}
