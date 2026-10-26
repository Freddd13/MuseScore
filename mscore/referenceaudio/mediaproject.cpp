// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "mediaproject.h"
#include "pcmring.h"
#include "audioreader.h"
#include "auditionfilter.h"
#include <QtConcurrent>
#include <QAudioOutput>
#include <QAudioDeviceInfo>
#include <QIODevice>
#include <QFileInfo>
#include <QUrl>
#include <QDir>
#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>
#include <QSet>
#include <QSettings>
#include <QElapsedTimer>
#include <QThread>
#include <cmath>
#include <array>
using KumoAudio::PcmRing;
namespace {
QString localPath(QString path){return QUrl(path).isLocalFile()?QUrl(path).toLocalFile():path;}
class RingDevice : public QIODevice {
      void* _context;
      unsigned (*_available)(void*);
      unsigned (*_read)(void*,float*,unsigned,bool);
      bool _integer;
public:
      RingDevice(void* context,unsigned (*available)(void*),unsigned (*read)(void*,float*,unsigned,bool),QObject* parent,bool integer=false):QIODevice(parent),_context(context),_available(available),_read(read),_integer(integer){open(QIODevice::ReadOnly);}
      bool isSequential()const override{return true;}
      qint64 bytesAvailable()const override{return qint64(_available(_context))*(_integer?4:8)+QIODevice::bytesAvailable();}
      qint64 readData(char* data,qint64 size)override{
            if(!_integer){const unsigned count=unsigned(size/8);_read(_context,reinterpret_cast<float*>(data),count,false);return qint64(count)*8;}
            const unsigned count=unsigned(size/4);std::array<float,2048> samples;unsigned written=0;
            while(written<count){unsigned n=qMin(1024u,count-written);_read(_context,samples.data(),n,false);for(unsigned i=0;i<n*2;++i){qint16 value=qint16(std::lround(qBound(-1.f,samples[i],1.f)*32767));std::memcpy(data+(written*2+i)*2,&value,2);}written+=n;}return qint64(count)*4;
            }
      qint64 writeData(const char*,qint64)override{return -1;}
};
}
struct ReferenceMediaProject::Session {
      std::atomic_bool canceled{false},ready{false},discontinuous{false};
      std::atomic<qint64> expected{-1},outputFrames{0};
      int rate=48000,startFrame=0;
      qint64 total=0,offset=0;
      std::atomic<float> master{.8f};
      bool joint=false,attached=false,loop=false,notifiedStart=false,outputRequested=false;
      std::atomic_bool started{false};
      std::atomic<qint64> deviceMicroseconds{0},startMicroseconds{0};
      std::vector<std::shared_ptr<PcmRing>> lanes;
      QStringList trackIds;
      std::vector<std::shared_ptr<std::atomic<float>>> gains;
      // Only the output consumer touches these values; GUI controls set atomic targets.
      std::vector<float> currentGains;
      qint64 consumedFrames=0;
      std::atomic<quint64> underruns{0};
      unsigned available()const {
            unsigned count=131072;for(const auto& lane:lanes)count=qMin(count,lane->available());return count;
            }
      unsigned consume(float* destination,unsigned frames,bool add) {
            if(!add)std::fill(destination,destination+frames*2,0.f);
            if(canceled)return 0;
            const unsigned count=loop?frames:unsigned(qMin<qint64>(frames,qMax<qint64>(0,total-consumedFrames)));
            for(const auto& lane:lanes)if(lane->available()<count&&!lane->done.load()){++underruns;canceled=true;return 0;}
            const float masterGain=master.load(std::memory_order_relaxed),step=1.f/qMax(1,rate/200); // 5 ms fade for track controls.
            std::array<float,2048> samples;
            for(size_t lane=0;lane<lanes.size();++lane){
                  float gain=currentGains[lane];const float target=gains[lane]->load(std::memory_order_relaxed);
                  for(unsigned offset=0;offset<count;){const unsigned n=qMin(1024u,count-offset);lanes[lane]->pop(samples.data(),n);
                        for(unsigned frame=0;frame<n;++frame){gain+=qBound(-step,target-gain,step);for(unsigned channel=0;channel<2;++channel)destination[(offset+frame)*2+channel]+=samples[frame*2+channel]*gain*masterGain;}offset+=n;
                        }
                  currentGains[lane]=gain;
                  }
            consumedFrames+=count;return count;
            }
      QMutex errorsMutex;
      QString error;
      void fail(QString value){QMutexLocker lock(&errorsMutex);if(error.isEmpty())error=value;canceled=true;}
};

// Independent playback owns its Qt audio objects on one thread. Device lookup,
// opening and closing can take hundreds of milliseconds on Windows; none of
// these operations may hold up score input or the playback cursor.
class ReferenceMediaProject::OutputThread {
      struct State {
            QAudioOutput* audio=nullptr;
            RingDevice* device=nullptr;
            QTimer* clock=nullptr;
            std::shared_ptr<Session> session;
            void stop() {
                  if(clock){clock->stop();clock->deleteLater();clock=nullptr;}
                  if(audio){audio->stop();delete audio;audio=nullptr;}
                  delete device;device=nullptr;session.reset();
                  }
            void start(QObject* owner,const std::shared_ptr<Session>& next) {
                  stop();if(next->canceled)return;session=next;
                  QElapsedTimer timer;timer.start();
                  auto outputDevice=QAudioDeviceInfo::defaultOutputDevice();
                  next->deviceMicroseconds=timer.nsecsElapsed()/1000;
                  if(next->canceled){stop();return;}
                  QAudioFormat format;format.setSampleRate(next->rate);format.setChannelCount(2);
                  format.setSampleSize(32);format.setSampleType(QAudioFormat::Float);
                  format.setByteOrder(QAudioFormat::LittleEndian);format.setCodec("audio/pcm");
                  bool integer=false;
                  if(!outputDevice.isFormatSupported(format)) {
                        format.setSampleSize(16);format.setSampleType(QAudioFormat::SignedInt);integer=true;
                        }
                  if(outputDevice.isNull()||!outputDevice.isFormatSupported(format)) {
                        next->fail(QObject::tr("声音设备不能播放此采样格式。"));stop();return;
                        }
                  if(next->canceled){stop();return;}
                  device=new RingDevice(next.get(),[](void* context){return static_cast<Session*>(context)->available();},
                        [](void* context,float* samples,unsigned count,bool add){return static_cast<Session*>(context)->consume(samples,count,add);},owner,integer);
                  audio=new QAudioOutput(outputDevice,format,owner);
                  audio->setBufferSize(next->rate*(integer?4:8)/10);
                  timer.restart();audio->start(device);next->startMicroseconds=timer.nsecsElapsed()/1000;
                  if(qEnvironmentVariableIsSet("KUMO_MEDIA_PROFILE"))
                        qInfo("Reference worker output: device %.3f ms, start %.3f ms",next->deviceMicroseconds.load()/1000.,next->startMicroseconds.load()/1000.);
                  if(next->canceled){stop();return;}
                  if(audio->error()!=QAudio::NoError){next->fail(QObject::tr("无法打开声音输出设备。请检查当前声音设备。"));stop();return;}
                  next->started=true;
                  clock=new QTimer(owner);clock->setInterval(10);clock->setTimerType(Qt::PreciseTimer);
                  QObject::connect(clock,&QTimer::timeout,owner,[this] {
                        if(!session||session->canceled){stop();return;}
                        const qint64 frames=session->offset+audio->processedUSecs()*session->rate/1000000;
                        session->outputFrames=session->loop?frames:qMin(session->total,frames);
                        });
                  clock->start();
                  }
            };
      QThread _thread;
      QObject* _owner=new QObject;
      std::shared_ptr<State> _state=std::make_shared<State>();
public:
      OutputThread() {
            _thread.setObjectName("referenceIndependentOutput");_owner->moveToThread(&_thread);
            QObject::connect(&_thread,&QThread::finished,_owner,&QObject::deleteLater);
            _thread.start();
            }
      ~OutputThread() {
            const auto state=_state;
            QMetaObject::invokeMethod(_owner,[state]{state->stop();},Qt::BlockingQueuedConnection);
            _thread.quit();_thread.wait();
            }
      void start(const std::shared_ptr<Session>& session) {
            const auto state=_state;auto owner=_owner;
            QMetaObject::invokeMethod(owner,[state,owner,session]{state->start(owner,session);},Qt::QueuedConnection);
            }
      void stop() {
            const auto state=_state;
            QMetaObject::invokeMethod(_owner,[state]{state->stop();},Qt::QueuedConnection);
            }
      };
ReferenceMediaProject::ReferenceMediaProject(QObject* parent,const KumoReferenceHost& host):QObject(parent),_host(host) {
      _pool.setMaxThreadCount(17);_pool.setExpiryTimeout(1000);
      _timer.setInterval(30);connect(&_timer,&QTimer::timeout,this,&ReferenceMediaProject::poll);
      _status=tr("添加音频后，选择要播放的片段。");
      }
ReferenceMediaProject::~ReferenceMediaProject(){stop();_output.reset();_pool.waitForDone();}
double ReferenceMediaProject::volume()const{return _session?_session->master.load(std::memory_order_relaxed):_volume;}
void ReferenceMediaProject::setVolume(double value){if(!std::isfinite(value))return;value=qBound(0.,value,1.);_volume=value;if(_session)_session->master.store(float(value),std::memory_order_relaxed);emit volumeChanged();}
double ReferenceMediaProject::position()const{if(!_session)return 0;auto s=_session;auto frame=s->outputFrames.load();if(s->loop&&s->total>0)frame%=s->total;return double(frame)*1000/s->rate;}
bool ReferenceMediaProject::scoreControls()const{return QSettings().value("workflow/reference/scoreControls",false).toBool();}
void ReferenceMediaProject::setScoreControls(bool value){if(scoreControls()==value)return;QSettings().setValue("workflow/reference/scoreControls",value);emit scoreControlsChanged();}
bool ReferenceMediaProject::audioStopsScore()const{return QSettings().value("workflow/reference/stopScoreOnAudio",false).toBool();}
bool ReferenceMediaProject::scoreStopsAudio()const{return QSettings().value("workflow/reference/stopAudioOnScore",false).toBool();}
void ReferenceMediaProject::setAudioStopsScore(bool value){QSettings().setValue("workflow/reference/stopScoreOnAudio",value);emit changed();}
void ReferenceMediaProject::setScoreStopsAudio(bool value){QSettings().setValue("workflow/reference/stopAudioOnScore",value);emit changed();}
double ReferenceMediaProject::sourcePosition(const QString& path,const QString& trackId)const {
      const auto wanted=QDir::cleanPath(QFileInfo(localPath(path)).absoluteFilePath());
      const double output=_session?position():_stoppedOutput;
      for(auto value:playbackPlan().value("clips").toList()){
            const auto clip=value.toMap();if(!trackId.isEmpty()&&clip.value("id").toString()!=trackId)continue;if(QDir::cleanPath(QFileInfo(localPath(clip.value("path").toString())).absoluteFilePath()).compare(wanted,Qt::CaseInsensitive)!=0)continue;
            const auto pieces=clip.value("pieces").toList();double previous=-1;
            for(auto part:pieces){const auto p=part.toMap();const double at=p.value("at").toDouble(),duration=p.value("duration").toDouble(),from=p.value("from").toDouble(),until=p.value("until").toDouble();
                  if(output<at)return previous<0?from:previous;if(output<=at+duration&&duration>0)return from+(until-from)*(output-at)/duration;previous=until;}
            return previous;
      }
      return -1;
}
QVariantMap ReferenceMediaProject::diagnostics()const {
      if(!_session)return {};auto s=_session;
      return {{"sampleRate",s->rate},{"bufferFrames",131072},{"audioTracks",int(s->lanes.size())},{"underruns",qulonglong(s->underruns.load())},{"outputFrames",s->outputFrames.load()},{"boundedBytes",qint64(s->lanes.size())*131072*8},
            {"outputStarted",s->started.load()},{"deviceMilliseconds",s->deviceMicroseconds.load()/1000.},{"outputStartMilliseconds",s->startMicroseconds.load()/1000.}};
      }
QVariantMap ReferenceMediaProject::setTracks(const QVariantList& values) {
      if(playing())return {{"error",tr("请先停止播放，再改变音轨。")}};
      if(values.size()>16)return {{"error",tr("本次最多使用 16 条音频。")}};
      QSet<QString> ids;
      for(auto value:values){auto v=value.toMap();QString id=v.value("id").toString();if(id.isEmpty()||ids.contains(id))return {{"error",tr("音轨名称对应的编号重复或缺失。")}};ids.insert(id);}
      _tracks=values;emit tracksChanged();return {{"ok",true}};
      }
QVariantMap ReferenceMediaProject::setTrackMix(const QVariantList& values,const QString& changedId) {
      if(values.size()>16)return {{"error",tr("本次最多使用 16 条音频。")}};
      QSet<QString> ids;bool solo=false;QVariantMap gains;
      for(auto value:values){const auto track=value.toMap();const auto id=track.value("id").toString();const double volume=track.value("volume",1.).toDouble();
            if(id.isEmpty()||ids.contains(id)||!std::isfinite(volume)||volume<0||volume>1)return {{"error",tr("请检查音轨编号和 0–100% 的音量。")}};
            ids.insert(id);if(track.value("participate",true).toBool()&&track.value("solo").toBool())solo=true;
            }
      for(auto value:values){const auto track=value.toMap();const auto id=track.value("id").toString();const bool audible=track.value("participate",true).toBool()&&!track.value("mute").toBool()&&(!solo||track.value("solo").toBool());
            gains[id]=audible?track.value("volume",1.).toDouble():0.;
            if(_session&&id==changedId&&audible&&!_session->trackIds.contains(id))return {{"error",tr("这条音轨尚未加入本次播放。停止后选择参加播放，再重新开始。")}};
            }
      if(_session)for(int lane=0;lane<_session->trackIds.size();++lane)if(gains.contains(_session->trackIds[lane]))_session->gains[size_t(lane)]->store(float(gains.value(_session->trackIds[lane]).toDouble()),std::memory_order_relaxed);
      auto plan=playbackPlan();plan["mixTracks"]=values;setProperty("workflowPlaybackPlan",plan);
      for(int i=0;i<_tracks.size();++i){auto saved=_tracks[i].toMap();for(auto value:values){const auto track=value.toMap();if(track.value("id")==saved.value("id")){for(const char* field:{"volume","mute","solo","participate"})if(track.contains(field))saved[field]=track.value(field);break;}}_tracks[i]=saved;}
      emit tracksChanged();return {{"ok",true}};
      }
QVariantMap ReferenceMediaProject::start(const QVariantMap& plan) {
      stop();setProperty("workflowPlaybackOwner",plan.value("owner"));const auto clips=plan.value("clips").toList();
      if(clips.isEmpty()||clips.size()>16)return {{"error",tr("请选择 1–16 条要播放的音频。")}};
      const int rate=_host.sampleRate?_host.sampleRate():48000;
      if(rate<8000||rate>192000)return {{"error",tr("声音设备的采样率不受支持。")}};
      auto s=std::make_shared<Session>();s->rate=rate;s->joint=plan.value("joint").toBool();s->startFrame=plan.value("startFrame").toInt();
      s->loop=plan.value("loop").toBool();if(s->loop&&s->joint)return {{"error",tr("联合循环请使用原曲面板的循环按钮。")}};
      const double master=plan.value("volume",.8).toDouble();if(!std::isfinite(master)||master<0||master>1)return {{"error",tr("总音量应在 0–100% 之间。")}};s->master=float(master);_volume=master;
      // Complete validation before starting any producer.
      for(auto clipValue:clips){auto clip=clipValue.toMap();const auto pieces=clip.value("pieces").toList();if(pieces.isEmpty()||pieces.size()>4096)return {{"error",tr("片段为空或对齐点过多。")}};
            QString path=localPath(clip.value("path").toString());QFileInfo file(path);if(!file.exists()||!file.isFile())return {{"error",tr("找不到音频：%1").arg(path)}};
            auto identity=clip.value("identity").toMap();if(!identity.isEmpty()&&(identity.value("size").toLongLong()!=file.size()||identity.value("modified").toLongLong()!=file.lastModified().toMSecsSinceEpoch()))return {{"error",tr("音频文件已经改变，请重新核对关联：%1").arg(path)}};
            const double gain=clip.value("volume",1.).toDouble();if(!std::isfinite(gain)||gain<0||gain>1)return {{"error",tr("音轨音量应在 0–100% 之间。")}};
            double previous=0;for(auto pieceValue:pieces){auto piece=pieceValue.toMap();double from=piece.value("from").toDouble(),until=piece.value("until").toDouble(),at=piece.value("at").toDouble(),duration=piece.value("duration").toDouble();double speed=(until-from)/duration;
                  if(!std::isfinite(from)||!std::isfinite(until)||!std::isfinite(at)||!std::isfinite(duration)||from<0||until<=from||at+0.01<previous||duration<=0||at+duration>86400000||speed<.499999||speed>1.500001)return {{"error",tr("对齐需要的保调速度超出 50–150%，或片段时间不正确；请调整对齐点。")}};
                  previous=at+duration;s->total=qMax(s->total,qint64(std::llround(previous*rate/1000)));}
            }
      if(s->total<=0||(s->loop&&s->total<s->rate/5))return {{"error",tr("循环片段至少需要 0.2 秒。请拖长片段的结束位置。")}};
      const double offset=plan.value("offset",0).toDouble();
      if(!std::isfinite(offset)||offset<0||offset>=double(s->total)*1000/s->rate||(s->joint&&offset>0))return {{"error",tr("播放位置应在片段内。")}};
      s->offset=qint64(std::llround(offset*s->rate/1000));s->outputFrames=s->offset;s->consumedFrames=s->offset;_stoppedOutput=0;
      setProperty("workflowPlaybackPlan",plan);_completed=false;_session=s;_joint=s->joint;_status=tr("正在准备 %1 条音频…").arg(clips.size());
      for(auto clipValue:clips){auto clip=clipValue.toMap();auto ring=std::make_shared<PcmRing>();s->lanes.push_back(ring);s->trackIds.append(clip.value("id").toString());const float gain=float(clip.value("volume",1).toDouble());s->gains.push_back(std::make_shared<std::atomic<float>>(gain));s->currentGains.push_back(gain);QString path=localPath(clip.value("path").toString());auto pieces=clip.value("pieces").toList();const auto listen=clip.value("listen").toMap();
            QtConcurrent::run(&_pool,[s,ring,path,pieces,listen]{
                  KumoAudio::AuditionFilter filter(s->rate,listen);
                  qint64 written=0;std::array<float,8192> silence{};
                  auto zeros=[&](qint64 end){while(written<end&&!s->canceled){unsigned n=unsigned(qMin<qint64>(4096,end-written));if(!ring->push(silence.data(),n,s->canceled))return false;written+=n;}return !s->canceled;};
                  bool firstPass=true;do{written=firstPass?s->offset:0;firstPass=false;
                  for(int pi=0;pi<pieces.size();++pi){auto p=pieces[pi].toMap();qint64 at=std::llround(p.value("at").toDouble()*s->rate/1000),end=std::llround((p.value("at").toDouble()+p.value("duration").toDouble())*s->rate/1000);if(end<=written)continue;
                        if(written>at){const double ratio=double(written-at)/double(end-at);p["from"]=p.value("from").toDouble()+(p.value("until").toDouble()-p.value("from").toDouble())*ratio;p["duration"]=double(end-written)*1000/s->rate;at=written;}
                        if(!zeros(at))break;
                        std::vector<KumoAudio::Segment> chain;auto append=[&](const QVariantMap& piece){chain.push_back({piece.value("from").toDouble(),piece.value("until").toDouble(),qBound(.5,(piece.value("until").toDouble()-piece.value("from").toDouble())/piece.value("duration").toDouble(),1.5),piece.value("fade",true).toBool()});};append(p);
                        while(pi+1<pieces.size()){auto next=pieces[pi+1].toMap();qint64 nextAt=std::llround(next.value("at").toDouble()*s->rate/1000);if(nextAt!=end||std::abs(next.value("from").toDouble()-chain.back().end)>.01)break;append(next);end=std::llround((next.value("at").toDouble()+next.value("duration").toDouble())*s->rate/1000);++pi;}
                        QString error;
                        bool ok=KumoAudio::streamSequence(path,chain,s->rate,[&](const float* data,int count){count=int(qMin<qint64>(count,end-written));std::array<float,8192> scaled;std::copy(data,data+count*2,scaled.data());filter.process(scaled.data(),count);written+=count;return ring->push(scaled.data(),unsigned(count),s->canceled);},s->canceled,&error);
                        if(!ok&&!s->canceled){s->fail(error);break;}if(!zeros(end))break;
                        }
                  zeros(s->total);
                  }while(s->loop&&!s->canceled);ring->done=true;
                  });
            }
      const auto initialMix=plan.value("mixTracks").toList();if(!initialMix.isEmpty()){const auto result=setTrackMix(initialMix);if(result.contains("error")){stop();return result;}for(size_t lane=0;lane<s->gains.size();++lane)s->currentGains[lane]=s->gains[lane]->load(std::memory_order_relaxed);}
      _timer.start();emit changed();return {{"ok",true},{"preparing",true}};
      }
void ReferenceMediaProject::mix(void* context,float* buffer,unsigned count,int frame,bool playing) {
      auto s=static_cast<Session*>(context);if(s->canceled)return;
      unsigned skipped=0;
      if(!s->started){if(!playing||qint64(frame)+count<=s->startFrame)return;skipped=unsigned(qMax(0,s->startFrame-frame));s->started=true;s->expected=frame;}
      const qint64 expected=s->expected.load();
      if(playing&&std::abs(qint64(frame)-expected)>qint64(count)*2){s->discontinuous=true;return;}
      s->consume(buffer+skipped*2,count-skipped,true);s->outputFrames+=count-skipped;s->expected=expected+count;
      }
void ReferenceMediaProject::poll() {
      auto s=_session;if(!s)return;
      if(s->canceled){QString error;{QMutexLocker lock(&s->errorsMutex);error=s->error;}const bool underrun=s->underruns.load()>0;stop();_status=underrun?tr("音频准备跟不上播放，已停止以免位置错开。请减少同时播放的音轨。"):error.isEmpty()?tr("播放已停止。"):tr("音频无法播放：%1").arg(error);emit changed();return;}
      if(!s->ready){bool ready=true;for(const auto& lane:s->lanes)if(lane->available()<qMin<qint64>(8192,s->total-s->offset)&&!lane->done.load())ready=false;s->ready=ready;}
      if(s->discontinuous){stop();_status=tr("乐谱播放位置改变，联合播放已停止。请从新的位置重新开始。");emit changed();return;}
      if(s->ready&&!s->started&&!s->outputRequested&&!s->attached){
            if(s->joint){if(!_host.attach||!_host.attach(&ReferenceMediaProject::mix,s.get())){stop();_status=tr("联合播放输出正被使用，请停止另一次播放。");emit changed();return;}s->attached=true;setProperty("prepared",true);_status=tr("音频已准备，等待乐谱开始。");emit changed();return;}
            s->outputRequested=true;if(!_output)_output.reset(new OutputThread);_output->start(s);
            _status=tr("正在打开声音输出…");
            }
      if(s->started&&!s->notifiedStart){s->notifiedStart=true;_status=s->joint?tr("乐谱与参考音频一起播放中。"):tr("音频播放中。");}
      if(s->started&&!s->loop&&s->outputFrames>=s->total){bool wasJoint=s->joint;stop();_completed=true;_status=tr("片段播放结束。");emit changed();emit finished(wasJoint);return;}emit changed();
      }
void ReferenceMediaProject::stop(){
      _timer.stop();setProperty("prepared",false);if(!_session)return;_stoppedOutput=position();auto s=_session;s->canceled=true;_pool.clear();if(s->joint&&_host.detach)_host.detach(s.get());
      if(_output)_output->stop();_session.reset();_joint=false;_status=tr("播放已停止。");emit changed();
      }
void ReferenceMediaProject::requestJoint(){if(receivers(SIGNAL(jointRequested()))==0){_status=tr("请先打开“原曲参考”，选择要联合播放的关联。");emit changed();return;}emit jointRequested();}
void ReferenceMediaProject::toggleIndependent(){
      if(receivers(SIGNAL(independentRequested()))>0){emit independentRequested();return;}
      if(playing()){stop();return;}int index=property("selectedTrack").toInt();if(index<0||index>=_tracks.size()){_status=tr("请先在原曲参考中选择音轨和片段。");emit changed();return;}
      auto t=_tracks[index].toMap();if(t.value("mute").toBool()){_status=tr("当前音轨已静音，取消静音后即可播放。");emit changed();return;}const bool limited=t.value("onlyRange",t.value("loop",false)).toBool();double a=limited?t.value("a").toDouble():0,b=limited?t.value("b").toDouble():t.value("duration",t.value("b")).toDouble(),speed=t.value("speed",1.).toDouble();
      QVariantMap piece{{"from",a},{"until",b},{"at",0},{"duration",(b-a)/speed},{"fade",t.value("fade",true)}};
      QVariantMap clip{{"path",t.value("path")},{"identity",t.value("identity")},{"volume",t.value("volume",.8)},{"pieces",QVariantList{piece}}};
      auto result=start({{"clips",QVariantList{clip}},{"joint",false},{"loop",t.value("loop",false)},{"volume",1.}});if(result.contains("error")){_status=result.value("error").toString();emit changed();}
      }
