// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#pragma once
#include <QObject>
#include <QVariant>
#include <QHash>
#include <QSet>
#include <QMap>
#include "libmscore/pianohands.h"
#include "libmscore/pianofingering.h"
namespace Ms { namespace PluginAPI {
// Constructed inside one analysis worker. Never retains a score object.
class PianoHandAnalysis : public QObject {
      Q_OBJECT
      Q_PROPERTY(int playbackFieldsVersion READ playbackFieldsVersion CONSTANT)
      struct Substitution {
            QString signature;
            QVector<int> fingers;
            QVector<double> changes;
            QVariantMap note;
            int epoch=0;
            int stage(double seconds) const {int n=0;while(n<changes.size()&&changes[n]<=seconds)++n;return n;}
            bool scheduled() const {return changes.size()+1==fingers.size();}
            double completion() const {return scheduled()?changes.back():std::numeric_limits<double>::quiet_NaN();}
            };
      struct State {
            QHash<QString,int> assigned;
            QSet<QString> released;
            PianoHands::Position previous[2];
            QVariantList previousNotes[2];
            QHash<QString,Substitution> substitutions;
            QHash<QString,double> scheduledReleases;
            int lastTick=-1;
            };
      QMap<int,State> _parts;
   public:
      explicit PianoHandAnalysis(QObject* parent=nullptr):QObject(parent) {}
      int playbackFieldsVersion() const {return 1;}
      Q_INVOKABLE QVariantMap analyse(const QVariantList&,const QVariantList&,const QVariantMap&);
      };
} }
