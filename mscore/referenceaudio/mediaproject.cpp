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
#include <cmath>
#include <array>
using KumoAudio::PcmRing;
namespace {
QString localPath(QString path){return QUrl(path).isLocalFile()?QUrl(path).toLocalFile():path;}
class RingDevice : public QIODevice {
      PcmRing* _ring;
      bool _integer;
      const std::atomic<float>* _gain;
public:
      RingDevice(PcmRing* ring,QObject* parent,const std::atomic<float>* gain,bool integer=false):QIODevice(parent),_ring(ring),_integer(integer),_gain(gain){open(QIODevice::ReadOnly);}
      bool isSequential()const override{return true;}
      qint64 bytesAvailable()const override{return qint64(_ring->available())*(_integer?4:8)+QIODevice::bytesAvailable();}
      qint64 readData(char* data,qint64 size)override{
            if(!_integer){const unsigned count=unsigned(size/8);_ring->pop(reinterpret_cast<float*>(data),count,false,_gain->load(std::memory_order_relaxed));return qint64(count)*8;}
            const unsigned count=unsigned(size/4);std::array<float,2048> samples;unsigned written=0;
            while(written<count){unsigned n=qMin(1024u,count-written);_ring->pop(samples.data(),n,false,_gain->load(std::memory_order_relaxed));for(unsigned i=0;i<n*2;++i){qint16 value=qint16(std::lround(qBound(-1.f,samples[i],1.f)*32767));std::memcpy(data+(written*2+i)*2,&value,2);}written+=n;}return qint64(count)*4;
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
      bool joint=false,attached=false,loop=false;
      std::atomic_bool started{false};
      PcmRing output;
      std::vector<std::shared_ptr<PcmRing>> lanes;
      QMutex errorsMutex;
      QString error;
      void fail(QString value){QMutexLocker lock(&errorsMutex);if(error.isEmpty())error=value;canceled=true;}
};
ReferenceMediaProject::ReferenceMediaProject(QObject* parent,const KumoReferenceHost& host):QObject(parent),_host(host) {
      _pool.setMaxThreadCount(17);_pool.setExpiryTimeout(1000);
      _timer.setInterval(30);connect(&_timer,&QTimer::timeout,this,&ReferenceMediaProject::poll);
      _status=tr("添加音频后，选择要播放的片段。");
      }
ReferenceMediaProject::~ReferenceMediaProject(){stop();_pool.waitForDone();}
double ReferenceMediaProject::volume()const{return _session?_session->master.load(std::memory_order_relaxed):_volume;}
void ReferenceMediaProject::setVolume(double value){if(!std::isfinite(value))return;value=qBound(0.,value,1.);_volume=value;if(_session)_session->master.store(float(value),std::memory_order_relaxed);emit volumeChanged();}
double ReferenceMediaProject::position()const{if(!_session)return 0;auto s=_session;auto frame=s->outputFrames.load();if(s->loop&&s->total>0)frame%=s->total;return double(frame)*1000/s->rate;}
bool ReferenceMediaProject::audioStopsScore()const{return QSettings().value("workflow/reference/stopScoreOnAudio",false).toBool();}
bool ReferenceMediaProject::scoreStopsAudio()const{return QSettings().value("workflow/reference/stopAudioOnScore",false).toBool();}
void ReferenceMediaProject::setAudioStopsScore(bool value){QSettings().setValue("workflow/reference/stopScoreOnAudio",value);emit changed();}
void ReferenceMediaProject::setScoreStopsAudio(bool value){QSettings().setValue("workflow/reference/stopAudioOnScore",value);emit changed();}
double ReferenceMediaProject::sourcePosition(const QString& path)const {
      const auto wanted=QDir::cleanPath(QFileInfo(localPath(path)).absoluteFilePath());
      const double output=_session?position():_stoppedOutput;
      for(auto value:playbackPlan().value("clips").toList()){
            const auto clip=value.toMap();if(QDir::cleanPath(QFileInfo(localPath(clip.value("path").toString())).absoluteFilePath()).compare(wanted,Qt::CaseInsensitive)!=0)continue;
            const auto pieces=clip.value("pieces").toList();double previous=-1;
            for(auto part:pieces){const auto p=part.toMap();const double at=p.value("at").toDouble(),duration=p.value("duration").toDouble(),from=p.value("from").toDouble(),until=p.value("until").toDouble();
                  if(output<at)return previous<0?from:previous;if(output<=at+duration&&duration>0)return from+(until-from)*(output-at)/duration;previous=until;}
            return previous;
      }
      return -1;
}
QVariantMap ReferenceMediaProject::diagnostics()const {
      if(!_session)return {};auto s=_session;
      return {{"sampleRate",s->rate},{"bufferFrames",131072},{"audioTracks",int(s->lanes.size())},{"underruns",qulonglong(s->output.underruns.load())},{"outputFrames",s->outputFrames.load()},{"boundedBytes",qint64(s->lanes.size()+1)*131072*8}};
      }
QVariantMap ReferenceMediaProject::setTracks(const QVariantList& values) {
      if(playing())return {{"error",tr("请先停止播放，再改变音轨。")}};
      if(values.size()>16)return {{"error",tr("本次最多使用 16 条音频。")}};
      QSet<QString> ids;
      for(auto value:values){auto v=value.toMap();QString id=v.value("id").toString();if(id.isEmpty()||ids.contains(id))return {{"error",tr("音轨名称对应的编号重复或缺失。")}};ids.insert(id);}
      _tracks=values;emit tracksChanged();return {{"ok",true}};
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
      if(s->total<=0||(s->loop&&s->total<s->rate/5))return {{"error",tr("多音轨循环至少选择 0.2 秒的片段。更短的循环可使用单曲播放器。")}};
      const double offset=plan.value("offset",0).toDouble();
      if(!std::isfinite(offset)||offset<0||offset>=double(s->total)*1000/s->rate||(s->joint&&offset>0))return {{"error",tr("播放位置应在片段内。")}};
      s->offset=qint64(std::llround(offset*s->rate/1000));s->outputFrames=s->offset;_stoppedOutput=0;
      setProperty("workflowPlaybackPlan",plan);_completed=false;_session=s;_joint=s->joint;_status=tr("正在准备 %1 条音频…").arg(clips.size());
      for(auto clipValue:clips){auto clip=clipValue.toMap();auto ring=std::make_shared<PcmRing>();s->lanes.push_back(ring);QString path=localPath(clip.value("path").toString());auto pieces=clip.value("pieces").toList();const float gain=float(clip.value("volume",1).toDouble());const auto listen=clip.value("listen").toMap();
            QtConcurrent::run(&_pool,[s,ring,path,pieces,gain,listen]{
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
                        bool ok=KumoAudio::streamSequence(path,chain,s->rate,[&](const float* data,int count){count=int(qMin<qint64>(count,end-written));std::array<float,8192> scaled;for(int i=0;i<count*2;++i)scaled[i]=data[i]*gain;filter.process(scaled.data(),count);written+=count;return ring->push(scaled.data(),unsigned(count),s->canceled);},s->canceled,&error);
                        if(!ok&&!s->canceled){s->fail(error);break;}if(!zeros(end))break;
                        }
                  zeros(s->total);
                  }while(s->loop&&!s->canceled);ring->done=true;
                  });
            }
      QtConcurrent::run(&_pool,[s]{
            std::array<float,2048> mixed,temp;qint64 produced=s->offset;
            while((s->loop||produced<s->total)&&!s->canceled){unsigned n=unsigned(qMin<qint64>(1024,s->total-produced%s->total));bool available=true;for(auto lane:s->lanes)if(lane->available()<n&&!lane->done.load())available=false;
                  if(!available){std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;}
                  std::fill(mixed.begin(),mixed.end(),0.f);for(auto lane:s->lanes){lane->pop(temp.data(),n);for(unsigned i=0;i<n*2;++i)mixed[i]+=temp[i];}
                  if(!s->output.push(mixed.data(),n,s->canceled))break;produced+=n;
                  if(s->output.available()>=qMin<qint64>(8192,s->total))s->ready=true;
                  }
            s->output.done=true;s->ready=true;
            });
      _timer.start();emit changed();return {{"ok",true},{"preparing",true}};
      }
void ReferenceMediaProject::mix(void* context,float* buffer,unsigned count,int frame,bool playing) {
      auto s=static_cast<Session*>(context);if(s->canceled)return;
      unsigned skipped=0;
      if(!s->started){if(!playing||qint64(frame)+count<=s->startFrame)return;skipped=unsigned(qMax(0,s->startFrame-frame));s->started=true;s->expected=frame;}
      const qint64 expected=s->expected.load();
      if(playing&&std::abs(qint64(frame)-expected)>qint64(count)*2){s->discontinuous=true;return;}
      const auto got=s->output.pop(buffer+skipped*2,count-skipped,true,s->master.load(std::memory_order_relaxed));if(got<count-skipped&&!s->output.done)s->canceled=true;s->outputFrames+=count-skipped;s->expected=expected+count;
      }
void ReferenceMediaProject::poll() {
      auto s=_session;if(!s)return;
      if(s->output.underruns.load()>0&&!s->output.done.load())s->canceled=true;
      if(s->canceled){QString error;{QMutexLocker lock(&s->errorsMutex);error=s->error;}const bool underrun=s->output.underruns.load()>0;stop();_status=underrun?tr("音频准备跟不上播放，已停止以免位置错开。请减少同时播放的音轨。"):error.isEmpty()?tr("播放已停止。"):tr("音频无法播放：%1").arg(error);emit changed();return;}
      if(s->discontinuous){stop();_status=tr("乐谱播放位置改变，联合播放已停止。请从新的位置重新开始。");emit changed();return;}
      if(s->ready&&!s->started&& !_audio&&!s->attached){
            if(s->joint){if(!_host.attach||!_host.attach(&ReferenceMediaProject::mix,s.get())){stop();_status=tr("联合播放输出正被使用，请停止另一次播放。");emit changed();return;}s->attached=true;setProperty("prepared",true);_status=tr("音频已准备，等待乐谱开始。");emit changed();return;}
            QAudioFormat f;f.setSampleRate(s->rate);f.setChannelCount(2);f.setSampleSize(32);f.setSampleType(QAudioFormat::Float);f.setByteOrder(QAudioFormat::LittleEndian);f.setCodec("audio/pcm");auto device=QAudioDeviceInfo::defaultOutputDevice();
            bool integer=false;if(!device.isFormatSupported(f)){f.setSampleSize(16);f.setSampleType(QAudioFormat::SignedInt);integer=true;}
            if(device.isNull()||!device.isFormatSupported(f)){stop();_status=tr("声音设备不能播放此采样格式。");emit changed();return;}
            _device=new RingDevice(&s->output,this,&s->master,integer);_audio=new QAudioOutput(device,f,this);_audio->setBufferSize(s->rate*(integer?4:8)/10);_audio->start(_device);s->started=true;_status=tr("音频播放中。");
            }
      if(!s->joint){qint64 frames=s->offset+(_audio?_audio->processedUSecs()*s->rate/1000000:0);s->outputFrames=s->loop?frames:qMin(s->total,frames);}
      if(s->started&&!s->loop&&s->outputFrames>=s->total){bool wasJoint=s->joint;stop();_completed=true;_status=tr("片段播放结束。");emit changed();emit finished(wasJoint);return;}emit changed();
      }
void ReferenceMediaProject::stop(){
      _timer.stop();setProperty("prepared",false);if(!_session)return;_stoppedOutput=position();auto s=_session;s->canceled=true;_pool.clear();if(s->joint&&_host.detach)_host.detach(s.get());
      if(_audio){_audio->stop();delete _audio;_audio=nullptr;}delete _device;_device=nullptr;_session.reset();_joint=false;_status=tr("播放已停止。");emit changed();
      }
void ReferenceMediaProject::requestJoint(){if(receivers(SIGNAL(jointRequested()))==0){_status=tr("请先打开“原曲参考”，选择要联合播放的关联。");emit changed();return;}emit jointRequested();}
void ReferenceMediaProject::toggleIndependent(){
      if(receivers(SIGNAL(independentRequested()))>0){emit independentRequested();return;}
      if(playing()){stop();return;}int index=property("selectedTrack").toInt();if(index<0||index>=_tracks.size()){_status=tr("请先在原曲参考中选择音轨和片段。");emit changed();return;}
      auto t=_tracks[index].toMap();if(t.value("mute").toBool()){_status=tr("当前音轨已静音，取消静音后即可播放。");emit changed();return;}double a=t.value("a").toDouble(),b=t.value("b").toDouble(),speed=t.value("speed",1.).toDouble();
      QVariantMap piece{{"from",a},{"until",b},{"at",0},{"duration",(b-a)/speed},{"fade",t.value("fade",true)}};
      QVariantMap clip{{"path",t.value("path")},{"identity",t.value("identity")},{"volume",t.value("volume",.8)},{"pieces",QVariantList{piece}}};
      auto result=start({{"clips",QVariantList{clip}},{"joint",false},{"loop",t.value("loop",false)},{"volume",1.}});if(result.contains("error")){_status=result.value("error").toString();emit changed();}
      }
