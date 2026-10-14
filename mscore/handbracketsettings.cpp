// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "handbracketsettings.h"
#include "libmscore/handbracket.h"
#include "libmscore/symbol.h"
#include "palette/palettetree.h"
#include "palette/paletteworkspace.h"
#include <cmath>
#include "inspector/inspector.h"
#include "musescore.h"
#include "performanceeditor/performanceeditor.h"
#include <QSettings>
#include <QLabel>
#include <QPushButton>
#include <QGridLayout>
namespace Ms {
static QString styleKey(SymId id) {return QString("handBracket/%1/").arg(Sym::id2name(id));}
static void refreshHandPaletteDefaults(SymId id) {
      if(!mscore)return;
      for(auto model:mscore->getPaletteWorkspace()->paletteModels()) {
            if(!model)continue;
            for(int row=0;row<model->rowCount();++row) {
                  const auto panel=model->index(row,0);
                  for(int column=0;column<model->rowCount(panel);++column) {
                        const auto index=model->index(column,0,panel);auto cell=model->findCell(index);
                        if(!cell || cell->custom || !cell->element || !cell->element->isSymbol())continue;
                        auto symbol=toSymbol(cell->element.get());
                        if(symbol->sym()!=id || !symbol->handBracket())continue;
                        symbol->applyHandPreset();symbol->layout();model->itemDataChanged(index);
                        }
                  }
            }
      }
void loadHandBracketDefaults() {
      QSettings settings;
      for(auto id:{SymId::keyboardPlayWithLH,SymId::keyboardPlayWithLHEnd,SymId::keyboardPlayWithRH,SymId::keyboardPlayWithRHEnd}) {
            auto style=HandBracket::defaults(id);const auto key=styleKey(id);
            style.horizontal=settings.value(key+"horizontal",style.horizontal).toDouble();style.vertical=settings.value(key+"vertical",style.vertical).toDouble();
            style.width=settings.value(key+"width",style.width).toDouble();style.text=settings.value(key+"text",style.text).toBool();HandBracket::setDefaults(id,style);
            }
      }
void addHandBracketDefaultControls(QWidget* host,QGridLayout* grid,Inspector* inspector) {
      auto use=new QPushButton(QObject::tr("使用默认形状"),host),save=new QPushButton(QObject::tr("保存为默认形状"),host);
      use->setObjectName("handBracketUseDefault");save->setObjectName("handBracketSaveDefault");auto message=new QLabel(host);message->setWordWrap(true);message->setObjectName("handBracketDefaultStatus");
      grid->addWidget(use,5,0,1,3);grid->addWidget(save,6,0,1,3);grid->addWidget(message,7,0,1,3);
      QObject::connect(save,&QPushButton::clicked,host,[inspector,message] {
            if(inspector->el()->size()!=1 || !inspector->element()->isSymbol()){message->setText(QObject::tr("请只选中一个左右手标记。"));return;}
            if(mscore->performanceEditor()->hasPending()){message->setText(QObject::tr("请停播，等待当前修改保存后再保存默认形状。"));return;}
            auto symbol=toSymbol(inspector->element());HandBracket::Style style{symbol->getProperty(Pid::HAND_BRACKET_H).toDouble(),symbol->getProperty(Pid::HAND_BRACKET_V).toDouble(),symbol->getProperty(Pid::HAND_BRACKET_WIDTH).toDouble(),symbol->getProperty(Pid::HAND_BRACKET_TEXT).toBool()};
            if(!HandBracket::setDefaults(symbol->sym(),style))return;
            QSettings settings;const auto key=styleKey(symbol->sym());settings.setValue(key+"horizontal",style.horizontal);settings.setValue(key+"vertical",style.vertical);settings.setValue(key+"width",style.width);settings.setValue(key+"text",style.text);settings.sync();
            if(settings.status()==QSettings::NoError)refreshHandPaletteDefaults(symbol->sym());
            message->setText(settings.status()==QSettings::NoError ? QObject::tr("已保存。以后添加的同方向分手记号使用此形状。") : QObject::tr("设置无法写入，请检查设置文件是否可写。"));
            });
      QObject::connect(use,&QPushButton::clicked,host,[inspector] {
            loadHandBracketDefaults();for(auto element:*inspector->el())if(element->isSymbol() && toSymbol(element)->handEligible()) {
                  const auto style=HandBracket::defaults(toSymbol(element)->sym());auto editor=mscore->performanceEditor();
                  editor->queueProperty(element,Pid::HAND_BRACKET_H,style.horizontal);editor->queueProperty(element,Pid::HAND_BRACKET_V,style.vertical);editor->queueProperty(element,Pid::HAND_BRACKET_WIDTH,style.width);editor->queueProperty(element,Pid::HAND_BRACKET_TEXT,style.text);editor->queueProperty(element,Pid::HAND_BRACKET_MODE,true);
                  }
            });
      }
void completeHandBracketPalette(PalettePanel* panel) {
      if(!panel || panel->type()!=PalettePanel::Type::Fingering || !gscore)return;loadHandBracketDefaults();
      const std::pair<SymId,const char*> entries[]={{SymId::keyboardPlayWithLH,"左手弹（可伸缩）"},{SymId::keyboardPlayWithRH,"右手弹（可伸缩）"},{SymId::keyboardPlayWithLHEnd,"左手结束（可伸缩）"},{SymId::keyboardPlayWithRHEnd,"右手结束（可伸缩）"}};
      for(const auto& entry:entries) {
            bool found=false;for(int i=0;i<panel->ncells();++i) {auto cell=panel->cell(i);if(cell->element && cell->element->isSymbol() && toSymbol(cell->element.get())->sym()==entry.first && toSymbol(cell->element.get())->handBracket()) {
                  found=true;if(cell->mag==1)cell->mag=.65;
                  auto symbol=toSymbol(cell->element.get());
                  // Upgrade the old factory cells; custom palette shapes keep their values.
                  if(!cell->custom && std::abs(symbol->getProperty(Pid::HAND_BRACKET_H).toDouble()-(symbol->handEnd()?-2.:2.))<.0001
                        && std::abs(symbol->getProperty(Pid::HAND_BRACKET_V).toDouble()+1.)<.0001
                        && std::abs(symbol->getProperty(Pid::HAND_BRACKET_WIDTH).toDouble()-.12)<.0001
                        && !symbol->getProperty(Pid::HAND_BRACKET_TEXT).toBool()) {symbol->applyHandPreset();symbol->layout();}
                  break;
                  }}
            if(found)continue;auto symbol=new Symbol(gscore);symbol->setSym(entry.first);symbol->applyHandPreset();panel->append(symbol,QString::fromUtf8(entry.second),QString(),.65);
            }
      }
}
