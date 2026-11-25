// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "scoremixstate.h"
#include "featuregate.h"
#include "audio/midi/msynthesizer.h"
#include "libmscore/score.h"
#include "libmscore/undo.h"
#include "mscore/musescore.h"
#include "mscore/shortcut.h"
#include <QAction>
namespace Ms {
SynthesizerState replaceScoreMix(SynthesizerState saved,const SynthesizerState& mix)
{
    for(const auto& group:mix) {
        for(auto it=saved.begin();it!=saved.end();)
            if(it->name()==group.name())it=saved.erase(it);else ++it;
        saved.push_back(group);
    }
    saved.setIsDefault(false);return saved;
}
void captureScoreMix(Score* score)
{
    if(!personalFeaturesEnabled()||!score||!synti||!mscore||!mscore->currentScore()
       ||mscore->currentScore()->masterScore()!=score->masterScore())return;
    auto master=score->masterScore();auto saved=master->synthesizerState();
    if(saved.isDefault())saved=synti->state();
    master->setSynthesizerState(replaceScoreMix(saved,synti->mixState()));
}
void restoreScoreMix(MasterSynthesizer* synth,Score* score)
{
    if(!personalFeaturesEnabled()||!synth)return;
    const auto saved=score?score->masterScore()->synthesizerState():SynthesizerState();
    synth->setMixState(saved.isDefault()?synth->defaultMixState():saved);
}
SynthesizerState withMasterGain(SynthesizerState state,float gain)
{
    for(auto& group:state)if(group.name()=="master")for(auto& value:group)if(value.id==2)
        value.data=QString::number(gain,'g',9);
    state.setIsDefault(false);return state;
}
namespace {
class ChangeMasterGain final : public UndoCommand {
    MasterScore* score;float gain;
    void flip(EditData*)override {
        auto state=score->synthesizerState();float old=gain;
        for(const auto& value:state.group("master"))if(value.id==2)old=value.data.toFloat();
        score->setSynthesizerState(withMasterGain(state,gain));
        if(synti&&mscore&&mscore->currentScore()&&mscore->currentScore()->masterScore()==score)
            synti->setGain(gain);
        gain=old;
    }
  public:
    ChangeMasterGain(MasterScore* owner,float value):score(owner),gain(value){}
    UNDO_NAME("ChangeMasterGain")
};
}
void applyScoreMasterGain(Score* score,float gain)
{
    if(!score||!synti)return;
    captureScoreMix(score);
    score->startCmd();score->undo(new ChangeMasterGain(score->masterScore(),gain));score->endCmd();
    getAction("undo")->setEnabled(score->undoStack()->canUndo());
    getAction("redo")->setEnabled(score->undoStack()->canRedo());
}
}
