// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "sharedpiano.h"
#include "vstpanel.h"
#include "featuregate.h"
#include "libmscore/score.h"
#include "libmscore/instrument.h"
#include "libmscore/part.h"
#include "libmscore/undo.h"
#include "mscore/musescore.h"
#include "mscore/seq.h"
#include "mscore/shortcut.h"
#include <QJsonDocument>
#include <QSettings>
#ifdef KUMO_VST_HOST
#include "audio/midi/msynthesizer.h"
#include "audio/midi/midipatch.h"
#include "audio/vst/vstsynth.h"
namespace Ms {
namespace {
quint64 commonSettingsRevision=0;
const char* settingsKey="kumo/vst/commonPiano";
VstSynth* engine(MasterSynthesizer* master) {
  return master?static_cast<VstSynth*>(master->synthesizer("KumoVST")):nullptr;
}
QVariantMap scoreConfiguration(Score* score) {
  if (!score) return {};
  for (const auto &v:score->masterScore()->synthesizerState().group("KumoVST"))
    if (v.id==0) return QJsonDocument::fromJson(v.data.toUtf8()).toVariant().toMap();
  return {};
}
QVariantMap instance(QVariantMap row) {
  row.remove("id"); row.remove("label"); row.remove("commonPiano"); row.remove("available"); row.remove("latency");
  return row;
}
bool sameInstance(const QVariantMap& a,const QVariantMap& b) {
  return VstHost::sameConfiguration({{"slots",QVariantList{a}}},{{"slots",QVariantList{b}}});
}
void writeSettings(const QVariantMap& value) {
  QSettings().setValue(settingsKey,QJsonDocument::fromVariant(value).toJson(QJsonDocument::Compact));
  ++commonSettingsRevision;
}
void reloadCurrent() {
  if (!mscore) return;
  if (seq) { seq->stopWait(); seq->waitForStoppedRendering(); }
  restoreVstForScore(mscore->currentScore());
  if (seq && mscore->currentScore()) { seq->initInstruments(); seq->invalidatePlaybackEvents(); }
}
class ChangeCommonPiano final:public UndoCommand {
  QVariantMap next;
  void flip(EditData*) override {
    const auto old=commonPianoSettings();
    const bool soundChanged=old["enabled"]!=next["enabled"] ||
        !sameInstance(old["instance"].toMap(),next["instance"].toMap()) ||
        (next["enabled"].toBool() && synti && !synti->property("kumoCommonPianoError").toString().isEmpty());
    writeSettings(next); next=old;
    if (soundChanged) {
      reloadCurrent();
      if (auto source=engine(synti))
        for (const auto &row:source->runtimeConfiguration()["slots"].toList())
          if (!row.toMap()["commonPiano"].toString().isEmpty()) {
            auto prepared=commonPianoSettings();prepared["instance"]=instance(row.toMap());writeSettings(prepared);break;
          }
    }
  }
public:
  explicit ChangeCommonPiano(const QVariantMap& value):next(value) {}
  UNDO_NAME("ChangeCommonPiano")
};
bool instrumentIsPiano(const Instrument* instrument,const Channel* channel,Score* score) {
  if (!instrument || instrument->useDrumset()) return false;
  QString id=instrument->instrumentId().trimmed().toLower();
  if (id.isEmpty()) id=instrument->getId().trimmed().toLower();
  if (id.contains("piano")) return true;
  if (!id.isEmpty()) return false;
  int program=channel->program();
  if (channel->synti()=="KumoVST")
    for (const auto &v:scoreConfiguration(score)["slots"].toList())
      if (v.toMap()["id"].toInt()==channel->bank()) program=v.toMap()["originalPatch"].toMap()["program"].toInt();
  return program>=0 && program<=5;
}
}
QVariantMap commonPianoSettings() {
  return QJsonDocument::fromJson(QSettings().value(settingsKey).toByteArray()).toVariant().toMap();
}
quint64 commonPianoRevision() { return commonSettingsRevision; }
bool isPianoChannel(Score* score,const Channel* channel) {
  if (!score || !channel) return false;
  auto master=score->masterScore();
  for (const auto &mapping:master->midiMapping()) if (mapping.articulation()==channel) {
    const auto part=mapping.part();
    for (const auto &entry:*part->instruments()) {
      const auto instrument=entry.second;
      for (int i=0;i<instrument->channel().size();++i)
        if (instrument->playbackChannel(i,master)==channel)
          return instrumentIsPiano(instrument,channel,score);
    }
    return instrumentIsPiano(part->instrument(),channel,score);
  }
  return false;
}
bool scoreUsesOwnPiano(Score* score) { return scoreConfiguration(score)["pianoSource"].toString()=="own"; }
QString activeCommonPianoName(Score* score,const Channel* channel) {
  auto source=engine(synti);if (!source || !score || !channel || !mscore || !mscore->currentScore() ||
      mscore->currentScore()->masterScore()!=score->masterScore()) return {};
  const int id=source->pianoSlot(channel->channel());if (!id) return {};
  for (const auto &row:source->rack().instruments()) if (row.toMap()["id"].toInt()==id)
    return row.toMap()["source"].toMap()["plugin"].toMap()["name"].toString();
  return {};
}
QVariantMap commonPianoFromCurrent(Score* score,const Channel* channel,bool builtIn) {
  auto source=engine(synti);
  if (!source || !isPianoChannel(score,channel)) return {};
  const int playing=source->pianoSlot(channel->channel());
  const int id=playing?playing:(channel->synti()=="KumoVST"?channel->bank():0);
  if (!builtIn) for (const auto &row:source->runtimeConfiguration()["slots"].toList())
    if (row.toMap()["id"].toInt()==id) return instance(row.toMap());
  auto fluid=synti->synthesizer("Fluid"); QStringList paths;
  if (fluid) for (const auto &font:fluid->soundFontsInfo()) paths.append(font.fileName);
  if (paths.isEmpty()) return {};
  int bank=channel->bank(),program=channel->program();
  if (channel->synti()=="KumoVST") for (const auto &row:source->configuration()["slots"].toList())
    if (row.toMap()["id"].toInt()==channel->bank()) {
      const auto original=row.toMap()["originalPatch"].toMap(); bank=original["bank"].toInt();program=original["program"].toInt();break;
    }
  const auto patch=synti->getPatchInfo("Fluid",bank,program);
  QVariantMap descriptor{{"format","SF2"},{"path",paths.join('\n')},
    {"uid",QString("%1:%2").arg(bank).arg(program)},
    {"name",patch?patch->name:QObject::tr("内置钢琴")},{"instrument",true}};
  return {{"source",QVariantMap{{"plugin",descriptor}}},
    {"originalPatch",QVariantMap{{"synti","Fluid"},{"bank",bank},{"program",program}}}};
}
QVariantMap applyCommonPianoSettings(Score* score,const QVariantMap& value) {
  if (!personalFeaturesEnabled()) return {{"error",QObject::tr("请先开启个人扩展功能。")}};
  if (value["enabled"].toBool() && value["instance"].toMap()["source"].toMap()["plugin"].toMap().isEmpty())
    return {{"error",QObject::tr("先为当前钢琴选择音源，再开启共用。")}};
  auto next=value; if (next.contains("instance")) next["instance"]=instance(next["instance"].toMap());
  if (score) {
    score=score->masterScore();score->startCmd();score->undo(new ChangeCommonPiano(next));score->endCmd();
    getAction("undo")->setEnabled(score->undoStack()->canUndo());
    getAction("redo")->setEnabled(score->undoStack()->canRedo());
  } else { writeSettings(next); reloadCurrent(); }
  if (synti && !synti->property("kumoCommonPianoError").toString().isEmpty())
    return {{"message",QObject::tr("共用音源未能载入，当前使用本谱音源。%1").arg(synti->property("kumoCommonPianoError").toString())}};
  return {{"message",value["enabled"].toBool()?QObject::tr("所有乐谱的钢琴已采用共用设置；本谱可单独切回自己的音源。")
                                             :QObject::tr("已关闭共用，各谱使用自己的音源。")}};
}
QVariantMap setScoreOwnPiano(Score* score,bool own) {
  if (!score) return {{"error",QObject::tr("请先打开乐谱。")}};
  auto value=scoreConfiguration(score);value["pianoSource"]=own?"own":"common";value["schema"]=1;
  return applyVstConfiguration(score,value);
}
void captureCommonPianoEdits(Score* score,int preferredSlot) {
  auto source=engine(synti);
  if (!source || !score || !mscore || !mscore->currentScore() ||
      score->masterScore()!=mscore->currentScore()->masterScore()) return;
  auto settings=commonPianoSettings(); if (!settings["enabled"].toBool() || scoreUsesOwnPiano(score)) return;
  source->rack().takeChanges();
  const int edited=source->rack().takeChangedCommonPiano();
  if (!preferredSlot) preferredSlot=edited;
  QVariantMap found;
  for (const auto& row:source->runtimeConfiguration()["slots"].toList()) {
    const auto played=row.toMap();if (played["commonPiano"].toString().isEmpty()) continue;
    if (found.isEmpty() || played["id"].toInt()==preferredSlot) found=played;
    if (played["id"].toInt()==preferredSlot) break;
  }
  if (found.isEmpty()) return;
  const auto next=instance(found);
  if (!sameInstance(next,settings["instance"].toMap())) {
    settings["instance"]=next;applyCommonPianoSettings(score,settings);
  }
}
void prepareCommonPianos(MasterSynthesizer* master,Score* score,bool refresh) {
  auto source=engine(master);if (!source) return;
  const auto settings=commonPianoSettings();QVariantList instances;QVector<int> banks(256,0);QSet<int> suppress,used;
  const bool enabled=personalFeaturesEnabled() && settings["enabled"].toBool() && score && !scoreUsesOwnPiano(score);
  if (enabled) {
    auto config=scoreConfiguration(score);
    for (const auto& row:config["slots"].toList()) used.insert(row.toMap()["id"].toInt());
    QMap<const Part*,int> parts; int nextId=16383;
    for (const auto &m:score->masterScore()->midiMapping()) {
      auto c=m.articulation();if (!c || c->channel()<0 || c->channel()>=banks.size() || !isPianoChannel(score,c)) continue;
      if (!parts.contains(m.part())) {
        while (used.contains(nextId) && nextId>0) --nextId;
        if (!nextId) break;
        const int ordinal=parts.size()+1;auto row=settings["instance"].toMap();
        row["id"]=nextId;row["commonPiano"]=QString("piano-%1").arg(ordinal);
        row["label"]=QString("%1 %2").arg(settings.value("name",QObject::tr("共用钢琴")).toString()).arg(ordinal);
        instances.append(row);parts.insert(m.part(),nextId);used.insert(nextId--);
      }
      banks[c->channel()]=parts[m.part()];
      if (c->synti()=="KumoVST") suppress.insert(c->bank());
    }
    for (const auto &m:score->masterScore()->midiMapping()) {
      auto c=m.articulation();if (c && c->synti()=="KumoVST" && !banks.value(c->channel())) suppress.remove(c->bank());
    }
  }
  source->setPianoRouting(instances,banks,suppress,refresh);
  if (!settings["enabled"].toBool() || !personalFeaturesEnabled()) source->rack().releaseCommonPianos();
}
void routeCommonPianos(MasterSynthesizer* master,Score*,QVector<int>& banks) {
  auto source=engine(master);if (!source) return;
  QVector<int> routing(256,-1);bool unavailable=false;
  for (int i=0;i<banks.size();++i) if (source->pianoSlot(i)) {
    const auto plugin=source->rack().plugin(source->pianoSlot(i));
    unavailable|=!plugin || !plugin->status().isEmpty();
  }
  if (unavailable) {
    const auto error=source->problems().join('\n');
    source->setPianoRouting({},QVector<int>(256,0),{},true);
    master->setProperty("kumoCommonPianoError",error);
  } else {
    master->setProperty("kumoCommonPianoError",QString());
    for (int i=0;i<banks.size();++i) if (source->pianoSlot(i)) {
      banks[i]=source->pianoSlot(i);routing[i]=master->index("KumoVST");
    }
  }
  master->setPlaybackRouting(routing);
}
}
#else
namespace Ms {
QVariantMap commonPianoSettings(){return {};}
quint64 commonPianoRevision(){return 0;}
bool isPianoChannel(Score*,const Channel*){return false;}
bool scoreUsesOwnPiano(Score*){return false;}
QString activeCommonPianoName(Score*,const Channel*){return {};}
QVariantMap commonPianoFromCurrent(Score*,const Channel*,bool){return {};}
QVariantMap applyCommonPianoSettings(Score*,const QVariantMap&){return {};}
QVariantMap setScoreOwnPiano(Score*,bool){return {};}
void captureCommonPianoEdits(Score*,int){}
void prepareCommonPianos(MasterSynthesizer*,Score*,bool){}
void routeCommonPianos(MasterSynthesizer*,Score*,QVector<int>&){}
}
#endif
