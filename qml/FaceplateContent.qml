import QtQuick
import QtQuick.Controls

Item {
    id: content
    readonly property var owner: parent?.instanceWidget ?? null
    readonly property var definition: owner?.faceplateDefinition ?? null
    readonly property var childrenConfigs: definition ? owner.api.resolveFaceplate(owner.config) : []
    readonly property real baseWidth: definition?.width ?? 480
    readonly property real baseHeight: definition?.height ?? 280
    readonly property real factor: Math.max(0.001, Math.min(width / baseWidth, height / baseHeight))

    Item {
        id: faceplateBoard
        width: content.width / content.factor
        height: content.height / content.factor
        scale: content.factor
        transformOrigin: Item.TopLeft
        readonly property real horizontalRatio: width / content.baseWidth
        readonly property real verticalRatio: height / content.baseHeight
        readonly property bool darkBackground: content.owner?.darkSurface ?? false
        readonly property bool faceplateLayer: true
        readonly property string moving: ""
        readonly property real dx: 0
        readonly property real dy: 0
        readonly property int gridStep: 10

        Repeater {
            model: content.childrenConfigs
            delegate: ScreenItem {
                id: child
                required property var modelData
                objectName: "faceplateChild_" + modelData.kind
                api: content.owner.api
                config: modelData
                board: faceplateBoard
                editing: false
                preview: content.owner.editing || content.owner.preview
                snap: false
            }
        }
    }
    Label {
        anchors.fill: parent
        anchors.margins: 8
        visible: content.childrenConfigs.length === 0 && (content.owner?.editing ?? false)
        text: content.definition?.name || qsTr("Фейсплейс")
        color: content.owner?.secondary ?? "#506479"
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
