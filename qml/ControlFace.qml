import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: face
    required property var config
    required property var tag
    required property var reading
    required property var feedbackTag
    required property var feedbackReading
    required property bool canAdjust
    required property bool customActions
    property bool darkSurface: false
    property color ink: "#13263b"
    property color secondary: "#506479"
    property color accent: "#087c70"
    signal occurred(string eventName, real value)

    readonly property bool binary: config.kind === "switch" || config.kind === "valve"
    readonly property bool analog: ["setpoint", "slider", "knob"].includes(config.kind)
    readonly property bool known: reading.quality === "good" && !!reading.hasValue
    readonly property bool positionKnown: feedbackReading.quality === "good" && !!feedbackReading.hasValue
    readonly property real actual: Number(reading.value ?? 0)
    readonly property real lower: config.min ?? 0
    readonly property real upper: config.max ?? 100
    readonly property real step: config.step ?? Math.min(upper - lower,
        tag.address?.type === "f32" || !tag.id ? (upper - lower) / 100 : Math.abs(tag.scale || 1))
    readonly property real offValue: tag.bias ?? 0
    readonly property real onValue: offValue + (tag.scale ?? 1)
    readonly property bool active: positionKnown && equal(Number(feedbackReading.value),
        (feedbackTag.bias ?? 0) + (feedbackTag.scale ?? 1))
    readonly property var positions: config.positions || [{ label: "Стоп", value: 0 }, { label: "Ручной", value: 1 }, { label: "Авто", value: 2 }]
    property bool editingValue: false
    property string draft: ""
    property bool adjusting: false
    property real gestureValue: NaN
    property real lastReportedEdit: NaN
    readonly property real target: editingValue ? (draft.trim() ? Number(draft.trim().replace(",", ".")) : NaN) : actual
    readonly property bool validDraft: Number.isFinite(target) && target >= lower && target <= upper
    readonly property real fraction: Math.max(0, Math.min(1, ((validDraft ? target : actual) - lower) / (upper - lower)))
    readonly property string units: tag.units ? " " + tag.units : ""
    spacing: 5

    function equal(a, b) {
        return a === b || (Number.isFinite(a) && Number.isFinite(b) &&
            Math.abs(a - b) <= Number.EPSILON * 8 * Math.max(Math.abs(a), Math.abs(b), Number.MIN_VALUE))
    }
    function matchesActual(value) {
        if (tag.address?.type !== "f32") return equal(value, actual)
        const scale = tag.scale ?? 1
        const bias = tag.bias ?? 0
        return equal(Math.fround((value - bias) / scale) * scale + bias, actual)
    }
    function format(value) {
        const digits = config.decimals ?? 1
        return Math.abs(value) >= 1e9 || (value && Math.abs(value) < 10 ** (-digits)) ?
            value.toExponential(Math.min(digits, 4)) : value.toFixed(digits)
    }
    function propose(value) {
        const stepped = value <= lower || value >= upper ? value : lower + Math.round((value - lower) / step) * step
        updateDraft(String(Number(Math.max(lower, Math.min(upper, stepped)).toPrecision(15))))
    }
    function updateDraft(text) {
        if (!editingValue || draft !== text) lastReportedEdit = NaN
        draft = text
        editingValue = true
    }
    function submit(value) {
        if (!canAdjust || (!customActions && matchesActual(value))) return
        occurred("click", value)
        editingValue = false
    }
    function buttonEvent(eventName, value) {
        if (eventName === "click") submit(value)
        else occurred(eventName, value)
    }
    function reportEdit() {
        if (!canAdjust || !editingValue || !validDraft || equal(target, lastReportedEdit)) return
        lastReportedEdit = target
        occurred("change", target)
    }
    function beginAdjustment() {
        if (adjusting) return
        adjusting = true
        gestureValue = target
        occurred("press", target)
    }
    function finishAdjustment(applyEdit) {
        if (!adjusting) return
        adjusting = false
        occurred("release", target)
        if (applyEdit && !equal(target, gestureValue)) reportEdit()
    }
    function nudge(delta) {
        propose((validDraft ? target : actual) + delta)
        reportEdit()
    }
    function status() {
        if (!tag.id) return customActions ? "Тег показания не выбран" : "Выберите тег"
        if (!known) return "Нет актуальных данных"
        if (binary) {
            if (!positionKnown) return "Нет обратной связи"
            const state = config.kind === "valve" ? (active ? "Открыт" : "Закрыт") : (active ? "Включён" : "Выключен")
            return config.feedback ? "Факт · " + state : state
        }
        if (analog) return "Текущее · " + format(actual) + units
        return positions.find(position => matchesActual(position.value))?.label || "Значение · " + String(actual) + units
    }
    onConfigChanged: {
        editingValue = false
        lastReportedEdit = NaN
    }
    onCanAdjustChanged: if (!canAdjust) {
        finishAdjustment(false)
        editingValue = false
    }
    onVisibleChanged: if (!visible) finishAdjustment(false)
    onActualChanged: if (customActions && editingValue && !adjusting && validDraft &&
        equal(target, lastReportedEdit) && matchesActual(target)) editingValue = false

    ProcessSymbol {
        visible: face.config.kind === "valve" && face.height > 86
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 21
        mode: "valve"
        known: face.positionKnown
        active: face.active
        accent: face.accent
    }
    LabelValue {
        Layout.fillWidth: true
        text: face.status()
        color: face.binary && face.active ? face.accent : face.secondary
        font.pixelSize: 12
        ToolTip.visible: statusHover.hovered
        ToolTip.text: !face.canAdjust ? "Проверьте теги, опрос и разрешение записи" : text
        HoverHandler { id: statusHover }
    }
    RowLayout {
        visible: face.binary
        Layout.fillWidth: true
        ActionEventButton {
            objectName: "controlOff"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            compact: true
            text: face.config.kind === "valve" ? "Закрыть" : "Выкл."
            highlighted: face.known && face.equal(face.actual, face.offValue)
            enabled: face.canAdjust
            onOccurred: eventName => face.buttonEvent(eventName, face.offValue)
        }
        ActionEventButton {
            objectName: "controlOn"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            compact: true
            text: face.config.kind === "valve" ? "Открыть" : "Вкл."
            highlighted: face.known && face.equal(face.actual, face.onValue)
            enabled: face.canAdjust
            onOccurred: eventName => face.buttonEvent(eventName, face.onValue)
        }
    }
    Slider {
        id: slider
        objectName: "controlSlider"
        visible: face.config.kind === "slider"
        enabled: face.canAdjust
        Layout.fillWidth: true
        from: face.lower
        to: face.upper
        stepSize: face.step
        value: face.validDraft ? face.target : face.actual
        function finishDrag() { face.finishAdjustment(true) }
        onPressedChanged: {
            if (pressed) face.beginAdjustment()
            else Qt.callLater(slider.finishDrag)
        }
        onMoved: {
            face.propose(value)
            if (!face.adjusting) face.reportEdit()
        }
        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + (slider.availableHeight - height) / 2
            width: slider.availableWidth
            height: 5
            radius: 2
            color: face.darkSurface ? "#445b70" : "#dce7ed"
            Rectangle { width: slider.visualPosition * parent.width; height: parent.height; radius: 2; color: face.accent }
        }
        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: slider.topPadding + (slider.availableHeight - height) / 2
            implicitWidth: 17
            implicitHeight: 17
            radius: width / 2
            color: face.accent
            border.color: slider.visualFocus ? face.ink : "transparent"
        }
    }
    Item {
        id: dial
        visible: face.config.kind === "knob" && face.height > 81
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 27
        LabelValue {
            visible: dial.width > 145 && dialFace.height > 36
            anchors.right: dialFace.left; anchors.rightMargin: 8
            anchors.bottom: dialFace.bottom; anchors.bottomMargin: 2
            text: face.format(face.lower)
            color: face.secondary
            font.pixelSize: 11
        }
        LabelValue {
            visible: dial.width > 145 && dialFace.height > 36
            anchors.left: dialFace.right; anchors.leftMargin: 8
            anchors.bottom: dialFace.bottom; anchors.bottomMargin: 2
            text: face.format(face.upper)
            color: face.secondary
            font.pixelSize: 11
        }
        Rectangle {
            id: dialFace
            anchors.centerIn: parent
            width: Math.min(parent.width * 0.72, parent.height, 92)
            height: width
            radius: width / 2
            color: face.darkSurface ? "#25415a" : "#eef5f7"
            border.width: 2
            border.color: dialInput.activeFocus ? face.accent : face.darkSurface ? "#5d7890" : "#bdced9"
            Repeater {
                model: dialFace.width > 42 ? 9 : 0
                delegate: Item {
                    required property int index
                    anchors.fill: parent
                    rotation: -135 + 33.75 * index
                    Rectangle {
                        anchors.horizontalCenter: parent.horizontalCenter
                        y: parent.height * 0.06
                        width: 1; height: parent.height * 0.075
                        color: index / 8 <= face.fraction ? face.accent : "#91a6b4"
                    }
                }
            }
            Item {
                anchors.fill: parent
                rotation: -135 + 270 * face.fraction
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: parent.height * 0.14
                    width: Math.max(3, parent.width * 0.055)
                    height: parent.height * 0.25
                    radius: width / 2
                    color: face.accent
                }
            }
            MouseArea {
                id: dialInput
                objectName: "controlDial"
                anchors.fill: parent
                enabled: face.canAdjust
                preventStealing: true
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                activeFocusOnTab: enabled
                function move(x, y) {
                    const degrees = Math.atan2(x - width / 2, height / 2 - y) * 180 / Math.PI
                    const position = Math.max(0, Math.min(1, (degrees + 135) / 270))
                    face.propose(face.lower + (face.upper - face.lower) * position)
                }
                onPressed: mouse => {
                    forceActiveFocus()
                    face.beginAdjustment()
                    move(mouse.x, mouse.y)
                }
                onPositionChanged: mouse => { if (pressed) move(mouse.x, mouse.y) }
                onReleased: face.finishAdjustment(true)
                onCanceled: face.finishAdjustment(false)
                Keys.onLeftPressed: face.nudge(-face.step)
                Keys.onRightPressed: face.nudge(face.step)
            }
        }
    }
    RowLayout {
        visible: face.analog
        Layout.fillWidth: true
        ActionButton {
            visible: face.config.kind === "setpoint" && face.width >= 168
            compact: true; text: "−"
            enabled: face.canAdjust
            onClicked: face.nudge(-face.step)
        }
        TextField {
            id: field
            objectName: "controlValue"
            Layout.fillWidth: true
            Layout.minimumWidth: 30
            implicitHeight: 30
            readOnly: !face.canAdjust || face.config.kind !== "setpoint"
            selectByMouse: true
            text: face.editingValue ? face.draft : face.known ? face.format(face.actual) : ""
            placeholderText: "—"
            color: face.editingValue && !face.validDraft ? "#ad3a25" : face.ink
            background: Rectangle {
                radius: 5; color: face.darkSurface ? "#263d52" : "#f3f7fa"
                border.color: field.activeFocus ? face.accent : face.darkSurface ? "#5d7286" : "#c6d5df"
            }
            onTextEdited: face.updateDraft(text)
            onEditingFinished: if (face.config.kind === "setpoint") face.reportEdit()
            onAccepted: if (applyValue.enabled) {
                face.reportEdit()
                face.submit(face.target)
            }
            ToolTip.visible: hovered && face.editingValue && !face.validDraft
            ToolTip.text: "Допустимо от " + face.lower + " до " + face.upper
        }
        ActionButton {
            visible: face.config.kind === "setpoint" && face.width >= 168
            compact: true; text: "+"
            enabled: face.canAdjust
            onClicked: face.nudge(face.step)
        }
        ActionEventButton {
            id: applyValue
            objectName: "controlApply"
            compact: true
            text: face.width >= 160 ? "Записать" : "✓"
            Accessible.name: "Записать уставку"
            highlighted: enabled
            enabled: face.canAdjust && face.editingValue && face.validDraft &&
                (face.customActions || !face.matchesActual(face.target))
            onOccurred: eventName => {
                if (eventName === "click") face.submit(face.target)
                else if (face.config.kind === "setpoint") face.occurred(eventName, face.target)
            }
        }
    }
    GridLayout {
        visible: face.config.kind === "selector"
        Layout.fillWidth: true
        columns: face.width < 180 ? 2 : 3
        rowSpacing: 4; columnSpacing: 4
        Repeater {
            model: face.config.kind === "selector" ? face.positions : []
            delegate: ActionEventButton {
                required property var modelData
                required property int index
                objectName: "controlPosition_" + index
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                compact: true
                text: modelData.label
                highlighted: face.known && face.matchesActual(modelData.value)
                enabled: face.canAdjust
                onOccurred: eventName => face.buttonEvent(eventName, modelData.value)
            }
        }
    }
}
