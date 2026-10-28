// SPDX-License-Identifier: GPL-2.0-or-later
#include "midicroppanel.h"
#include <QCheckBox>
#include <QPointer>
#include <QUndoCommand>
#include <QUndoStack>

namespace Ms {
namespace {
class GateEditCommand final : public QUndoCommand {
      std::function<void(bool)> _apply;
public:
      GateEditCommand(const QString& title,std::function<void(bool)> apply)
         : QUndoCommand(title),_apply(std::move(apply)) {}
      void undo() override { _apply(false); }
      void redo() override { _apply(true); }
      };
}

QUndoStack* MidiCropPanel::editHistory(Score* score)
      {
      auto found=_histories.constFind(score);
      if(found!=_histories.cend())return found.value();
      auto stack=new QUndoStack(this);stack->setObjectName("midiCropEditHistory");
      _histories.insert(score,stack);
      connect(score,&QObject::destroyed,stack,[this,score,stack] {
            _histories.remove(score);_releaseOverrides.remove(score);_releaseStates.remove(score);
            _exclusions.remove(score);_staves.remove(score);_tracks.remove(score);_protectedTracks.remove(score);
            stack->deleteLater();
            });
      return stack;
      }
void MidiCropPanel::clearSessions()
      {
      qDeleteAll(_handHistories);_handHistories.clear();_handOverrides.clear();_handStates.clear();
      qDeleteAll(_histories);_histories.clear();
      _exclusions.clear();_releaseOverrides.clear();_releaseStates.clear();
      _staves.clear();_tracks.clear();_protectedTracks.clear();
      }
void MidiCropPanel::changeRelease(Score* score,const QString& id,int release)
      {
      const auto previous=_releaseOverrides.value(score);
      const bool existed=previous.contains(id);const int old=previous.value(id);
      const bool excluded=_exclusions.value(score).contains(id);
      if(!excluded&&((release<0&&!existed)||(existed&&release==old)))return;
      const bool wasEnabled=_enabled->isChecked();QPointer<Score> target(score);
      auto apply=[this,target,id,release,old,existed,excluded,wasEnabled](bool redo) {
            if(!target)return;
            auto& values=_releaseOverrides[target];
            const bool keep=redo?release>=0:existed;const int value=redo?release:old;
            if(keep)values[id]=value;else values.remove(id);
            if(!redo&&excluded)_exclusions[target].insert(id);else _exclusions[target].remove(id);
            _enabled->setChecked(redo?true:wasEnabled);
            _releaseStates[target]=target->masterScore()->state();
            emit exportEditsChanged(target);
            };
      editHistory(score)->push(new GateEditCommand(release<0?tr("恢复自动松手"):tr("调整松手位置"),apply));
      }
void MidiCropPanel::toggleExclusion(Score* score,const QString& id)
      {
      const bool excluded=_exclusions.value(score).contains(id);QPointer<Score> target(score);
      auto apply=[this,target,id,excluded](bool redo) {
            if(!target)return;
            auto& values=_exclusions[target];
            if(redo?!excluded:excluded)values.insert(id);else values.remove(id);
            _releaseStates[target]=target->masterScore()->state();
            emit exportEditsChanged(target);
            };
      editHistory(score)->push(new GateEditCommand(excluded?tr("恢复这个音的处理"):tr("排除这个音"),apply));
      }
}
