import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtQuick.Dialogs
import QtCore
import XscraperUi

ApplicationWindow {
    id: root
    width: 960
    height: 620
    minimumWidth: 760
    minimumHeight: 500
    visible: true
    title: qsTr("Xscraper")
    background: Rectangle {
        color: Theme.bgPrimary
    }

    property string mode: "archive"
    property bool archiveMode: mode === "archive"
    property bool apiMode: mode === "api"
    property bool processMode: mode === "process"
    property string outputDirectory: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
    property string archiveDirectory: ""
    property string exportJsonPath: ""

    FolderDialog {
        id: outputDialog
        title: "Select Export Folder"
        onAccepted: {
            var p = selectedFolder.toString()
            if (p.startsWith("file://")) p = p.substring(7)
            root.outputDirectory = p
        }
    }

    FolderDialog {
        id: archiveDialog
        title: "Select Extracted X Archive Folder"
        onAccepted: {
            var p = selectedFolder.toString()
            if (p.startsWith("file://")) p = p.substring(7)
            root.archiveDirectory = p
        }
    }

    FileDialog {
        id: exportJsonDialog
        title: "Select Xscraper JSON Export"
        nameFilters: ["JSON files (*.json)", "All files (*)"]
        onAccepted: {
            var p = selectedFile.toString()
            if (p.startsWith("file://")) p = p.substring(7)
            root.exportJsonPath = decodeURIComponent(p)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 58
            color: Theme.bgPrimary
            border.color: Theme.border

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 22
                anchors.rightMargin: 22
                spacing: 12

                Text {
                    text: "Xscraper"
                    color: Theme.textPrimary
                    font.pixelSize: 20
                    font.weight: Font.Bold
                }

                Chip {
                    text: root.archiveMode ? "Archive import" : (root.apiMode ? "X API v2" : "Download Media")
                    tone: Theme.accent
                }

                Item { Layout.fillWidth: true }

                Text {
                    text: controller.status
                    color: controller.error.length > 0 ? Theme.danger : Theme.textSecondary
                    font.pixelSize: 13
                    elide: Text.ElideRight
                    Layout.maximumWidth: 420
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.preferredWidth: 250
                Layout.fillHeight: true
                color: Theme.bgSecondary
                border.color: Theme.border

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 14

                    Text {
                        text: "EXPORT"
                        color: Theme.textTertiary
                        font.pixelSize: 11
                        font.weight: Font.Bold
                    }

                    Text {
                        text: root.archiveMode
                              ? "Import posts, links, and embedded media references from an extracted X archive folder."
                              : (root.apiMode
                                 ? "Collect a user's public posts, URL entities, expanded link metadata when X provides it, and embedded media references."
                                 : "Read an Xscraper JSON export, download direct image/video media, and write a pretty posts.txt and posts.html.")
                        color: Theme.textSecondary
                        wrapMode: Text.WordWrap
                        font.pixelSize: 13
                        lineHeight: 1.15
                        Layout.fillWidth: true
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: Theme.border
                    }

                    Text {
                        text: root.processMode ? "Posts processed" : (root.archiveMode ? "Posts imported" : "Posts fetched")
                        color: Theme.textTertiary
                        font.pixelSize: 12
                    }

                    Text {
                        text: String(controller.postsCount)
                        color: Theme.textPrimary
                        font.pixelSize: 42
                        font.weight: Font.Bold
                    }

                    Item { Layout.fillHeight: true }

                    Text {
                        text: root.archiveMode
                              ? "Free local mode. Download your X archive, unzip it, then select the extracted folder."
                              : (root.apiMode
                                 ? "Uses official API pagination. Access level, protected accounts, deleted posts, and rate limits are controlled by X."
                                 : "Downloads direct media URLs already present in the export; it does not crawl linked websites.")
                        color: Theme.textTertiary
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12
                        lineHeight: 1.15
                        Layout.fillWidth: true
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: Theme.bgPrimary

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 14

                    Text {
                        text: root.archiveMode ? "X archive import" : (root.apiMode ? "User timeline scrape" : "Download Media")
                        color: Theme.textPrimary
                        font.pixelSize: 24
                        font.weight: Font.Bold
                    }

                    Text {
                        text: root.archiveMode
                              ? "Select the folder created when you unzip your X data archive. Xscraper reads the local archive files and writes a normalized JSON export."
                              : (root.apiMode
                                 ? "Enter a username and bearer token from an X developer app. The export is written as JSON so you can process it later without losing raw API data."
                                 : "Select an Xscraper JSON export. The app writes posts.txt, an X-style posts.html, and downloads images/videos into a media folder.")
                        color: Theme.textSecondary
                        wrapMode: Text.WordWrap
                        font.pixelSize: 13
                        Layout.fillWidth: true
                    }

                    RowLayout {
                        spacing: 8

                        FlatButton {
                            text: "Archive import"
                            fill: root.archiveMode ? Theme.accent : Theme.bgTertiary
                            hoverFill: root.archiveMode ? Theme.accentHover : Theme.borderStrong
                            textColor: root.archiveMode ? Theme.bgPrimary : Theme.textPrimary
                            onClicked: root.mode = "archive"
                        }

                        FlatButton {
                            text: "API token"
                            fill: root.apiMode ? Theme.accent : Theme.bgTertiary
                            hoverFill: root.apiMode ? Theme.accentHover : Theme.borderStrong
                            textColor: root.apiMode ? Theme.bgPrimary : Theme.textPrimary
                            onClicked: root.mode = "api"
                        }

                        FlatButton {
                            text: "Download Media"
                            fill: root.processMode ? Theme.accent : Theme.bgTertiary
                            hoverFill: root.processMode ? Theme.accentHover : Theme.borderStrong
                            textColor: root.processMode ? Theme.bgPrimary : Theme.textPrimary
                            onClicked: root.mode = "process"
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 14
                        rowSpacing: 8

                        Text {
                            text: "Username"
                            color: Theme.textSecondary
                            font.pixelSize: 13
                            visible: root.apiMode
                            Layout.preferredHeight: visible ? implicitHeight : 0
                        }
                        Field {
                            id: usernameField
                            Layout.fillWidth: true
                            placeholderText: "@username"
                            visible: root.apiMode
                            Layout.preferredHeight: visible ? implicitHeight : 0
                        }

                        Text {
                            text: "Archive folder"
                            color: Theme.textSecondary
                            font.pixelSize: 13
                            visible: root.archiveMode
                            Layout.preferredHeight: visible ? implicitHeight : 0
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: root.archiveMode
                            Layout.preferredHeight: visible ? implicitHeight : 0
                            spacing: 8

                            Field {
                                text: root.archiveDirectory
                                readOnly: true
                                Layout.fillWidth: true
                                placeholderText: "Extracted archive folder"
                            }

                            FlatButton {
                                text: "Browse"
                                onClicked: archiveDialog.open()
                            }
                        }

                        Text {
                            text: "Bearer token"
                            color: Theme.textSecondary
                            font.pixelSize: 13
                            visible: root.apiMode
                            Layout.preferredHeight: visible ? implicitHeight : 0
                        }
                        Field {
                            id: tokenField
                            Layout.fillWidth: true
                            echoMode: TextInput.Password
                            placeholderText: "X API bearer token"
                            visible: root.apiMode
                            Layout.preferredHeight: visible ? implicitHeight : 0
                        }

                        Text {
                            text: "Export JSON"
                            color: Theme.textSecondary
                            font.pixelSize: 13
                            visible: root.processMode
                            Layout.preferredHeight: visible ? implicitHeight : 0
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            visible: root.processMode
                            Layout.preferredHeight: visible ? implicitHeight : 0
                            spacing: 8

                            Field {
                                text: root.exportJsonPath
                                readOnly: true
                                Layout.fillWidth: true
                                placeholderText: "Xscraper export JSON"
                            }

                            FlatButton {
                                text: "Browse"
                                onClicked: exportJsonDialog.open()
                            }
                        }

                        Text { text: "Output folder"; color: Theme.textSecondary; font.pixelSize: 13 }
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Field {
                                text: root.outputDirectory
                                readOnly: true
                                Layout.fillWidth: true
                            }

                            FlatButton {
                                text: "Browse"
                                onClicked: outputDialog.open()
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 18
                        visible: !root.processMode
                        Layout.preferredHeight: visible ? implicitHeight : 0

                        CheckBox {
                            id: repliesBox
                            checked: false
                            text: "Exclude replies"
                            indicator: Rectangle {
                                implicitWidth: 14
                                implicitHeight: 14
                                x: repliesBox.leftPadding
                                y: parent.height / 2 - height / 2
                                radius: 3
                                color: repliesBox.checked ? Theme.accent : Theme.bgTertiary
                                border.color: repliesBox.checked ? Theme.accent : Theme.border
                                Rectangle {
                                    width: 8; height: 8
                                    anchors.centerIn: parent
                                    radius: 1
                                    color: Theme.bgPrimary
                                    visible: repliesBox.checked
                                }
                            }
                            contentItem: Text {
                                text: repliesBox.text
                                color: Theme.textSecondary
                                font.pixelSize: 13
                                leftPadding: repliesBox.indicator.width + 8
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        CheckBox {
                            id: repostsBox
                            checked: false
                            text: "Exclude reposts"
                            indicator: Rectangle {
                                implicitWidth: 14
                                implicitHeight: 14
                                x: repostsBox.leftPadding
                                y: parent.height / 2 - height / 2
                                radius: 3
                                color: repostsBox.checked ? Theme.accent : Theme.bgTertiary
                                border.color: repostsBox.checked ? Theme.accent : Theme.border
                                Rectangle {
                                    width: 8; height: 8
                                    anchors.centerIn: parent
                                    radius: 1
                                    color: Theme.bgPrimary
                                    visible: repostsBox.checked
                                }
                            }
                            contentItem: Text {
                                text: repostsBox.text
                                color: Theme.textSecondary
                                font.pixelSize: 13
                                leftPadding: repostsBox.indicator.width + 8
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        Item { Layout.fillWidth: true }
                    }

                    RowLayout {
                        spacing: 10

                        FlatButton {
                            text: controller.running ? "Running" : (root.archiveMode ? "Import archive" : (root.apiMode ? "Start export" : "Download media"))
                            fill: Theme.accent
                            hoverFill: Theme.accentHover
                            textColor: Theme.bgPrimary
                            enabled: !controller.running
                            onClicked: {
                                if (root.archiveMode)
                                    controller.importArchive(root.archiveDirectory, root.outputDirectory,
                                                             repliesBox.checked, repostsBox.checked)
                                else if (root.apiMode)
                                    controller.start(usernameField.text, tokenField.text, root.outputDirectory,
                                                     repliesBox.checked, repostsBox.checked)
                                else
                                    controller.processExport(root.exportJsonPath, root.outputDirectory)
                            }
                        }

                        FlatButton {
                            text: "Cancel"
                            visible: controller.running
                            onClicked: controller.cancel()
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: root.processMode && (controller.running || controller.mediaTotal > 0) ? 96 : 0
                        visible: Layout.preferredHeight > 0
                        radius: Theme.radius
                        color: Theme.bgSecondary
                        border.color: Theme.border

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 7

                            RowLayout {
                                Layout.fillWidth: true

                                Text {
                                    text: "MEDIA DOWNLOADS"
                                    color: Theme.textTertiary
                                    font.pixelSize: 12
                                    font.weight: Font.Bold
                                }

                                Item { Layout.fillWidth: true }

                                Text {
                                    text: controller.mediaCompleted + "/" + controller.mediaTotal
                                          + (controller.mediaFailed > 0 ? " failed " + controller.mediaFailed : "")
                                    color: controller.mediaFailed > 0 ? Theme.warning : Theme.textSecondary
                                    font.pixelSize: 12
                                    font.family: Theme.mono
                                }
                            }

                            ProgressBar {
                                Layout.fillWidth: true
                                from: 0
                                to: Math.max(1, controller.mediaTotal)
                                value: controller.mediaCompleted + controller.mediaFailed
                            }

                            Text {
                                text: controller.currentMedia.length > 0 ? controller.currentMedia : "Waiting for media URLs..."
                                color: Theme.textSecondary
                                font.pixelSize: 12
                                font.family: Theme.mono
                                elide: Text.ElideMiddle
                                Layout.fillWidth: true
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 150
                        radius: Theme.radius
                        color: Theme.bgSecondary
                        border.color: Theme.border

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 14
                            spacing: 8

                            Text {
                                text: "Latest output"
                                color: Theme.textTertiary
                                font.pixelSize: 12
                                font.weight: Font.Bold
                            }

                            Text {
                                text: controller.outputPath.length > 0 ? controller.outputPath : "No export written yet."
                                color: controller.outputPath.length > 0 ? Theme.textPrimary : Theme.textTertiary
                                font.family: Theme.mono
                                font.pixelSize: 12
                                wrapMode: Text.WrapAnywhere
                                Layout.fillWidth: true
                            }

                            Text {
                                text: controller.error
                                visible: controller.error.length > 0
                                color: Theme.danger
                                font.pixelSize: 13
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }

                            Item { Layout.fillHeight: true }
                        }
                    }

                    Item { Layout.fillHeight: true }
                }
            }
        }
    }
}
