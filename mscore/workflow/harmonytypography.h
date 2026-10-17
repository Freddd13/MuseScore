// GPL-2.0-or-later. Temporary annotations share a horizontal root/slash/bass layout.
#pragma once
#include "libmscore/score.h"
#include "libmscore/chordlist.h"
namespace Ms {
inline void alignTemporaryHarmonyBass(MasterScore& scratch){
 for(auto& action:scratch.style().chordList()->renderListBase)
  if(action.type==RenderAction::RenderActionType::MOVE)action.movey=0;
}
}
