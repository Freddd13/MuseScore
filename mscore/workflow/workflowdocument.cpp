// GPL-2.0-or-later. Per-score workflow documents stored as ordinary metadata.
#include "mscore/plugin/api/scoreobserver.h"
#include "mscore/musescore.h"
#include "mscore/shortcut.h"
#include "libmscore/undo.h"
#include <QJsonDocument>
namespace Ms {namespace PluginAPI {
static bool documentSection(const QString& section){return section=="harmony"||section=="reference";}
QVariantMap ScoreObserver::workflowData(const QString& section) const {
 if(!_score||!documentSection(section))return {};
 const auto bytes=_score->masterScore()->metaTag("kumo."+section).toUtf8();
 return QJsonDocument::fromJson(bytes).toVariant().toMap();
}
QVariantMap ScoreObserver::setWorkflowData(const QString& section,const QVariantMap& value){
 if(!_score||!documentSection(section))return {{"error","请打开乐谱后保存。"}};
 if(playing())return {{"error","请停止播放后保存修改。"}};
 auto master=_score->masterScore();if(master->readOnly())return {{"error","这份乐谱以只读方式打开，请另存后修改。"}};
 const auto bytes=QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact);
 if(bytes.size()>4*1024*1024)return {{"error","项目记录过大，请减少附加数据后保存。"}};
 const QString key="kumo."+section,text=QString::fromUtf8(bytes);
 if(master->metaTag(key)==text)return {{"ok",true},{"changed",false}};
 master->startCmd();master->undo(new ChangeMetaText(master,key,text));master->endCmd();
 if(mscore)mscore->endCmd();return {{"ok",true},{"changed",true}};
}
void ScoreObserver::workflowUndo(bool redo){
 if(mscore&&_score&&mscore->currentScore()&&mscore->currentScore()->masterScore()==_score->masterScore())mscore->cmd(getAction(redo?"redo":"undo"));
}
}}
