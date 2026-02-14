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
                text: "Diagnostics"
                font.pixelSize: 20
                font.bold: true
                Layout.fillWidth: true
            }

            Button {
                text: root.testing ? "Testing..." : "Run All"
                flat: true
                enabled: !root.testing
                onClicked: root.runAllTests()
            }
        }
    }

    property bool testing: false

    // Test targets
    ListModel {
        id: testModel

        ListElement {
            name: "Discord"
            url: "https://discord.com"
            status: "pending"  // pending, testing, ok, fail
            latency: ""
        }
        ListElement {
            name: "Discord CDN"
            url: "https://cdn.discordapp.com"
            status: "pending"
            latency: ""
        }
        ListElement {
            name: "Discord Gateway"
            url: "https://gateway.discord.gg"
            status: "pending"
            latency: ""
        }
        ListElement {
            name: "YouTube"
            url: "https://youtube.com"
            status: "pending"
            latency: ""
        }
        ListElement {
            name: "YouTube Video"
            url: "https://redirector.googlevideo.com"
            status: "pending"
            latency: ""
        }
        ListElement {
            name: "Google"
            url: "https://google.com"
            status: "pending"
            latency: ""
        }
        ListElement {
            name: "CloudFlare"
            url: "https://cloudflare.com"
            status: "pending"
            latency: ""
        }
        ListElement {
            name: "CloudFlare DNS"
            url: "https://1.1.1.1"
            status: "pending"
            latency: ""
        }
    }

    ListView {
        id: testList
        anchors.fill: parent
        anchors.margins: 12
        spacing: 4
        clip: true

        model: testModel

        delegate: ItemDelegate {
            width: testList.width
            height: 56

            contentItem: RowLayout {
                spacing: 12

                // Status indicator
                Rectangle {
                    width: 12
                    height: 12
                    radius: 6
                    color: {
                        switch (model.status) {
                        case "ok": return "#4CAF50"
                        case "fail": return "#F44336"
                        case "testing": return "#FFC107"
                        default: return "#9E9E9E"
                        }
                    }

                    SequentialAnimation on opacity {
                        running: model.status === "testing"
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.3; duration: 500 }
                        NumberAnimation { to: 1.0; duration: 500 }
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Label {
                        text: model.name
                        font.pixelSize: 15
                    }

                    Label {
                        text: model.url
                        font.pixelSize: 11
                        color: Material.hintTextColor
                        elide: Text.ElideMiddle
                        Layout.maximumWidth: parent.width
                    }
                }

                Label {
                    visible: model.latency.length > 0
                    text: model.latency
                    font.pixelSize: 13
                    color: {
                        if (model.status === "ok") return "#4CAF50"
                        if (model.status === "fail") return "#F44336"
                        return Material.secondaryTextColor
                    }
                }

                Label {
                    text: {
                        switch (model.status) {
                        case "ok": return "OK"
                        case "fail": return "FAIL"
                        case "testing": return "..."
                        default: return ""
                        }
                    }
                    font.pixelSize: 13
                    font.bold: true
                    color: {
                        switch (model.status) {
                        case "ok": return "#4CAF50"
                        case "fail": return "#F44336"
                        default: return Material.secondaryTextColor
                        }
                    }
                }
            }

            onClicked: {
                // Run single test
                runTest(index)
            }
        }

        ScrollBar.vertical: ScrollBar { }
    }

    // Summary at bottom
    Rectangle {
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        height: summaryLayout.implicitHeight + 24
        color: Material.dialogColor

        Rectangle {
            anchors.top: parent.top
            width: parent.width
            height: 1
            color: Material.dividerColor
        }

        RowLayout {
            id: summaryLayout
            anchors.centerIn: parent
            spacing: 24

            Label {
                property int okCount: {
                    let c = 0
                    for (let i = 0; i < testModel.count; i++)
                        if (testModel.get(i).status === "ok") c++
                    return c
                }
                text: okCount + " passed"
                color: "#4CAF50"
                font.bold: true
            }

            Label {
                property int failCount: {
                    let c = 0
                    for (let i = 0; i < testModel.count; i++)
                        if (testModel.get(i).status === "fail") c++
                    return c
                }
                text: failCount + " failed"
                color: failCount > 0 ? "#F44336" : Material.secondaryTextColor
                font.bold: failCount > 0
            }
        }
    }

    // Test execution using XMLHttpRequest (available in QML)
    function runTest(index) {
        let item = testModel.get(index)
        testModel.setProperty(index, "status", "testing")
        testModel.setProperty(index, "latency", "")

        let startTime = Date.now()
        let xhr = new XMLHttpRequest()
        xhr.timeout = 10000

        xhr.onreadystatechange = function() {
            if (xhr.readyState === XMLHttpRequest.DONE) {
                let elapsed = Date.now() - startTime
                if (xhr.status >= 200 && xhr.status < 400) {
                    testModel.setProperty(index, "status", "ok")
                    testModel.setProperty(index, "latency", elapsed + " ms")
                } else {
                    testModel.setProperty(index, "status", "fail")
                    testModel.setProperty(index, "latency",
                        xhr.status > 0 ? "HTTP " + xhr.status : "Timeout")
                }
            }
        }

        xhr.ontimeout = function() {
            testModel.setProperty(index, "status", "fail")
            testModel.setProperty(index, "latency", "Timeout")
        }

        xhr.onerror = function() {
            testModel.setProperty(index, "status", "fail")
            testModel.setProperty(index, "latency", "Error")
        }

        xhr.open("HEAD", item.url)
        xhr.send()
    }

    function runAllTests() {
        testing = true
        let index = 0

        function runNext() {
            if (index >= testModel.count) {
                testing = false
                return
            }
            runTest(index)
            index++
            // Stagger tests slightly
            testTimer.interval = 200
            testTimer.triggered.connect(runNext)
            testTimer.start()
        }

        // Reset all
        for (let i = 0; i < testModel.count; i++) {
            testModel.setProperty(i, "status", "pending")
            testModel.setProperty(i, "latency", "")
        }

        runNext()
    }

    Timer {
        id: testTimer
        repeat: false
    }
}
