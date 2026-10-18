// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include "../taptempo/tapestimate.h"
#include <QElapsedTimer>
#include <QVariantMap>
#include <QWidget>
class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QCheckBox;
class QLabel;
class QToolButton;
namespace Ms {
class SpectrumBeatControls : public QWidget {
    Q_OBJECT
    QDoubleSpinBox *_bpm, *_first;
    QSpinBox *_numerator;
    QComboBox *_denominator, *_source, *_candidates, *_unit;
    QCheckBox *_visible;
    QToolButton *_estimate;
    QLabel *_status;
    QVariantList _choices;
    TapEstimate _tap;
    QElapsedTimer _clock;
    bool _loading = false, _tapSource = false;
    qint64 _lastTap = -1;
    double _firstTap = 0;
    void edited();
    void choose(int);

  public:
    explicit SpectrumBeatControls(QWidget *);
    QVariantMap values() const;
    void restore(const QVariantMap &);
    void setFirstBeat(double ms);
    void tap(double sourceMs, bool sourcePlaying);
    void setBusy(bool);
    void setCandidates(const QVariantMap &);
  signals:
    void changed();
    void estimateRequested();
    void tapRequested();
    void firstBeatRequested();
    void saveRequested();
};
} // namespace Ms
