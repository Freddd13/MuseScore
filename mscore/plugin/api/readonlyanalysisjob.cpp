// SPDX-License-Identifier: GPL-2.0-or-later
#include "readonlyanalysisjob.h"
#include <QJSEngine>
#include <QJSValue>
#include <QElapsedTimer>
#include <QtConcurrent>
namespace Ms { namespace PluginAPI {
ReadOnlyAnalysisJob::ReadOnlyAnalysisJob(QObject* parent):QObject(parent),_state(new State) {
      connect(&_watcher,&QFutureWatcher<QVariantMap>::finished,this,[this] {
            if(!_state->canceled.load())emit finished(_watcher.result());
            });
      }
ReadOnlyAnalysisJob::~ReadOnlyAnalysisJob() {cancel();}
void ReadOnlyAnalysisJob::cancel() {
      _state->canceled.store(true);
      QMutexLocker guard(&_state->mutex);
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
      if(_state->engine)_state->engine->setInterrupted(true);
#endif
      }
void ReadOnlyAnalysisJob::start(const QString& source,const QVariantMap& input) {
      const auto state=_state;
      _watcher.setFuture(QtConcurrent::run([state,source,input] {
            if(state->canceled.load())return QVariantMap{{"error","canceled"}};
            QElapsedTimer timer;timer.start();QJSEngine engine;
            {QMutexLocker guard(&state->mutex);state->engine=&engine;}
            engine.globalObject().setProperty("input",engine.toScriptValue(input));
            QJSValue value;
            if(!state->canceled.load())value=engine.evaluate(source,"arrangement-rules");
            {QMutexLocker guard(&state->mutex);state->engine=nullptr;}
            if(state->canceled.load())return QVariantMap{{"error","canceled"}};
            if(value.isError())return QVariantMap{{"error",value.toString()},{"line",value.property("lineNumber").toInt()}};
            auto result=value.toVariant().toMap();result.insert("workerMilliseconds",double(timer.nsecsElapsed())/1000000.);return result;
            }));
      }
}}
