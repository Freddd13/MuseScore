// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "referencebus.h"
#include "libmscore/mscore.h"
#include <atomic>
#include <thread>
#include <chrono>
#include <algorithm>
#include <memory>
namespace Ms {
namespace {
std::atomic<KumoReferenceMix> callback{nullptr};
std::atomic<void*> owner{nullptr};
std::atomic<unsigned> readers{0};
using Clock=QVector<QPair<double,double>>;
std::atomic<const Clock*> clock{nullptr};
std::unique_ptr<Clock> ownedClock;
std::atomic<unsigned> clockReaders{0};
std::atomic<int> endTick{-1},reachedTick{-1};
double mapped(double value,bool reverse) {
      ++clockReaders;const Clock* points=clock.load();double result=value;
      if(points && points->size()>1){auto x=[reverse](const QPair<double,double>& p){return reverse?p.second:p.first;};auto y=[reverse](const QPair<double,double>& p){return reverse?p.first:p.second;};
            auto it=std::upper_bound(points->begin(),points->end(),value,[&](double v,const QPair<double,double>& p){return v<x(p);});
            if(it==points->begin())result=value+y(points->front())-x(points->front());
            else if(it==points->end())result=value+y(points->back())-x(points->back());
            else{const auto& a=*(it-1);const auto& b=*it;result=y(a)+(value-x(a))*(y(b)-y(a))/(x(b)-x(a));}}
      --clockReaders;return result;
      }
int rate(){return int(MScore::sampleRate);}
bool attach(KumoReferenceMix mix,void* context) {
      if(!mix || !context || callback.load())return false;
      owner.store(context);callback.store(mix);return true;
      }
void detach(void* context) {
      if(owner.load()!=context)return;callback.store(nullptr);
      // GUI shutdown can wait for one period; the sound output never waits.
      while(readers.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));owner.store(nullptr);
      }
}
KumoReferenceHost referenceAudioHost(){return {1,rate,attach,detach};}
void setReferenceClock(const QVector<QPair<double,double>>& points){
      clock.store(nullptr);while(clockReaders.load())std::this_thread::sleep_for(std::chrono::milliseconds(1));ownedClock.reset();
      if(points.size()>1){ownedClock.reset(new Clock(points));clock.store(ownedClock.get());}
      }
double referenceOutputTime(double value){if(!clock.load())return value;return mapped(value,false);}
double referenceScoreTime(double value){if(!clock.load())return value;return mapped(value,true);}
void setReferenceEndTick(int tick){if(tick>=0)reachedTick=-1;endTick=tick;}
void markReferenceEndReached(int tick){reachedTick=tick;}
int referenceReachedTick(){return reachedTick.load();}
int referenceEndTick(){return endTick.load();}
void mixReferenceAudio(float* output,unsigned frames,int musicFrame,bool playing) {
      if(!callback.load())return;
      ++readers;const auto mix=callback.load();if(mix)mix(owner.load(),output,frames,musicFrame,playing);--readers;
      }
}
