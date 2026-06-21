import QtQuick
import "."

Rectangle {
    id: root

    property alias text: label.text
    property color tone: Theme.info

    implicitWidth: label.implicitWidth + 18
    implicitHeight: 24
    radius: 12
    color: Qt.rgba(tone.r, tone.g, tone.b, 0.12)
    border.color: Qt.rgba(tone.r, tone.g, tone.b, 0.35)

    Text {
        id: label
        anchors.centerIn: parent
        color: root.tone
        font.pixelSize: 12
        font.weight: Font.DemiBold
    }
}
