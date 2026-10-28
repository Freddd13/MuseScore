// Export-only reading of written hand indications. Symbol ranges remain shared
// with the existing HandBracket reader; text changes are scoped to their staff.
static int writtenHandText(const QString& text) {
      QString value=text.toLower().trimmed();value.remove(QRegularExpression("[\\s.。]"));
      if(value=="rh" || value==QString::fromUtf8("右手"))return 1;
      if(value=="lh" || value==QString::fromUtf8("左手"))return 0;
      if(value=="ord" || value=="ordinary")return -3;
      return -1;
      }
static QHash<const Note*,int> exportWrittenHands(const Score* score,QHash<const Note*,QString>& sources) {
      auto result=HandBracket::assignments(score,false);for(auto i=result.cbegin();i!=result.cend();++i)sources[i.key()]=QString::fromUtf8("分手折线");
      QHash<int,int> staffHands;
      auto merge=[&](const Note* note,int hand,const QString& source) {
            if(hand<0)return;
            const int previous=result.value(note,-1);
            result[note]=previous==-1||previous==hand?hand:-2;
            sources[note]=previous==-1?source:sources.value(note)+QString::fromUtf8("、")+source;
            };
      for(auto segment=score->firstSegment(SegmentType::ChordRest);segment;segment=segment->next1(SegmentType::ChordRest)) {
            for(auto e:segment->annotations())if(e->isStaffText()) {
                  auto text=dynamic_cast<const TextBase*>(e);if(!text)continue;const int hand=writtenHandText(text->plainText());
                  if(hand==-3)staffHands.remove(e->staffIdx());else if(hand>=0)staffHands[e->staffIdx()]=hand;
                  }
            for(int track=0;track<score->nstaves()*VOICES;++track) {
                  auto element=segment->element(track);if(!element||!element->isChord())continue;auto chord=toChord(element);
                  for(auto note:chord->notes()) {
                        if(staffHands.contains(chord->staffIdx()))merge(note,staffHands[chord->staffIdx()],QString::fromUtf8("谱表r.h.／l.h.文字"));
                        for(const auto& elements:{chord->el(),note->el()})for(auto e:elements)if(auto text=dynamic_cast<const TextBase*>(e))
                              merge(note,writtenHandText(text->plainText()),QString::fromUtf8("音符r.h.／l.h.文字"));
                        }
                  }
            }
      return result;
      }
