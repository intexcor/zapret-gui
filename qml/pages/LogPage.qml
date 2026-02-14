import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material

Page {
    id: root

    header: ToolBar {
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16

            Label {
                text: "Log"
                font.pixelSize: 20
                font.bold: true
                Layout.fillWidth: true
            }

            Label {
                text: logModel.count + " entries"
                font.pixelSize: 13
                color: Material.secondaryTextColor
            }

            ToolButton {
                text: "Copy"
                onClicked: {
                    let text = logModel.exportText()
                    // Use clipboard
                    clipboardHelper.text = text
                    clipboardHelper.selectAll()
                    clipboardHelper.copy()
                    copiedLabel.visible = true
                    copiedTimer.restart()
                }
            }

            ToolButton {
                text: "Clear"
                onClicked: logModel.clear()
            }
        }
    }

    // Hidden TextEdit for clipboard
    TextEdit {
        id: clipboardHelper
        visible: false
    }

    Label {
        id: copiedLabel
        visible: false
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 8
        text: "Copied to clipboard!"
        color: "#4CAF50"
        font.bold: true
        z: 1

        Timer {
            id: copiedTimer
            interval: 2000
            onTriggered: copiedLabel.visible = false
        }
    }

    ListView {
        id: logList
        anchors.fill: parent
        anchors.margins: 8
        clip: true
        spacing: 1

        model: logModel

        delegate: Label {
            width: logList.width
            text: model.formatted
            font.family: Qt.platform.os === "osx" ? "Menlo" : "Monospace"
            font.pixelSize: 12
            wrapMode: Text.WrapAnywhere
            color: {
                if (model.message.startsWith("[Engine] Error"))
                    return "#F44336"
                if (model.message.startsWith("[Engine]"))
                    return Material.accentColor
                if (model.message.startsWith("[stderr]"))
                    return "#FF9800"
                return Material.foreground
            }
        }

        // Auto-scroll to bottom
        onCountChanged: {
            if (atYEnd || count <= 1) {
                Qt.callLater(function() {
                    logList.positionViewAtEnd()
                })
            }
        }

        property bool atYEnd: contentY + height >= contentHeight - 50

        ScrollBar.vertical: ScrollBar { }
    }

    // Empty state
    Label {
        visible: logModel.count === 0
        anchors.centerIn: parent
        text: "No log entries yet.\nStart a strategy to see output here."
        horizontalAlignment: Text.AlignHCenter
        color: Material.secondaryTextColor
        font.pixelSize: 14
    }
}
