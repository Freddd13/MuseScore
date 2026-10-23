// SPDX-License-Identifier: GPL-2.0-or-later
// GUI-side workflow operations. Preflight accepts values; audio callbacks remain untouched.
#include "libmscore/chord.h"
#include "libmscore/excerpt.h"
#include "libmscore/measure.h"
#include "libmscore/note.h"
#include "libmscore/segment.h"
#include "libmscore/tempo.h"
#include "libmscore/tempoexpression.h"
#include "libmscore/tempotext.h"
#include "libmscore/textline.h"
#include "libmscore/undo.h"
#include "libmscore/velocitybackup.h"
#include "libmscore/select.h"
#include "mscore/musescore.h"
#include "mscore/workflow/featuregate.h"
#include "mscore/performanceeditor/parameteredit.h"
#include "mscore/performanceeditor/performanceselection.h"
#include "mscore/plugin/api/score.h"
#include "mscore/plugin/api/scoreobserver.h"
#include "mscore/scoretab.h"
#include "mscore/scoreview.h"
#include "mscore/seq.h"
#include "referencefactory.h"
#include "workflowcontext.h"
#include "expressiontempo.h"
#include "playbackpreview.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QRegularExpression>
#include <memory>
#include <QAction>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDir>
#include <QDockWidget>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>
#include <QToolButton>
#include <QComboBox>
#include <QUrl>
#include <QUuid>
#include <cmath>
namespace Ms { namespace PluginAPI {
namespace {
QString localFile(const QString& value){return QUrl(value).isLocalFile()?QUrl(value).toLocalFile():value;}
QString canonical(const QString& path){return QDir::cleanPath(QFileInfo(path).absoluteFilePath()).toLower();}
bool idle(){return !seq||(!seq->isPlaying()&&seq->backgroundRenderingIdle());}
Ms::Note* noteAt(Ms::Score* score,const QVariantMap& value){
 int tick=value.value("tick",-1).toInt(),track=value.value("track",-1).toInt(),index=value.value("index",-1).toInt();
 if(track<0||track>=score->ntracks()||index<0)return nullptr;
 auto segment=score->tick2segment(Fraction::fromTicks(tick),false,SegmentType::ChordRest);if(!segment)return nullptr;
 auto e=segment->element(track);if(!e||!e->isChord())return nullptr;auto chord=toChord(e);
 if(value.contains("graceIndex")&&value["graceIndex"].isValid()&&!value["graceIndex"].isNull()){int gi=value["graceIndex"].toInt();if(gi<0||gi>=chord->graceNotes().size())return nullptr;chord=chord->graceNotes()[gi];}
 if(index>=int(chord->notes().size()))return nullptr;auto note=chord->notes()[index];
 if(!value.contains("writtenPitch")||!value.contains("writtenTpc")||note->pitch()!=value["writtenPitch"].toInt()||note->tpc()!=value["writtenTpc"].toInt())return nullptr;
 return note;
}
}
QVariantMap ScoreObserver::applyExpression(const QVariantMap& candidate){
 if(!_score)return {{"error",tr("打开要编辑的乐谱后使用力度。")}};
 if(!idle())return {{"error",tr("正在播放或准备乐谱。停播后即可使用新的力度。")}};
 if(_score->readOnly())return {{"error",tr("这份谱子以只读方式打开。另存为可编辑的乐谱后继续修改。")}};
 if(mscore&&mscore->currentScore()&&mscore->currentScore()!=_score&&!_workflowApplyingPreview)return {{"error","return to the source score before applying"}};
 if(candidate.value("revision").toString()!=analysisRevision())return {{"error","stale score session or content version"}};
 auto scope=candidate["scope"].toMap();int start=scope.value("start",-1).toInt(),end=scope.value("end",-1).toInt(),first=scope.value("firstTrack",-1).toInt(),last=scope.value("endTrack",-1).toInt();
 if(start<0||end<=start||first<0||last>_score->ntracks()||last<=first||!_score->lastMeasure()||end>_score->lastMeasure()->endTick().ticks())return {{"error","invalid candidate range"}};
 auto values=candidate["velocity"].toList(),tempoValues=candidate["tempos"].toList();
 if(values.size()+tempoValues.size()>20000)return {{"error","candidate too large; use smaller ranges"}};
 QMap<Ms::Note*,VelocityEdit> velocities;QMap<int,double> bpm;QMap<int,QVariantMap> requestedTempos;
 for(auto value:values){
  auto v=value.toMap();int tick=v["tick"].toInt(),track=v["track"].toInt();auto note=noteAt(_score,v);
  if(!note||tick<start||tick>=end||track<first||track>=last||velocities.contains(note))return {{"error","invalid, duplicate or changed note"}};
  int target=v.value("target",-1).toInt(),base=NoteVelocity::referenceBase(note);
  if(target<1||target>127||!v.contains("oldType")||!v.contains("oldRaw")||!v.contains("oldBase")||int(note->veloType())!=v["oldType"].toInt()||note->veloOffset()!=v["oldRaw"].toInt()||base!=v["oldBase"].toInt())return {{"error","changed velocity or playback base"}};
  bool protectedNote=note->veloType()!=Ms::Note::ValueType::OFFSET_VAL||note->veloOffset()!=0||note->chord()->playEventType()==PlayEventType::User||(v.contains("graceIndex")&&v["graceIndex"].isValid()&&!v["graceIndex"].isNull());
  if(protectedNote&&!v.value("unlock").toBool())return {{"error","protected note has not been explicitly unlocked"}};
  const auto mode=v.value("writeMode","preserve").toString();
  if(mode!="fixed"&&mode!="follow"&&mode!="preserve")return {{"error",tr("请选择逐音固定力度、跟随力度记号或保留原保存方式。")}};
  if(mode=="follow"&&base<=0)return {{"error",tr("当前位置的力度参考为 0，选择逐音固定力度后可以采用演奏值。")}};
  auto type=mode=="fixed"?Ms::Note::ValueType::USER_VAL:mode=="follow"?Ms::Note::ValueType::OFFSET_VAL:note->veloType();
  velocities.insert(note,{type,type==Ms::Note::ValueType::USER_VAL?target:NoteVelocity::offsetFor(base,target)});
 }
 for(auto value:tempoValues){
  auto v=value.toMap();int tick=v.value("tick",-1).toInt();double target=v.value("bpm").toDouble(),old=v.value("oldBpm").toDouble();
  if(tick<start||tick>end||requestedTempos.contains(tick)||!std::isfinite(target)||target<5||target>999||!std::isfinite(old)||std::abs(_score->tempomap()->tempo(tick)*60-old)>.001)return {{"error",tr("这处速度已经改变，请重新读取演奏结果。")}};
  requestedTempos.insert(tick,v);
 }
 const auto savedTempos=ExpressionTempo::read(_score->masterScore());
 TempoMap simulation=*_score->tempomap();simulation.setRelTempo(1.0);
 for(auto it=requestedTempos.begin();it!=requestedTempos.end();++it){
  const int tick=it.key();const double target=it.value().value("bpm").toDouble();
  if(std::abs(simulation.tempo(tick)*60-target)<.00001)continue;
  auto segment=_score->masterScore()->tick2segment(Fraction::fromTicks(tick),false,SegmentType::ChordRest);if(!segment)return {{"error","tempo target is not an onset"}};
  for(auto e:segment->annotations())if(e->isTempoText()){auto t=toTempoText(e);if(!ExpressionTempo::owns(t,savedTempos))return {{"error",tr("这一处保留谱上已有的速度记号；可以在速度区直接编辑它。")}};}
  for(auto curve:TempoExpression::curves(_score->masterScore()))if(tick>=curve->tick().ticks()&&tick<curve->tick2().ticks())return {{"error","existing tempo curve protected"}};
  bpm.insert(tick,target);simulation.setTempo(tick,target/60.);
 }
 TempoMap originalClock=*_score->tempomap();originalClock.setRelTempo(1.0);
 double oldDuration=originalClock.tick2time(end)-originalClock.tick2time(start),newDuration=simulation.tick2time(end)-simulation.tick2time(start);
 double error=std::abs(newDuration-oldDuration);
 if(candidate.value("keepDuration",true).toBool()&&!bpm.isEmpty()&&error>qMax(.015,oldDuration*.005))return {{"error",tr("拟采用速度会改变这段时长。重新对应这一段后再使用速度，或在设置中采用演奏时长。")},{"durationError",error}};
 QString issue;
 auto rollback=[this](const QString& why){EditData ed;_score->undoRedo(true,&ed);return QVariantMap{{"error",why+"; application rolled back"}};};
 if(velocities.isEmpty()&&bpm.isEmpty())return {{"ok",true},{"changed",0},{"durationError",error}};
 _score->startCmd();
 if(!ParameterEdit::expression(_score,velocities,bpm,&issue,!_workflowApplyingPreview,false)){_score->endCmd(true);return {{"error",issue.isEmpty()?tr("请重新读取这一段后使用演奏结果。"):issue}};}
 if(!bpm.isEmpty()){auto master=_score->masterScore();auto tags=master->metaTags();tags[ExpressionTempo::key()]=ExpressionTempo::updated(master,bpm);master->undo(new ChangeMetaTags(master,tags));}
 _score->endCmd();
 for(auto i=velocities.begin();i!=velocities.end();++i)if(i.key()->veloType()!=i->type||i.key()->veloOffset()!=i->raw)return rollback("velocity readback failed");
 for(auto i=bpm.begin();i!=bpm.end();++i)if(std::abs(_score->tempomap()->tempo(i.key())*60-i.value())>.01)return rollback("tempo readback failed");

 QVariantList appliedTempos;
 if(!tempoValues.isEmpty())for(auto value:candidate.value("tempoNodes").toList()){const int tick=value.toInt();if(tick>=start&&tick<=end)appliedTempos.append(QVariantMap{{"tick",tick},{"bpm",_score->tempomap()->tempo(tick)*60}});}
 if(appliedTempos.isEmpty())appliedTempos=tempoValues;
 QVariantList appliedValues;
 for(auto value:values){auto item=value.toMap();auto note=noteAt(_score,item);item["oldType"]=int(note->veloType());item["oldRaw"]=note->veloOffset();item["oldBase"]=NoteVelocity::referenceBase(note);appliedValues.append(item);}
 return {{"ok",true},{"changed",velocities.size()+bpm.size()},{"durationError",error},{"duration",newDuration},{"revision",analysisRevision()},{"appliedVelocity",appliedValues},{"appliedTempos",appliedTempos}};
}
QVariantMap ScoreObserver::copyWorkflowScore(const QString& destination,const QString& expected,bool overwrite){
 if(!_score||!idle())return {{"error","stop playback and wait for rendering before copying"}};
 if(expected!=analysisRevision())return {{"error","stale score before copy"}};
 const QString path=localFile(destination);if(QFileInfo(path).suffix().compare("mscz",Qt::CaseInsensitive)!=0)return {{"error","workflow copies require MSCZ"}};
 auto master=_score->masterScore();
 for(auto open:mscore->scores())if(canonical(open->fileInfo()->filePath())==canonical(path)){
  if(open!=master||master->readOnly()||!master->property("kumoWorkflowOwned").toBool())return {{"error","cannot overwrite an open user/original score"}};
 }
 if(QFile::exists(path)&&!overwrite)return {{"error","destination exists; choose another path"}};
 QElapsedTimer clock;clock.start();
 // Native compressed serialization packages excerpts, links, images and audio.
 // Restore serialization's version bookkeeping; no source path/dirty/undo mutation.
 QString version=master->mscoreVersion();int revisionValue=master->mscoreRevision(),format=master->mscVersion();
 auto state=master->state();const QString originalPath=master->fileInfo()->filePath();const bool dirty=master->dirty();int undoIndex=master->undoStack()->getCurIdx();
 QBuffer buffer;buffer.open(QIODevice::WriteOnly);bool saved=master->saveCompressedFile(&buffer,"score.mscx",false,false);
 master->setMscoreVersion(version);master->setMscoreRevision(revisionValue);master->setMscVersion(format);
 if(!saved||master->state()!=state||master->fileInfo()->filePath()!=originalPath||master->dirty()!=dirty||master->undoStack()->getCurIdx()!=undoIndex)return {{"error","copy serialization failed isolation check"}};
 QSaveFile output(path);if(!output.open(QIODevice::WriteOnly)||output.write(buffer.data())!=buffer.size()||!output.commit())return {{"error","copy write failed; previous file preserved"}};
 return {{"ok",true},{"path",path},{"sha256",QString::fromLatin1(QCryptographicHash::hash(buffer.data(),QCryptographicHash::Sha256).toHex())},{"milliseconds",double(clock.nsecsElapsed())/1000000}};
}
QVariantMap ScoreObserver::saveWorkflowScore(const QString& expected){
 if(!personalFeaturesEnabled())return {{"error",tr("在kumo.ini中开启个人工具后可保存试写。")}};
 if(!_score||!mscore||!mscore->currentScore()||mscore->currentScore()->masterScore()!=_score->masterScore())
  return {{"error",tr("先切到要保存的写法，再保存。")}};
 if(seq&&seq->isPlaying()){seq->stopWait();seq->waitForStoppedRendering();}
 if(!_score||!mscore||!idle())return {{"error",tr("结束播放后再保存当前写法。")}};
 if(expected!=analysisRevision())return {{"error",tr("乐谱刚有变化，请重新保存当前写法。")}};
 auto master=_score->masterScore();
 if(!master->property("kumoWorkflowOwned").toBool()||master->created())return {{"needsCopy",true}};
 if(master->readOnly())return {{"error",tr("选中一份可编辑写法后保存。")}};
 if(master->dirty()&&!mscore->saveFile())return {{"error",tr("当前写法保存失败，请检查项目文件夹。")}};
 const QString path=master->fileInfo()->absoluteFilePath();QFile file(path);
 if(!file.open(QIODevice::ReadOnly))return {{"error",tr("无法读取刚保存的写法文件。")}};
 return {{"ok",true},{"path",path},{"revision",analysisRevision()},
         {"sha256",QString::fromLatin1(QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256).toHex())}};
}
QVariantMap ScoreObserver::openWorkflowScore(const QString& filename,bool baseline,const QString& expectedHash){
 if(!mscore)return {{"error","no GUI host"}};
 if(seq&&seq->isPlaying()){seq->stopWait();if(seq->isPlaying())return {{"error","waiting for stop"}};}
 if(!idle())return {{"error","waiting for rendering to finish"}};
 const QString path=localFile(filename);
 if(!expectedHash.isEmpty()){QFile file(path);if(!file.open(QIODevice::ReadOnly)||QString::fromLatin1(QCryptographicHash::hash(file.readAll(),QCryptographicHash::Sha256).toHex())!=expectedHash)return {{"error","workspace score file changed; verify it before opening"}};}
 for(auto score:mscore->scores())if(canonical(score->fileInfo()->filePath())==canonical(path)){
  if(baseline&&!score->property("kumoWorkflowOwned").toBool())return {{"error","an open original cannot become a read-only baseline"}};
  mscore->setCurrentScoreView(mscore->scores().indexOf(score));return {{"ok",true},{"path",path}};
 }
 // Workflow files are already native copies; bypass optional legacy style migration.
 auto score=mscore->readScore(path);
 if(!score)return {{"error","cannot open independent score"}};
 score->masterScore()->QObject::setProperty("kumoWorkflowOwned",true);score->masterScore()->setReadOnly(baseline);
 mscore->setCurrentScoreView(mscore->appendScore(score));
 return {{"ok",true},{"path",path}};
}
QString ScoreObserver::temporaryWorkflowProject() {
 const QString folder=QStandardPaths::writableLocation(QStandardPaths::TempLocation)+"/musescore-expression/"+QUuid::createUuid().toString(QUuid::WithoutBraces);
 if(!QDir().mkpath(folder))return QString();
 return QUrl::fromLocalFile(folder+"/expression.json").toString();
}
QVariantMap ScoreObserver::showEditorComparison() {
 if(!mscore||!_score)return {{"error",tr("打开乐谱后可以查看力度对比。")}};
 auto oldDock=mscore->findChild<QDockWidget*>("performanceEditorDock");const bool wasVisible=oldDock&&oldDock->isVisible();
 if(auto action=mscore->findChild<QAction*>("performance-editor"))action->setChecked(true);mscore->showPerformanceEditor(true);
 if(auto button=mscore->findChild<QToolButton*>("performanceExpressionCompare"))button->setChecked(true);
 if(auto parameter=mscore->findChild<QComboBox*>("performanceParameter"))if(parameter->currentIndex()==2)parameter->setCurrentIndex(0);
 if(auto dock=mscore->findChild<QDockWidget*>("performanceEditorDock")){
  dock->show();dock->raise();const auto area=mscore->dockWidgetArea(dock);
  if(!wasVisible&&(area==Qt::BottomDockWidgetArea||area==Qt::TopDockWidgetArea))mscore->resizeDocks({dock},{qMin(mscore->height()-180,qMax(dock->widget()->minimumSizeHint().height()+24,mscore->height()/2))},Qt::Vertical);
 }
 return {{"ok",true}};
}
QVariantMap ScoreObserver::showEditorCorrespondence(){
 auto shown=showEditorComparison();if(shown.contains("error"))return shown;
 if(auto toggle=mscore->findChild<QToolButton*>("performanceWorkflowToggle"))toggle->setChecked(true);
 if(auto dock=mscore->findChild<QDockWidget*>("performanceEditorDock")){auto area=mscore->dockWidgetArea(dock);if(area==Qt::BottomDockWidgetArea||area==Qt::TopDockWidgetArea)mscore->resizeDocks({dock},{qMin(mscore->height()-180,qMax(dock->height(),dock->widget()->minimumSizeHint().height()+100))},Qt::Vertical);}
 return shown;
}
QVariantMap ScoreObserver::selectExpressionNote(const QVariantMap& value) {
 if(!personalFeaturesEnabled())return {{"error",tr("在kumo.ini中开启个人工具后，可以使用演奏对应选音。")}};
 if(!_score||!mscore||mscore->currentScore()!=_score)return {{"error",tr("回到对应的乐谱后，可以选择这个音。")}};
 if(value.value("revision").toString()!=analysisRevision())return {{"error",tr("乐谱刚有修改，重新对应后可以选择这个音。")}};
 auto note=noteAt(_score,value);if(!note)return {{"error",tr("这个音的位置有变化，请重新对应。")}};
 if(_score->noteEntryMode())return {{"error",tr("按 Esc 退出音符输入后，可以选择对应音。")}};
 if(!PerformanceSelection::apply(_score,{note},note))return {{"error",tr("选择这个音失败，请重新对应后再选。")}};
 if(!playing()&&mscore->currentScoreView())mscore->currentScoreView()->adjustCanvasPosition(note,false);
 return {{"ok",true},{"tick",note->chord()->tick().ticks()},{"track",note->track()}};
}
QVariantMap ScoreObserver::expressionSelection(bool exactElements){
 QVariantList notes;if(_score)for(auto note:_score->selection().noteList()){auto value=describe(note,note->ppitch());value["end"]=note->chord()->endTick().ticks();if(note->chord()->isGrace()&&note->chord()->parent()&&note->chord()->parent()->isChord())value["graceIndex"]=toChord(note->chord()->parent())->graceNotes().indexOf(note->chord());notes.append(value);}
 auto scope=analysisScope(false);
 if(_score&&_score->selection().isRange()) {
  const auto& selection=_score->selection();const int start=selection.tickStart().ticks(),end=selection.tickEnd().ticks();
  scope["partial"]=scope.value("start").toInt()!=start||scope.value("end").toInt()!=end;
  scope["start"]=start;scope["end"]=end;scope["firstTrack"]=selection.staffStart()*VOICES;scope["endTrack"]=selection.staffEnd()*VOICES;scope["exact"]=true;
  if(auto measure=_score->tick2measure(Fraction::fromTicks(start)))scope["firstBeat"]=1.+(start-measure->tick().ticks())/(DIVISION*4./measure->timesig().denominator());
  auto last=_score->tick2measure(Fraction::fromTicks(end));if(!last)last=_score->lastMeasure();
  if(last){scope["endMeasure"]=last->no()+1;scope["endBeat"]=1.+(end-last->tick().ticks())/(DIVISION*4./last->timesig().denominator());scope["endAtBar"]=end==last->endTick().ticks();}

 }
 if(_score&&exactElements){
  int start=-1,end=-1;
  if(_score->selection().isRange()){start=_score->selection().tickStart().ticks();end=_score->selection().tickEnd().ticks();}
  else for(auto element:_score->selection().elements()){
   auto chord=element->isNote()?toNote(element)->chord():element->isChordRest()?toChordRest(element):nullptr;
   if(!chord)continue;
   start=start<0?chord->tick().ticks():qMin(start,chord->tick().ticks());end=qMax(end,chord->endTick().ticks());
  }
  if(start>=0&&end>start){
   auto first=_score->tick2measure(Fraction::fromTicks(start)),last=_score->tick2measure(Fraction::fromTicks(end-1));
   scope["start"]=start;scope["end"]=end;scope["exact"]=true;
   if(first&&last){
    scope["firstMeasure"]=first->no()+1;scope["lastMeasure"]=last->no()+1;scope["endMeasure"]=last->no()+1;
    scope["firstBeat"]=1.+(start-first->tick().ticks())/(DIVISION*4./first->timesig().denominator());
    scope["endBeat"]=1.+(end-last->tick().ticks())/(DIVISION*4./last->timesig().denominator());
    scope["endAtBar"]=end==last->endTick().ticks();scope["partial"]=start!=first->tick().ticks()||end!=last->endTick().ticks();
   }
  }
 }
 return {{"scope",scope},{"notes",notes},{"rangeSelected",_score&&_score->selection().isRange()}};
}
QVariantMap ScoreObserver::closeExpressionPreviewTabs(){
 if(!idle())return {{"error",tr("停止播放后可以整理旧试听页。")}};
 if(!mscore||!_score)return {{"error",tr("回到正在编写的乐谱后，可以关闭旧试听页。")}};
 const QRegularExpression generated("^expression-[0-9a-fA-F-]{36}\\.mscz$");
 int closed=0,kept=0;const auto open=mscore->scores();
 for(auto score:open){
  if(score==_score->masterScore()||!generated.match(score->fileInfo()->fileName()).hasMatch())continue;
  const QString folder=QDir::fromNativeSeparators(score->fileInfo()->absolutePath());
  if(!score->property("kumoWorkflowOwned").toBool()&&!folder.contains("/musescore-expression/",Qt::CaseInsensitive))continue;
  if(score->dirty()){++kept;continue;}mscore->closeScore(score);++closed;
 }
 return {{"ok",true},{"closed",closed},{"kept",kept}};
}
void ScoreObserver::setExpressionAuditionLoop(bool enabled){setPlaybackPreviewLoop(_score,enabled);if(workflowContext()->current(_score)){auto project=referenceProject();if(project->property("joint").toBool())project->setProperty("repeatSelection",enabled);}}
QVariantMap ScoreObserver::finishExpressionAudition(){delete _workflowRepeat.data();_workflowRepeat.clear();workflowContext()->finishPlaybackSession();stopPlaybackPreview(true);return {{"ok",true}};}
QVariantMap ScoreObserver::auditionExpression(const QString& workspace,const QVariantMap& candidate,const QVariantMap& scope,bool proposed){
 Q_UNUSED(workspace);
 if(!_score||candidate.value("revision").toString()!=analysisRevision())return {{"error",tr("乐谱已有修改，请更新演奏对应后试听。")}};
 if(!seq||!seq->driver())return {{"error",tr("请在软件设置中启用声音设备后试听。")}};
 auto cacheScope=scope;cacheScope.remove("loopRestart");
 const QVariantMap settings{{"candidate",candidate},{"scope",cacheScope},{"relTempo",_score->tempomap()->relTempo()}};
 const QString key=analysisRevision()+QString::fromLatin1(QCryptographicHash::hash(QJsonDocument::fromVariant(settings).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
 if(playbackPreviewPrepared(_score,key)&&playbackPreviewActive(_score,proposed)){auto state=workflowContext()->audition();state["ok"]=true;state["continuing"]=true;return state;}
 if(!playbackPreviewPrepared(_score,key)){
  workflowContext()->finishPlaybackSession();stopPlaybackPreview();if(seq&&seq->isPlaying())seq->stopWait();if(!seq->isPlaying())seq->synchronizeStoppedGui();if(!idle())return {{"error",tr("正在准备声音，稍后即可试听。")}};
  QTemporaryDir materials(QStandardPaths::writableLocation(QStandardPaths::TempLocation)+"/musescore-expression-playback-XXXXXX");if(!materials.isValid())return {{"error",tr("暂时无法准备试听材料，请检查临时文件夹的空间。")}};
  const QString path=materials.filePath("expression.mscz");
  auto copied=copyWorkflowScore(path,analysisRevision(),true);if(copied.contains("error"))return copied;
  std::unique_ptr<MasterScore> material(mscore->readScore(path));if(!material)return {{"error",tr("读取试听材料失败，请重新准备。")}};
  Ms::Score* target=material.get();
  if(_score!=_score->masterScore()){
   int index=-1;auto excerpts=_score->masterScore()->excerpts();for(int i=0;i<excerpts.size();++i)if(excerpts[i]->partScore()==_score)index=i;
   if(index<0||index>=material->excerpts().size())return {{"error",tr("找不到这份分谱，请从总谱准备试听。")}};target=material->excerpts()[index]->partScore();
  }
  auto original=renderPlaybackPreview(_score,target);
  PluginAPI::Score wrapped(target);ScoreObserver local;local.setScore(&wrapped);local._workflowApplyingPreview=true;local.workflowInfo();
  auto reference=[&](){auto request=scope.value("reference").toMap();if(request.isEmpty())return QVariantMap();request["revision"]=local.analysisRevision();request["start"]=scope.value("start");request["end"]=scope.value("end");request["occurrence"]=scope.value("occurrence",1);return local.referencePlan(request);};
  original.reference=reference();if(original.reference.contains("error"))return original.reference;alignPreviewReference(original);
  auto values=candidate;values["revision"]=local.analysisRevision();auto applied=local.applyExpression(values);if(applied.contains("error"))return applied;
  auto changed=renderPlaybackPreview(_score,target);changed.reference=reference();if(changed.reference.contains("error"))return changed.reference;alignPreviewReference(changed);
  preparePlaybackPreview(_score,key,std::move(original),std::move(changed));
 }
 auto guard=QPointer<ScoreObserver>(this);setPlaybackPreviewRepeat(this,[guard,candidate,scope,proposed]{if(guard){auto again=scope;again["loop"]=playbackPreviewLoop();again["loopRestart"]=true;auto result=guard->auditionExpression(QString(),candidate,again,proposed);if(result.contains("error"))emit guard->workflowPlaybackError(result.value("error").toString());}});
 auto range=scope;range["exactStart"]=true;range["deferStart"]=scope.contains("reference");auto started=startPlaybackPreview(_score,proposed,range);
 if(started.contains("error")||!scope.contains("reference"))return started;
 auto plan=playbackPreviewReferencePlan(_score,proposed,started.value("fromUtick").toInt());plan["revision"]=analysisRevision();plan["loop"]=scope.value("loop",false);auto audio=startReferencePlayback(plan);if(audio.contains("error")){stopPlaybackPreview(true);return audio;}
 started["preparing"]=true;return started;
}
QVariantMap ScoreObserver::previewExpression(const QString& workspace,const QVariantMap& candidate,const QVariantMap& scope){
 if(!_score||!idle())return {{"error","stop and wait for rendering"}};
 if(!candidate.isEmpty()&&candidate.value("revision").toString()!=analysisRevision())return {{"error","stale preview candidate"}};
 auto origin=_score;_workflowOriginal=origin;
 int excerptIndex=-1;if(origin!=origin->masterScore()){for(int i=0;i<origin->masterScore()->excerpts().size();++i)if(origin->masterScore()->excerpts()[i]->partScore()==origin)excerptIndex=i;if(excerptIndex<0)return {{"error",tr("找不到这份分谱的来源，请回到总谱后试听。")}};}
 QString folder=QFileInfo(localFile(workspace)).absolutePath(),path=folder+"/expression-"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".mscz";
 auto copied=copyWorkflowScore(path,analysisRevision(),false);if(copied.contains("error"))return copied;
 // Read/apply without selecting the copy; original panel/observer is still attached.
 auto preview=mscore->readScore(path);if(!preview)return {{"error","cannot read preview copy"}};
 Ms::Score* previewScore=preview;if(excerptIndex>=0){if(excerptIndex>=preview->excerpts().size()){delete preview;return {{"error",tr("试听副本缺少对应分谱，请重新保存乐谱后再试。")}};}previewScore=preview->excerpts()[excerptIndex]->partScore();}
 PluginAPI::Score wrapped(previewScore);ScoreObserver local;local.setScore(&wrapped);local._workflowApplyingPreview=true;local.workflowInfo();
 if(!candidate.isEmpty()){auto adjusted=candidate;adjusted["revision"]=local.analysisRevision();auto applied=local.applyExpression(adjusted);
  if(applied.contains("error")){delete preview;return applied;}}
 preview->QObject::setProperty("kumoWorkflowOwned",true);
 if(!candidate.isEmpty()){auto stored=local.copyWorkflowScore(path,local.analysisRevision(),true);if(stored.contains("error")){delete preview;return stored;}}
 auto media=referenceProject();media->setProperty("expressionPreview",true);
 mscore->setCurrentScoreView(mscore->appendScore(preview));
 if(previewScore!=preview)for(auto tab:mscore->findChildren<ScoreTab*>())if(tab->view()==mscore->currentScoreView()){tab->setCurrentScore(previewScore);break;}
 workflowContext()->setPreview(origin,previewScore);
 connect(previewScore,&QObject::destroyed,media,[previewScore,media]{if(workflowContext()->clearPreview(previewScore))media->setProperty("expressionPreview",false);});
 auto audition=new ScoreObserver(this);audition->_score=previewScore;
 QVariantMap range;
 if(scope.contains("reference")){auto request=scope.value("reference").toMap();request["revision"]=audition->analysisRevision();request["start"]=scope.value("start");request["end"]=scope.value("end");auto plan=audition->referencePlan(request);range=plan.contains("error")?plan:audition->startReferencePlayback(plan);}
 else range=audition->navigateWorkflow(scope,true);
 if(range.contains("error")){range.insert("warning",range.take("error"));range.insert("ok",true);}
 range.insert("path",path);return range;
}
QVariantMap ScoreObserver::returnWorkflowSource(){
 if(!_workflowOriginal||!mscore)return {{"error","original score has been closed"}};
 if(seq&&seq->isPlaying())seq->stopWait();if(!idle())return {{"error","waiting for stop/render"}};
 int index=mscore->scores().indexOf(_workflowOriginal->masterScore());if(index<0)return {{"error","original tab no longer available"}};
 workflowContext()->clearPreview();referenceProject()->setProperty("expressionPreview",false);mscore->setCurrentScoreView(index);if(mscore->currentScore()!=_workflowOriginal)for(auto tab:mscore->findChildren<ScoreTab*>())if(tab->view()==mscore->currentScoreView()){tab->setCurrentScore(_workflowOriginal);break;}return {{"ok",true}};
}
namespace {
QSet<Ms::Note*> historySelection(Ms::Score* score,const QVariantMap& range) {
 QSet<Ms::Note*> result;if(range.value("selectedOnly").toBool())for(auto note:score->selection().noteList())result.insert(note);return result;
}
bool validHistoryRange(Ms::Score* score,const QVariantMap& range) {
 if(!score || !score->lastMeasure())return false;
 const int start=range.value("start",0).toInt(),end=range.value("end",score->lastMeasure()->endTick().ticks()).toInt(),first=range.value("firstTrack",0).toInt(),last=range.value("endTrack",score->ntracks()).toInt();
 return start>=0&&end>start&&end<=score->lastMeasure()->endTick().ticks()&&first>=0&&last>first&&last<=score->ntracks();
}
bool insideHistoryRange(Ms::Score* score, Ms::Note* note, const QVariantMap& range,const QSet<Ms::Note*>& selected) {
 const int tick=note->chord()->tick().ticks();
 if(tick<range.value("start",0).toInt() || tick>=range.value("end",score->lastMeasure()->endTick().ticks()).toInt() || note->track()<range.value("firstTrack",0).toInt() || note->track()>=range.value("endTrack",score->ntracks()).toInt())return false;
 return !range.value("selectedOnly").toBool() || selected.contains(note);
}
}
QVariantMap ScoreObserver::expressionVelocityHistory(const QVariantMap& range) {
 if(!validHistoryRange(_score,range))return {{"count",0}};
 const auto selected=historySelection(_score,range);
 int count=0;for(auto note:VelocityBackup::notes(_score))if(insideHistoryRange(_score,note,range,selected))++count;
 return {{"count",count},{"revision",analysisRevision()}};
}
QVariantMap ScoreObserver::restoreExpressionVelocity(const QVariantMap& range) {
 if(mscore&&mscore->currentScore()&&mscore->currentScore()!=_score)return {{"error",tr("切换到要修改的乐谱后继续。")}};
 if(!validHistoryRange(_score,range))return {{"error",tr("请选择要恢复力度的有效音符或小节。")}};
 if(!idle())return {{"error",tr("停止播放后即可恢复力度。")}};
 const auto selected=historySelection(_score,range);
 if(_score->readOnly())return {{"error",tr("请在可编辑的乐谱中恢复力度。")}};
 if(range.value("revision").toString()!=analysisRevision())return {{"error",tr("乐谱刚有修改，请刷新这一区域后恢复。")}};
 auto notes=VelocityBackup::notes(_score);QList<Ms::Note*> targets;
 for(auto note:notes)if(insideHistoryRange(_score,note,range,selected)){auto old=VelocityBackup::original(note);if(int(note->veloType())!=old.first || note->veloOffset()!=old.second)targets.append(note);}
 if(targets.isEmpty())return {{"ok",true},{"changed",0},{"revision",analysisRevision()}};
 _score->startCmd();for(auto note:targets){auto old=VelocityBackup::original(note);note->undoChangeProperty(Pid::VELO_TYPE,old.first);note->undoChangeProperty(Pid::VELO_OFFSET,old.second);}_score->endCmd();
 return {{"ok",true},{"changed",targets.size()},{"revision",analysisRevision()}};
}
QVariantMap ScoreObserver::saveCurrentExpressionVelocity(const QVariantMap& range) {
 if(mscore&&mscore->currentScore()&&mscore->currentScore()!=_score)return {{"error",tr("切换到要修改的乐谱后继续。")}};
 if(!validHistoryRange(_score,range))return {{"error",tr("请选择要保存力度的有效音符或小节。")}};
 if(!idle())return {{"error",tr("停止播放后即可保存当前力度。")}};
 const auto selected=historySelection(_score,range);
 if(_score->readOnly())return {{"error",tr("请在可编辑的乐谱中保存力度起点。")}};
 if(range.value("revision").toString()!=analysisRevision())return {{"error",tr("乐谱刚有修改，请刷新这一区域后保存。")}};
 QList<Ms::Note*> targets;const int start=range.value("start",0).toInt(),end=range.value("end",_score->lastMeasure()->endTick().ticks()).toInt();
 auto measure=_score->tick2measure(Fraction::fromTicks(start));
 for(auto segment=measure?measure->first(SegmentType::ChordRest):nullptr;segment&&segment->tick().ticks()<end;segment=segment->next1(SegmentType::ChordRest))
  for(int track=range.value("firstTrack",0).toInt();track<range.value("endTrack",_score->ntracks()).toInt();++track){auto e=segment->element(track);if(!e||!e->isChord())continue;auto chords=toChord(e)->graceNotes();chords.append(toChord(e));for(auto chord:chords)for(auto note:chord->notes())if(insideHistoryRange(_score,note,range,selected))targets.append(note);}
 _score->startCmd();for(auto note:targets)VelocityBackup::remember(_score,note,true);_score->endCmd();
 return {{"ok",true},{"changed",targets.size()},{"revision",analysisRevision()}};
}

}}
