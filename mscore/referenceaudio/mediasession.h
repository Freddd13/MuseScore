// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QVariantMap>
#include <QVariantList>
#include <QTimer>
#include <QAudioOutput>
#include <QFuture>
#include <QVector>
#include <QHash>
#include <memory>
#include <atomic>
#include "audioreader.h"
class AudioRing;
class ReferenceMediaSession:public QObject {
 Q_OBJECT
 Q_PROPERTY(bool available READ available CONSTANT)
 Q_PROPERTY(bool playing READ playing NOTIFY changed)
 Q_PROPERTY(QString path READ path NOTIFY changed)
 Q_PROPERTY(QString status READ status NOTIFY changed)
 Q_PROPERTY(double duration READ duration NOTIFY changed)
 Q_PROPERTY(double position READ position NOTIFY changed)
 Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY changed)
 Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY changed)
 Q_PROPERTY(QVariantList waveform READ waveform NOTIFY waveformChanged)
 QString _path,_status;
 double _duration=0,_position=0,_speed=1,_volume=.8,_a=0,_b=0,_origin=0;
 bool _loop=false,_fade=true;
 int _rate=44100,_generation=0,_loadGeneration=0;
 QAudioOutput* _audio=nullptr;AudioRing* _ring=nullptr;QTimer _timer;QVariantList _wave;
 std::shared_ptr<std::atomic_bool> _cancel,_loadCancel;
 QFuture<void> _playJob;QVector<QFuture<void>> _loads;
 std::shared_ptr<std::atomic_bool> _waveCancel;int _waveGeneration=0;
 QHash<QString,QVariantList> _tiles;bool _outputStarted=false;
 void stopWorker();void updateClock();void sampleClock();
public:
 explicit ReferenceMediaSession(QObject*);~ReferenceMediaSession()override;
 bool available()const{return true;}bool playing()const{return _audio!=nullptr;}
 QString path()const{return _path;}QString status()const{return _status;}
 double duration()const{return _duration;}double position()const{return _position;}
 double speed()const{return _speed;}double volume()const{return _volume;}
 QVariantList waveform()const{return _wave;}
 Q_INVOKABLE void open(const QString&);Q_INVOKABLE void play();Q_INVOKABLE void pause();
 Q_INVOKABLE void seek(double);Q_INVOKABLE void setLoop(double,double,bool,bool fade=true);
 Q_INVOKABLE QVariantMap identity()const;
 Q_INVOKABLE void requestWaveform(double from,double until,int bins);
 void setSpeed(double);void setVolume(double);
signals:
 void changed();void waveformChanged();void waveformReady(double from,double until,const QVariantList& peaks);void fileReady(const QVariantMap& info);
};
