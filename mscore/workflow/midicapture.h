// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#pragma once
#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <QTimer>
#include <QVector>
#include <QSet>
#include <QMap>
#include "libmscore/score.h"

namespace Ms {
class NPlayEvent;
qint64 midiInputTime();
// Called on the GUI thread, after the existing sequencer input FIFO.
bool captureMidiInput(const NPlayEvent&, qint64 timestamp);
void stopMidiCaptureInput(const QString& reason);

class MidiCaptureSession : public QObject {
      Q_OBJECT
      Q_PROPERTY(QVariantMap state READ state NOTIFY changed)
      struct RecordedEvent { qint64 time; int type, channel, a, b; };
      QVector<RecordedEvent> _events;
      QVector<QSet<int>> _target;
      QVector<double> _cost;
      QSet<int> _onset;
      QMap<int,int> _keys, _pedals;
      QPointer<Score> _source;
      ScoreContentState _sourceState;
      QTimer _timer;
      qint64 _start=0, _end=0, _lastOn=0, _groupTime=0;
      int _count=0, _groups=0, _lastPitch=-1, _lastVelocity=0, _monitorChannel=0;
      int _generation=0;
      double _duration=0;
      QString _mode, _reason, _device;
      bool _recording=false, _stopping=false, _saving=false, _monitor=false, _checkSource=false, _requireDevice=false, _tap=false, _atEnd=false;
      void receive(const NPlayEvent&, qint64);
      void monitor(const NPlayEvent&);
      void releaseMonitor();
      void finishGroup();
      void pulse();
      void finish();
      friend bool captureMidiInput(const NPlayEvent&, qint64);
      friend void stopMidiCaptureInput(const QString&);
public:
      explicit MidiCaptureSession(QObject* parent=nullptr);
      ~MidiCaptureSession() override;
      QVariantMap state() const;
      Q_INVOKABLE QVariantMap start(const QVariantMap& options);
      // C++ test harnesses may supply a synthetic MIDI source without a sound device.
      QVariantMap begin(const QVariantMap& options, bool requireDevice);
      Q_INVOKABLE void stop();
      Q_INVOKABLE void cancel();
signals:
      void changed();
      void finished(const QString& path, const QString& reason);
      void failed(const QString& message);
};
}
