import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs as Native
import QtQuick.Layouts

Dialog {
    id: dialog
    objectName: "trendDialog"
    required property var api
    readonly property var choices: [{ id: "", name: "Выбрать тег" }].concat(api.editorTags.filter(t => t.address?.type !== "text"))
    readonly property var accents: ["#008975", "#bd6200", "#ae3e70", "#3368bc", "#8546ac", "#64772b", "#39758a", "#97472e"]
    readonly property var styles: [{ name: "Авто", id: "auto" }, { name: "Линия", id: "line" },
                                    { name: "Ступени", id: "step" }, { name: "Точки", id: "points" }]
    readonly property var timeModes: [{ name: "Автоматически", id: "auto" }, { name: "От конца периода", id: "relative" },
                                      { name: "Время записи", id: "clock" }]
    property var colorTarget: null
    title: "Настройка графика"
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(910, parent ? parent.width - 32 : 910)

    ListModel { id: rows }
    function append(data) {
        rows.append({ tagId: data.tag || "", tint: data.color || accents[rows.count % accents.length],
            side: data.side || (rows.count ? "right" : "left"), style: data.style || "auto", divisions: data.divisions ?? 5,
            low: data.min === undefined ? "" : String(data.min), high: data.max === undefined ? "" : String(data.max) })
    }
    function edit(item) {
        const options = item.trend || {}
        seconds.text = String(options.seconds ?? 300)
        axes.currentIndex = Math.max(0, axes.indexOfValue(options.axis || "individual"))
        timeLabels.currentIndex = Math.max(0, dialog.timeModes.findIndex(mode => mode.id === (options.timeLabels ?? "auto")))
        legend.checked = options.legend ?? true
        legendValues.checked = options.legendValues ?? false
        toolbar.checked = options.toolbar ?? true
        rows.clear()
        for (const series of options.series || []) append(series)
        if (!rows.count) append({ tag: item.tag })
        open()
    }
    function apply() {
        const series = []
        for (let index = 0; index < rows.count; ++index) {
            const row = rows.get(index)
            const data = { tag: row.tagId, color: row.tint.trim(), side: row.side, style: row.style, divisions: row.divisions }
            if (row.low.trim() || row.high.trim()) {
                data.min = row.low.trim() ? Number(row.low.replace(",", ".")) : NaN
                data.max = row.high.trim() ? Number(row.high.replace(",", ".")) : NaN
            }
            series.push(data)
        }
        if (api.configureWidget({ tag: "", trend: { seconds: Number(seconds.text.trim()), axis: axes.currentValue,
            timeLabels: timeLabels.currentValue, legend: legend.checked, legendValues: legendValues.checked,
            toolbar: toolbar.checked, series: series } })) close()
    }
    Native.ColorDialog {
        id: colorPicker
        title: "Цвет ряда"
        onAccepted: {
            const color = selectedColor
            const hex = channel => Math.round(channel * 255).toString(16).padStart(2, "0")
            if (dialog.colorTarget) dialog.colorTarget.setColor("#" + hex(color.r) + hex(color.g) + hex(color.b))
            dialog.colorTarget = null
        }
        onRejected: dialog.colorTarget = null
    }

    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Field { id: seconds; caption: "Период по умолчанию, сек"; Layout.preferredWidth: 186 }
            ColumnLayout {
                Layout.fillWidth: true
                LabelValue { text: "Вертикальные шкалы"; mode: "muted" }
                ComboBox {
                    id: axes
                    objectName: "trendAxisMode"
                    Layout.fillWidth: true
                    textRole: "name"; valueRole: "id"
                    model: [{ name: "Общая", id: "shared" }, { name: "Левая и правая", id: "dual" },
                            { name: "Отдельная для каждого ряда", id: "individual" }]
                }
            }
            ColumnLayout {
                Layout.preferredWidth: 173
                LabelValue { text: "Подписи времени"; mode: "muted" }
                ComboBox {
                    id: timeLabels
                    objectName: "trendTimeLabels"
                    Layout.fillWidth: true
                    textRole: "name"; valueRole: "id"
                    model: dialog.timeModes
                }
            }
        }
        RowLayout {
            CheckBox { id: legend; text: "Легенда" }
            CheckBox { id: legendValues; objectName: "trendLegendValues"; text: "Значения в легенде"; enabled: legend.checked }
            CheckBox { id: toolbar; text: "Меню периода" }
        }
        LabelValue {
            Layout.fillWidth: true
            text: axes.currentValue === "individual" ? "У каждого ряда цветная ось. Пустые границы означают автоматический диапазон." :
                  axes.currentValue === "dual" ? "Назначьте каждому ряду левую или правую ось." : "Все ряды используют общую шкалу."
            mode: "muted"
        }
        RowLayout {
            Layout.fillWidth: true
            LabelValue { text: "Тег"; Layout.fillWidth: true }
            LabelValue { text: "Цвет"; Layout.preferredWidth: 113 }
            LabelValue { text: "Линия"; Layout.preferredWidth: 98 }
            LabelValue { text: "Ось"; visible: axes.currentValue !== "shared"; Layout.preferredWidth: 105 }
            LabelValue { text: "Минимум"; visible: axes.currentValue === "individual"; Layout.preferredWidth: 76 }
            LabelValue { text: "Максимум"; visible: axes.currentValue === "individual"; Layout.preferredWidth: 76 }
            LabelValue { text: "Деления"; visible: axes.currentValue === "individual"; Layout.preferredWidth: 111 }
            Item { Layout.preferredWidth: 32 }
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(300, Math.max(50, rows.count * 44))
            clip: true
            spacing: 4
            model: rows
            delegate: RowLayout {
                id: row
                required property int index
                required property var model
                width: ListView.view.width
                height: 40
                ComboBox {
                    objectName: "trendTag_" + row.index
                    Layout.fillWidth: true
                    model: dialog.choices
                    textRole: "name"; valueRole: "id"
                    currentIndex: Math.max(0, dialog.choices.findIndex(tag => tag.id === row.model.tagId))
                    onActivated: rows.setProperty(row.index, "tagId", currentValue)
                }
                ColorField {
                    Layout.preferredWidth: 113
                    text: row.model.tint
                    caption: "Выбрать цвет ряда"
                    onEdited: value => rows.setProperty(row.index, "tint", value)
                    onChoose: field => {
                        dialog.colorTarget = field
                        colorPicker.selectedColor = /^#[a-fA-F0-9]{6}$/.test(field.text.trim()) ? field.text.trim() : dialog.accents[0]
                        colorPicker.open()
                    }
                }
                ComboBox {
                    objectName: "trendStyle_" + row.index
                    Layout.preferredWidth: 98
                    textRole: "name"; valueRole: "id"
                    model: dialog.styles
                    currentIndex: Math.max(0, dialog.styles.findIndex(style => style.id === row.model.style))
                    onActivated: rows.setProperty(row.index, "style", currentValue)
                }
                ComboBox {
                    objectName: "trendSide_" + row.index
                    visible: axes.currentValue !== "shared"
                    Layout.preferredWidth: 105
                    model: ["Левая", "Правая"]
                    currentIndex: row.model.side === "right" ? 1 : 0
                    onActivated: rows.setProperty(row.index, "side", currentIndex === 1 ? "right" : "left")
                }
                TextField {
                    visible: axes.currentValue === "individual"; Layout.preferredWidth: 76
                    placeholderText: "Авто"; text: row.model.low
                    onTextEdited: rows.setProperty(row.index, "low", text)
                }
                TextField {
                    visible: axes.currentValue === "individual"; Layout.preferredWidth: 76
                    placeholderText: "Авто"; text: row.model.high
                    onTextEdited: rows.setProperty(row.index, "high", text)
                }
                SpinBox {
                    objectName: "trendDivisions_" + row.index
                    visible: axes.currentValue === "individual"
                    Layout.minimumWidth: 111; Layout.preferredWidth: 111
                    from: 2; to: 12
                    value: row.model.divisions
                    onValueModified: rows.setProperty(row.index, "divisions", value)
                }
                ActionButton {
                    Layout.preferredWidth: 32
                    text: "×"
                    objectName: "trendRemoveSeries_" + row.index
                    ToolTip.visible: hovered; ToolTip.text: "Убрать ряд"
                    onClicked: rows.remove(row.index)
                }
            }
            ScrollBar.vertical: ScrollBar {}
        }
        RowLayout {
            Layout.fillWidth: true
            ActionButton { objectName: "trendAddSeries"; text: "+ Ряд"; enabled: rows.count < 32; onClicked: dialog.append({}) }
            Item { Layout.fillWidth: true }
            ActionButton { text: "Отмена"; onClicked: dialog.close() }
            ActionButton { objectName: "trendSave"; text: "Применить"; highlighted: true; onClicked: dialog.apply() }
        }
    }
}
