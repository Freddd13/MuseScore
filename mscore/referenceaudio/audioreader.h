// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
#include <functional>
#include <atomic>
#include <vector>
#include <memory>
namespace KumoAudio {
class Reader {
 struct State;std::unique_ptr<State> s;
public:
 Reader();~Reader();bool open(const QString&,QString*,const std::atomic_bool* cancel=nullptr);
 int rate()const;int channels()const;qint64 frames()const;
 qint64 read(float*,qint64);bool seek(qint64);
};
struct Segment {double start=0,end=0,speed=1;bool fade=false;};
bool stream(const QString&,const Segment&,int,
 const std::function<bool(const float*,int)>&,const std::atomic_bool&,QString*);
bool streamSequence(const QString&,const std::vector<Segment>&,int,
 const std::function<bool(const float*,int)>&,const std::atomic_bool&,QString*);
}
