import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material

Page {
    id: root

    header: ToolBar {
        Label {
            text: "Settings"
            font.pixelSize: 20
            font.bold: true
            anchors.centerIn: parent
        }
    }

    ScrollView {
        anchors.fill: parent

        ColumnLayout {
            width: root.availableWidth
            spacing: 0

            // Section: General
            Label {
                text: "General"
                font.pixelSize: 14
                font.bold: true
                color: Material.accentColor
                leftPadding: 16
                topPadding: 16
                bottomPadding: 8
            }

            ItemDelegate {
                Layout.fillWidth: true
                contentItem: RowLayout {
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label { text: "Auto-start on boot"; font.pixelSize: 16 }
                        Label { text: "Install as system service and start automatically"; font.pixelSize: 12; color: Material.secondaryTextColor; wrapMode: Text.WordWrap }
                    }
                    Switch {
                        checked: configManager.autoStart
                        onCheckedChanged: configManager.autoStart = checked
                    }
                }
            }

            ItemDelegate {
                Layout.fillWidth: true
                contentItem: RowLayout {
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label { text: "Check for updates"; font.pixelSize: 16 }
                        Label { text: "Check for new versions on startup"; font.pixelSize: 12; color: Material.secondaryTextColor; wrapMode: Text.WordWrap }
                    }
                    Switch {
                        checked: configManager.checkUpdates
                        onCheckedChanged: configManager.checkUpdates = checked
                    }
                }
            }

            MenuSeparator { Layout.fillWidth: true }

            // Section: Filtering
            Label {
                text: "Filtering"
                font.pixelSize: 14
                font.bold: true
                color: Material.accentColor
                leftPadding: 16
                topPadding: 16
                bottomPadding: 8
            }

            ItemDelegate {
                Layout.fillWidth: true
                contentItem: RowLayout {
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label { text: "Game filter"; font.pixelSize: 16 }
                        Label { text: "Extended port range (1024-65535) for gaming traffic"; font.pixelSize: 12; color: Material.secondaryTextColor; wrapMode: Text.WordWrap }
                    }
                    Switch {
                        checked: configManager.gameFilter
                        onCheckedChanged: configManager.gameFilter = checked
                    }
                }
            }

            ItemDelegate {
                Layout.fillWidth: true
                contentItem: RowLayout {
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label { text: "IPSet mode"; font.pixelSize: 16 }
                        Label { text: "Filter by IP ranges in addition to domain lists"; font.pixelSize: 12; color: Material.secondaryTextColor; wrapMode: Text.WordWrap }
                    }
                    Switch {
                        checked: configManager.ipsetMode
                        onCheckedChanged: configManager.ipsetMode = checked
                    }
                }
            }

            MenuSeparator { Layout.fillWidth: true }

            // Section: Appearance
            Label {
                text: "Appearance"
                font.pixelSize: 14
                font.bold: true
                color: Material.accentColor
                leftPadding: 16
                topPadding: 16
                bottomPadding: 8
            }

            ItemDelegate {
                Layout.fillWidth: true
                contentItem: RowLayout {
                    spacing: 16
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label {
                            text: "Theme"
                            font.pixelSize: 16
                        }
                        Label {
                            text: "Choose app color scheme"
                            font.pixelSize: 13
                            color: Material.secondaryTextColor
                        }
                    }
                    ComboBox {
                        model: ["System", "Light", "Dark"]
                        currentIndex: {
                            switch (configManager.theme) {
                            case "light": return 1
                            case "dark": return 2
                            default: return 0
                            }
                        }
                        onActivated: function(index) {
                            switch (index) {
                            case 1: configManager.theme = "light"; break
                            case 2: configManager.theme = "dark"; break
                            default: configManager.theme = "system"; break
                            }
                        }
                    }
                }
            }

            MenuSeparator { Layout.fillWidth: true }

            // Section: About
            Label {
                text: "About"
                font.pixelSize: 14
                font.bold: true
                color: Material.accentColor
                leftPadding: 16
                topPadding: 16
                bottomPadding: 8
            }

            ItemDelegate {
                Layout.fillWidth: true
                contentItem: RowLayout {
                    Label {
                        text: "App Version"
                        Layout.fillWidth: true
                    }
                    Label {
                        text: updateChecker.currentVersion
                        color: Material.secondaryTextColor
                    }
                }
            }

            ItemDelegate {
                Layout.fillWidth: true
                contentItem: RowLayout {
                    Label {
                        text: "Latest Version"
                        Layout.fillWidth: true
                    }
                    Label {
                        text: updateChecker.latestVersion.length > 0 ? updateChecker.latestVersion : "Unknown"
                        color: updateChecker.updateAvailable ? "#FF9800" : Material.secondaryTextColor
                    }
                }
                onClicked: updateChecker.check()
            }

            ItemDelegate {
                Layout.fillWidth: true
                contentItem: RowLayout {
                    Label {
                        text: "Platform"
                        Layout.fillWidth: true
                    }
                    Label {
                        text: Qt.platform.os
                        color: Material.secondaryTextColor
                    }
                }
            }

            // Bottom padding
            Item { Layout.preferredHeight: 20 }
        }
    }
}
