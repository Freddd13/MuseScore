// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "workflowcontext.h"
#include <QCoreApplication>
#include <QUuid>
namespace Ms {
namespace { QPointer<WorkflowContext> context; }
WorkflowContext* workflowContext(){if(!context)context=new WorkflowContext(qApp);return context;}
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
