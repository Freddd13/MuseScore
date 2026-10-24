// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include <QVariantMap>
#include <QVector>
namespace Ms {
class Score;
class Channel;
class MasterSynthesizer;
QVariantMap commonPianoSettings();
quint64 commonPianoRevision();
bool isPianoChannel(Score*,const Channel*);
bool scoreUsesOwnPiano(Score*);
QString activeCommonPianoName(Score*,const Channel*);
QVariantMap commonPianoFromCurrent(Score*,const Channel*,bool builtIn=false);
QVariantMap applyCommonPianoSettings(Score*,const QVariantMap&);
QVariantMap setScoreOwnPiano(Score*,bool);
void captureCommonPianoEdits(Score*,int preferredSlot=0);
void prepareCommonPianos(MasterSynthesizer*,Score*,bool refresh=false);
void routeCommonPianos(MasterSynthesizer*,Score*,QVector<int>& banks);
}
