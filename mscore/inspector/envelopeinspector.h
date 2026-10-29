// Shared controls for the new generated-event parameters; GPL version 2.
#ifndef MS_ENVELOPEINSPECTOR_H
#define MS_ENVELOPEINSPECTOR_H
#include "inspector.h"
#include "scrubproperty.h"
#include "libmscore/playbackenvelope.h"
#include "libmscore/trill.h"
#include "libmscore/tremolo.h"
#include "libmscore/articulation.h"
#include "../musescore.h"
#include "../performanceeditor/performanceeditor.h"
#include <QComboBox>
#include <QSignalBlocker>
namespace Ms {
inline Element* envelopeInspectorOwner(Element* element)
      { return element->isTrillSegment() ? toTrillSegment(element)->spanner() : element; }
inline std::vector<InspectorItem> envelopeInspectorControls(QWidget* host, Inspector* inspector)
      {
      auto panel=new QWidget(host); panel->setObjectName("playbackEnvelopePanel"); host->layout()->addWidget(panel);
      auto layout=new QGridLayout(panel);
      auto mode=new QComboBox(panel); mode->setObjectName("envelopeMode");
      mode->addItems({QObject::tr("原样"),QObject::tr("柔和"),QObject::tr("渐强"),QObject::tr("渐弱"),QObject::tr("自定义")});
      layout->addWidget(new QLabel(QObject::tr("逐次力度"),panel),0,0); layout->addWidget(mode,0,1);
      auto reset=new QToolButton(panel); reset->setObjectName("envelopePresetReset"); reset->setText(QString::fromUtf8("↺"));
      reset->setToolTip(QObject::tr("重新应用当前力度预设；Ctrl+Z可撤销。")); layout->addWidget(reset,0,2);
      auto apply=[inspector,mode] {
            auto editor=mscore->performanceEditor(); QSet<Element*> owners;
            for(auto element:*inspector->el()) owners.insert(envelopeInspectorOwner(element));
            for(auto owner:owners) {
                  if (!(owner->isTremolo() || owner->isTrill() || (owner->isArticulation() && toArticulation(owner)->hasTrillEnvelope()))) continue;
                  const auto values=PlaybackEnvelope::preset(mode->currentIndex(),!owner->isTremolo());
                  for(auto i=values.cbegin();i!=values.cend();++i) editor->queueProperty(owner,i.key(),i.value());
                  }
            };
      QObject::connect(mode,QOverload<int>::of(&QComboBox::currentIndexChanged),panel,[apply](int){apply();});
      QObject::connect(reset,&QToolButton::clicked,panel,apply);
      auto hint=new QLabel(QObject::tr("100%采用这个音的当前力度，80%会轻一些。“柔和”调整第一下和后续音；“渐强／渐弱”调整开始和结束。交替音减弱10表示每隔一下再减10个百分点。"),panel);
      hint->setWordWrap(true); layout->addWidget(hint,1,0,1,3);
      return {
            scrubProperty(panel,QObject::tr("起始倍率"),Pid::ENVELOPE_START,1,400,1," %"),
            scrubProperty(panel,QObject::tr("后续／结束倍率"),Pid::ENVELOPE_END,1,400,1," %"),
            scrubProperty(panel,QObject::tr("曲率"),Pid::ENVELOPE_CURVE,.1,8,.05),
            scrubProperty(panel,QObject::tr("交替音减弱"),Pid::ENVELOPE_ALTERNATE,-100,100,1)
            };
      }
inline void syncEnvelopeInspector(QWidget* host, Inspector* inspector)
      {
      auto panel=host->findChild<QWidget*>("playbackEnvelopePanel"); if (!panel) return;
      auto owner=envelopeInspectorOwner(inspector->element());
      const bool eligible=owner->isTremolo() || owner->isTrill() || (owner->isArticulation() && toArticulation(owner)->hasTrillEnvelope());
      panel->setVisible(personalFeaturesEnabled() && eligible); if (!personalFeaturesEnabled() || !eligible) return;
      const int value=owner->getProperty(Pid::ENVELOPE_MODE).toInt();
      auto mode=panel->findChild<QComboBox*>("envelopeMode"); QSignalBlocker blocker(mode); mode->setCurrentIndex(value);
      const bool playable=owner->isTremolo() ? !toTremolo(owner)->isBuzzRoll() : owner->getProperty(Pid::PLAY).toBool();
      mode->setEnabled(playable); panel->findChild<QToolButton*>("envelopePresetReset")->setEnabled(playable);
      for(auto id:PlaybackEnvelope::properties()) if(id!=Pid::ENVELOPE_MODE)
            panel->findChild<QDoubleSpinBox*>(QString::fromLatin1(propertyName(id)))->setEnabled(playable && value>0 && (id!=Pid::ENVELOPE_CURVE || value>1));
      }
}
#endif
