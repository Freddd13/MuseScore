// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#pragma once
#include "referencehost.h"
#include <QObject>
#include <QVariantMap>
#include <QVariantList>
#include <QThreadPool>
#include <QTimer>
#include <memory>
class QAudioOutput;
class QIODevice;
class ReferenceMediaProject : public QObject {
      Q_OBJECT
      Q_PROPERTY(bool available READ available CONSTANT)
      Q_PROPERTY(bool playing READ playing NOTIFY changed)
      Q_PROPERTY(bool joint READ joint NOTIFY changed)
      Q_PROPERTY(QString status READ status NOTIFY changed)
      Q_PROPERTY(double position READ position NOTIFY changed)
      Q_PROPERTY(QVariantList tracks READ tracks NOTIFY tracksChanged)
      Q_PROPERTY(int selectedTrack MEMBER _selectedTrack NOTIFY tracksChanged)
      Q_PROPERTY(QVariantMap playbackSettings MEMBER _settings NOTIFY tracksChanged)
      Q_PROPERTY(bool expressionPreview MEMBER _expressionPreview NOTIFY tracksChanged)
      Q_PROPERTY(int scoreTick MEMBER _scoreTick NOTIFY changed)
      Q_PROPERTY(QVariantMap diagnostics READ diagnostics NOTIFY changed)
      struct Session;
      KumoReferenceHost _host;
      QThreadPool _pool;
      QTimer _timer;
      std::shared_ptr<Session> _session;
      QAudioOutput* _audio=nullptr;
      QIODevice* _device=nullptr;
      QVariantList _tracks;
      int _selectedTrack=0;
      QVariantMap _settings;
      bool _expressionPreview=false;
      int _scoreTick=0;
      QString _status;
      bool _joint=false;
      static void mix(void*,float*,unsigned,int,bool);
      void poll();
public:
      ReferenceMediaProject(QObject*,const KumoReferenceHost&);
      ~ReferenceMediaProject() override;
      bool available()const{return true;}
      bool playing()const{return bool(_session);}
      bool joint()const{return _joint;}
      QString status()const{return _status;}
      double position()const;
      QVariantList tracks()const{return _tracks;}
      QVariantMap diagnostics()const;
      Q_INVOKABLE QVariantMap setTracks(const QVariantList&);
      // Plan contains only file names, sample times and gains; never score objects.
      Q_INVOKABLE QVariantMap start(const QVariantMap&);
      Q_INVOKABLE void stop();
      Q_INVOKABLE void toggleIndependent();
      Q_INVOKABLE void requestJoint();
signals:
      void changed();
      void tracksChanged();
      void finished(bool wasJoint);
      void independentRequested();
      void jointRequested();
};
