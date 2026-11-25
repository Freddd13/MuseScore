// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include "libmscore/synthesizerstate.h"
namespace Ms {
class Score;class MasterSynthesizer;
void captureScoreMix(Score*);
void restoreScoreMix(MasterSynthesizer*,Score*);
void applyScoreMasterGain(Score*,float);
SynthesizerState replaceScoreMix(SynthesizerState,const SynthesizerState&);
SynthesizerState withMasterGain(SynthesizerState,float);
}
