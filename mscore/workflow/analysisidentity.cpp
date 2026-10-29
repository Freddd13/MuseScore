// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "mscore/plugin/api/scoreobserver.h"
#include "libmscore/score.h"
#include "libmscore/measurebase.h"
#include "libmscore/textbase.h"
#include "libmscore/page.h"
#include "libmscore/excerpt.h"
#include "mscore/musescore.h"
#include <QTabBar>
#include "mscore/scoretab.h"
#include "personalbranding.h"
#include <QCoreApplication>
#include <QFileInfo>
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

QString ScoreObserver::applicationInstanceId()
      {
      static const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
      return id;
      }

QVariantMap ScoreObserver::scoreIdentity() const
      { return scoreIdentityFor(_score); }

QVariantMap ScoreObserver::scoreIdentityFor(Ms::Score* score)
      {
      QVariantMap application{{"instanceId", applicationInstanceId()},
            {"pid", QCoreApplication::applicationPid()},
            {"executable", QCoreApplication::applicationFilePath()},
            {"version", KUMO_PERSONAL_VERSION},
            {"windowTitle", mscore ? mscore->windowTitle() : QString()}};
      QVariantMap result{{"schemaVersion", "score-identity/1"}, {"application", application}, {"hasScore", bool(score)}};
      if (!score) return result;
      auto master = score->masterScore();
      const bool current = mscore && mscore->currentScore() == score;
      const int tabIndex = mscore ? mscore->scores().indexOf(master) : -1;
      QString documentTab = master->title();
      QString pane = "primary";
      if (mscore) {
            auto tab = mscore->getTab1();
            if (current && mscore->getTab2() && mscore->currentScoreTab() == mscore->getTab2()) {
                  tab = mscore->getTab2(); pane = "secondary";
                  }
            if (tab && tab->getTab() && tabIndex >= 0 && tabIndex < tab->getTab()->count())
                  documentTab = tab->getTab()->tabText(tabIndex);
            }
      const QString partName = score->isMaster() || !score->excerpt() ? QString() : score->excerpt()->title();
      const QString tabLabel = partName.isEmpty() ? documentTab : partName;
      struct Titles { QVariantList rows; int frame = 0; } titles;
      // Titles live in frames, including later movements and nested frames.
      // Walking those frames leaves the note and playback indexes untouched.
      for (auto frame = score->first(); frame; frame = frame->next(), ++titles.frame) {
            if (frame->isMeasure()) continue;
            frame->scanElements(&titles, [](void* opaque, Ms::Element* element) {
                  if (!element->isTextBase()) return;
                  auto text = Ms::toTextBase(element);
                  const QString role = text->tid() == Tid::TITLE ? "title" : text->tid() == Tid::SUBTITLE ? "subtitle" : QString();
                  if (role.isEmpty()) return;
                  auto target = static_cast<Titles*>(opaque);
                  bool visible = true;
                  for (auto parent = element; parent; parent = parent->parent()) visible &= parent->visible();
                  QVariantMap row{{"text", text->plainText()}, {"role", role}, {"visible", visible},
                        {"frameIndex", target->frame}, {"index", target->rows.size()}};
                  for (auto parent = element->parent(); parent; parent = parent->parent())
                        if (parent->isPage()) { row["page"] = Ms::toPage(parent)->no() + 1; break; }
                  target->rows.append(row);
                  }, true);
            }
      QString visibleTitle;
      for (const auto& value : titles.rows) {
            const auto row = value.toMap();
            if (row.value("role") == "title" && row.value("visible").toBool() && !row.value("text").toString().trimmed().isEmpty()) {
                  visibleTitle = row.value("text").toString().trimmed(); break;
                  }
            }
      const QString workTitle = score->metaTag("workTitle");
      const QString path = master->created() ? QString() : master->fileInfo()->absoluteFilePath();
      QString displayTitle = visibleTitle, source = "visibleTitle";
      if (displayTitle.isEmpty()) { displayTitle = workTitle.trimmed(); source = "workTitle"; }
      if (displayTitle.isEmpty()) { displayTitle = tabLabel; source = "tabLabel"; }
      if (displayTitle.isEmpty()) { displayTitle = QFileInfo(path).fileName(); source = "fileName"; }
      if (displayTitle.isEmpty()) { displayTitle = tr("未命名乐谱"); source = "untitled"; }
      auto local = identity(score), root = identity(master);
      result.insert("scoreSessionId", local->id);
      result.insert("masterSessionId", root->id);
      result.insert("isMaster", score->isMaster());
      result.insert("partName", partName);
      result.insert("isCurrentScore", current);
      result.insert("tabIndex", tabIndex);
      result.insert("pane", pane);
      result.insert("tabLabel", tabLabel);
      result.insert("documentTabLabel", documentTab);
      result.insert("filePath", path);
      result.insert("fileName", QFileInfo(path).fileName());
      result.insert("importedFrom", master->importedFilePath());
      result.insert("newUnsaved", master->created());
      result.insert("modified", master->dirty());
      result.insert("visibleTitle", visibleTitle);
      result.insert("visibleTexts", titles.rows);
      result.insert("workTitle", workTitle);
      result.insert("displayTitle", displayTitle);
      result.insert("titleSource", source);
      return result;
      }
}
}
