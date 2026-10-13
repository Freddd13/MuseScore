// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#pragma once
#include "../referenceaudio/referencehost.h"
#include <QVector>
#include <QPair>
namespace Ms {
KumoReferenceHost referenceAudioHost();
void mixReferenceAudio(float*,unsigned,int,bool);
void setReferenceClock(const QVector<QPair<double,double>>&);
double referenceOutputTime(double);
double referenceScoreTime(double);
void setReferenceEndTick(int);
int referenceEndTick();
class ReferenceAudioPeriod {
      float* _output;unsigned _frames;int _musicFrame;bool _musicPlaying;
   public:
      ReferenceAudioPeriod(float* output,unsigned frames,int frame,bool playing):_output(output),_frames(frames),_musicFrame(frame),_musicPlaying(playing) {}
      ~ReferenceAudioPeriod(){mixReferenceAudio(_output,_frames,_musicFrame,_musicPlaying);}
      };
}
