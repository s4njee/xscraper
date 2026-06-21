import QtQuick
import QtQuick.Controls.Basic
import "."

TextField {
    id: root

    property string label: ""

    implicitHeight: 38
    color: Theme.textPrimary
    placeholderTextColor: Theme.textTertiary
    selectedTextColor: Theme.bgPrimary
    selectionColor: Theme.accent
    font.pixelSize: 14
    leftPadding: 12
    rightPadding: 12

    background: Rectangle {
        radius: Theme.radius
        color: Theme.bgSecondary
        border.color: root.activeFocus ? Theme.accent : Theme.border
    }
}
