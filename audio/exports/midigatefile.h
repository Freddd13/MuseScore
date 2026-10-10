// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "midigate.h"
#include "audio/midi/midifile.h"

namespace Ms {
// Tags captured during normal render conversion, before source pointers are discarded.
struct MidiGateSource {
      int track=0,on=0,pitch=0,channel=0,staff=-1,voice=-1;
      QString partName;
      bool piano=false,manual=false;
      };
MidiGateData midiGateSnapshot(const MidiFile&,const QVector<MidiGateSource>&);
void applyMidiGate(MidiFile&,const QVector<MidiGateNote>&);
}
