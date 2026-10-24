//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Copyright (C) 2002-2013 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "msynthesizer.h"
#include "synthesizer.h"
#include "synthesizergui.h"

#include "libmscore/xml.h"

#include "midi/event.h"
#include "midi/midipatch.h"

#include "mscore/preferences.h"

namespace Ms {

extern QString dataPath;

//---------------------------------------------------------
//   MasterSynthesizer
//---------------------------------------------------------

MasterSynthesizer::MasterSynthesizer()
   : QObject(0)
      {
      defaultGainAsDecibels = convertGainToDecibels(defaultGain);
      for (auto& target : _playbackRouting) target.store(-1);
      }

//---------------------------------------------------------
//   init
//---------------------------------------------------------

void MasterSynthesizer::init()
      {
      SynthesizerState state;
      QString s(dataPath + "/synthesizer.xml");
      QFile f(s);
      if (!f.open(QIODevice::ReadOnly)) {
            // qDebug("cannot read synthesizer settings <%s>", qPrintable(s));
            setState(defaultState);
            return;
            }
      XmlReader e(&f);
      while (e.readNextStartElement()) {
            if (e.name() == "Synthesizer")
                  state.read(e);
            else
                  e.unknown();
            }
      if (!setState(state)) {
            f.remove();
            setState(defaultState);
            }
      }

//---------------------------------------------------------
//   MasterSynthesizer
//---------------------------------------------------------

MasterSynthesizer::~MasterSynthesizer()
      {
      for (Synthesizer* s : _synthesizer)
            delete s;
      for (int i = 0; i < MAX_EFFECTS; ++i) {
            for (Effect* e : _effectList[i])
                  delete e;
            // delete _effect[i];   // _effect takes from _effectList
            }
      }

//---------------------------------------------------------
//   reset
//---------------------------------------------------------

void MasterSynthesizer::reset()
      {
      for (Synthesizer* s : _synthesizer)
            s->reset();
      }

//---------------------------------------------------------
//   play
//---------------------------------------------------------

void MasterSynthesizer::play(const NPlayEvent& event, unsigned syntiIdx)
      {
      if (event.channel()>=0 && event.channel()<int(_playbackRouting.size())) {
            const int target=_playbackRouting[event.channel()].load(std::memory_order_acquire);
            if (target>=0) syntiIdx=unsigned(target);
            }
      if (syntiIdx >= _synthesizer.size())
            return;
      _synthesizer[syntiIdx]->setActive(true);
      _synthesizer[syntiIdx]->play(event);
      }

void MasterSynthesizer::setPlaybackRouting(const QVector<int>& targets)
      {
      for (int i=0;i<int(_playbackRouting.size());++i)
            _playbackRouting[i].store(i<targets.size()?targets[i]:-1,std::memory_order_release);
      }

//---------------------------------------------------------
//   synthNameToIndex
//---------------------------------------------------------

int MasterSynthesizer::index(const QString& name) const
      {
      int idx = 0;
      for (Synthesizer* s : _synthesizer) {
            if (s->name() == name)
                  return idx;
            ++idx;
            }
      qDebug("MasterSynthesizer::index for <%s> not found", qPrintable(name));
      return 0;
      }

//---------------------------------------------------------
//   synthIndexToName
//---------------------------------------------------------

QString MasterSynthesizer::name(unsigned idx) const
      {
      if (idx >= _synthesizer.size()) {
            qDebug("MasterSynthesizer::name() bad index %d", idx);
            return QString();
            }
      return QString(_synthesizer[idx]->name());
      }

//---------------------------------------------------------
//   getPatchInfo
//---------------------------------------------------------

QList<MidiPatch*> MasterSynthesizer::getPatchInfo() const
      {
      QList<MidiPatch*> pl;
      for (Synthesizer* s : _synthesizer)
            pl += s->getPatchInfo();
      return pl;
      }

//---------------------------------------------------------
//   getPatchInfo
//---------------------------------------------------------

MidiPatch* MasterSynthesizer::getPatchInfo(QString synti, int bank, int program)
      {
      for (Synthesizer* s : _synthesizer) {
            for (MidiPatch* p: s->getPatchInfo()) {
                  if (p->synti == synti && p->bank == bank && p->prog == program)
                        return p;
                  }
            }
      return nullptr;
      }

//---------------------------------------------------------
//   allSoundsOff
//---------------------------------------------------------

void MasterSynthesizer::allSoundsOff(int channel)
      {
      for (Synthesizer* s : _synthesizer)
            s->allSoundsOff(channel);
      }

//---------------------------------------------------------
//   allNotesOff
//---------------------------------------------------------

void MasterSynthesizer::allNotesOff(int channel)
      {
      for (Synthesizer* s : _synthesizer)
            s->allNotesOff(channel);
      }

//---------------------------------------------------------
//   synth
//---------------------------------------------------------

Synthesizer* MasterSynthesizer::synthesizer(const QString& name)
      {
      for (Synthesizer* s : _synthesizer) {
            if (s->name() == name)
                  return s;
            }
      return 0;
      }

//---------------------------------------------------------
//   hasSoundFontsLoaded
///   Checks whether any of the synthesizers in use has
///   at least one sound font loaded. \p false value may
///   indicate some errors in the used synthesizer state
///   as such configuration will produce no sound.
//---------------------------------------------------------

bool MasterSynthesizer::hasSoundFontsLoaded() const
      {
      for (const Synthesizer* s : _synthesizer) {
            if (!s->soundFontsInfo().empty())
                  return true;
            }
      return false;
      }

//---------------------------------------------------------
//   registerSynthesizer
//    ownership of s moves to MasterSynthesizer
//---------------------------------------------------------

void MasterSynthesizer::registerSynthesizer(Synthesizer* s)
      {
      _synthesizer.push_back(s);
      }

//---------------------------------------------------------
//   registerEffect
//---------------------------------------------------------

void MasterSynthesizer::registerEffect(int ab, Effect* e)
      {
      _effectList[ab].push_back(e);
      }

//---------------------------------------------------------
//   setEffect
//---------------------------------------------------------

void MasterSynthesizer::setEffect(int ab, int idx)
      {
      if (idx < 0 || idx >= int(_effectList[ab].size())) {
            qDebug("MasterSynthesizer::setEffect: bad idx %d %d", ab, idx);
            return;
            }
      lock2 = true;
      while(lock1)
#if (!defined (_MSCVER) && !defined (_MSC_VER))
         sleep(1);
#else
         Sleep(1000);      // MS-equivalent function, time in ms instead of seconds.
#endif
      _effect[ab] = _effectList[ab][idx];
      lock2 = false;
      }

//---------------------------------------------------------
//   effect
//---------------------------------------------------------

Effect* MasterSynthesizer::effect(int idx)
      {
      return _effect[idx];
      }

//---------------------------------------------------------
//   setSampleRate
//---------------------------------------------------------

void MasterSynthesizer::setSampleRate(float val)
      {
      _sampleRate = val;
      for (Synthesizer* s : _synthesizer) {
            s->init(_sampleRate);
            if (s->gui()) connect(s->gui(), SIGNAL(sfChanged()), SLOT(sfChanged()), Qt::UniqueConnection);
            }
      for (Effect* e : _effectList[0])
            e->init(_sampleRate);
      for (Effect* e : _effectList[1])
            e->init(_sampleRate);
      lock2 = false;
      lock1 = false;
      }

//---------------------------------------------------------
//   process
//---------------------------------------------------------

void MasterSynthesizer::process(unsigned n, float* p)
      {
      if (lock2)
            return;
      lock1 = true;
      if (lock2) {
            lock1 = false;
            return;
            }
      // avoid overflow
      if (n > MAX_BUFFERSIZE / 2) { lock1 = false; return; }
      for (Synthesizer* s : _synthesizer) {
            if (s->active())
                  s->process(n, p, effect1Buffer, effect2Buffer);
            }

      for (Synthesizer* s : _synthesizer) s->mixMaster(n, p);

      if (_effect[0] && _effect[1]) {
            memset(effect1Buffer, 0, n * sizeof(float) * 2);
            _effect[0]->process(n, p, effect1Buffer);
            _effect[1]->process(n, effect1Buffer, p);
            }
      else if (_effect[0] || _effect[1]) {
            memcpy(effect1Buffer, p, n * sizeof(float) * 2);
            if (_effect[0])
                  _effect[0]->process(n, effect1Buffer, p);
            else
                  _effect[1]->process(n, effect1Buffer, p);
            }
      for (Synthesizer* s : _synthesizer) s->processMaster(n, p);
      float g = _gain * _boost;
      for (unsigned i = 0; i < n * 2; ++i)
            *p++ *= g;
      lock1 = false;
      }

//---------------------------------------------------------
//   indexOfEffect
//---------------------------------------------------------

int MasterSynthesizer::indexOfEffect(int ab, const QString& name)
      {
      int idx = 0;
      for (Effect* e : _effectList[ab]) {
            if (e && e->name() == name)
                  return idx;
            ++idx;
            }
      qDebug("indexOfEffect %d %s not found", ab, qPrintable(name));
      return -1;
      }

int MasterSynthesizer::indexOfEffect(int ab)
      {
      if (!_effect[ab])
            return 0;
      return indexOfEffect(ab, _effect[ab]->name());
      }

//---------------------------------------------------------
//   setState
//---------------------------------------------------------

bool MasterSynthesizer::setState(const SynthesizerState& ss)
      {
      bool result = true;
      _unavailableState.clear();
      for (auto s : _synthesizer) {
            bool found=false; for (const auto& g : ss) if (g.name()==s->name()) {found=true;break;}
            if (!found) s->clearState();
            }
      for (const SynthesizerGroup& g : ss) {
            if (g.name() == "master") {
                  for (const IdValue& v : g) {
                        switch (v.id) {
                              case 0:
                                    setEffect(0, indexOfEffect(0, v.data));
                                    break;
                              case 1:
                                    setEffect(1, indexOfEffect(1, v.data));
                                    break;
                              case 2: {
                                    float f = preferences.getDouble(PREF_APP_PLAYBACK_DEFAULT_MASTER_VOLUME);
                                    setGain(f);
                                    }
                                    break;
                              case 3:
                                    setMasterTuning(v.data.toDouble());
                                    break;
                              case 4:
                                    setDynamicsMethod(v.data.toInt());
                                    break;
                              case 5:
                                    setCcToUseIndex(v.data.toInt());
                                    break;
                              default:
                                    qDebug("MasterSynthesizer::setState: unknown master id <%d>", v.id);
                              }
                        }
                  }
            else {
                  Synthesizer* s = synthesizer(g.name());
                  if (s) {
                        bool r = s->setState(g);
                        result = result && r;
                        }
                  else {
                        if (effect(0) && effect(0)->name() == g.name())
                              effect(0)->setState(g);
                        else if (effect(1) && effect(1)->name() == g.name())
                              effect(1)->setState(g);
                        else
                              _unavailableState.push_back(g);
                        }
                  }
            }
      return result;
      }

//---------------------------------------------------------
//   state
//---------------------------------------------------------

SynthesizerState MasterSynthesizer::state() const
      {
      SynthesizerState ss;
      SynthesizerGroup g;
      g.setName("master");
      g.push_back(IdValue(0, QString("%1").arg(_effect[0] ? _effect[0]->name() : "NoEffect")));
      g.push_back(IdValue(1, QString("%1").arg(_effect[1] ? _effect[1]->name() : "NoEffect")));
      g.push_back(IdValue(2, QString("%1").arg(gain())));
      g.push_back(IdValue(3, QString("%1").arg(masterTuning())));
      g.push_back(IdValue(4, QString("%1").arg(dynamicsMethod())));
      g.push_back(IdValue(5, QString("%1").arg(ccToUseIndex())));
      ss.push_back(g);
      for (Synthesizer* s : _synthesizer)
            if (s->hasStateToStore()) ss.push_back(s->state());
      ss.insert(ss.end(), _unavailableState.begin(), _unavailableState.end());
      if (_effect[0])
            ss.push_back(_effect[0]->state());
      if (_effect[1])
            ss.push_back(_effect[1]->state());
      return ss;
      }

//---------------------------------------------------------
//   storeState
//---------------------------------------------------------

bool MasterSynthesizer::storeState()
      {
      QString s(dataPath + "/synthesizer.xml");
      QFile f(s);
      if (!f.open(QIODevice::WriteOnly)) {
            qDebug("cannot write synthesizer settings <%s>", qPrintable(s));
            return false;
            }
      XmlWriter xml(0, &f);
      xml.header();
      // force the write, since the msynth state is created when state() is called and so will
      // automatically have _isDefault = true, when in fact we need to write the state here, default or not
      state().write(xml, true);
      return true;
      }

void MasterSynthesizer::setPlaybackContext(qint64 sample,double quarter,double tempo,bool playing,int numerator,int denominator)
      { for (auto s : _synthesizer) s->setPlaybackContext(sample,quarter,tempo,playing,numerator,denominator); }
int MasterSynthesizer::processingLatency() const
      { int result=0;for (auto s : _synthesizer) result=std::max(result,s->processingLatency());return result; }
qint64 MasterSynthesizer::processingTail() const
      { qint64 result=0;for (auto s : _synthesizer) result=std::max(result,s->processingTail());return result; }

//---------------------------------------------------------
//   setGain
//---------------------------------------------------------

void MasterSynthesizer::setGain(float f)
      {
      if (_gain != f) {
            _gain = f;
            emit gainChanged(_gain);
            }
      preferences.setPreference(PREF_APP_PLAYBACK_DEFAULT_MASTER_VOLUME, f);
      }


//---------------------------------------------------------
//   setGainAsDecibels
//---------------------------------------------------------

void MasterSynthesizer::setGainAsDecibels(float decibelValue)
      {
      if (decibelValue == minGainAsDecibels)
            setGain(MUTE);
      else
            setGain(pow(10, ((decibelValue + N) / N )));
      }

//---------------------------------------------------------
//   convertGainToDecibels
//---------------------------------------------------------

float MasterSynthesizer::convertGainToDecibels(float gain) const
      {
      if (gain == MUTE)
            return minGainAsDecibels; // return a usable value instead of -∞
      return ((N * std::log10(gain)) - N);
      }

//---------------------------------------------------------
//   gainAsDecibels
//---------------------------------------------------------

float MasterSynthesizer::gainAsDecibels() const
      {
      return convertGainToDecibels(_gain);
      }

//---------------------------------------------------------
//   setMasterTuning
//---------------------------------------------------------

void MasterSynthesizer::setMasterTuning(double val)
      {
      _masterTuning = val;
      for (Synthesizer* s : _synthesizer)
            s->setMasterTuning(_masterTuning);
      }
} // namespace Ms
