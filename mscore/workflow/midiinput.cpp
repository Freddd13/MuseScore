// SPDX-License-Identifier: GPL-2.0-or-later
// Bounded SMF reader: values only, independent of score/import/audio state.
#include "midiinput.h"
#include <QFile>
#include <QUrl>
#include <QCryptographicHash>
#include <QMap>
#include <QVector>
#include <algorithm>
#include <stdexcept>
#include <climits>
namespace Ms {
namespace {
struct Input {
 QByteArray bytes;int pos=0,limit=0;
 int byte(){if(pos>=limit)throw std::runtime_error("truncated MIDI");return uchar(bytes[pos++]);}
 quint32 number(int n){quint32 value=0;for(int i=0;i<n;++i)value=(value<<8)|byte();return value;}
 quint32 variable(){quint32 n=0;for(int i=0;i<4;++i){int b=byte();n=(n<<7)|(b&127);if(!(b&128))return n;}throw std::runtime_error("invalid MIDI delta");}
 QByteArray take(int n){if(n<0||n>limit-pos)throw std::runtime_error("invalid MIDI chunk");auto result=bytes.mid(pos,n);pos+=n;return result;}
};
struct Tempo {int tick;double micros;};
}
QVariantMap readPerformanceMidi(const QString& url,const std::atomic_bool& canceled) {
 try {
  const QString path=QUrl(url).isLocalFile()?QUrl(url).toLocalFile():url;QFile file(path);
  if(!file.open(QIODevice::ReadOnly)||file.size()>32*1024*1024)throw std::runtime_error("MIDI unavailable or over 32 MiB");
  Input in{file.readAll(),0,0};in.limit=in.bytes.size();
  if(in.take(4)!="MThd")throw std::runtime_error("MThd expected");
  int header=in.number(4);if(header<6)throw std::runtime_error("invalid MIDI header");
  int format=in.number(2),tracks=in.number(2),division=in.number(2);in.take(header-6);
  if(format>1||(format==0&&tracks!=1)||tracks<1||tracks>256||!division)throw std::runtime_error("only SMF 0/1 with valid clock supported");
  QVariantList events,notes,trackInfo;QVector<Tempo> tempos{{0,500000.}};int eventId=0;
  for(int t=0;t<tracks;++t){
   if(canceled)return {{"error","canceled"}};
   if(in.take(4)!="MTrk")throw std::runtime_error("MTrk expected");quint32 size=in.number(4);
   if(size>quint32(in.limit-in.pos))throw std::runtime_error("truncated track");
   int end=in.pos+int(size),outer=in.limit;in.limit=end;int tick=0,status=0,port=0;QString name;QMap<QString,QVector<int>> pending;
   while(in.pos<end){
    if(canceled)return {{"error","canceled"}};
    auto delta=in.variable();if(delta>quint32(INT_MAX-tick))throw std::runtime_error("MIDI time overflow");tick+=delta;
    int value=in.byte();int type=value;
    if(value<128){if(status<128||status>=240)throw std::runtime_error("invalid running status");type=status;--in.pos;}
    else if(type<240)status=type;else status=0;
    QVariantMap e{{"id",eventId++},{"track",t},{"tick",tick},{"type",type},{"port",port}};
    if(eventId>500000)throw std::runtime_error("MIDI exceeds 500000 events");
    if(type==255){
     int meta=in.byte();auto data=in.take(int(in.variable()));e.insert("meta",meta);e.insert("data",QString::fromLatin1(data.toHex()));
     if(meta==81&&data.size()==3){double micros=(uchar(data[0])<<16)|(uchar(data[1])<<8)|uchar(data[2]);if(micros<=0)throw std::runtime_error("invalid MIDI tempo");tempos.append({tick,micros});}
     if(meta==3)name=QString::fromUtf8(data);if(meta==33&&data.size()==1)port=uchar(data[0]);
     events.append(e);if(meta==47){in.pos=end;break;}
    }else if(type==240||type==247){auto data=in.take(int(in.variable()));e.insert("data",QString::fromLatin1(data.toHex()));events.append(e);}
    else if(type>=128&&type<240){
     int a=in.byte(),b=(type&240)==192||(type&240)==208?0:in.byte();if(a>127||b>127)throw std::runtime_error("invalid MIDI data");
     int channel=type&15,kind=type&240;e.insert("channel",channel);e.insert("a",a);e.insert("b",b);events.append(e);
     QString key=QString("%1:%2:%3").arg(port).arg(channel).arg(a);
     if(kind==144&&b>0){int index=notes.size();auto n=e;n.insert("id",QString("m%1").arg(e.value("id").toInt()));n.insert("pitch",a);n.insert("velocity",b);n.insert("off",tick);n.insert("unreleased",true);n.insert("ambiguous",!pending[key].isEmpty());for(int old:pending[key]){auto row=notes[old].toMap();row.insert("ambiguous",true);notes[old]=row;}notes.append(n);pending[key].append(index);}
     else if(kind==128||(kind==144&&b==0)){if(!pending[key].isEmpty()){int index=pending[key].takeFirst();auto n=notes[index].toMap();n.insert("off",tick);n.insert("unreleased",false);notes[index]=n;}}
    }else throw std::runtime_error("unsupported system status in SMF");
   }
   in.limit=outer;in.pos=end;trackInfo.append(QVariantMap{{"index",t},{"name",name}});
  }
  std::stable_sort(tempos.begin(),tempos.end(),[](const Tempo& a,const Tempo& b){return a.tick<b.tick;});
  struct Clock{int tick;double seconds,rate;};QVector<Clock> clock;
  if(division&32768){int fps=-qint8(division>>8),sub=division&255;double rate=(fps==29?29.97:fps)*sub;if((fps!=24&&fps!=25&&fps!=29&&fps!=30)||rate<=0)throw std::runtime_error("invalid SMPTE clock");clock.append({0,0,1/rate});}
  else {clock.append({0,0,.5/division});for(auto tempo:tempos){auto last=clock.last();double sec=last.seconds+(tempo.tick-last.tick)*last.rate;if(clock.last().tick==tempo.tick)clock.removeLast();clock.append({tempo.tick,sec,tempo.micros/1000000/division});}}
  auto seconds=[&](int tick){auto i=std::upper_bound(clock.begin(),clock.end(),tick,[](int t,const Clock& c){return t<c.tick;});if(i!=clock.begin())--i;return i->seconds+(tick-i->tick)*i->rate;};
  for(auto& value:notes){auto n=value.toMap();n.insert("seconds",seconds(n["tick"].toInt()));n.insert("endSeconds",seconds(n["off"].toInt()));value=n;}
  QVariantList tempoValues;for(auto t:tempos)tempoValues.append(QVariantMap{{"tick",t.tick},{"micros",t.micros}});
  return {{"path",path},{"sha256",QString::fromLatin1(QCryptographicHash::hash(in.bytes,QCryptographicHash::Sha256).toHex())},{"format",format},{"division",division},{"smpte",bool(division&32768)},{"tracks",trackInfo},{"events",events},{"notes",notes},{"tempos",tempoValues}};
 }catch(const std::exception& e){return {{"error",QString::fromUtf8(e.what())}};}
}
}
