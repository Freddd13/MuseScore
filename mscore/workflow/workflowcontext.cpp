// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "workflowcontext.h"
#include <QCoreApplication>
namespace Ms {
namespace { QPointer<WorkflowContext> context; }
WorkflowContext* workflowContext(){if(!context)context=new WorkflowContext(qApp);return context;}
void notifyWorkflowCurrentScore(Score* score){if(context)context->setActiveScore(score);}
}
