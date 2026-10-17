import QtQuick 2.9
import MuseScore 3.0
MuseScore {
 menuPath:"Plugins.Performance Expression"
 description:"打开内置演奏表达。"
 version:"2.0.0"
 requiresScore:true
 onRun:{if(typeof openExpressionPanel==="function")openExpressionPanel()}
}
