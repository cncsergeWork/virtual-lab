import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: chart
    objectName: "dashboardTrend"
    required property var api
    required property var config
    required property bool editing
    property bool darkSurface: false
    property string textTone: ""
    property string captionTone: ""
    property string titleText: config.title || ""
    readonly property alias periodPopup: navigationMenu
    readonly property color ink: textTone || (darkSurface ? "#c4d4e1" : "#546a79")
    readonly property var options: config.trend || { seconds: 300, axis: "shared", legend: true, toolbar: true,
        series: [{ tag: config.tag || "", color: "#087970", side: "left", style: "auto" }] }
    readonly property int defaultSeconds: options.seconds
    readonly property var channels: options.series.filter(row => row.tag && api.tags.some(tag => tag.id === row.tag))
    readonly property var shown: channels.filter(row => !hidden.includes(row.tag))
    property var hidden: []
    property bool following: true
    property var range: [Date.now() - defaultSeconds * 1000, Date.now()]
    readonly property double startTime: range[0]
    readonly property double endTime: range[1]
    readonly property double span: endTime - startTime
    property double cursor: 0
    property bool pinned: false
    readonly property int markA: 0
    readonly property int markB: 0
    property var snapshot: ({})
    spacing: 4

    function findTag(id) { return api.tags.find(tag => tag.id === id) || {} }
    function tick() {
        const now = Date.now()
        if (following) range = [now - span, now]
    }
    function setRange(from, to) {
        if (to <= from) return
        following = false
        range = [from, to]
    }
    function zoom(ratio, portion) {
        const next = Math.max(500, Math.min(Math.max(span, 365 * 86400000), span * ratio))
        const pivot = startTime + span * portion
        setRange(pivot - next * portion, pivot + next * (1 - portion))
    }
    function fitAll() {
        const bounds = api.archiveBounds(shown.map(row => row.tag))
        if (bounds.from === undefined) return
        const padding = Math.max(250, (bounds.to - bounds.from) * 0.025)
        setRange(bounds.from - padding, bounds.to + padding)
    }
    function selectCursor(time) {
        cursor = Math.round(time)
        if (!cursorTimer.running) cursorTimer.start()
    }
    function periodLabel() {
        if (cursor) return Qt.formatDateTime(new Date(cursor), "dd.MM HH:mm:ss.zzz")
        if (following) {
            const seconds = Math.round(span / 1000)
            return qsTr("Последние ") + (seconds % 3600 === 0 ? (seconds / 3600) + qsTr(" ч") :
                seconds % 60 === 0 && seconds > 60 ? (seconds / 60) + qsTr(" мин") : seconds + qsTr(" с"))
        }
        const format = span >= 86400000 ? "dd.MM HH:mm" : "HH:mm:ss"
        return Qt.formatDateTime(new Date(startTime), format) + " – " + Qt.formatDateTime(new Date(endTime), format)
    }
    function follow() {
        const now = Date.now()
        following = true
        pinned = false
        cursor = 0
        range = [now - defaultSeconds * 1000, now]
    }
    function toggle(id) {
        hidden = hidden.includes(id) ? hidden.filter(tag => tag !== id) : hidden.concat([id])
    }
    function refreshCursor() {
        snapshot = cursor ? api.archiveSamples(shown.map(row => row.tag), cursor) : ({})
    }
    function display(row) {
        const reading = cursor ? snapshot[row.tag] : api.readings[row.tag]
        if (reading?.quality === "stale") return qsTr("устар.")
        if (reading?.quality === "error") return qsTr("ошибка")
        if (!reading?.hasValue) return "—"
        const unit = findTag(row.tag).units || ""
        return Number(Number(reading.value).toPrecision(7)).toLocaleString(Qt.locale(), "g", 7) + (unit ? " " + unit : "")
    }
    onShownChanged: if (cursor && cursorTimer) cursorTimer.restart()
    onDefaultSecondsChanged: if (following) follow()
    Component.onCompleted: tick()
    Timer { interval: 500; running: chart.visible && chart.api.running && chart.following; repeat: true; onTriggered: chart.tick() }
    Timer { id: cursorTimer; interval: 35; onTriggered: chart.refreshCursor() }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Label {
            Layout.fillWidth: true
            text: chart.titleText
            elide: Text.ElideRight
            color: chart.captionTone || chart.ink
            font.pixelSize: 12
            font.weight: Font.DemiBold
        }
        AbstractButton {
            objectName: "dashboardTrendPeriod"
            visible: chart.width > 200
            enabled: plot.periodMenu
            hoverEnabled: true
            topPadding: 1; bottomPadding: 1
            text: chart.periodLabel()
            Accessible.name: qsTr("Период графика. ") + text
            contentItem: Label {
                text: parent.text
                color: chart.ink
                font.pixelSize: 12
                font.underline: parent.hovered && plot.periodMenu
            }
            onClicked: if (plot.periodMenu) navigationMenu.popup()
            ToolTip.visible: hovered && plot.periodMenu
            ToolTip.delay: 500
            ToolTip.text: qsTr("Выбрать период")
        }
    }
    Menu {
        id: navigationMenu
        MenuItem { objectName: "dashboardTrendLive"; text: qsTr("К текущему времени"); onTriggered: chart.follow() }
        MenuItem { objectName: "dashboardTrendFit"; text: qsTr("Весь доступный архив"); onTriggered: chart.fitAll() }
        MenuItem { objectName: "dashboardTrendInterval"; text: qsTr("Выбрать период…"); onTriggered: rangeDialog.edit() }
    }
    ArchivePlot {
        id: plot
        objectName: "dashboardTrendPlot"
        navigator: chart
        gestureName: "dashboardTrendGesture"
        interactive: !chart.editing
        periodMenu: chart.options.toolbar && !chart.editing
        onMenuRequested: navigationMenu.popup()
        Layout.fillWidth: true
        Layout.fillHeight: true
        showTimeAxis: chart.height > 102
        operatorStyle: true
        dualAxis: chart.options.axis === "dual"
        individualAxes: chart.options.axis === "individual"
        relativeTime: chart.options.timeLabels === "relative" ||
            ((chart.options.timeLabels ?? "auto") === "auto" && chart.following)
        darkSurface: chart.darkSurface
        textTone: chart.textTone
        seriesDefinitions: chart.shown.map(row => {
            const tag = chart.findTag(row.tag)
            const binary = tag.address?.type === "bool"
            return { id: row.tag, color: row.color, side: row.side, binary: binary, units: tag.units, divisions: row.divisions,
                style: row.style, stepped: row.style === "step" || (row.style === "auto" && binary),
                min: plot.individualAxes ? row.min : undefined, max: plot.individualAxes ? row.max : undefined }
        })
    }
    Flickable {
        visible: chart.options.legend && chart.channels.length > 0 && chart.height > 105
        Layout.fillWidth: true
        Layout.preferredHeight: 24
        clip: true
        flickableDirection: Flickable.HorizontalFlick
        contentWidth: legendRow.width
        contentHeight: height
        ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
        Row {
            id: legendRow
            spacing: 13
            Repeater {
                model: chart.channels
                delegate: AbstractButton {
                    required property var modelData
                    readonly property var boundTag: chart.findTag(modelData.tag)
                    readonly property bool shown: !chart.hidden.includes(modelData.tag)
                    id: legendEntry
                    objectName: "dashboardTrendLegend_" + modelData.tag
                    height: 22
                    hoverEnabled: true
                    text: boundTag.name + (chart.options.legendValues ?
                        " · " + chart.display(modelData) : boundTag.units ? " · " + boundTag.units : "")
                    opacity: shown ? 1 : 0.45
                    contentItem: RowLayout {
                        spacing: 5
                        Rectangle {
                            Layout.preferredWidth: 14
                            Layout.preferredHeight: 2
                            color: legendEntry.modelData.color
                        }
                        Label {
                            text: legendEntry.text
                            color: chart.ink
                            font.pixelSize: 11
                            font.underline: legendEntry.hovered && !chart.editing
                        }
                    }
                    ToolTip.visible: hovered && !chart.options.legendValues
                    ToolTip.delay: 350
                    ToolTip.text: boundTag.name + " · " + chart.display(modelData)
                    onClicked: if (!chart.editing) chart.toggle(modelData.tag)
                }
            }
        }
    }
    Dialog {
        id: rangeDialog
        title: qsTr("Период графика")
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: 440
        modal: true
        function edit() {
            fromField.text = Qt.formatDateTime(new Date(chart.startTime), "yyyy-MM-dd HH:mm:ss")
            toField.text = Qt.formatDateTime(new Date(chart.endTime), "yyyy-MM-dd HH:mm:ss")
            lastSeconds.text = String(Math.round(chart.span / 1000))
            invalid.visible = false
            open()
        }
        function parse(input) {
            const date = new Date(input.trim().replace(" ", "T"))
            return isFinite(date.getTime()) && Qt.formatDateTime(date, "yyyy-MM-dd HH:mm:ss") === input.trim() ? date.getTime() : NaN
        }
        contentItem: ColumnLayout {
            Field { id: lastSeconds; objectName: "trendLastSeconds"; caption: qsTr("Последние секунды") }
            ActionButton {
                objectName: "trendApplyLivePeriod"
                text: qsTr("Следить за текущим временем")
                onClicked: {
                    const seconds = Number(lastSeconds.text)
                    if (!Number.isInteger(seconds) || seconds < 1 || seconds > 31536000) { invalid.visible = true; return }
                    const now = Date.now()
                    chart.range = [now - seconds * 1000, now]
                    chart.following = true
                    chart.pinned = false
                    chart.cursor = 0
                    rangeDialog.close()
                }
            }
            Field { id: fromField; objectName: "trendDateFrom"; caption: qsTr("Начало · ГГГГ-ММ-ДД ЧЧ:ММ:СС") }
            Field { id: toField; objectName: "trendDateTo"; caption: qsTr("Конец · ГГГГ-ММ-ДД ЧЧ:ММ:СС") }
            LabelValue { id: invalid; visible: false; mode: "error"; text: qsTr("Проверьте период и даты.") }
            RowLayout {
                Item { Layout.fillWidth: true }
                ActionButton { text: qsTr("Отмена"); onClicked: rangeDialog.close() }
                ActionButton {
                    objectName: "trendApplyInterval"
                    text: qsTr("Показать интервал"); highlighted: true
                    onClicked: {
                        const from = rangeDialog.parse(fromField.text), to = rangeDialog.parse(toField.text)
                        if (!isFinite(from) || !isFinite(to) || to <= from) { invalid.visible = true; return }
                        chart.setRange(from, to)
                        rangeDialog.close()
                    }
                }
            }
        }
    }
}
