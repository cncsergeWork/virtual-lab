import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

Item {
    id: page
    objectName: "archivePage"
    required property var api
    property string seenProject: ""
    property var selected: []
    readonly property var numericalTags: api.tags.filter(tag => tag.address?.type !== "text")
    readonly property var showing: numericalTags.filter(tag => selected.includes(tag.id))
    readonly property var tagIds: showing.map(tag => tag.id)
    readonly property var paletteColors: ["#087970", "#3368bc", "#b45726", "#8546ac", "#ab3563", "#6a7527",
                                          "#39758a", "#97472e", "#588743", "#5742b3", "#925d19", "#9a347e"]
    property bool combined: false
    property bool follow: true
    property var range: [Date.now() - 60000, Date.now()]
    readonly property double startTime: range[0]
    readonly property double endTime: range[1]
    readonly property double span: endTime - startTime
    property double cursor: 0
    property bool pinned: false
    property double markA: 0
    property double markB: 0
    property var snapshot: ({})
    property string exportTag
    property var exportRange

    function syncProject() {
        if (seenProject !== api.projectId) {
            seenProject = api.projectId
            selected = api.tags.filter(tag => tag.archiveMs > 0).slice(0, 3).map(tag => tag.id)
            search.clear()
            follow = true
            const now = Date.now()
            range = [now - 60000, now]
            cursor = 0
            pinned = false
            markA = 0
            markB = 0
            snapshot = ({})
        } else {
            const next = selected.filter(id => api.tags.some(tag => tag.id === id))
            if (next.length !== selected.length) selected = next
        }
    }
    function toggle(id) {
        selected = selected.includes(id) ? selected.filter(tag => tag !== id) : selected.concat([id])
    }
    function colorFor(tag) {
        const index = Math.max(0, api.tags.findIndex(entry => entry.id === tag))
        return index < paletteColors.length ? paletteColors[index] :
            Qt.hsla((index * 0.61803398875) % 1, 0.68, index % 2 ? 0.36 : 0.45, 1)
    }
    function timeText(t) { return Qt.formatDateTime(new Date(t), "dd.MM.yyyy HH:mm:ss.zzz") }
    function displayValue(reading, units) {
        if (!reading || !reading.hasValue) {
            if (reading?.quality === "stale") return qsTr("Устарело")
            if (reading?.quality === "error") return qsTr("Ошибка")
            return "—"
        }
        const value = Number(reading.value)
        const suffix = reading.quality === "stale" ? qsTr(" · устарело") : reading.quality === "error" ? qsTr(" · ошибка") : ""
        return (Math.abs(value) >= 10000000 ? value.toExponential(3) : Number(value.toPrecision(7)).toLocaleString(Qt.locale(), "g", 7))
            + (units ? " " + units : "") + suffix
    }
    function refreshCursor() {
        snapshot = cursor && visible && tagIds.length ? api.archiveSamples(tagIds, Math.round(cursor)) : ({})
    }
    function selectCursor(time) {
        cursor = Math.round(time)
        if (!cursorDelay.running) cursorDelay.start()
    }
    function setRange(from, to) {
        if (to <= from) return
        follow = false
        range = [from, to]
    }
    function zoom(ratio, portion) {
        const next = Math.max(500, Math.min(Math.max(span, 366 * 86400000), span * ratio))
        const pivot = startTime + span * portion
        setRange(pivot - next * portion, pivot + next * (1 - portion))
    }
    function shift(amount) { setRange(startTime + span * amount, endTime + span * amount) }
    function tickNow() {
        const now = Date.now()
        range = [now - span, now]
    }
    function showPeriod(ms) {
        if (follow) {
            const now = Date.now()
            range = [now - ms, now]
        } else {
            const middle = cursor >= startTime && cursor <= endTime ? cursor : (startTime + endTime) / 2
            setRange(middle - ms / 2, middle + ms / 2)
        }
    }
    function fitAll() {
        const bounds = api.archiveBounds(tagIds)
        if (bounds.from === undefined) return
        const padding = Math.max(250, (bounds.to - bounds.from) * 0.025)
        setRange(bounds.from - padding, bounds.to + padding)
    }
    function clearCursor() {
        pinned = false
        cursor = 0
        snapshot = ({})
    }

    onTagIdsChanged: if (visible && cursor && cursorDelay) cursorDelay.restart()
    onVisibleChanged: if (visible) {
        if (follow) tickNow()
        else range = [startTime, endTime]
        refreshCursor()
    }
    Component.onCompleted: syncProject()
    Connections {
        target: page.api
        function onProjectChanged() { page.syncProject() }
        function onArchiveChanged() {
            if (page.visible && page.follow && page.cursor && !cursorDelay.running) cursorDelay.start()
        }
    }
    Timer { id: cursorDelay; interval: 35; onTriggered: page.refreshCursor() }
    Timer { interval: 500; running: page.visible && page.follow && page.api.running; repeat: true; onTriggered: page.tickNow() }

    RowLayout {
        anchors.fill: parent
        spacing: 12
        Rectangle {
            Layout.preferredWidth: page.width < 1120 ? 237 : 282
            Layout.fillHeight: true
            color: "#ffffff"; radius: 8; border.color: "#d7e0e8"
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 12; spacing: 7
                LabelValue { text: qsTr("Каналы"); mode: "strong"; font.pixelSize: 17; Layout.fillWidth: true }
                TextField { id: search; objectName: "archiveSearch"; Layout.fillWidth: true; placeholderText: qsTr("Найти тег или источник") }
                RowLayout {
                    Layout.fillWidth: true
                    ActionButton {
                        objectName: "archiveAllChannels"
                        text: qsTr("Добавить все")
                        enabled: list.model.length > 0
                        Layout.fillWidth: true
                        onClicked: page.selected = page.selected.concat(list.model.filter(tag => !page.selected.includes(tag.id)).map(tag => tag.id))
                    }
                    ActionButton {
                        objectName: "archiveClearChannels"
                        text: qsTr("Очистить")
                        enabled: page.selected.length > 0
                        onClicked: page.selected = []
                    }
                }
                LabelValue {
                    text: page.cursor ? page.timeText(page.cursor) : qsTr("Наведите курсор на график")
                    mode: page.cursor ? "strong" : "muted"; Layout.fillWidth: true
                }
                ListView {
                    id: list
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true
                    spacing: 5
                    model: {
                        const filter = search.text.toLowerCase().trim()
                        if (!filter) return page.numericalTags
                        const sources = page.api.sources.filter(source => source.name.toLowerCase().includes(filter)).map(source => source.id)
                        return page.numericalTags.filter(tag => tag.name.toLowerCase().includes(filter) || sources.includes(tag.source))
                    }
                    delegate: Rectangle {
                        id: row
                        required property var modelData
                        readonly property var live: page.api.readings[modelData.id] || {}
                        readonly property var archived: page.snapshot[modelData.id] || {}
                        readonly property bool shown: page.selected.includes(modelData.id)
                        width: ListView.view.width; height: shown && page.cursor ? 66 : 48
                        radius: 5; color: shown ? "#f0f6f7" : "#f7f9fa"
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 5; anchors.leftMargin: 7; spacing: 0
                            CheckBox {
                                objectName: "archiveChannel_" + row.modelData.id
                                Layout.fillWidth: true; Layout.preferredHeight: 22
                                leftPadding: 2
                                checked: row.shown
                                text: row.modelData.name
                                font.pixelSize: 13
                                font.weight: row.shown ? Font.DemiBold : Font.Normal
                                palette.windowText: page.colorFor(row.modelData.id)
                                onToggled: page.toggle(row.modelData.id)
                                ToolTip.visible: hovered && row.modelData.archiveMs === 0
                                ToolTip.text: qsTr("Архивирование выключено. Сохранённые ранее записи доступны.")
                            }
                            LabelValue {
                                text: qsTr("Сейчас  ") + page.displayValue(row.live, row.modelData.units)
                                mode: "muted"; Layout.fillWidth: true; Layout.leftMargin: 22; font.pixelSize: 11
                            }
                            LabelValue {
                                objectName: "archiveCursorValue_" + row.modelData.id
                                visible: row.shown && !!page.cursor
                                text: qsTr("Курсор  ") + page.displayValue(row.archived, row.modelData.units)
                                mode: "strong"; Layout.fillWidth: true; Layout.leftMargin: 22; font.pixelSize: 12
                                ToolTip.visible: archivedHover.hovered && !!row.archived.time
                                ToolTip.text: row.archived.time ? qsTr("Записано ") + page.timeText(row.archived.time) : ""
                                HoverHandler { id: archivedHover }
                            }
                        }
                    }
                    ScrollBar.vertical: ScrollBar {}
                }
            }
        }
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            color: "#ffffff"; radius: 8; border.color: "#d7e0e8"
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 13; spacing: 8
                RowLayout {
                    Layout.fillWidth: true; spacing: 5
                    ActionButton {
                        objectName: "archiveLive"
                        text: qsTr("До сейчас"); highlighted: page.follow
                        onClicked: { page.follow = true; page.tickNow(); page.clearCursor() }
                    }
                    ComboBox {
                        objectName: "archivePeriod"
                        model: [{ text: qsTr("10 сек"), ms: 10000 }, { text: qsTr("1 мин"), ms: 60000 }, { text: qsTr("5 мин"), ms: 300000 },
                                { text: qsTr("1 час"), ms: 3600000 }, { text: qsTr("24 часа"), ms: 86400000 }, { text: qsTr("7 дней"), ms: 604800000 }]
                        currentIndex: 1
                        textRole: "text"
                        valueRole: "ms"
                        onActivated: page.showPeriod(currentValue)
                    }
                    ActionButton { objectName: "archiveFit"; text: qsTr("Весь архив"); enabled: page.tagIds.length > 0; onClicked: page.fitAll() }
                    ActionButton { objectName: "archiveZoomIn"; text: "+"; onClicked: page.zoom(0.65, 0.5); ToolTip.visible: hovered; ToolTip.text: qsTr("Приблизить") }
                    ActionButton { objectName: "archiveZoomOut"; text: "−"; onClicked: page.zoom(1.54, 0.5); ToolTip.visible: hovered; ToolTip.text: qsTr("Отдалить") }
                    ActionButton { objectName: "archiveEarlier"; text: "‹"; onClicked: page.shift(-0.5); ToolTip.visible: hovered; ToolTip.text: qsTr("Раньше") }
                    ActionButton { objectName: "archiveLater"; text: "›"; onClicked: page.shift(0.5); ToolTip.visible: hovered; ToolTip.text: qsTr("Позже") }
                    Item { Layout.fillWidth: true }
                    ActionButton { objectName: "archiveInterval"; text: qsTr("Интервал…"); onClicked: periodDialog.edit() }
                    ActionButton { objectName: "archiveExport"; text: "CSV…"; enabled: page.tagIds.length > 0; onClicked: exportDialog.open() }
                }
                RowLayout {
                    Layout.fillWidth: true
                    LabelValue {
                        text: Qt.formatDateTime(new Date(page.startTime), "dd.MM HH:mm:ss") + "  —  "
                              + Qt.formatDateTime(new Date(page.endTime), "dd.MM HH:mm:ss")
                        mode: "strong"; Layout.fillWidth: true
                    }
                    ActionButton {
                        objectName: "archiveSeparate"
                        text: qsTr("Раздельно")
                        highlighted: !page.combined
                        onClicked: page.combined = false
                    }
                    ActionButton {
                        objectName: "archiveCombined"
                        text: qsTr("Сводный")
                        highlighted: page.combined
                        onClicked: page.combined = true
                    }
                    ActionButton {
                        objectName: "archiveMarkA"
                        text: "A"; enabled: page.cursor > 0
                        onClicked: page.markA = page.cursor
                        ToolTip.visible: hovered; ToolTip.text: qsTr("Поставить метку A")
                    }
                    ActionButton {
                        objectName: "archiveMarkB"
                        text: "B"; enabled: page.cursor > 0
                        onClicked: page.markB = page.cursor
                        ToolTip.visible: hovered; ToolTip.text: qsTr("Поставить метку B")
                    }
                    LabelValue {
                        visible: page.markA && page.markB
                        text: "Δ " + (Math.abs(page.markB - page.markA) / 1000).toLocaleString(Qt.locale(), "f", 3) + qsTr(" с")
                        mode: "strong"
                    }
                    ActionButton {
                        visible: page.markA || page.markB
                        objectName: "archiveClearMarks"
                        text: "×"; onClicked: { page.markA = 0; page.markB = 0 }
                        ToolTip.visible: hovered; ToolTip.text: qsTr("Сбросить метки")
                    }
                }
                StackLayout {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    currentIndex: page.combined ? 1 : 0
                    ListView {
                        id: tracks
                        objectName: "archiveTracks"
                        Layout.fillWidth: true; Layout.fillHeight: true
                        spacing: 5
                        clip: true
                        cacheBuffer: 0
                        model: page.showing
                        delegate: Rectangle {
                            id: track
                            required property var modelData
                            width: ListView.view.width
                            height: Math.max(118, Math.min(235, (tracks.height - 5 * (tracks.count - 1)) / Math.max(1, tracks.count)))
                            radius: 6
                            color: "#fbfdfe"
                            border.color: "#e1e9ef"
                            ColumnLayout {
                                anchors.fill: parent; anchors.margins: 6; spacing: 0
                                LabelValue {
                                    Layout.leftMargin: 6; Layout.fillWidth: true
                                    text: track.modelData.name + (track.modelData.units ? "  ·  " + track.modelData.units : "")
                                    color: page.colorFor(track.modelData.id); font.weight: Font.DemiBold
                                }
                                ArchivePlot {
                                    Layout.fillWidth: true; Layout.fillHeight: true
                                    navigator: page
                                    gestureName: "archivePlot_" + track.modelData.id
                                    tag: track.modelData.id
                                    lineColor: page.colorFor(track.modelData.id)
                                    stepped: track.modelData.address?.type === "bool"
                                }
                            }
                        }
                        LabelValue {
                            anchors.centerIn: parent
                            visible: tracks.count === 0
                            text: list.model.length || page.api.tags.length ? qsTr("Выберите теги слева") : qsTr("В проекте ещё нет тегов")
                            mode: "muted"
                        }
                        ScrollBar.vertical: ScrollBar {}
                    }
                    Rectangle {
                        radius: 6
                        color: "#fbfdfe"
                        border.color: "#e1e9ef"
                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 6
                            spacing: 0
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.leftMargin: 6
                                Layout.rightMargin: 6
                                LabelValue {
                                    Layout.fillWidth: true
                                    text: qsTr("Сводный график · ") + page.tagIds.length
                                    mode: "strong"
                                }
                                ComboBox {
                                    id: scaleMode
                                    objectName: "archiveScaleMode"
                                    Layout.preferredWidth: 172
                                    model: [qsTr("Общая шкала"), qsTr("0–100% по каналу")]
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("В режиме 0–100% минимум и максимум берутся из видимого интервала каждого канала.")
                                }
                            }
                            ArchivePlot {
                                objectName: "archiveCombinedChart"
                                gestureName: "archiveCombinedPlot"
                                navigator: page
                                Layout.fillWidth: true; Layout.fillHeight: true
                                seriesDefinitions: page.combined ? page.showing.map(channel => ({
                                    id: channel.id, color: page.colorFor(channel.id), stepped: channel.address?.type === "bool"
                                })) : []
                                normalized: scaleMode.currentIndex === 1
                            }
                        }
                    }
                }
                Item {
                    Layout.fillWidth: true; Layout.preferredHeight: 17
                    visible: tracks.count > 0
                    Repeater {
                        model: 5
                        delegate: LabelValue {
                            required property int index
                            x: Math.min(parent.width - width, Math.max(0, 61 + (parent.width - 87) * index / 4 - width / 2))
                            text: Qt.formatDateTime(new Date(page.startTime + page.span * index / 4),
                                  page.span >= 86400000 ? "dd.MM HH:mm" : page.span > 600000 ? "HH:mm" :
                                  page.span < 10000 ? "HH:mm:ss.zzz" : "HH:mm:ss")
                            mode: "muted"; font.pixelSize: 11
                        }
                    }
                }
                LabelValue {
                    text: page.pinned ? qsTr("Курсор закреплён. Щёлкните по графику, чтобы освободить.") :
                        qsTr("Колесо — масштаб · перетаскивание — сдвиг времени · щелчок — закрепить курсор")
                    mode: "muted"; Layout.fillWidth: true; font.pixelSize: 11
                }
            }
        }
    }

    Dialog {
        id: periodDialog
        parent: Overlay.overlay
        anchors.centerIn: parent; modal: true; width: 390
        title: qsTr("Интервал архива")
        function edit() {
            dateFrom.text = Qt.formatDateTime(new Date(page.startTime), "yyyy-MM-dd HH:mm:ss")
            dateTo.text = Qt.formatDateTime(new Date(page.endTime), "yyyy-MM-dd HH:mm:ss")
            badDate.visible = false
            open()
        }
        function parse(text) {
            const input = text.trim()
            const date = new Date(input.replace(" ", "T"))
            return isFinite(date.getTime()) && Qt.formatDateTime(date, "yyyy-MM-dd HH:mm:ss") === input ? date.getTime() : NaN
        }
        contentItem: ColumnLayout {
            spacing: 9
            Field { id: dateFrom; objectName: "archiveDateFrom"; caption: qsTr("Начало · ГГГГ-ММ-ДД ЧЧ:ММ:СС") }
            Field { id: dateTo; objectName: "archiveDateTo"; caption: qsTr("Конец · ГГГГ-ММ-ДД ЧЧ:ММ:СС") }
            LabelValue { id: badDate; objectName: "archiveInvalidDates"; visible: false; text: qsTr("Проверьте обе даты и порядок интервала."); mode: "error" }
            RowLayout {
                Item { Layout.fillWidth: true }
                ActionButton { text: qsTr("Отмена"); onClicked: periodDialog.close() }
                ActionButton {
                    objectName: "archiveDateApply"
                    text: qsTr("Показать"); highlighted: true
                    onClicked: {
                        const from = periodDialog.parse(dateFrom.text), to = periodDialog.parse(dateTo.text)
                        if (!isFinite(from) || !isFinite(to) || to <= from) { badDate.visible = true; return }
                        page.setRange(from, to)
                        periodDialog.close()
                    }
                }
            }
        }
    }
    Dialog {
        id: exportDialog
        parent: Overlay.overlay
        anchors.centerIn: parent; modal: true; width: 360
        title: qsTr("Экспорт видимого интервала CSV")
        contentItem: ColumnLayout {
            spacing: 10
            ComboBox { id: exportChannel; Layout.fillWidth: true; model: page.showing; textRole: "name"; valueRole: "id" }
            RowLayout {
                Item { Layout.fillWidth: true }
                ActionButton { text: qsTr("Отмена"); onClicked: exportDialog.close() }
                ActionButton {
                    objectName: "archiveExportApply"
                    text: qsTr("Сохранить…"); highlighted: true; enabled: !!exportChannel.currentValue
                    onClicked: {
                        page.exportTag = exportChannel.currentValue
                        page.exportRange = page.range.slice()
                        exportDialog.close()
                        csv.open()
                    }
                }
            }
        }
    }
    FileDialog {
        id: csv
        objectName: "archiveCsvPicker"
        title: qsTr("Сохранить архив CSV")
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Таблица CSV (*.csv)")]
        onAccepted: page.api.exportCsv(selectedFile, page.exportTag, Math.round(page.exportRange[0]), Math.round(page.exportRange[1]))
    }
}
