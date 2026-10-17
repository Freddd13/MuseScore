import QtQuick 2.9
import MuseScore 3.0
MuseScore {
 menuPath:"Plugins.Harmony Assistant"
 description:"打开内置和弦助手。"
 version:"2.0.0"
 requiresScore:true
 onRun:{if(typeof openHarmonyPanel==="function")openHarmonyPanel()}
}
