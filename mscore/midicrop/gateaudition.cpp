// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "gateaudition.h"
#include "libmscore/chord.h"
#include "libmscore/note.h"
#include "libmscore/score.h"
#include "libmscore/playbacktiming.h"
#include "libmscore/tempo.h"
#include <QHash>
#include <QSet>
namespace Ms {
GateAudition prepareGateAudition(Score* score,const MidiGateData& data,const QVector<MidiGateNote>& rows){
      GateAudition result;if(!score)return result;result.original=renderPlaybackPreview(score,score,!data.writtenPedal);result.proposed=result.original;result.proposed.events.clear();
      MidiGateProcessor clock;clock.setClock(data);QHash<QString,int> sources;QSet<int> expected,changed;
      auto sourceKey=[](int tick,int track,int pitch,int port,int channel){return QString("%1:%2:%3:%4:%5").arg(tick).arg(track).arg(pitch).arg(port).arg(channel);};
      for(int i=0;i<rows.size();++i){const auto& n=rows[i];if(n.known)sources[sourceKey(n.sourceUtick,n.voice,n.pitch,n.port,n.channel)]=i;if(n.newOff<n.off)expected.insert(i);}
      QHash<int,QVector<int>> pending;
      for(const auto& pair:result.original.events){const auto& event=pair.second;int tick=pair.first;const int key=event.channel()*128+event.pitch();
            if(event.type()==ME_NOTEON&&event.velo()>0){auto note=event.noteEventOwner()?event.noteEventOwner():event.note();const int row=note?sources.value(sourceKey(pair.first,note->track(),event.pitch(),score->masterScore()->midiPort(event.channel()),score->masterScore()->midiChannel(event.channel())),-1):-1;pending[key].append(row);}
            else if(event.type()==ME_NOTEOFF||(event.type()==ME_NOTEON&&event.velo()==0)){
                  auto& queue=pending[key];if(!queue.isEmpty()){const int i=queue.takeFirst();if(expected.contains(i)){
                        const auto& note=rows[i];const double seconds=PlaybackTiming::time(score,tick)-clock.durationMs(note.newOff,note.off)/1000.;
                        const int at=seconds<0?qRound(seconds*DIVISION*score->tempomap()->tempo(0)*score->tempomap()->relTempo()):score->utime2utick(seconds);
                        if(at>note.sourceUtick&&at<tick){tick=at;changed.insert(i);}
                        }}
                  }
            result.proposed.events.insert({tick,event});
            }
      result.changed=changed.size();result.unmatched=(expected-changed).size();return result;
      }
}
