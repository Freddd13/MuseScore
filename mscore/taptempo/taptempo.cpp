// SPDX-License-Identifier: GPL-2.0-or-later
#include "taptempo.h"
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
void TapTempo::tap() {
      if (_estimate.add(_clock.elapsed())) update();
      if (!_button) {auto owner=qobject_cast<QWidget*>(parent());if(owner)createButton(owner,getAction("tap-tempo"))->hide();}
      if (_button && !_button->isVisible() && _button->menu())_button->menu()->popup(qobject_cast<QWidget*>(parent())->mapToGlobal(QPoint(120,60)));
      }
void TapTempo::update() {
      const double bpm = _estimate.bpm(_quarterBeats)*_multiplier;
      if (_button) _button->setText(bpm > 0 ? QString("TAP · %1").arg(bpm,0,'f',0) : QString("TAP · —"));
      if (_value) _value->setText(bpm > 0 ? tr("Quarter BPM: %1\nTap BPM: %2").arg(bpm,0,'f',1).arg(bpm/_quarterBeats,0,'f',1) : tr("Tap at least four beats"));
      if (_status) _status->setText(tr("%1 · %2 taps").arg(!_estimate.ready()?tr("Starting"):_estimate.stable()?tr("Stable"):tr("Unstable")).arg(_estimate.count()));
      if (_apply) {
            const bool following = seq && seq->isPlaying() && seq->independentMetronomeFollowPlayback();
            _apply->setEnabled(_estimate.ready() && seq && bpm >= 20. && bpm <= 400. && !following);
            _apply->setToolTip(following ? tr("Metronome is following the score") : tr("Independent metronome range: 20–400 quarter BPM"));
            }
      }
QWidget* TapTempo::createButton(QWidget* parent, QAction* action) {
      auto button = new TapButton(parent); _button = button;
      action->setAutoRepeat(false);
      button->setDefaultAction(action); button->setToolButtonStyle(Qt::ToolButtonTextOnly);
      button->setFixedWidth(qMax(112, button->fontMetrics().horizontalAdvance("TAP · 9999")+32));
      button->setAccessibleName(tr("Tap tempo"));
      button->setPopupMode(QToolButton::MenuButtonPopup);
      auto menu = new QMenu(button); button->setMenu(menu);
      auto body = new QWidget(menu); auto grid = new QGridLayout(body);
      auto value = new QLabel(body); _value = value; grid->addWidget(value,0,0,1,3);
      auto status = new QLabel(body); _status = status; grid->addWidget(status,1,0,1,3);
      auto unit = new QComboBox(body);
      unit->addItem(tr("Eighth note"),.5); unit->addItem(tr("Quarter note"),1.);
      unit->addItem(tr("Dotted quarter"),1.5); unit->addItem(tr("Half note"),2.); unit->setCurrentIndex(unit->findData(_quarterBeats));
      grid->addWidget(unit,2,0,1,3);
      auto half = new QPushButton("÷2",body); auto twice = new QPushButton("×2",body); auto reset = new QPushButton(tr("Reset"),body);
      grid->addWidget(half,3,0); grid->addWidget(twice,3,1); grid->addWidget(reset,3,2);
      auto tap = new TapButton(body);tap->setDefaultAction(action);tap->setText(tr("TAP")); grid->addWidget(tap,4,0,1,3);
      auto apply = new QPushButton(tr("Set independent metronome"),body); _apply=apply; grid->addWidget(apply,5,0,1,3);
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
