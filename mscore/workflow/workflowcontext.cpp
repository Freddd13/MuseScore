// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "workflowcontext.h"
#include <QCoreApplication>
namespace Ms {
WorkflowContext* workflowContext(){static QPointer<WorkflowContext> context;if(!context)context=new WorkflowContext(qApp);return context;}
}
