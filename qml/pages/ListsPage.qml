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
                text: "Domain & IP Lists"
                font.pixelSize: 20
                font.bold: true
                Layout.fillWidth: true
            }

            Button {
                text: "Save"
                flat: true
                onClicked: {
                    hostlistManager.generalList = generalEditor.text
                    hostlistManager.excludeList = excludeEditor.text
                    hostlistManager.googleList = googleEditor.text
                    hostlistManager.ipsetAll = ipsetEditor.text
                    hostlistManager.ipsetExclude = ipsetExcludeEditor.text
                    hostlistManager.save()
                    savedLabel.visible = true
                    savedTimer.restart()
                }
            }
        }
    }

    Label {
        id: savedLabel
        visible: false
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 8
        text: "Saved!"
        color: "#4CAF50"
        font.bold: true
        z: 1

        Timer {
            id: savedTimer
            interval: 2000
            onTriggered: savedLabel.visible = false
        }
    }

    TabBar {
        id: tabBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: savedLabel.visible ? 32 : 0

        TabButton { text: "General" }
        TabButton { text: "Exclude" }
        TabButton { text: "Google" }
        TabButton { text: "IPSet" }
        TabButton { text: "IPSet Excl." }
    }

    StackLayout {
        anchors.top: tabBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: addDomainRow.top
        anchors.margins: 12
        currentIndex: tabBar.currentIndex

        ScrollView {
            TextArea {
                id: generalEditor
                text: hostlistManager.generalList
                font.family: Qt.platform.os === "osx" ? "Menlo" : "Monospace"
                font.pixelSize: 13
                placeholderText: "One domain per line\ne.g. discord.com"
                wrapMode: TextEdit.NoWrap
            }
        }

        ScrollView {
            TextArea {
                id: excludeEditor
                text: hostlistManager.excludeList
                font.family: Qt.platform.os === "osx" ? "Menlo" : "Monospace"
                font.pixelSize: 13
                placeholderText: "Domains to exclude from filtering"
                wrapMode: TextEdit.NoWrap
            }
        }

        ScrollView {
            TextArea {
                id: googleEditor
                text: hostlistManager.googleList
                font.family: Qt.platform.os === "osx" ? "Menlo" : "Monospace"
                font.pixelSize: 13
                placeholderText: "Google/YouTube domains"
                wrapMode: TextEdit.NoWrap
            }
        }

        ScrollView {
            TextArea {
                id: ipsetEditor
                text: hostlistManager.ipsetAll
                font.family: Qt.platform.os === "osx" ? "Menlo" : "Monospace"
                font.pixelSize: 13
                placeholderText: "IP ranges in CIDR notation\ne.g. 104.16.0.0/12"
                wrapMode: TextEdit.NoWrap
            }
        }

        ScrollView {
            TextArea {
                id: ipsetExcludeEditor
                text: hostlistManager.ipsetExclude
                font.family: Qt.platform.os === "osx" ? "Menlo" : "Monospace"
                font.pixelSize: 13
                placeholderText: "IP ranges to exclude"
                wrapMode: TextEdit.NoWrap
            }
        }
    }

    // Quick add domain row
    RowLayout {
        id: addDomainRow
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 12
        spacing: 8

        TextField {
            id: domainInput
            Layout.fillWidth: true
            placeholderText: "Add domain..."
            font.pixelSize: 14

            onAccepted: addButton.clicked()
        }

        Button {
            id: addButton
            text: "Add"
            enabled: domainInput.text.trim().length > 0

            onClicked: {
                let listName
                switch (tabBar.currentIndex) {
                case 0: listName = "general"; break
                case 1: listName = "exclude"; break
                case 2: listName = "google"; break
                default: return
                }
                hostlistManager.addDomain(listName, domainInput.text.trim())
                domainInput.text = ""

                // Refresh editors
                generalEditor.text = hostlistManager.generalList
                excludeEditor.text = hostlistManager.excludeList
                googleEditor.text = hostlistManager.googleList
            }
        }
    }
}
