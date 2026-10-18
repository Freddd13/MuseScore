// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "regionalcontrols.h"
#include "featuregate.h"
#include "libmscore/measure.h"
#include "libmscore/page.h"
#include "libmscore/regionalvariants.h"
#include "libmscore/score.h"
#include "libmscore/segment.h"
#include "libmscore/system.h"
#include "mscore/musescore.h"
#include "mscore/scoreview.h"
#include "mscore/seq.h"
#include "referencebus.h"
#include "referencefactory.h"
#include "regionalpanel.h"
#include "regionalcomparison.h"
#include <QWheelEvent>
#include <cmath>
#include <QApplication>
#include <QHash>
#include <QMenu>
#include <QPointer>
#include <QSet>
#include <QSettings>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
namespace Ms {
namespace {
class Controls : public QObject {
      QPointer<ScoreView> _view;
      QHash<QString, QPointer<QToolButton>> _buttons;
      bool _queued = false;
      QPointer<Score> _owner;
      void choose(const QString &group, const QString &variant) {
            auto score = _view ? _view->score() : nullptr;
            if (!score || !score->isMaster())
                  return;
            const auto result=switchRegionalWriting(_view,group,variant);
            if(result.contains("error"))QToolTip::showText(QCursor::pos(),result.value("error").toString(),_view);
            else if(result.contains("playbackError"))QToolTip::showText(QCursor::pos(),result.value("playbackError").toString(),_view);
      }
      bool eventFilter(QObject* object,QEvent* event)override{
            if(event->type()!=QEvent::Wheel)return QObject::eventFilter(object,event);auto wheel=static_cast<QWheelEvent*>(event);if(wheel->modifiers()!=Qt::ControlModifier)return false;
            auto score=_view?_view->score():nullptr;if(!score)return false;const auto key=object->property("regionalGroup").toString();
            for(auto value:RegionalVariants::state(score).value("groups").toList()){auto g=value.toMap();if(g.value("id").toString()!=key)continue;auto variants=g.value("variants").toList();if(variants.size()<2)break;
                  int index=0;for(int i=0;i<variants.size();++i)if(variants[i].toMap().value("id")==g.value("active"))index=i;
                  const int delta=wheel->angleDelta().y()+object->property("regionalWheelDelta").toInt();object->setProperty("regionalWheelDelta",delta%120);if(std::abs(delta)<120){wheel->accept();return true;}index=(index-delta/120)%variants.size();if(index<0)index+=variants.size();choose(key,variants[index].toMap().value("id").toString());wheel->accept();return true;}
            return false;
      }
      void menu(QMenu *menu, const QString &id) {
            menu->clear();
            auto score = _view ? _view->score() : nullptr;
            if (!score)
                  return;
            for (auto value : RegionalVariants::state(score).value("groups").toList()) {
                  const auto group = value.toMap();
                  if (group.value("id").toString() != id)
                        continue;
                  auto title = menu->addAction(group.value("name").toString());
                  title->setEnabled(false);
                  for (auto option : group.value("variants").toList()) {
                        const auto version = option.toMap();
                        auto action = menu->addAction(version.value("name").toString());
                        action->setData(version.value("id"));
                        action->setCheckable(true);
                        action->setChecked(version.value("id") == group.value("active"));
                        connect(action, &QAction::triggered, this,
                                [this, id, version] { choose(id, version.value("id").toString()); });
                  }
                  menu->addSeparator();
                  auto open = menu->addAction(tr("打开这段的方案面板"));
                  connect(open, &QAction::triggered, this, [id] { showRegionalVariantPanel(id); });
                  break;
            }
      }
      void refresh() {
            _queued = false;
            if (!_view)
                  return;
            auto score = _view->score();
            QSet<QString> shown;
            if (_owner != score) {
                  for (auto button : _buttons)
                        if (button)
                              button->deleteLater();
                  _buttons.clear();
                  _owner = score;
            }
            if (regionalControlsVisible() && score && score->isMaster() && _view->isVisible()) {
                  QHash<int, int> stacked;
                  for (auto value : RegionalVariants::state(score).value("groups").toList()) {
                        auto group = value.toMap();
                        const auto tracks = group.value("tracks").toList();
                        if (tracks.isEmpty())
                              continue;
                        auto measure = score->tick2measure(Fraction::fromTicks(group.value("start").toInt()));
                        if (!measure || !measure->system())
                              continue;
                        auto system = measure->system();
                        const int staff = tracks.first().toInt() / VOICES;
                        const auto key = group.value("id").toString();
                        QString name;
                        for (auto option : group.value("variants").toList())
                              if (option.toMap().value("id") == group.value("active"))
                                    name = option.toMap().value("name").toString();
                        auto segment = measure->first(SegmentType::ChordRest);
                        QPointF anchor(segment ? segment->canvasPos().x() : measure->canvasPos().x(),
                                       system->staffYpage(staff) + system->page()->pos().y());
                        const auto where = _view->toPhysical(anchor).toPoint();
                        if (where.y() < -100 || where.y() > _view->height() + 100 || where.x() < -120 ||
                            where.x() > _view->width())
                              continue;
                        auto button = _buttons.value(key);
                        if (!button) {
                              button = new QToolButton(_view);
                              button->setObjectName("regionalScoreSwitch/" + key);
                              button->setProperty("regionalGroup",key);button->installEventFilter(this);
                              button->setAutoRaise(false);
                              button->setPopupMode(QToolButton::InstantPopup);
                              button->setMaximumWidth(180);
                              auto popup = new QMenu(button);
                              button->setMenu(popup);
                              connect(popup, &QMenu::aboutToShow, this,
                                      [this, popup, key] { menu(popup, key); });
                              _buttons.insert(key, button);
                        }
                        QStringList voices;
                        for (auto track : tracks) {
                              auto voice = QString::number(track.toInt() % 4 + 1);
                              if (!voices.contains(voice))
                                    voices << voice;
                        }
                        button->setText(button->fontMetrics().elidedText(
                            tr("声部%1 · %2").arg(voices.join(","), name), Qt::ElideRight, 145));
                        button->setToolTip(
                            tr("%1 · "
                               "谱表%2\n点击或Ctrl+滚轮切换写法；当前修改会保存。可在方案面板的“更多”里隐藏这些按钮。")
                                .arg(group.value("name").toString())
                                .arg(staff + 1));
                        button->adjustSize();
                        const int bucket = group.value("start").toInt() * 32 + staff;
                        const int row = stacked[bucket]++;
                        const int x = qBound(0, where.x(), qMax(0, _view->width() - button->width()));
                        int y = qBound(0, where.y() - (row + 1) * (button->height() + 3) - 8,
                                       qMax(0, _view->height() - button->height()));
                        for (const auto &marker : _view->notePreviewMarkerBoxes()) {
                              auto obstacle = _view->toPhysical(marker);
                              if (QRect(x, y, button->width(), button->height())
                                      .intersects(obstacle.adjusted(-3, -3, 3, 3)))
                                    y = qMax(0, obstacle.top() - button->height() - 5);
                        }
                        const QRect box(QPoint(x, y), button->size());
                        if (button->geometry() != box)
                              button->setGeometry(box);
                        if (!button->isVisible()) {
                              button->show();
                              button->raise();
                        }
                        shown.insert(key);
                  }
            }
            for (auto it = _buttons.begin(); it != _buttons.end();) {
                  if (!shown.contains(it.key())) {
                        if (it.value())
                              it.value()->hide();
                  }
                  if (!it.value())
                        it = _buttons.erase(it);
                  else
                        ++it;
            }
      }

    public:
      explicit Controls(ScoreView *view) : QObject(view), _view(view) {
            setObjectName("regionalScoreControls");
      }
      void schedule() {
            if (_queued)
                  return;
            _queued = true;
            QTimer::singleShot(0, this, [this] { refresh(); });
      }
};
} // namespace
bool regionalControlsVisible() {
      return personalFeaturesEnabled() && QSettings().value("regionalVariants/scoreButtons", true).toBool();
}
void setRegionalControlsVisible(bool value) {
      QSettings().setValue("regionalVariants/scoreButtons", value);
      for (auto widget : QApplication::allWidgets())
            if (auto view = qobject_cast<ScoreView *>(widget))
                  view->update();
}
void updateRegionalControls(ScoreView *view) {
      auto object = view->findChild<QObject *>("regionalScoreControls", Qt::FindDirectChildrenOnly);
      if (!object && (!RegionalVariants::active(view->score()) || !regionalControlsVisible()))
            return;
      auto controls = object ? static_cast<Controls *>(object) : new Controls(view);
      controls->schedule();
}
} // namespace Ms
