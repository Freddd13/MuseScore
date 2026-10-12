// SPDX-License-Identifier: GPL-2.0-or-later
#include "inputnotationmenu.h"
#include "inputrhythmpreference.h"
#include "musescore.h"
#include "preferences.h"
#include "seq.h"
#include "workspace.h"
#include "libmscore/inputnotation.h"
#include "libmscore/score.h"
#include <QMenu>
#include <QAction>
namespace Ms {
void addInputNotationMenu(QMenu* menu)
      {
      auto add = [&](const QString& label, const char* name, const char* preference, void(*setter)(bool)) {
            auto action = menu->addAction(label); action->setObjectName(name); action->setCheckable(true);
            action->setChecked(preferences.getBool(preference)); Workspace::addActionAndString(action,name);
            action->setToolTip(QObject::tr("改变已打开乐谱的自动排版；手动符干和符杠保持原设置。"));
            QObject::connect(action,&QAction::triggered,action,[=](bool enabled) {
                  if (seq && (seq->isPlaying() || !seq->backgroundRenderingIdle())) { action->setChecked(!enabled); mscore->showMessage(QObject::tr("请先停止播放并等待乐谱准备完成，再改变符杠或符干的自动排版。"),8000); return; }
                  preferences.setPreference(preference,enabled); setter(enabled);
                  for (auto score : mscore->scores()) { score->setLayoutAll(); score->update(); }
                  mscore->endCmd();
                  });
            if (seq) {
                  QObject::connect(seq,&Seq::started,action,[=]{action->setEnabled(false);});
                  QObject::connect(seq,&Seq::stopped,action,[=]{action->setEnabled(true);});
                  }
            };
      add(QObject::tr("允许延音音符跨拍连符杠"),"auto-tie-beams",PREF_SCORE_TIED_BEAMS,&InputNotation::setTiedBeams);
      add(QObject::tr("协调延音两端的自动符干"),"coherent-tie-stems",PREF_SCORE_COHERENT_TIE_STEMS,&InputNotation::setCoherentStems);
      }
}
