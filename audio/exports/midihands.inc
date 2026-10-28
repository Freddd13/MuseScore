// Value-only hand inference and event routing. Onset hand is kept independently
// from a later transfer in a tie. No score objects or file access occur here.
QVector<MidiGateNote> assignMidiHands(const MidiGateData& data,const QVector<MidiGateNote>& input,
      const MidiExportOptions& options,const std::function<bool()>& canceled) {
      auto rows=input;MidiGateProcessor clock;clock.setClock(data);
      std::map<int,std::map<int,QVector<int>>> parts;
      for(int i=0;i<rows.size();++i) {
            auto& n=rows[i];n.exportHand=-1;n.handReview=false;n.exportHandSource.clear();
            if(!n.piano)continue;
            const int override=options.handOverrides.value(n.id,-1);
            if(override==0||override==1){n.exportHand=override;n.exportHandSource=QString::fromUtf8("手动指定");}
            else if(n.hand>=0&&n.hand<2){n.exportHand=n.hand;n.exportHandSource=n.handSource.isEmpty()?QString::fromUtf8("分手记号"):n.handSource;}
            if(n.hand==-2||!n.known||n.ambiguous||n.offOrdinal<0) {
                  n.handReview=override<0;
                  if(override<0)n.exportHandSource=n.hand==-2?QString::fromUtf8("分手说明冲突；暂按显示谱表"):QString::fromUtf8("来源或同音关系需核对");
                  }
            auto& frames=parts[n.partId>=0?n.partId:100000+n.track];frames[n.on].append(i);
            for(const auto& point:n.instructions)if(point.handSet&&point.tick>n.on&&point.tick<n.newOff)frames[point.tick];
            for(const auto& change:n.fingerChanges)if(change.tick>n.on&&change.tick<n.newOff)frames[change.tick];
            }
      for(const auto& part:parts) {
            const auto& frames=part.second;
            PianoHands::Position previous[2];QVector<int> active;
            auto frameKeys=[&](const QVector<int>& ids,int tick) {
                  QVector<PianoHands::Key> keys;
                  for(int id:ids) {
                        const auto& n=rows[id];PianoHands::Key k;k.id=id;k.pitch=n.pitch;k.hand=n.exportHand;k.hint=n.handHint;k.attack=n.on==tick;
                        k.finger=n.finger;k.fingerMask=n.fingerMask;k.endSeconds=clock.durationMs(0,n.newOff)/1000.;
                        if(!k.attack) {for(const auto& p:n.instructions)if(p.tick<=tick){if(p.handSet)k.hand=p.hand;if(p.fingeringSet){k.finger=p.finger;k.fingerMask=p.fingerMask;}}
                              for(const auto& change:n.fingerChanges)if(change.tick<=tick){k.finger=change.to;k.fingerMask=1<<(change.to-1);}}
                        // A pedal-held written note can free the hand without changing
                        // this export's note length. Only binary safe holds qualify.
                        if(!k.attack)for(const auto& p:data.pedals)if(p.supported&&p.port==n.port&&p.channel==n.channel&&n.on>=p.down&&n.newOff<p.up&&tick>n.on&&tick<p.up){k.releasable=true;k.releaseSeconds=clock.durationMs(0,tick)/1000.;break;}
                        keys.append(k);
                        }
                  return keys;
                  };
            for(auto f=frames.cbegin();f!=frames.cend();++f) {
                  if(canceled&&canceled())return {};
                  const int tick=f->first;active.erase(std::remove_if(active.begin(),active.end(),[&](int i){return rows[i].newOff<=tick;}),active.end());active+=f->second;
                  const auto keys=frameKeys(active,tick);QVector<PianoHands::Future> future;auto next=f;auto futureActive=active;
                  for(int count=0;count<2&&++next!=frames.cend();++count) {
                        futureActive.erase(std::remove_if(futureActive.begin(),futureActive.end(),[&](int i){return rows[i].newOff<=next->first;}),futureActive.end());futureActive+=next->second;
                        future.append({frameKeys(futureActive,next->first),clock.durationMs(0,next->first)/1000.});
                        }
                  const auto plan=PianoHands::planWithContext(keys,options.gate.hands,previous,clock.durationMs(0,tick)/1000.,future);
                  for(int i=0;i<keys.size();++i)if(keys[i].attack) {
                        auto& n=rows[keys[i].id];
                        if(n.exportHand<0) {
                              n.exportHand=i<plan.hands.size()&&plan.hands[i]>=0?plan.hands[i]:n.handHint>=0?n.handHint:(n.pitch<60?0:1);
                              if(n.exportHandSource.isEmpty())n.exportHandSource=n.exportHand==n.handHint?QString::fromUtf8("显示谱表与前后手位"):QString::fromUtf8("前后手位与按键占用");
                              n.handReview|=!plan.possible||plan.uncertain||plan.limited;
                              }
                        if(!plan.possible&&n.exportHand>=0&&!plan.after[n.exportHand].fits){n.handReview=true;n.exportHandSource+=QString::fromUtf8("；这组音的手位需核对");}
                        }
                  for(int side=0;side<2;++side)previous[side]=PianoHands::positionFor(keys,plan,side,clock.durationMs(0,tick)/1000.);
                  }
            }
      return rows;
      }
void applyMidiHands(MidiFile& file,const QVector<MidiGateNote>& notes) {
      struct Route {int track=-1,tick=-1;};
      QMap<int,QMap<int,Route>> routes;QMap<QString,int> destinations;
      auto tracks=file.tracks();
      for(const auto& n:notes) {
            if(!n.piano||n.exportHand<0||n.exportHand>1||n.onOrdinal<0||n.offOrdinal<0||n.track<0||n.track>=file.tracks().size())continue;
            const auto key=QString("%1:%2:%3").arg(n.partId>=0?n.partId:100000+n.track).arg(n.port).arg(n.exportHand);
            int dest=destinations.value(key,-1);
            if(dest<0) {
                  dest=tracks.size();destinations[key]=dest;MidiTrack track;track.setOutPort(n.port);track.setOutChannel(n.channel);
                  MidiEvent name;name.setType(ME_META);name.setMetaType(META_TRACK_NAME);
                  auto title=(n.partName+" · "+(n.exportHand==0?"Left Hand":"Right Hand")).toUtf8();title.append('\0');name.setLen(title.size());name.setEData(std::vector<uchar>(title.begin(),title.end()));track.insert(0,name);
                  // Explicit port keeps a second MIDI device distinct after reload.
                  MidiEvent port;port.setType(ME_META);port.setMetaType(META_PORT_CHANGE);port.setLen(1);port.setEData(std::vector<uchar>{uchar(n.port)});track.insert(0,port);
                  tracks.append(track);
                  }
            routes[n.track][n.onOrdinal]={dest,n.on};routes[n.track][n.offOrdinal]={dest,n.newOff};
            }
      for(int track=0;track<file.tracks().size();++track) {
            std::multimap<int,MidiEvent> kept;int ordinal=0;
            QMap<int,int> releaseMoves;for(const auto& n:notes)if(n.track==track&&n.offOrdinal>=0&&n.newOff<n.off)releaseMoves[n.offOrdinal]=n.newOff;
            for(const auto& pair:file.tracks()[track].events()) {
                  const auto route=routes[track].value(ordinal);
                  if(route.track>=0)tracks[route.track].insert(route.tick,pair.second);
                  else kept.emplace(releaseMoves.value(ordinal,pair.first),pair.second);
                  ++ordinal;
                  }
            tracks[track].events().swap(kept);
            }
      file.tracks().swap(tracks);
      }
