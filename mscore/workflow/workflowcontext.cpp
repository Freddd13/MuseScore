// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "workflowcontext.h"
#include "mscore/plugin/api/score.h"
#include <QCoreApplication>
#include <QUuid>
namespace Ms {
namespace { QPointer<WorkflowContext> context; }
WorkflowContext* workflowContext(){if(!context)context=new WorkflowContext(qApp);return context;}
QVariantMap WorkflowContext::expressionSession(QObject* object) const {
      auto wrapped=qobject_cast<PluginAPI::Score*>(object);
      auto score=wrapped?wrapped->score():nullptr;
      if(!score||!_expressionSessions.contains(score))return {};
      const auto saved=_expressionSessions.value(score);auto result=saved.values;
      result["contentCurrent"]=saved.content==score->masterScore()->state();
      return result;
}
bool WorkflowContext::rememberExpressionSession(QObject* object,const QVariantMap& value) {
      auto wrapped=qobject_cast<PluginAPI::Score*>(object);
      auto score=wrapped?wrapped->score():nullptr;
      if(!score)return false;
      // Open-score session only. Closing the score releases it; score files use explicit project save.
      if(!_expressionSessions.contains(score))connect(score,&QObject::destroyed,this,[this,score]{_expressionSessions.remove(score);});
      _expressionSessions[score]={score->masterScore()->state(),value};
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
