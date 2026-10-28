// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "midigate.h"
#include "audio/midi/midifile.h"

namespace Ms {
// Tags captured during normal render conversion, before source pointers are discarded.
struct MidiGateSource {
      int track=0,on=0,pitch=0,channel=0,staff=-1,voice=-1;
      int sourceTick=-1,sourceMeasure=-1,sourceUtick=-1;
      int partId=-1,hand=-1,handHint=-1,finger=0;
      int displayStaff=-1;
      QString handSource;
      int fingerMask=0;
      QStringList fingerings;
      QVector<MidiGateInstruction> instructions;
      bool melody=false;
      QString partName;
      bool piano=false,manual=false;
      };
MidiGateData midiGateSnapshot(const MidiFile&,const QVector<MidiGateSource>&);
void applyMidiGate(MidiFile&,const QVector<MidiGateNote>&);
QVector<MidiGateNote> assignMidiHands(const MidiGateData&,const QVector<MidiGateNote>&,
      const MidiExportOptions&,const std::function<bool()>& canceled={});
void applyMidiHands(MidiFile&,const QVector<MidiGateNote>&);
QVector<MidiGateNote> prepareMidiPostprocess(const MidiGateData&,const MidiExportOptions&,const std::function<bool()>& canceled={});
}
