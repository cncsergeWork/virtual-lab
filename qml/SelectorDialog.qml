import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dialog
    required property var api
    title: qsTr("Положения переключателя")
    parent: Overlay.overlay
    anchors.centerIn: parent
    modal: true
    width: 485
    ListModel { id: rows }

    function edit(positions) {
        rows.clear()
        for (const position of positions || [{ label: qsTr("Стоп"), value: 0 }, { label: qsTr("Ручной"), value: 1 }, { label: qsTr("Авто"), value: 2 }])
            rows.append({ label: position.label, entry: String(position.value) })
        open()
    }

    contentItem: ColumnLayout {
        RowLayout {
            LabelValue { text: qsTr("Название режима"); Layout.fillWidth: true }
            LabelValue { text: qsTr("Значение"); Layout.preferredWidth: 108 }
            Item { Layout.preferredWidth: 36 }
        }
        Repeater {
            model: rows
            delegate: RowLayout {
                id: row
                required property var model
                required property int index
                Layout.fillWidth: true
                TextField {
                    Layout.fillWidth: true
                    maximumLength: 40
                    text: row.model.label
                    onTextEdited: rows.setProperty(row.index, "label", text)
                }
                TextField {
                    Layout.preferredWidth: 108
                    text: row.model.entry
                    onTextEdited: rows.setProperty(row.index, "entry", text)
                }
                ActionButton { objectName: "selectorRemove_" + row.index; text: "×"; enabled: rows.count > 2; onClicked: rows.remove(row.index) }
            }
        }
        RowLayout {
            ActionButton {
                objectName: "selectorAdd"
                text: qsTr("+ Положение"); enabled: rows.count < 6
                onClicked: rows.append({ label: "", entry: "" })
            }
            Item { Layout.fillWidth: true }
            ActionButton { text: qsTr("Отмена"); onClicked: dialog.close() }
            ActionButton {
                objectName: "selectorApply"
                text: qsTr("Применить"); highlighted: true
                onClicked: {
                    const positions = []
                    for (let i = 0; i < rows.count; ++i) {
                        const row = rows.get(i)
                        positions.push({ label: row.label.trim(), value: row.entry.trim() ? Number(row.entry.replace(",", ".")) : NaN })
                    }
                    if (dialog.api.configureWidget({ positions })) dialog.close()
                }
            }
        }
    }
}
