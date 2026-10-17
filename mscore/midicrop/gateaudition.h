// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#pragma once
#include "audio/exports/midigate.h"
#include "mscore/workflow/playbackpreview.h"
namespace Ms {
struct GateAudition {PlaybackPreviewMaterial original,proposed;int changed=0,unmatched=0;};
GateAudition prepareGateAudition(Score*,const MidiGateData&,const QVector<MidiGateNote>&);
}
