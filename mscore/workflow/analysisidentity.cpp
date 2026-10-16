// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "mscore/plugin/api/scoreobserver.h"
#include "libmscore/score.h"
#include <QUuid>

namespace Ms {
namespace PluginAPI {
namespace {
// Owned by the score, so independent plugin wrappers see the same identity.
class AnalysisIdentity final : public QObject {
public:
      QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
      ScoreContentState state;
      quint64 revision = 0;
      explicit AnalysisIdentity(Ms::Score* score) : QObject(score)
            { setObjectName("kumoAnalysisIdentity"); }
      };

AnalysisIdentity* identity(Ms::Score* score)
      {
      if (!score) return nullptr;
      for (auto child : score->children())
            if (auto value = dynamic_cast<AnalysisIdentity*>(child)) return value;
      return new AnalysisIdentity(score);
      }
}

QString ScoreObserver::analysisScoreId() const
      {
      const auto value = identity(_score);
      return value ? value->id : QString();
      }

QString ScoreObserver::analysisContentRevision() const
      {
      auto value = identity(_score);
      if (!value) return QString();
      const auto state = _score->masterScore()->state();
      if (!value->revision || value->state != state) {
            value->state = state;
            ++value->revision;
            }
      return value->id + ":" + QString::number(value->revision);
      }
}
}
