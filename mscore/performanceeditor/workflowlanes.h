// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#pragma once
#include <QWidget>
#include <QPointer>
#include <QVariantMap>
#include <QHash>
#include <QVector>
namespace Ms {
class PerformanceEditor;
class WorkflowLanes : public QWidget {
      Q_OBJECT
      PerformanceEditor* _editor;
      QHash<QString,QPointer<QObject>> _waves;
      struct Hit {
            QRectF bounds;
            QString scoreId, midiId;
            int track = -1;
            QString tooltip;
      };
      QVector<Hit> _hits;
      QString _expressionVersion;
      QVariantList _matches;
      QVector<QPair<double,double>> _timing;
      void refreshExpression();
      QString _scoreId,_midiId;
      int _vertical=0;
      bool _tempo=false;
      QObject* _media=nullptr;
      QObject* wave(const QVariantMap&);
      bool matchEnabled(const QVariantMap &) const;
      void paintEvent(QPaintEvent*)override;
      void mousePressEvent(QMouseEvent*)override;
      void mouseMoveEvent(QMouseEvent *) override;
      void wheelEvent(QWheelEvent*)override;
      void showEvent(QShowEvent*)override;
      void hideEvent(QHideEvent*)override;
public:
      explicit WorkflowLanes(PerformanceEditor*);
      QSize minimumSizeHint()const override{return QSize(260,100);}
      void operation(const QString&);
      void setTempo(bool value){_tempo=value;update();}
private slots:
      void waveChanged(){update();}
};
void addWorkflowLanes(PerformanceEditor*,QWidget**,QWidget*);
}
