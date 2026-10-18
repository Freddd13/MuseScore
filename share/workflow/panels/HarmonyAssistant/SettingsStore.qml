import QtQuick 2.9
import Qt.labs.settings 1.0

Item {
    property alias payload: storage.payload
    Settings { id: storage; category: "HarmonyAssistant-v1"; property string payload: "" }
}
