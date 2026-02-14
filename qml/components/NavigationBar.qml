import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material

Rectangle {
    id: root
    height: 56
    color: Material.dialogColor

    property int currentIndex: 0
    signal navigated(int index)

    // Top separator line
    Rectangle {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 1
        color: Material.dividerColor
    }

    RowLayout {
        anchors.fill: parent
        anchors.topMargin: 1
        spacing: 0

        Repeater {
            model: [
                { label: "Home", icon: "\u2302" },
                { label: "Strategies", icon: "\u2699" },
                { label: "Lists", icon: "\u2630" },
                { label: "Settings", icon: "\u2638" },
                { label: "Log", icon: "\u2261" },
                { label: "Tests", icon: "\u2713" }
            ]

            delegate: ItemDelegate {
                Layout.fillWidth: true
                Layout.fillHeight: true

                contentItem: ColumnLayout {
                    spacing: 2

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: modelData.icon
                        font.pixelSize: 20
                        color: root.currentIndex === index
                            ? Material.accentColor
                            : Material.secondaryTextColor
                    }

                    Label {
                        Layout.alignment: Qt.AlignHCenter
                        text: modelData.label
                        font.pixelSize: 10
                        color: root.currentIndex === index
                            ? Material.accentColor
                            : Material.secondaryTextColor
                    }
                }

                onClicked: {
                    root.currentIndex = index
                    root.navigated(index)
                }
            }
        }
    }
}
