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
static QString lineFontFace(Score* score) { TempoText text(score); return text.getProperty(Pid::FONT_FACE).toString(); }
void appendPlayableTempoSymbols(PalettePanel* panel)
      {
      if(!panel||panel->type()!=PalettePanel::Type::Tempo||!gscore)return;
      auto makeRit=[](){
            auto line=new TextLine(gscore,true);line->setLen(4*gscore->spatium());TempoText prototype(gscore);
            line->setBeginText("rit.");line->setContinueText("(rit.)");
            line->setProperty(Pid::BEGIN_FONT_FACE,prototype.getProperty(Pid::FONT_FACE));line->setProperty(Pid::BEGIN_FONT_SIZE,prototype.getProperty(Pid::FONT_SIZE));line->setProperty(Pid::BEGIN_FONT_STYLE,prototype.getProperty(Pid::FONT_STYLE));
            line->setLineStyle(Qt::DashLine);line->setProperty(Pid::RIT_MODE,true);line->setProperty(Pid::RIT_PLAY,true);return line;
            };
      auto makeRestore=[](){auto text=new TempoText(gscore);text->setFollowText(false);text->setXmlText("a tempo");text->setProperty(Pid::TEMPO_RESTORE_MODE,1);text->setProperty(Pid::FONT_FACE,lineFontFace(gscore));return text;};
      bool found[2]={false,false};
      for(int i=0;i<panel->ncells();) {
            auto cell=panel->cell(i);auto e=cell->element.get();if(!e){++i;continue;}
            const QString plain=e->isTextBase()?toTextBase(e)->plainText().simplified().toLower():e->isTextLine()?toTextLine(e)->beginText().simplified().toLower():QString();
            const bool factory=!cell->custom||cell->name==QObject::tr("渐慢 rit.（可播放）")||cell->name==QObject::tr("a tempo（恢复渐变前的速度）")||cell->name.startsWith("Kumo rit");
            const bool oldRit=cell->tag=="kumo-rit"||(factory&&(plain=="rit."||plain=="ritardando")),oldRestore=cell->tag=="kumo-a-tempo"||(factory&&plain=="a tempo");
            int role=oldRit||(e->isTextLine()&&toTextLine(e)->ritPlay())?0:oldRestore||(e->isTempoText()&&toTempoText(e)->restoreMode()==1)?1:-1;
            if(role<0||(cell->custom&&!oldRit&&!oldRestore)){++i;continue;}
            if(found[role]){panel->takeCells(i,1);continue;}found[role]=true;
            if(oldRit||oldRestore){cell->element.reset(role==0?static_cast<Element*>(makeRit()):static_cast<Element*>(makeRestore()));cell->custom=false;e=cell->element.get();}
            cell->tag.clear();cell->name=role==0?QObject::tr("渐慢 rit.（可播放）"):QObject::tr("a tempo（恢复渐变前的速度）");
            cell->mag=role==0?1.:1.1;if(role==0)toTextLine(e)->setLen(4*gscore->spatium());++i;
            }
      if(!found[0])panel->append(makeRit(),QObject::tr("渐慢 rit.（可播放）"),QString(),1.);
      if(!found[1])panel->append(makeRestore(),QObject::tr("a tempo（恢复渐变前的速度）"),QString(),1.1);
      }

void deduplicatePlayableTempoSymbols(PaletteTree* tree){
 if(!tree)return;PalettePanel* preferred=nullptr;for(const auto& p:tree->palettes)if(p->type()==PalettePanel::Type::Tempo){preferred=p.get();break;}
 if(!preferred)return;bool kept[2]={false,false};
 auto clean=[&](PalettePanel* panel){for(int i=0;i<panel->ncells();){auto cell=panel->cell(i);auto e=cell->element.get();
  const bool generated=cell->tag=="kumo-rit"||cell->tag=="kumo-a-tempo"||cell->name==QObject::tr("渐慢 rit.（可播放）")||cell->name==QObject::tr("a tempo（恢复渐变前的速度）");
  if(!e||!isPlayableTempoSymbol(e)||(!generated&&cell->custom)){++i;continue;}
  const int role=e->isTextLine()?0:1;if(kept[role])panel->takeCells(i,1);else{kept[role]=true;++i;}
 }};clean(preferred);for(const auto& p:tree->palettes)if(p.get()!=preferred)clean(p.get());
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
