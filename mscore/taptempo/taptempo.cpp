// SPDX-License-Identifier: GPL-2.0-or-later
#include "taptempo.h"
#include "mscore/workflow/tapwrite.h"
#include "libmscore/score.h"
#include "mscore/seq.h"
#include "mscore/shortcut.h"
#include "mscore/musescore.h"
#include <QAction>
#include <QComboBox>
#include <QGridLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPushButton>
#include <QToolButton>
#include <QWidgetAction>
namespace Ms {
namespace {
class TapButton : public QToolButton {
      void press(QMouseEvent* event) {
            if (event->button() != Qt::LeftButton) { QToolButton::mousePressEvent(event); return; }
            if (menu() && event->pos().x() >= width()-20) { showMenu(); event->accept(); return; }
            setDown(true); defaultAction()->trigger(); event->accept();
            }
      void mousePressEvent(QMouseEvent* e) override { press(e); }
      void mouseDoubleClickEvent(QMouseEvent* e) override { press(e); }
      void mouseReleaseEvent(QMouseEvent* e) override { setDown(false); e->accept(); }
      void keyPressEvent(QKeyEvent* e) override {
            if(e->isAutoRepeat()) {e->accept();return;}
            if(e->key()==Qt::Key_Space || e->key()==Qt::Key_Return || e->key()==Qt::Key_Enter) {setDown(true);defaultAction()->trigger();e->accept();return;}
            QToolButton::keyPressEvent(e);
            }
      void keyReleaseEvent(QKeyEvent* e) override {
            if(e->isAutoRepeat()) {e->accept();return;}
            if(e->key()==Qt::Key_Space || e->key()==Qt::Key_Return || e->key()==Qt::Key_Enter) {setDown(false);e->accept();return;}
            QToolButton::keyReleaseEvent(e);
            }
   public:
      using QToolButton::QToolButton;
      };
}
TapTempo::TapTempo(QObject* parent) : QObject(parent) { setObjectName("tapTempo"); _clock.start(); }
TapTempo* TapTempo::instance(QWidget* owner) {
      auto value = owner->findChild<TapTempo*>("tapTempo", Qt::FindDirectChildrenOnly);
      return value ? value : new TapTempo(owner);
      }
QWidget* TapTempo::createTapControl(QWidget* parent) {
      auto button = new TapButton(parent); button->setDefaultAction(getAction("tap-tempo"));
      button->setText(tr("Tap · 点击一拍")); button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon); return button;
      }
void TapTempo::populateUnits(QComboBox* unit) {
      for (const auto& item : QVector<QPair<QString,double>>{{tr("十六分音符"),.25},{tr("八分音符"),.5},
                  {tr("附点八分音符"),.75},{tr("四分音符"),1.},{tr("附点四分音符"),1.5},
                  {tr("二分音符"),2.},{tr("附点二分音符"),3.},{tr("全音符"),4.}})
            unit->addItem(item.first,item.second);
      }
void TapTempo::tap() {
      emit tapRecorded(_clock.elapsed());
      if (_estimate.add(_clock.elapsed())) update();
      if (!_button) {auto owner=qobject_cast<QWidget*>(parent());if(owner)createButton(owner,getAction("tap-tempo"))->hide();}
      if (_button && !_button->isVisible() && _button->menu() && !parent()->findChild<QWidget*>("tapMarkDialog"))
            _button->menu()->popup(qobject_cast<QWidget*>(parent())->mapToGlobal(QPoint(120,60)));
      }
void TapTempo::update() {
      const double bpm = _estimate.bpm(_quarterBeats)*_multiplier;
      if (_button) _button->setText(bpm > 0 ? QString("TAP · %1").arg(bpm,0,'f',0) : QString("TAP · —"));
      if (_value) _value->setText(bpm > 0 ? tr("四分音符：%1 BPM\n点击单位：%2 BPM").arg(bpm,0,'f',1).arg(bpm/_quarterBeats,0,'f',1) : tr("请至少点击四拍"));
      if (_status) _status->setText(tr("%1 · 已点击 %2 次").arg(!_estimate.ready()?tr("正在起步"):_estimate.stable()?tr("点击稳定"):tr("点击间隔不均匀")).arg(_estimate.count()));
      if (_apply) {
            const bool following = seq && seq->isPlaying() && seq->independentMetronomeFollowPlayback();
            _apply->setEnabled(_estimate.ready() && seq && bpm >= 20. && bpm <= 400. && !following);
            _apply->setToolTip(following ? tr("节拍器正在跟随乐谱播放；停止后可设置独立速度。") : tr("独立节拍器可设置20–400四分音符BPM。"));
            }
      }
QWidget* TapTempo::createButton(QWidget* parent, QAction* action) {
      auto button = new TapButton(parent); _button = button;
      action->setAutoRepeat(false);
      button->setDefaultAction(action); button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
      button->setFixedWidth(qMax(112, button->fontMetrics().horizontalAdvance("TAP · 9999")+54));
      button->setAccessibleName(tr("Tap tempo"));
      button->setPopupMode(QToolButton::MenuButtonPopup);
      auto menu = new QMenu(button); button->setMenu(menu);
      auto body = new QWidget(menu); auto grid = new QGridLayout(body);
      auto value = new QLabel(body); _value = value; grid->addWidget(value,0,0,1,3);
      auto status = new QLabel(body); _status = status; grid->addWidget(status,1,0,1,3);
      auto unit = new QComboBox(body);
      populateUnits(unit); unit->setCurrentIndex(unit->findData(_quarterBeats));
      grid->addWidget(unit,2,0,1,3);
      auto half = new QPushButton("÷2",body); auto twice = new QPushButton("×2",body); auto reset = new QPushButton(tr("重新测量"),body);
      grid->addWidget(half,3,0); grid->addWidget(twice,3,1); grid->addWidget(reset,3,2);
      auto tap = new TapButton(body);tap->setDefaultAction(action);tap->setText(tr("TAP")); grid->addWidget(tap,4,0,1,3);
      auto apply = new QPushButton(tr("设为独立节拍器"),body); _apply=apply; grid->addWidget(apply,5,0,1,3);
      auto write = new QPushButton(tr("写入指定谱面位置…"),body);grid->addWidget(write,6,0,1,3);
      connect(write,&QPushButton::clicked,this,[this]{const double bpm=_estimate.bpm(_quarterBeats)*_multiplier;if(_estimate.ready()&&bpm>=5&&bpm<=999)showTapWriteDialog(mscore->currentScore(),bpm);});
      connect(menu,&QMenu::aboutToShow,write,[this,write]{const double bpm=_estimate.bpm(_quarterBeats)*_multiplier;write->setEnabled(_estimate.ready()&&bpm>=5&&bpm<=999&&mscore->currentScore()&&(!seq||(!seq->isPlaying()&&seq->backgroundRenderingIdle())));});
      auto item = new QWidgetAction(menu); item->setDefaultWidget(body); menu->addAction(item);
      connect(unit,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this,unit](int){_quarterBeats=unit->currentData().toDouble();update();});
      connect(half,&QPushButton::clicked,this,[this]{_multiplier=qMax(1./1024.,_multiplier*.5);update();});
      connect(twice,&QPushButton::clicked,this,[this]{_multiplier=qMin(1024.,_multiplier*2.);update();});
      connect(reset,&QPushButton::clicked,this,[this]{_estimate.reset();_multiplier=1.;update();});
      connect(apply,&QPushButton::clicked,this,[this]{if(seq && _apply->isEnabled())seq->setIndependentMetronomeBpm(_estimate.bpm(_quarterBeats)*_multiplier);});
      connect(menu,&QMenu::aboutToShow,this,&TapTempo::update);
      if(seq) { connect(seq,&Seq::started,this,&TapTempo::update,Qt::UniqueConnection);connect(seq,&Seq::stopped,this,&TapTempo::update,Qt::UniqueConnection); }
      update(); return button;
      }
}
