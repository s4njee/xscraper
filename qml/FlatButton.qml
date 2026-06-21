import QtQuick
import QtQuick.Controls.Basic
import "."

Button {
    id: root

    property color fill: Theme.bgTertiary
    property color hoverFill: Theme.borderStrong
    property color textColor: Theme.textPrimary

    implicitHeight: 34
    padding: 10
    enabled: !controller.running || text === "Cancel"

    contentItem: Text {
        text: root.text
        color: root.enabled ? root.textColor : Theme.textTertiary
        font.pixelSize: 13
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    background: Rectangle {
        color: root.down ? Theme.border : (root.hovered ? root.hoverFill : root.fill)
        radius: Theme.radius
        border.color: Theme.borderStrong
    }
}
