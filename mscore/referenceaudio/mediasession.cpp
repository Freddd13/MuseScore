// SPDX-License-Identifier: GPL-2.0-or-later
#include "mediasession.h"
#include <QAudioDeviceInfo>
#include <QIODevice>
#include <QUrl>
#include <QFileInfo>
#include <QDateTime>
#include <QtConcurrent>
#include <thread>
#include <cstring>
#include <cmath>
#include <algorithm>
// One producer/one consumer; audio callback never waits for IO, decoding or locks.
class AudioRing:public QIODevice {
 QByteArray buffer;std::atomic<quint64> readIndex{0},writeIndex{0};
public:
 std::atomic_bool done{false};std::atomic<quint64> underruns{0};
 explicit AudioRing(QObject* p):QIODevice(p),buffer(1024*1024,0){open(ReadOnly);}
 bool isSequential()const override{return true;}
 qint64 bytesAvailable()const override{return qint64(writeIndex.load()-readIndex.load())+QIODevice::bytesAvailable();}
 bool push(const char* data,qint64 size,const std::atomic_bool& cancel) {
  while(size>0&&!cancel) {
   auto r=readIndex.load(std::memory_order_acquire),w=writeIndex.load();
   auto n=qMin<quint64>(size,quint64(buffer.size())-(w-r));
   if(!n){std::this_thread::sleep_for(std::chrono::milliseconds(2));continue;}
   auto at=w%buffer.size();n=qMin(n,quint64(buffer.size())-at);
   std::memcpy(buffer.data()+at,data,size_t(n));writeIndex.store(w+n,std::memory_order_release);size-=n;data+=n;
  }
  return !cancel;
 }
protected:
 qint64 readData(char* out,qint64 maximum)override {
  auto r=readIndex.load(),w=writeIndex.load(std::memory_order_acquire);
  qint64 amount=qMin<quint64>(maximum,w-r),copied=0;
  while(copied<amount){auto at=(r+copied)%buffer.size();auto n=qMin<qint64>(amount-copied,buffer.size()-at);std::memcpy(out+copied,buffer.constData()+at,size_t(n));copied+=n;}
  readIndex.store(r+amount,std::memory_order_release);
  if(amount==0&&!done)++underruns;return amount;
 }
 qint64 writeData(const char*,qint64)override{return -1;}
};
ReferenceMediaSession::ReferenceMediaSession(QObject* p):QObject(p){
 _timer.setInterval(33);connect(&_timer,&QTimer::timeout,this,&ReferenceMediaSession::updateClock);
}
ReferenceMediaSession::~ReferenceMediaSession(){stopWorker();if(_loadCancel)*_loadCancel=true;if(_waveCancel)*_waveCancel=true;for(auto& job:_loads)job.waitForFinished();}
void ReferenceMediaSession::stopWorker() {
 ++_generation;_timer.stop();_outputStarted=false;if(_cancel)*_cancel=true;
 if(_audio){_audio->stop();delete _audio;_audio=nullptr;}
 _playJob.waitForFinished();delete _ring;_ring=nullptr;
}
void ReferenceMediaSession::cancelRead(){if(_loadCancel)*_loadCancel=true;if(_waveCancel)*_waveCancel=true;++_loadGeneration;++_waveGeneration;}
void ReferenceMediaSession::open(const QString& url) {
 stopWorker();if(_loadCancel)*_loadCancel=true;if(_waveCancel)*_waveCancel=true;++_waveGeneration;_tiles.clear();
 _path=QUrl(url).isLocalFile()?QUrl(url).toLocalFile():url;
 _duration=0;_position=0;_wave.clear();_status=tr("Reading audio…");emit changed();emit waveformChanged();
 const int token=++_loadGeneration;const QString path=_path;const bool makeWave=_waveformEnabled;
 auto cancel=std::make_shared<std::atomic_bool>(false);_loadCancel=cancel;
 _loads.erase(std::remove_if(_loads.begin(),_loads.end(),[](const QFuture<void>& f){return f.isFinished();}),_loads.end());
 _loads.append(QtConcurrent::run([this,path,token,cancel,makeWave]{
  KumoAudio::Reader reader;QString error;
  if(!reader.open(path,&error,cancel.get())){QMetaObject::invokeMethod(this,[this,error,token]{if(token==_loadGeneration){_status=error;emit changed();}},Qt::QueuedConnection);return;}
  auto count=reader.frames();int rate=reader.rate(),channels=reader.channels();
  QMetaObject::invokeMethod(this,[this,count,rate,token]{if(token!=_loadGeneration)return;_duration=double(count)*1000/rate;_a=0;_b=_duration;_rate=rate;_status=tr("Ready");emit changed();emit fileReady(identity());},Qt::QueuedConnection);
  if(!makeWave)return;
  QVector<double> peaks(4096,0);std::vector<float> block(8192*channels);qint64 position=0;
  while(position<count&&!*cancel) {
   auto n=reader.read(block.data(),8192);if(!n)break;
   for(int f=0;f<n;++f){int bin=qMin(4095,int((position+f)*4096/count));for(int c=0;c<channels;++c)peaks[bin]=qMax(peaks[bin],std::abs(double(block[f*channels+c])));}
   position+=n;
  }
  if(*cancel)return;QVariantList result;for(double p:peaks)result.append(p);
  QMetaObject::invokeMethod(this,[this,result,token]{if(token==_loadGeneration){_wave=result;emit waveformChanged();}},Qt::QueuedConnection);
 }));
}
void ReferenceMediaSession::play() {
 if(_audio||_duration<=0)return;
 QAudioFormat format;format.setSampleRate(qMin(48000,_rate));format.setChannelCount(2);format.setSampleSize(16);
 format.setCodec("audio/pcm");format.setByteOrder(QAudioFormat::LittleEndian);format.setSampleType(QAudioFormat::SignedInt);
 auto device=QAudioDeviceInfo::defaultOutputDevice();
 if(device.isNull()||!device.isFormatSupported(format)){_status=tr("Audio output does not support stereo PCM at this rate");emit changed();return;}
 if(_position>=(_loop?_b:_duration))_position=_loop?_a:0;
 _origin=_position;const int token=++_generation;
 _ring=new AudioRing(this);_audio=new QAudioOutput(device,format,this);_audio->setBufferSize(format.sampleRate()/10*4);_audio->setVolume(_volume);
 auto ring=_ring;auto cancel=std::make_shared<std::atomic_bool>(false);_cancel=cancel;
 auto path=_path;double start=_position,a=_a,b=_loop?_b:_duration,speed=_speed;bool loop=_loop,fade=_fade;int rate=format.sampleRate();
 _playJob=QtConcurrent::run([this,ring,cancel,path,start,a,b,speed,loop,fade,rate,token]{
  QString error;double from=start;
  do {
   if(!KumoAudio::stream(path,{from,b,speed,loop&&fade},rate,[ring,cancel](const float* samples,int frames){
    QByteArray bytes(frames*4,0);auto dest=reinterpret_cast<qint16*>(bytes.data());
    for(int i=0;i<frames*2;++i)dest[i]=qint16(qBound(-32768,int(std::lround(samples[i]*32767)),32767));
    return ring->push(bytes.constData(),bytes.size(),*cancel);
   },*cancel,&error))break;
   from=a;
  }while(loop&&!*cancel);
  ring->done=true;
  if(!error.isEmpty()&&!*cancel)QMetaObject::invokeMethod(this,[this,error,token]{if(token==_generation){_status=error;emit changed();}},Qt::QueuedConnection);
 });
 _status=tr("Preparing audio");_timer.start();emit changed();
}
void ReferenceMediaSession::sampleClock() {
 if(!_audio)return;double position=_origin+_audio->processedUSecs()/1000.*_speed;
 if(_loop&&_b>_a&&position>=_b)position=_a+std::fmod(position-_b,_b-_a);
 _position=qBound(0.,position,_duration);
}
void ReferenceMediaSession::updateClock() {
 if(!_audio)return;
 if(!_outputStarted){if(_ring->bytesAvailable()<16384&&!_ring->done)return;_outputStarted=true;_audio->start(_ring);_status=tr("Playing");}
 sampleClock();
 if((_ring->done&&_ring->bytesAvailable()==0&&_audio->state()==QAudio::IdleState)||(!_loop&&_position>=_duration)){pause();return;}
 if(_audio->error()!=QAudio::NoError&&_audio->error()!=QAudio::UnderrunError){pause();_status=tr("Audio device error");emit changed();return;}
 emit changed();
}
void ReferenceMediaSession::pause(){sampleClock();stopWorker();_status=tr("Paused");emit changed();}
void ReferenceMediaSession::seek(double v){bool resume=playing();sampleClock();stopWorker();_position=qBound(0.,v,_duration);emit changed();if(resume)play();}
void ReferenceMediaSession::setSpeed(double v){v=qBound(.5,v,1.5);if(qFuzzyCompare(v,_speed))return;bool resume=playing();sampleClock();stopWorker();_speed=v;emit changed();if(resume)play();}
void ReferenceMediaSession::setVolume(double v){_volume=qBound(0.,v,1.);if(_audio)_audio->setVolume(_volume);emit changed();}
void ReferenceMediaSession::setLoop(double a,double b,bool enabled,bool fade){
 bool resume=playing();sampleClock();stopWorker();_a=qBound(0.,a,_duration);_b=qBound(_a,b,_duration);_loop=enabled&&_b>_a;_fade=fade;emit changed();if(resume)play();
}
QVariantMap ReferenceMediaSession::identity()const {
 QFileInfo file(_path);return {{"path",_path},{"size",file.size()},{"modified",file.lastModified().toMSecsSinceEpoch()},{"duration",_duration}};
}

void ReferenceMediaSession::requestWaveform(double from,double until,int bins) {
 if(_duration<=0)return;from=qBound(0.,from,_duration);until=qBound(from,until,_duration);bins=qBound(64,bins,4096);if(until<=from)return;
 QString key=QString::number(from)+":"+QString::number(until)+":"+QString::number(bins);
 if(_tiles.contains(key)){emit waveformReady(from,until,_tiles.value(key));return;}
 if(_waveCancel)*_waveCancel=true;auto cancel=std::make_shared<std::atomic_bool>(false);_waveCancel=cancel;
 const int token=++_waveGeneration;auto path=_path;
 _loads.append(QtConcurrent::run([this,path,from,until,bins,cancel,token,key]{
  KumoAudio::Reader reader;QString error;if(!reader.open(path,&error,cancel.get()))return;
  qint64 begin=std::llround(from*reader.rate()/1000),end=std::llround(until*reader.rate()/1000),pos=begin;
  if(!reader.seek(begin))return;QVector<double> peaks(bins,0);std::vector<float> block(4096*reader.channels());
  while(pos<end&&!*cancel){auto n=reader.read(block.data(),qMin<qint64>(4096,end-pos));if(!n)break;for(int f=0;f<n;++f){auto bin=qMin(bins-1,int((pos+f-begin)*bins/(end-begin)));for(int c=0;c<reader.channels();++c)peaks[bin]=qMax(peaks[bin],std::abs(double(block[f*reader.channels()+c])));}pos+=n;}
  if(*cancel)return;QVariantList result;for(double p:peaks)result.append(p);
  QMetaObject::invokeMethod(this,[this,result,from,until,key,token]{if(token!=_waveGeneration)return;if(_tiles.size()>=16)_tiles.clear();_tiles.insert(key,result);emit waveformReady(from,until,result);},Qt::QueuedConnection);
 }));
}
