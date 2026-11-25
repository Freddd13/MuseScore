// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "workflowcontext.h"
#include "documentsettings.h"
#include "mscore/plugin/api/score.h"
#include "midicapture.h"
#include <QQmlEngine>
#include <QFileInfo>
#include <QUrl>
#include <QCoreApplication>
#include <QUuid>
namespace Ms {
namespace { QPointer<WorkflowContext> context;
QString expressionStateKey(Score* score){return "toolProjects/expressionSession"+(score->isMaster()?QString():"/"+QString::fromLatin1(score->title().toUtf8().toBase64()));}
}
WorkflowContext* workflowContext(){
      if(!context){
            context=new WorkflowContext(qApp);
            QObject::connect(documentUiState(),&DocumentUiState::captureRequested,context,[](Score*){
                  if(context)emit context->saveStateRequested();
                  });
            }
      return context;
      }
WorkflowContext* workflowContextIfCreated(){return context;}
QObject* WorkflowContext::expressionCaptureSession(QObject* object) {
      auto wrapped=qobject_cast<PluginAPI::Score*>(object);auto score=wrapped?wrapped->score():nullptr;
      if(!score)return nullptr;
      if(auto saved=_expressionCaptures.value(score))return saved;
      auto capture=new MidiCaptureSession(this,score);QQmlEngine::setObjectOwnership(capture,QQmlEngine::CppOwnership);
      _expressionCaptures.insert(score,capture);
      connect(score,&QObject::destroyed,this,[this,score,capture]{
            _expressionCaptures.remove(score);capture->finishBeforeLeaving();
            const auto state=capture->state();
            if(state.value("stopping").toBool()||state.value("saving").toBool()){
                  connect(capture,&MidiCaptureSession::finished,capture,&QObject::deleteLater);
                  connect(capture,&MidiCaptureSession::failed,capture,&QObject::deleteLater);
                  }
            else capture->deleteLater();
            });
      return capture;
}
QVariantMap WorkflowContext::lastRecording() const {
      const auto take=DocumentSettings().value("workflow/lastRecording").toMap();
      return QFileInfo(QUrl(take.value("path").toString()).toLocalFile()).isFile()?take:QVariantMap();
}
QVariantMap WorkflowContext::expressionSession(QObject* object) const {
      auto wrapped=qobject_cast<PluginAPI::Score*>(object);
      auto score=wrapped?wrapped->score():nullptr;
      if(!score)return {};
      if(!_expressionSessions.contains(score)) {
            auto saved=DocumentSettings(score).value(expressionStateKey(score)).toMap();
            if(!saved.isEmpty()){saved["fromScoreFile"]=true;saved["contentCurrent"]=false;}
            return saved;
            }
      const auto saved=_expressionSessions.value(score);auto result=saved.values;
      result["contentCurrent"]=saved.content==score->masterScore()->state();
      return result;
}
bool WorkflowContext::rememberExpressionSession(QObject* object,const QVariantMap& value) {
      auto wrapped=qobject_cast<PluginAPI::Score*>(object);
      auto score=wrapped?wrapped->score():nullptr;
      if(!score)return false;
      if(!_expressionSessions.contains(score))connect(score,&QObject::destroyed,this,[this,score]{_expressionSessions.remove(score);});
      _expressionSessions[score]={score->masterScore()->state(),value};
      DocumentSettings(score).setValue(expressionStateKey(score),value);
      return true;
}
void notifyWorkflowCurrentScore(Score* score){if(context)context->setActiveScore(score);}
void notifyWorkflowScoreFileChanged(Score* score){if(context)emit context->scoreFileChanged(score);}
QString workflowScoreSession(Score* score){
      if(!score)return {};
      auto master=score->masterScore();
      auto session=master->property("kumoWorkflowSession").toString();
      if(session.isEmpty()){
            session=QUuid::createUuid().toString(QUuid::WithoutBraces);
            master->QObject::setProperty("kumoWorkflowSession",session);
            }
      return session;
}
}
