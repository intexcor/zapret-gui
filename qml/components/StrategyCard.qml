import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material

ItemDelegate {
    id: root
    width: parent ? parent.width : 300
    height: contentLayout.implicitHeight + 24

    property string strategyId: ""
    property string strategyName: ""
    property string strategyDescription: ""
    property bool strategyAvailable: true
    property string platforms: ""
    property int filterCount: 0
    property bool isSelected: false

    opacity: strategyAvailable ? 1.0 : 0.5

    background: Rectangle {
        color: root.isSelected
            ? Qt.alpha(Material.accentColor, 0.15)
            : (root.hovered ? Qt.alpha(Material.foreground, 0.05) : "transparent")
        radius: 8
        border.color: root.isSelected ? Material.accentColor : Material.dividerColor
        border.width: root.isSelected ? 2 : 1
    }

    contentItem: ColumnLayout {
        id: contentLayout
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: root.strategyName
                font.pixelSize: 16
                font.bold: true
                Layout.fillWidth: true
                elide: Text.ElideRight
            }

            Label {
                visible: !root.strategyAvailable
                text: "N/A"
                font.pixelSize: 11
                color: Material.accent
                padding: 4
                background: Rectangle {
                    color: Qt.alpha(Material.accent, 0.15)
                    radius: 4
                }
            }
        }

        Label {
            visible: root.strategyDescription.length > 0
            text: root.strategyDescription
            font.pixelSize: 13
            color: Material.secondaryTextColor
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        RowLayout {
            spacing: 12

            Label {
                text: root.filterCount + " filter(s)"
                font.pixelSize: 11
                color: Material.hintTextColor
            }

            Label {
                text: root.platforms
                font.pixelSize: 11
                color: Material.hintTextColor
            }
        }
    }
}
