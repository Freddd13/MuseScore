import QtQuick 2.9
import MuseScore 3.0
MuseScore {
 menuPath:"Plugins.Reference Player"
 description:"打开内置原曲参考面板。音轨与关联随乐谱保存。"
 version:"1.4.0"
 requiresScore:false
 onRun:{if(typeof openReferencePanel==="function")openReferencePanel()}
}
