// SPDX-License-Identifier: GPL-2.0-or-later
#include "audioreader.h"
#include <QFileInfo>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QHash>
#include <QDateTime>
#ifdef Q_OS_WIN
#include <windows.h>
#define ENABLE_SNDFILE_WINDOWS_PROTOTYPES 1
#endif
#include <sndfile.h>
#define DR_MP3_IMPLEMENTATION
#include "vendor/dr_mp3.h"
#include "SoundTouch.h"
#include <cmath>
namespace KumoAudio {
struct Reader::State {SNDFILE* file=nullptr;SF_INFO info={};drmp3 mp3={};bool compressed=false;qint64 count=0;QFile input;const std::atomic_bool* cancel=nullptr;};
Reader::Reader():s(new State){}
Reader::~Reader(){if(s->file)sf_close(s->file);if(s->compressed)drmp3_uninit(&s->mp3);}
bool Reader::open(const QString& path,QString* error,const std::atomic_bool* cancel) {
 if(s->file||s->compressed)return false;
 if(QFileInfo(path).suffix().compare("mp3",Qt::CaseInsensitive)==0) {
  s->cancel=cancel;s->input.setFileName(path);
  if(!s->input.open(QIODevice::ReadOnly))return false;
  auto read=[](void* ctx,void* out,size_t amount)->size_t {auto st=static_cast<State*>(ctx);if(st->cancel&&*st->cancel)return 0;auto n=st->input.read(static_cast<char*>(out),qint64(amount));return size_t(qMax<qint64>(0,n));};
  auto seek=[](void* ctx,int offset,drmp3_seek_origin origin)->drmp3_bool32 {auto st=static_cast<State*>(ctx);if(st->cancel&&*st->cancel)return DRMP3_FALSE;qint64 base=origin==DRMP3_SEEK_SET?0:origin==DRMP3_SEEK_END?st->input.size():st->input.pos();return st->input.seek(base+offset)?DRMP3_TRUE:DRMP3_FALSE;};
  auto tell=[](void* ctx,drmp3_int64* out)->drmp3_bool32 {*out=static_cast<State*>(ctx)->input.pos();return DRMP3_TRUE;};
  s->compressed=drmp3_init(&s->mp3,read,seek,tell,nullptr,s.get(),nullptr);
  if(s->compressed){
   // A bounded metadata cache prevents repeated full scans on seek/loop.
   static QMutex mutex;static QHash<QString,qint64> counts;
   QFileInfo info(path);QString key=info.absoluteFilePath()+QString::number(info.size())+QString::number(info.lastModified().toMSecsSinceEpoch());
   {QMutexLocker lock(&mutex);s->count=counts.value(key,0);}
   if(!s->count){s->count=drmp3_get_pcm_frame_count(&s->mp3);if(!(cancel&&*cancel)){QMutexLocker lock(&mutex);if(counts.size()>=16)counts.clear();counts.insert(key,s->count);}}
   if(cancel&&*cancel)return false;
   drmp3_seek_to_pcm_frame(&s->mp3,0);
  }
 }else{
#ifdef Q_OS_WIN
  s->file=sf_wchar_open(reinterpret_cast<const wchar_t*>(path.utf16()),SFM_READ,&s->info);
#else
  s->file=sf_open(path.toUtf8().constData(),SFM_READ,&s->info);
#endif
  if(s->file)s->count=s->info.frames;
 }
 if((!s->file&&!s->compressed)||channels()<1||channels()>2||rate()<8000||rate()>192000||!s->count) {
  if(error)*error=QString("Cannot decode mono/stereo WAV, FLAC or MP3: %1").arg(path);return false;
 }
 return true;
}
int Reader::rate()const{return s->compressed?s->mp3.sampleRate:s->info.samplerate;}
int Reader::channels()const{return s->compressed?s->mp3.channels:s->info.channels;}
qint64 Reader::frames()const{return s->count;}
qint64 Reader::read(float* b,qint64 n){return s->compressed?drmp3_read_pcm_frames_f32(&s->mp3,n,b):sf_readf_float(s->file,b,n);}
bool Reader::seek(qint64 n){return s->compressed?bool(drmp3_seek_to_pcm_frame(&s->mp3,n)):sf_seek(s->file,n,SEEK_SET)>=0;}
bool streamSequence(const QString& path,const std::vector<Segment>& segments,int outputRate,
 const std::function<bool(const float*,int)>& sink,const std::atomic_bool& cancel,QString* error) {
 if(segments.empty())return false;Reader reader;if(!reader.open(path,error,&cancel))return false;
 const int rate=reader.rate(),channels=reader.channels();qint64 target=0,previous=-1;
 for(auto seg:segments){qint64 begin=std::llround(seg.start*rate/1000),end=std::llround(seg.end*rate/1000);if(begin<0||end>reader.frames()||end<=begin||(previous>=0&&begin!=previous)||seg.speed<.5||seg.speed>1.5)return false;previous=end;target+=std::llround(double(end-begin)*outputRate/(rate*seg.speed));}
 if(!reader.seek(std::llround(segments.front().start*rate/1000)))return false;
 soundtouch::SoundTouch stretch;stretch.setChannels(2);stretch.setSampleRate(outputRate);stretch.setRate(double(rate)/outputRate);stretch.setSetting(SETTING_USE_QUICKSEEK,0);
 std::vector<float> input(256*channels),stereo(512),out(8192);qint64 produced=0;
 auto drain=[&](){while(!cancel){int count=stretch.receiveSamples(out.data(),4096);if(!count)break;int accepted=int(qMin<qint64>(count,target-produced));if(accepted<=0)break;
  if(segments.front().fade)for(int f=0;f<accepted;++f){double gain=qMin(1.,qMin(double(produced+f)/qMax(1,outputRate/200),double(target-produced-f)/qMax(1,outputRate/200)));out[f*2]*=float(gain);out[f*2+1]*=float(gain);}
  if(!sink(out.data(),accepted))return false;produced+=accepted;}return !cancel;};
 for(auto seg:segments){stretch.setTempo(seg.speed);qint64 remaining=std::llround(seg.end*rate/1000)-std::llround(seg.start*rate/1000);
  while(remaining>0&&!cancel){qint64 count=reader.read(input.data(),qMin<qint64>(256,remaining));if(count<=0)return false;remaining-=count;for(int f=0;f<count;++f)for(int c=0;c<2;++c)stereo[f*2+c]=input[f*channels+qMin(c,channels-1)];stretch.putSamples(stereo.data(),unsigned(count));if(!drain())return false;}
 }
 stretch.flush();return drain()&&!cancel;
}
bool stream(const QString& path,const Segment& seg,int outputRate,
 const std::function<bool(const float*,int)>& sink,const std::atomic_bool& cancel,QString* error) {
 Reader reader;if(!reader.open(path,error,&cancel))return false;
 const int rate=reader.rate(),channels=reader.channels();
 const qint64 begin=qBound<qint64>(0,std::llround(seg.start*rate/1000),reader.frames());
 const qint64 end=qBound<qint64>(begin,std::llround(seg.end*rate/1000),reader.frames());
 if(end<=begin||seg.speed<.5||seg.speed>1.5||outputRate<8000||!reader.seek(begin))return false;
 soundtouch::SoundTouch stretch;stretch.setChannels(2);stretch.setSampleRate(outputRate);
 const double ratio=double(rate)/outputRate;
 stretch.setRate(ratio);stretch.setTempo(seg.speed);
 stretch.setSetting(SETTING_USE_QUICKSEEK,0);
 std::vector<float> input(4096*channels),stereo(8192),out(8192);
 qint64 remaining=end-begin,produced=0;
 const qint64 target=std::llround(double(end-begin)*outputRate/(rate*seg.speed));
 auto drain=[&]() {
  while(!cancel) {
   const int count=stretch.receiveSamples(out.data(),4096);if(!count)break;
   int accepted=int(qMin<qint64>(count,target-produced));if(accepted<=0)break;
   if(seg.fade)for(int f=0;f<accepted;++f) {
    double gain=qMin(1.,qMin(double(produced+f)/qMax(1,outputRate/200),double(target-produced-f)/qMax(1,outputRate/200)));
    out[f*2]*=float(gain);out[f*2+1]*=float(gain);
   }
   if(!sink(out.data(),accepted))return false;produced+=accepted;
  }
  return !cancel;
 };
 while(remaining>0&&!cancel) {
  qint64 count=reader.read(input.data(),qMin<qint64>(4096,remaining));if(!count)break;remaining-=count;
  for(int f=0;f<count;++f)for(int c=0;c<2;++c)stereo[f*2+c]=input[f*channels+qMin(c,channels-1)];
  stretch.putSamples(stereo.data(),unsigned(count));if(!drain())return false;
 }
 stretch.flush();if(!drain())return false;
 return !cancel&&remaining==0;
}
}
