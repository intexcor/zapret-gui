import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material

ApplicationWindow {
    id: window
    width: 420
    height: 720
    minimumWidth: 380
    minimumHeight: 600
    visible: true
    title: "Zapret"

    Material.theme: {
        if (configManager.theme === "dark") return Material.Dark
        if (configManager.theme === "light") return Material.Light
        return Material.System
    }
    Material.accent: Material.Blue
    Material.primary: Material.BlueGrey

    // Navigation model
    ListModel {
        id: navModel
        ListElement { name: "Home"; icon: "qrc:/icons/home.svg"; page: "pages/HomePage.qml" }
        ListElement { name: "Strategies"; icon: "qrc:/icons/strategy.svg"; page: "pages/StrategiesPage.qml" }
        ListElement { name: "Lists"; icon: "qrc:/icons/list.svg"; page: "pages/ListsPage.qml" }
        ListElement { name: "Settings"; icon: "qrc:/icons/settings.svg"; page: "pages/SettingsPage.qml" }
        ListElement { name: "Log"; icon: "qrc:/icons/log.svg"; page: "pages/LogPage.qml" }
        ListElement { name: "Diagnostics"; icon: "qrc:/icons/diagnostic.svg"; page: "pages/DiagnosticsPage.qml" }
    }

    property int currentPageIndex: 0

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Page content
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: currentPageIndex

            HomePage { id: homePage }
            StrategiesPage { id: strategiesPage }
            ListsPage { id: listsPage }
            SettingsPage { id: settingsPage }
            LogPage { id: logPage }
            DiagnosticsPage { id: diagnosticsPage }
        }

        // Bottom navigation bar
        NavigationBar {
            Layout.fillWidth: true
            currentIndex: currentPageIndex
            onNavigated: function(index) {
                currentPageIndex = index
            }
        }
    }

    // Check for updates on startup
    Component.onCompleted: {
        if (configManager.checkUpdates) {
            updateChecker.check()
        }
    }

    // Update notification dialog
    Connections {
        target: updateChecker
        function onCheckFinished(hasUpdate) {
            if (hasUpdate) {
                updateDialog.open()
            }
        }
    }

    Dialog {
        id: updateDialog
        title: "Update Available"
        anchors.centerIn: parent
        width: Math.min(parent.width - 40, 320)
        modal: true
        standardButtons: Dialog.Ok

        Label {
            width: parent.width
            text: "New version " + updateChecker.latestVersion + " is available.\n" +
                  "Current version: " + updateChecker.currentVersion
            wrapMode: Text.WordWrap
        }
    }
}
