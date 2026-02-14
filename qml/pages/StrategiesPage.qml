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
                text: "Strategies"
                font.pixelSize: 20
                font.bold: true
                Layout.fillWidth: true
            }

            Label {
                text: strategyManager.count + " total"
                font.pixelSize: 13
                color: Material.secondaryTextColor
            }
        }
    }

    ListView {
        id: strategyList
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        clip: true

        model: strategyListModel

        delegate: StrategyCard {
            width: strategyList.width
            strategyId: model.strategyId
            strategyName: model.name
            strategyDescription: model.description
            strategyAvailable: model.available
            platforms: model.supportedPlatforms
            filterCount: model.filterCount
            isSelected: zapretEngine.currentStrategyId === model.strategyId

            onClicked: {
                if (model.available) {
                    zapretEngine.currentStrategyId = model.strategyId
                    configManager.lastStrategy = model.strategyId
                }
            }
        }

        // Empty state
        Label {
            visible: strategyList.count === 0
            anchors.centerIn: parent
            text: "No strategies loaded.\nPlace strategies.json next to the application."
            horizontalAlignment: Text.AlignHCenter
            color: Material.secondaryTextColor
            font.pixelSize: 14
        }

        ScrollBar.vertical: ScrollBar { }
    }
}
