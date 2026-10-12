// SPDX-License-Identifier: GPL-2.0-or-later
#include "temposymbols.h"
#include "palette/palettetree.h"
#include "musescore.h"
#include "performanceeditor/parameteredit.h"
#include "libmscore/score.h"
#include "libmscore/select.h"
#include "libmscore/measure.h"
#include "libmscore/segment.h"
#include "libmscore/chordrest.h"
#include "libmscore/textline.h"
#include "libmscore/tempotext.h"
namespace Ms {
void appendPlayableTempoSymbols(PalettePanel* panel)
      {
      if (!panel || panel->type() != PalettePanel::Type::Tempo || !gscore) return;
      bool hasRit=false, hasRestore=false;
      for (int i=0;i<panel->ncells();++i) {
            auto element=panel->cell(i)->element.get();if(!element)continue;
            hasRit |= element->isTextLine() && static_cast<TextLine*>(element)->ritPlay();
            hasRestore |= element->isTempoText() && toTempoText(element)->restoreMode()==1;
            }
      if (!hasRit) {
            auto line=new TextLine(gscore,true);line->setLen(7*gscore->spatium());
            line->setBeginText("rit.");line->setContinueText("(rit.)");line->setBeginFontStyle(FontStyle::Italic);
            line->setLineStyle(Qt::DashLine);line->setProperty(Pid::RIT_MODE,true);line->setProperty(Pid::RIT_PLAY,true);
            panel->append(line,QObject::tr("渐慢 rit.（可播放）"),"kumo-rit",.8);
            }
      if (!hasRestore) {
            auto text=new TempoText(gscore);text->setFollowText(false);text->setXmlText("a tempo");
            text->setProperty(Pid::TEMPO_RESTORE_MODE,1);
            panel->append(text,QObject::tr("a tempo（恢复渐变前的速度）"),"kumo-a-tempo",1.1);
            }
      }
bool isPlayableTempoSymbol(const Element* element)
      {
      return element && ((element->isTextLine() && static_cast<const TextLine*>(element)->ritPlay())
            || (element->isTempoText() && toTempoText(element)->restoreMode()==1));
      }
namespace {
bool applyAt(const Element* element, int from, int until, bool continuous)
      {
      auto score=mscore->currentScore();if(!score||!isPlayableTempoSymbol(element))return false;
      QString error;bool changed=false;
      if (element->isTextLine()) {
            if (!continuous || until<=from) error=QObject::tr("先在谱面选一段连续范围，再添加渐慢；拖动现有渐慢线可调整两端。");
            else changed=ParameterEdit::tempoCurve(score,from,until,nullptr,false,&error,static_cast<const TextLine*>(element));
            }
      else {
            if(from<0)error=QObject::tr("请先选中恢复速度位置的音符或休止符。");
            else changed=ParameterEdit::restoreTempo(score,from,&error,toTempoText(element));
            }
      if(changed){mscore->endCmd();mscore->showMessage(element->isTextLine()?QObject::tr("已添加渐慢；在检视器或演奏编辑器调整范围和速度。"):QObject::tr("已添加 a tempo，恢复到渐变前的速度。"),8000);}
      else mscore->showMessage(error.isEmpty()?QObject::tr("未添加；请停止播放、等待乐谱准备完成，并核对所选位置。"):error,8000);
      return changed;
      }
}
bool applyPlayableTempoSymbol(const Element* element)
      {
      auto score=mscore->currentScore();if(!score)return false;
      const auto& selection=score->selection();auto chord=selection.firstChordRest();
      bool range=selection.isRange()&&selection.startSegment();
      return applyAt(element,range?selection.tickStart().ticks():chord?chord->tick().ticks():-1,
            range?selection.tickEnd().ticks():-1,range);
      }
bool applyPlayableTempoDrop(const Element* element, const QPointF& pos)
      {
      auto score=mscore->currentScore();if(!score)return false;
      int staff=-1;Segment* segment=nullptr;auto measure=score->pos2measure(pos,&staff,nullptr,&segment,nullptr);
      if(!measure||!segment){mscore->showMessage(QObject::tr("请把速度记号拖到实际谱表的小节内。"),8000);return false;}
      return applyAt(element,segment->tick().ticks(),measure->endTick().ticks(),true);
      }
}
