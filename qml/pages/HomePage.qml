import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material

Page {
    id: root

    header: ToolBar {
        Label {
            text: "Zapret"
            font.pixelSize: 20
            font.bold: true
            anchors.centerIn: parent
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 24

        // Spacer
        Item { Layout.fillHeight: true; Layout.preferredHeight: 20 }

        // Status section
        ColumnLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 12

            StatusIndicator {
                Layout.alignment: Qt.AlignHCenter
                size: 48
                status: {
                    if (zapretEngine.running) return "running"
                    if (zapretEngine.status === "Starting...") return "starting"
                    if (zapretEngine.errorString.length > 0) return "error"
                    return "stopped"
                }
            }

            Label {
                Layout.alignment: Qt.AlignHCenter
                text: zapretEngine.status
                font.pixelSize: 18
                font.bold: true
            }

            Label {
                Layout.alignment: Qt.AlignHCenter
                visible: zapretEngine.errorString.length > 0
                text: zapretEngine.errorString
                font.pixelSize: 13
                color: "#F44336"
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                Layout.maximumWidth: root.width - 60
            }
        }

        // Strategy selector
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: "Strategy"
                font.pixelSize: 14
                color: Material.secondaryTextColor
            }

            ComboBox {
                id: strategyCombo
                Layout.fillWidth: true
                model: strategyManager.strategyNames()
                currentIndex: {
                    let id = zapretEngine.currentStrategyId
                    return id ? strategyManager.indexOfStrategy(id) : -1
                }

                onActivated: function(index) {
                    let id = strategyManager.strategyIdAt(index)
                    zapretEngine.currentStrategyId = id
                    configManager.lastStrategy = id
                }

            }

            Label {
                visible: zapretEngine.currentStrategyId.length > 0
                text: strategyManager.strategyDescriptionById(zapretEngine.currentStrategyId)
                font.pixelSize: 12
                color: Material.hintTextColor
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            Label {
                visible: zapretEngine.currentStrategyId.length > 0
                         && !strategyManager.isStrategyAvailableOnPlatform(zapretEngine.currentStrategyId)
                text: "This strategy is not available on your platform"
                font.pixelSize: 12
                color: "#FF9800"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        // Spacer
        Item { Layout.fillHeight: true }

        // Start/Stop button
        Button {
            Layout.fillWidth: true
            Layout.preferredHeight: 56
            text: zapretEngine.running ? "Stop" : "Start"
            font.pixelSize: 18
            font.bold: true

            Material.background: zapretEngine.running ? "#F44336" : "#4CAF50"
            Material.foreground: "white"
            Material.roundedScale: Material.MediumScale

            enabled: zapretEngine.status !== "Starting..." && zapretEngine.status !== "Stopping..."

            onClicked: {
                if (zapretEngine.running) {
                    zapretEngine.stop()
                } else {
                    zapretEngine.start()
                }
            }
        }

        // Quick info
        RowLayout {
            Layout.fillWidth: true
            spacing: 16

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    text: "Version"
                    font.pixelSize: 11
                    color: Material.hintTextColor
                }
                Label {
                    text: updateChecker.currentVersion
                    font.pixelSize: 13
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    text: "Game Filter"
                    font.pixelSize: 11
                    color: Material.hintTextColor
                }
                Label {
                    text: configManager.gameFilter ? "ON" : "OFF"
                    font.pixelSize: 13
                    color: configManager.gameFilter ? "#4CAF50" : Material.secondaryTextColor
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    text: "IPSet"
                    font.pixelSize: 11
                    color: Material.hintTextColor
                }
                Label {
                    text: configManager.ipsetMode ? "ON" : "OFF"
                    font.pixelSize: 13
                    color: configManager.ipsetMode ? "#4CAF50" : Material.secondaryTextColor
                }
            }
        }

        // Service buttons
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Button {
                Layout.fillWidth: true
                text: "Install Service"
                flat: true
                onClicked: zapretEngine.installService()
            }

            Button {
                Layout.fillWidth: true
                text: "Remove Service"
                flat: true
                onClicked: {
                    removeConfirmDialog.open()
                }
            }
        }

        Item { Layout.preferredHeight: 8 }
    }

    Dialog {
        id: removeConfirmDialog
        title: "Remove Service"
        anchors.centerIn: parent
        modal: true
        standardButtons: Dialog.Yes | Dialog.No

        Label {
            text: "Are you sure you want to remove the Zapret service?"
        }

        onAccepted: zapretEngine.removeService()
    }
}
