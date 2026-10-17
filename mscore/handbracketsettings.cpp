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
#include <QFontComboBox>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QSignalBlocker>
#include "libmscore/text.h"
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
            QSettings settings;
            for(auto id:{SymId::keyboardPlayWithLH,SymId::keyboardPlayWithLHEnd,SymId::keyboardPlayWithRH,SymId::keyboardPlayWithRHEnd}){
                  auto mirrored=style;mirrored.horizontal=(id==SymId::keyboardPlayWithLHEnd||id==SymId::keyboardPlayWithRHEnd?-1:1)*std::abs(style.horizontal);
                  if(!HandBracket::setDefaults(id,mirrored))return;const auto key=styleKey(id);settings.setValue(key+"horizontal",mirrored.horizontal);settings.setValue(key+"vertical",mirrored.vertical);settings.setValue(key+"width",mirrored.width);settings.setValue(key+"text",mirrored.text);refreshHandPaletteDefaults(id);
                  }settings.sync();
            message->setText(settings.status()==QSettings::NoError ? QObject::tr("已保存四种方向的默认长度，开始与结束互为镜像。") : QObject::tr("设置无法写入，请检查设置文件是否可写。"));
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
            bool found=false;
            for(int i=0;i<panel->ncells();) {
                  auto cell=panel->cell(i);auto e=cell->element.get();
                  if(cell->custom||!e||!e->isSymbol()||toSymbol(e)->sym()!=entry.first){++i;continue;}
                  if(found){panel->takeCells(i,1);continue;}found=true;
                  auto symbol=toSymbol(e);cell->mag=.55;cell->name=QString::fromUtf8(entry.second);
                  const bool oldFactory=std::abs(symbol->getProperty(Pid::HAND_BRACKET_H).toDouble()-(symbol->handEnd()?-2.:2.))<.0001
                        &&std::abs(symbol->getProperty(Pid::HAND_BRACKET_V).toDouble()+1.)<.0001
                        &&std::abs(symbol->getProperty(Pid::HAND_BRACKET_WIDTH).toDouble()-.12)<.0001
                        &&!symbol->getProperty(Pid::HAND_BRACKET_TEXT).toBool();
                  if(!symbol->handBracket()||oldFactory){symbol->applyHandPreset();symbol->layout();}++i;
                  }
            if(!found){auto symbol=new Symbol(gscore);symbol->setSym(entry.first);symbol->applyHandPreset();panel->append(symbol,QString::fromUtf8(entry.second),QString(),.55);}
            }
      }
void addHandTextControls(QWidget* host,QGridLayout* grid,Inspector* inspector){
 auto box=new QWidget(host);auto layout=new QGridLayout(box);layout->setContentsMargins(0,0,0,0);grid->addWidget(box,8,0,1,3);
 auto face=new QFontComboBox(box);face->setObjectName("handTextFont");layout->addWidget(new QLabel(QObject::tr("手部文字字体"),box),0,0);layout->addWidget(face,0,1);
 auto size=new QDoubleSpinBox(box);size->setObjectName("handTextSize");size->setRange(0,96);size->setSpecialValueText(QObject::tr("跟随表情文字"));size->setSuffix(" pt");size->setKeyboardTracking(false);layout->addWidget(new QLabel(QObject::tr("文字大小"),box),1,0);layout->addWidget(size,1,1);
 auto automatic=new QCheckBox(QObject::tr("手部文字自动放置"),box);automatic->setObjectName("handTextAuto");layout->addWidget(automatic,2,0,1,2);
 auto reset=new QPushButton(QObject::tr("跟随表情文字样式"),box);layout->addWidget(reset,3,0,1,2);
 auto change=[inspector](Pid id,QVariant value){for(auto e:*inspector->el())if(e->isSymbol()&&toSymbol(e)->handEligible())mscore->performanceEditor()->queueProperty(e,id,value);};
 QObject::connect(face,&QFontComboBox::currentFontChanged,box,[change](const QFont& f){change(Pid::HAND_TEXT_FONT,f.family());});
 QObject::connect(size,QOverload<double>::of(&QDoubleSpinBox::valueChanged),box,[change](double v){change(Pid::HAND_TEXT_SIZE,v);});
 QObject::connect(automatic,&QCheckBox::clicked,box,[change](bool v){change(Pid::HAND_TEXT_AUTO,v);});
 QObject::connect(reset,&QPushButton::clicked,box,[change]{change(Pid::HAND_TEXT_FONT,QString());change(Pid::HAND_TEXT_SIZE,0.);});
}
void syncHandTextControls(QWidget* host,Symbol* symbol){
 if(auto face=host->findChild<QFontComboBox*>("handTextFont")){QSignalBlocker block(face);QString family=symbol->getProperty(Pid::HAND_TEXT_FONT).toString();if(family.isEmpty()){Text expression(symbol->score(),Tid::EXPRESSION);family=expression.font().family();}face->setCurrentFont(QFont(family));}
 if(auto size=host->findChild<QDoubleSpinBox*>("handTextSize")){QSignalBlocker block(size);size->setValue(symbol->getProperty(Pid::HAND_TEXT_SIZE).toDouble());}
 if(auto automatic=host->findChild<QCheckBox*>("handTextAuto")){QSignalBlocker block(automatic);automatic->setChecked(symbol->getProperty(Pid::HAND_TEXT_AUTO).toBool());}
}

}
