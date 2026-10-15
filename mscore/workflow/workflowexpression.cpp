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
#include "mscore/musescore.h"
#include "mscore/performanceeditor/parameteredit.h"
#include "mscore/plugin/api/score.h"
#include "mscore/plugin/api/scoreobserver.h"
#include "mscore/scoretab.h"
#include "mscore/scoreview.h"
#include "mscore/seq.h"
#include "referencefactory.h"
#include "workflowcontext.h"
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
 QMap<Ms::Note*,VelocityEdit> velocities;QMap<int,double> bpm;
 for(auto value:values){
  auto v=value.toMap();int tick=v["tick"].toInt(),track=v["track"].toInt();auto note=noteAt(_score,v);
  if(!note||tick<start||tick>=end||track<first||track>=last||velocities.contains(note))return {{"error","invalid, duplicate or changed note"}};
  int target=v.value("target",-1).toInt(),base=NoteVelocity::referenceBase(note);
  if(target<1||target>127||!v.contains("oldType")||!v.contains("oldRaw")||!v.contains("oldBase")||int(note->veloType())!=v["oldType"].toInt()||note->veloOffset()!=v["oldRaw"].toInt()||base!=v["oldBase"].toInt())return {{"error","changed velocity or playback base"}};
  bool protectedNote=note->veloType()!=Ms::Note::ValueType::OFFSET_VAL||note->veloOffset()!=0||note->chord()->playEventType()==PlayEventType::User||(v.contains("graceIndex")&&v["graceIndex"].isValid()&&!v["graceIndex"].isNull());
  if(protectedNote&&!v.value("unlock").toBool())return {{"error","protected note has not been explicitly unlocked"}};
  auto type=note->veloType();velocities.insert(note,{type,type==Ms::Note::ValueType::USER_VAL?target:NoteVelocity::offsetFor(base,target)});
 }
 for(auto value:tempoValues){
  auto v=value.toMap();int tick=v.value("tick",-1).toInt();double target=v.value("bpm").toDouble(),old=v.value("oldBpm").toDouble();
  if(tick<start||tick>end||bpm.contains(tick)||!std::isfinite(target)||target<5||target>999||!std::isfinite(old)||std::abs(_score->tempomap()->tempo(tick)*60-old)>.001)return {{"error","invalid, duplicate or changed tempo"}};
  if(std::abs(target-old)<.00001)continue;
  auto segment=_score->masterScore()->tick2segment(Fraction::fromTicks(tick),false,SegmentType::ChordRest);if(!segment)return {{"error","tempo target is not an onset"}};
  for(auto e:segment->annotations())if(e->isTempoText()){auto t=toTempoText(e);if(t->visible()||t->isRelative()||t->restoreMode())return {{"error","existing visible/relative/a-tempo marker protected"}};}
  for(auto curve:TempoExpression::curves(_score->masterScore()))if(tick>=curve->tick().ticks()&&tick<curve->tick2().ticks())return {{"error","existing tempo curve protected"}};
  bpm.insert(tick,target);
 }
 TempoMap simulation=*_score->tempomap();for(auto i=bpm.begin();i!=bpm.end();++i)simulation.setTempo(i.key(),i.value()/60);
 double oldDuration=_score->tempomap()->tick2time(end)-_score->tempomap()->tick2time(start),newDuration=simulation.tick2time(end)-simulation.tick2time(start);
 double error=std::abs(newDuration-oldDuration);
 if(candidate.value("keepDuration",true).toBool()&&!bpm.isEmpty()&&error>qMax(.015,oldDuration*.005))return {{"error","written tempo approximation cannot preserve duration; adjust anchors/strength or use performed duration"},{"durationError",error}};
 QString issue;
 auto rollback=[this](const QString& why){EditData ed;_score->undoRedo(true,&ed);return QVariantMap{{"error",why+"; application rolled back"}};};
 if(velocities.isEmpty()&&bpm.isEmpty())return {{"ok",true},{"changed",0},{"durationError",error}};
 if(!ParameterEdit::expression(_score,velocities,bpm,&issue))return {{"error",issue.isEmpty()?"application preflight failed":issue}};
 for(auto i=velocities.begin();i!=velocities.end();++i)if(i.key()->veloType()!=i->type||i.key()->veloOffset()!=i->raw)return rollback("velocity readback failed");
 for(auto i=bpm.begin();i!=bpm.end();++i)if(std::abs(_score->tempomap()->tempo(i.key())*60-i.value())>.01)return rollback("tempo readback failed");
 return {{"ok",true},{"changed",velocities.size()+bpm.size()},{"durationError",error},{"duration",newDuration}};
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
 if(auto action=mscore->findChild<QAction*>("performance-editor"))action->setChecked(true);
 mscore->showPerformanceEditor(true);
 if (auto toggle = mscore->findChild<QToolButton *>("performanceWorkflowToggle"))
       toggle->setChecked(true);
 if (auto dock = mscore->findChild<QDockWidget *>("performanceEditorDock")) {
       dock->show();
       dock->raise();
       const auto area=mscore->dockWidgetArea(dock);
       if(area==Qt::BottomDockWidgetArea || area==Qt::TopDockWidgetArea)
             mscore->resizeDocks({dock},{dock->widget()->minimumSizeHint().height()+24},Qt::Vertical);
 }
 return {{"ok",true}};
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
}}
