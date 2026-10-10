// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QElapsedTimer>
#include <QPointer>
#include "tapestimate.h"
class QAction;
class QToolButton;
class QMenu;
class QLabel;
class QPushButton;
class QWidget;
namespace Ms {
class TapTempo : public QObject {
      Q_OBJECT
      TapEstimate _estimate;
      QElapsedTimer _clock;
      double _quarterBeats = 1.;
      double _multiplier = 1.;
      QPointer<QToolButton> _button;
      QPointer<QLabel> _value, _status;
      QPointer<QPushButton> _apply;
      void update();
   public:
      explicit TapTempo(QObject* parent);
      static TapTempo* instance(QWidget* owner);
      QWidget* createButton(QWidget* parent, QAction* action);
      void tap();
      };
}
