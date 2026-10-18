// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "midicapture.h"
#include "featuregate.h"
#include "mscore/musescore.h"
#include "mscore/seq.h"
#include "mscore/scoreview.h"
#include "mscore/pianotools.h"
#include "mscore/preferences.h"
#include "libmscore/staff.h"
#include "libmscore/part.h"
#include "libmscore/instrument.h"
#include "audio/midi/event.h"
#include "config.h"
#if defined(USE_PORTAUDIO) && defined(USE_PORTMIDI)
#include "audiodrivers/pa.h"
#include "audiodrivers/pm.h"
#endif
#include <QFutureWatcher>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSettings>
#include <QDir>
#include <QUrl>
#include <QUuid>
#include <QtConcurrent>
#include <chrono>
#include <algorithm>
#include <cmath>

namespace Ms {
namespace {
QPointer<MidiCaptureSession> activeCapture;
qint64 discardThrough=0;
void variable(QByteArray& bytes, quint32 value) {
      uchar data[4]; int size=0; data[size++]=value&127;
      while ((value>>=7)) data[size++]=(value&127)|128;
      while (size) bytes.append(char(data[--size]));
      }
void number(QByteArray& bytes, quint32 value, int size) {
      for (int i=size-1;i>=0;--i) bytes.append(char((value>>(8*i))&255));
      }
}
qint64 midiInputTime() {
      return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
      }
bool captureMidiInput(const NPlayEvent& event, qint64 timestamp) {
      if (!timestamp) timestamp=midiInputTime();
      if (activeCapture) { activeCapture->receive(event,timestamp); return true; }
      return timestamp<=discardThrough;
      }
void stopMidiCaptureInput(const QString& reason) {
      if(activeCapture && activeCapture->_recording) {activeCapture->_reason=reason;activeCapture->stop();}
      }
MidiCaptureSession::MidiCaptureSession(QObject* parent):QObject(parent) {
      _timer.setInterval(50);
      connect(&_timer,&QTimer::timeout,this,&MidiCaptureSession::pulse);
      }
MidiCaptureSession::~MidiCaptureSession() { cancel(); }
QVariantMap MidiCaptureSession::state() const {
      const double elapsed=_start ? std::max(0.,double((_end?_end:midiInputTime())-_start)/1e9) : 0.;
      return {{"recording",_recording},{"stopping",_stopping},{"saving",_saving},{"seconds",elapsed},{"count",_count},
            {"pitch",_lastPitch},{"velocity",_lastVelocity},{"device",_device},{"reason",_reason}};
      }
QVariantMap MidiCaptureSession::start(const QVariantMap& options) { return begin(options,true); }
QVariantMap MidiCaptureSession::begin(const QVariantMap& options, bool requireDevice) {
      if (!personalFeaturesEnabled()) return {{"error",tr("个人扩展功能已关闭。")}};
      if (_recording||_stopping||_saving) return {{"error",tr("本次录制正在进行，请先停止。")}};
      if (activeCapture && activeCapture!=this) return {{"error",tr("另一个演奏表达窗口正在录制，请先在那个窗口停止。")}};
      if (requireDevice && (!seq || !seq->isRunning() || !seq->driver())) return {{"error",tr("MIDI 输入设备尚未就绪。请在编辑 → 偏好设置 → 输入／输出中选择键盘和声音设备。")}};
      if (requireDevice && (!mscore || !mscore->isMidiInEnabled())) return {{"error",tr("请先打开工具栏上的 MIDI 输入开关，再录制。")}};
#if defined(USE_PORTAUDIO) && defined(USE_PORTMIDI)
      if (requireDevice) if(auto audio=dynamic_cast<Portaudio*>(seq->driver())) {
            auto input=dynamic_cast<PortMidiDriver*>(audio->mididriver());
            if(!input || !input->getInputStream())return {{"error",tr("还没有连接可用的 MIDI 输入。请连接键盘，并在偏好设置的输入／输出中选择它。")}};
            }
#endif
      if (seq && (seq->isPlaying() || !seq->backgroundRenderingIdle())) return {{"error",tr("停播并等待乐谱准备完成后即可录制。")}};
      _mode=options.value("endMode","manual").toString();
      _duration=options.value("duration",30).toDouble();
      if ((_mode!="manual"&&_mode!="duration"&&_mode!="passage") || !std::isfinite(_duration) || _duration<1 || _duration>3600)
            return {{"error",tr("录制时长可设置为 1 秒至 1 小时。")}};
      _target.clear(); _tap=options.value("tap").toBool();
      QMap<int,QSet<int>> groups;
      const int role=options.value("role").toInt(), track=options.value("track").toInt();
      for (auto value:options.value("notes").toList()) {
            auto note=value.toMap(); if (note.value("tieBack").toBool() || note.value("grace").toBool()) continue;
            if(role==2 && note.value("track").toInt()!=track)continue;
            const int pitch=note.value("pitch",-1).toInt(); if(pitch>=0&&pitch<=127)groups[note.value("tick").toInt()].insert(pitch);
            }
      for (const auto& group:groups) {
            if(role==1)_target.append(QSet<int>{*std::max_element(group.cbegin(),group.cend())});
            else _target.append(group);
            }
      if (_mode=="passage" && (_target.isEmpty() || _target.size()>4096)) return {{"error",tr("请选中一段乐谱，再使用“弹完选段后结束”。")}};
      _source=mscore && mscore->currentScoreView() ? mscore->currentScoreView()->score() : nullptr;
      if (requireDevice && !_source) return {{"error",tr("打开乐谱并选中要演奏的段落。")}};
      _checkSource=bool(_source); _requireDevice=requireDevice; _sourceState=_source ? _source->masterScore()->state() : ScoreContentState();
      _monitor=requireDevice && options.value("monitor",true).toBool(); _monitorChannel=0;
      if (_source && _source->nstaves()) {
            int staff=qBound(0,options.value("firstTrack").toInt()/4,_source->nstaves()-1);
            auto instrument=_source->staff(staff)->part()->instrument(Fraction::fromTicks(options.value("start").toInt()));
            if (!instrument->channel().isEmpty()) _monitorChannel=instrument->channel(0)->channel();
            }
      if (_monitor) seq->stopNoteTimer();
      _device=preferences.getString(PREF_IO_PORTMIDI_INPUTDEVICE);
      if (_device.isEmpty()) _device=tr("软件当前 MIDI 输入");
      _events.clear(); _keys.clear(); _pedals.clear(); _onset.clear(); _cost.resize(_target.size()+1);
      for (int i=0;i<_cost.size();++i)_cost[i]=i;
      _count=0; _groups=0; _lastPitch=-1; _lastVelocity=0; _lastOn=0; _groupTime=0; _atEnd=false;
      ++_generation; _reason.clear(); _end=0; _start=midiInputTime(); _recording=true; activeCapture=this;
      _timer.start(); emit changed(); return {{"ok",true}};
      }
void MidiCaptureSession::monitor(const NPlayEvent& event) {
      if (!_monitor || !seq) return;
      if (event.type()==ME_NOTEON || event.type()==ME_NOTEOFF) {
            bool down=event.type()==ME_NOTEON && event.velo()>0;
            bool held=false; for(auto key=_keys.cbegin();key!=_keys.cend();++key) if(key.key()%128==event.pitch()&&key.value()>0){held=true;break;}
            if (down || !held) seq->startNote(_monitorChannel,event.pitch(),down?event.velo():0,0.);
            if (mscore) if(auto piano=mscore->findChild<PianoTools*>()) { if(down)piano->pressPitch(event.pitch());else if(!held)piano->releasePitch(event.pitch()); }
            }
      else if(event.type()==ME_CONTROLLER && (event.controller()==CTRL_SUSTAIN || event.controller()==CTRL_RESET_ALL_CTRL)) {
            int value=0; for(auto pedal:_pedals)value=std::max(value,pedal);
            seq->setController(_monitorChannel,CTRL_SUSTAIN,value);
            }
      else if(event.type()==ME_CONTROLLER && (event.controller()==CTRL_ALL_SOUNDS_OFF || event.controller()==CTRL_ALL_NOTES_OFF)) {
            seq->stopNotes(_monitorChannel);
            if(mscore)if(auto piano=mscore->findChild<PianoTools*>())for(int pitch=0;pitch<128;++pitch)piano->releasePitch(pitch);
            }
      }
void MidiCaptureSession::releaseMonitor() {
      if(_monitor && seq) {
            QSet<int> pitches;for(auto key=_keys.cbegin();key!=_keys.cend();++key)pitches.insert(key.key()%128);
            for(int pitch:pitches) {seq->startNote(_monitorChannel,pitch,0,0.);if(mscore)if(auto piano=mscore->findChild<PianoTools*>())piano->releasePitch(pitch);}
            seq->setController(_monitorChannel,CTRL_SUSTAIN,0);
            }
      _monitor=false;
      }
void MidiCaptureSession::receive(const NPlayEvent& event, qint64 timestamp) {
      if ((!_recording&&!_stopping)||timestamp<_start||(_end&&timestamp>_end))return;
      int type=event.type(),channel=event.channel(),a=event.dataA(),b=event.dataB();
      if(type<ME_NOTEOFF||type>ME_PITCHBEND||channel>15||a>127||b>127)return;
      _events.append({timestamp,type,channel,a,b});
      if(type==ME_NOTEON&&b>0) {
            if(_groupTime&&timestamp-_groupTime>80000000)finishGroup();
            if(_onset.isEmpty())_groupTime=timestamp;
            _onset.insert(a);_lastOn=timestamp;++_count;_lastPitch=a;_lastVelocity=b;++_keys[channel*128+a];
            }
      else if(type==ME_NOTEOFF||(type==ME_NOTEON&&b==0)) {
            int key=channel*128+a;if(_keys.value(key)>1)--_keys[key];else _keys.remove(key);
            }
      else if(type==ME_CONTROLLER&&a==CTRL_SUSTAIN)_pedals[channel]=b;
      else if(type==ME_CONTROLLER&&a==CTRL_RESET_ALL_CTRL)_pedals[channel]=0;
      else if(type==ME_CONTROLLER&&(a==CTRL_ALL_SOUNDS_OFF||a==CTRL_ALL_NOTES_OFF)) {
            for(auto it=_keys.begin();it!=_keys.end();)if(it.key()/128==channel)it=_keys.erase(it);else ++it;
            }
      monitor(event);
      if(_events.size()>=200000 && _recording) { _reason=tr("本次已录下 20 万条 MIDI 事件");stop(); }
      }
void MidiCaptureSession::finishGroup() {
      if(_onset.isEmpty())return;
      ++_groups;
      if(!_target.isEmpty()) {
            QVector<double> next(_cost.size());next[0]=_groups;
            for(int i=1;i<next.size();++i) {
                  int common=0;for(int pitch:_onset)if(_target[i-1].contains(pitch))++common;
                  double substitution=_tap?0.:1.-double(common)/std::max(_onset.size(),_target[i-1].size());
                  next[i]=std::min({_cost[i]+1.,next[i-1]+1.,_cost[i-1]+substitution});
                  }
            _cost.swap(next);
            int common=0;for(int pitch:_onset)if(_target.last().contains(pitch))++common;
            _atEnd=(_tap||common*2>=std::max(_onset.size(),_target.last().size())) && _groups>=std::ceil(_target.size()*.7)
                  && _cost.last()<=std::max(.5,_target.size()*.3);
            }
      _onset.clear();
      }
void MidiCaptureSession::pulse() {
      if(_checkSource && (!_source || !mscore || !mscore->currentScoreView() || mscore->currentScoreView()->score()!=_source || _source->masterScore()->state()!=_sourceState)) {
            cancel();emit failed(tr("乐谱或选段内容已改变。重新选段后可以继续录制。"));return;
            }
      if(_requireDevice&&(!seq||!seq->isRunning()||!seq->driver()||!mscore->isMidiInEnabled())){_reason=tr("MIDI 输入已停止，已保留收到的演奏");stop();}
      const qint64 now=midiInputTime();
      if(_recording) {
            if(_groupTime&&now-_groupTime>120000000)finishGroup();
            if(_mode=="duration"&&now-_start>=qint64(_duration*1e9)){_reason=tr("已到设定时长");stop();}
            else if(_mode=="passage"&&_atEnd&&_keys.isEmpty()&&now-_lastOn>=800000000){_reason=tr("已弹到选段末尾");stop();}
            else if(now-_start>=3600000000000LL){_reason=tr("已录制 1 小时");stop();}
            }
      if(_stopping&&now-_end>=200000000)finish();
      emit changed();
      }
void MidiCaptureSession::stop() {
      if(!_recording)return;
      _recording=false;_stopping=true;_end=midiInputTime();
      if(_reason.isEmpty())_reason=tr("已停止录制");emit changed();
      }
void MidiCaptureSession::cancel() {
      ++_generation;_timer.stop();if(activeCapture==this){discardThrough=midiInputTime();activeCapture.clear();}
      releaseMonitor();_recording=false;_stopping=false;_saving=false;_keys.clear();_pedals.clear();emit changed();
      }
void MidiCaptureSession::finish() {
      _timer.stop();discardThrough=_end;activeCapture.clear();releaseMonitor();
      // Close held keys in the saved take at the user's stop time.
      for(auto key=_keys.cbegin();key!=_keys.cend();++key)for(int i=0;i<key.value();++i)_events.append({_end,ME_NOTEOFF,key.key()/128,key.key()%128,0});
      for(auto pedal=_pedals.cbegin();pedal!=_pedals.cend();++pedal)if(pedal.value())_events.append({_end,ME_CONTROLLER,pedal.key(),CTRL_SUSTAIN,0});
      _keys.clear();_pedals.clear();_stopping=false;
      if(!_count){emit changed();emit failed(tr("这次还没有收到音符。检查键盘连接和 MIDI 输入设置后再录制。"));return;}
      _saving=true;emit changed();
      auto events=std::move(_events);auto start=_start;auto generation=_generation;auto watcher=new QFutureWatcher<QString>(this);
      connect(watcher,&QFutureWatcher<QString>::finished,this,[this,watcher,generation] {
            const QString path=watcher->result();watcher->deleteLater();if(generation!=_generation)return;_saving=false;emit changed();
            if(path.isEmpty())emit failed(tr("录制文件未能保存。请检查临时文件夹的可用空间。"));else emit finished(QUrl::fromLocalFile(path).toString(),_reason);
            });
      watcher->setFuture(QtConcurrent::run([events,start] {
            auto ordered=events;std::stable_sort(ordered.begin(),ordered.end(),[](const RecordedEvent&a,const RecordedEvent&b){return a.time<b.time;});
            QByteArray track=QByteArray::fromHex("00ff510307a120");quint32 previous=0;
            for(const auto& event:ordered) {
                  // PPQ 9600, 120 BPM: 19200 ticks per second, about 52 microseconds.
                  quint32 tick=quint32(std::max<qint64>(0,event.time-start)*19200/1000000000);
                  variable(track,tick-previous);previous=tick;track.append(char(event.type|event.channel));track.append(char(event.a));
                  if(event.type!=ME_PROGRAM&&event.type!=ME_AFTERTOUCH)track.append(char(event.b));
                  }
            track.append(QByteArray::fromHex("00ff2f00"));QByteArray data("MThd");number(data,6,4);number(data,0,2);number(data,1,2);number(data,9600,2);
            data.append("MTrk");number(data,track.size(),4);data.append(track);
            QString defaultFolder=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/recordings";
#ifdef Q_OS_WIN
            if(QFileInfo("E:/").isDir())defaultFolder="E:/MuseScoreCache/recordings";
#endif
            const QString folder=QSettings().value("workflow/recordingDirectory",defaultFolder).toString();
            if(!QDir().mkpath(folder))return QString();const QString path=folder+"/recording-"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".mid";
            QSaveFile file(path);if(!file.open(QIODevice::WriteOnly)||file.write(data)!=data.size()||!file.commit())return QString();return path;
            }));
      }
}
