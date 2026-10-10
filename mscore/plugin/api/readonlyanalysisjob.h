// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QVariantMap>
#include <QFutureWatcher>
#include <QSharedPointer>
#include <QMutex>
#include <atomic>
class QJSEngine;
namespace Ms { namespace PluginAPI {
class ReadOnlyAnalysisJob : public QObject {
      Q_OBJECT
      struct State {QMutex mutex;QJSEngine* engine=nullptr;std::atomic_bool canceled{false};};
      QSharedPointer<State> _state;
      QFutureWatcher<QVariantMap> _watcher;
   public:
      explicit ReadOnlyAnalysisJob(QObject* parent=nullptr);
      ~ReadOnlyAnalysisJob() override;
      void start(const QString& source,const QVariantMap& input);
      void cancel();
   signals:
      void finished(const QVariantMap& result);
      };
}}
