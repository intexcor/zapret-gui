import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Controls.Material

Rectangle {
    id: root
    width: size
    height: size
    radius: size / 2

    property int size: 16
    property string status: "stopped" // "stopped", "starting", "running", "error"

    color: {
        switch (status) {
        case "running": return "#4CAF50"   // Green
        case "starting": return "#FFC107"  // Amber
        case "error": return "#F44336"     // Red
        default: return "#9E9E9E"          // Grey
        }
    }

    // Pulse animation when starting
    SequentialAnimation on opacity {
        running: root.status === "starting"
        loops: Animation.Infinite
        NumberAnimation { to: 0.3; duration: 600; easing.type: Easing.InOutQuad }
        NumberAnimation { to: 1.0; duration: 600; easing.type: Easing.InOutQuad }
    }

    // Glow effect when running
    Rectangle {
        visible: root.status === "running"
        anchors.centerIn: parent
        width: parent.width + 8
        height: parent.height + 8
        radius: width / 2
        color: "transparent"
        border.color: "#4CAF50"
        border.width: 2
        opacity: 0.4
    }
}
