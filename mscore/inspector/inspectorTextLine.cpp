//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Copyright (C) 2013 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENSE.GPL
//=============================================================================

#include "inspector.h"
#include "inspectorTextLine.h"
#include "scrubproperty.h"
#include "libmscore/textline.h"
#include "libmscore/tempoexpression.h"
#include "../performanceeditor/parameteredit.h"
#include "../musescore.h"
#include <QPushButton>

namespace Ms {

//---------------------------------------------------------
//   InspectorTextLine
//---------------------------------------------------------

InspectorTextLine::InspectorTextLine(QWidget* parent)
   : InspectorTextLineBase(parent)
      {
      ttl.setupUi(addWidget());

      std::vector<InspectorItem> il = {
            { Pid::PLACEMENT,   0, ttl.placement,      ttl.resetPlacement        },
            { Pid::SYSTEM_FLAG, 0, ttl.systemTextLine, 0                         },
            };
      const std::vector<InspectorPanel> ppList = {
            { ttl.title, ttl.panel },
            };

      populatePlacement(ttl.placement);
      auto grid = qobject_cast<QGridLayout*>(ttl.panel->layout());
      auto configured = new QCheckBox(tr("渐变速度线"), ttl.panel); configured->setObjectName("ritMode");
      grid->addWidget(configured, grid->rowCount(), 0, 1, 3); il.push_back({Pid::RIT_MODE, 0, configured, nullptr});
      auto play = new QCheckBox(tr("播放渐变速度（系统文字线）"), ttl.panel); play->setObjectName("ritPlay");
      grid->addWidget(play, grid->rowCount(), 0, 1, 3); il.push_back({Pid::RIT_PLAY, 0, play, nullptr});
      auto mode = new QComboBox(ttl.panel); mode->setObjectName("ritTargetMode"); mode->addItems({tr("起速百分比"), tr("目标 BPM")});
      int row = grid->rowCount(); grid->addWidget(new QLabel(tr("终速单位"), ttl.panel), row, 0); grid->addWidget(mode, row, 1, 1, 2);
      il.push_back({Pid::RIT_TARGET_MODE, 0, mode, nullptr});
      il.push_back(scrubProperty(ttl.panel, tr("终速"), Pid::RIT_TARGET, 1, 999, 1));
      il.push_back(scrubProperty(ttl.panel, tr("曲率"), Pid::RIT_CURVE, .1, 8, .05));
      il.push_back(scrubProperty(ttl.panel, tr("起速 BPM（0 随前）"), Pid::RIT_START_BPM, 0, 999, 1));
      _ritStatus = new QLabel(ttl.panel); _ritStatus->setWordWrap(true); _ritStatus->setObjectName("ritStatus"); grid->addWidget(_ritStatus, grid->rowCount(), 0, 1, 3);
      auto replace = new QPushButton(tr("替换该范围速度"), ttl.panel); replace->setObjectName("ritReplaceRange");
      replace->setToolTip(tr("移除范围内部速度标记及整条重叠渐变，保留范围起点和终点标记。一次撤销。"));
      grid->addWidget(replace, grid->rowCount(), 0, 1, 3);
      connect(replace, &QPushButton::clicked, this, [this] {
            auto element = inspector->element(); auto line = element->isTextLine() ? toTextLine(element) : static_cast<TextLine*>(element->propertyDelegate(Pid::RIT_PLAY));
            if (!line || !line->systemFlag()) return;
            QString error; ParameterEdit::tempoCurve(line->score(), line->tick().ticks(), line->tick2().ticks(), line, true, &error);
            mscore->showMessage(error, 8000);
            });
      mapSignals(il, ppList);
      }
void InspectorTextLine::postInit()
      {
      auto element = inspector->element(); auto line = element->isTextLine() ? toTextLine(element) : static_cast<TextLine*>(element->propertyDelegate(Pid::RIT_PLAY));
      if (!line) return;
      findChild<QCheckBox*>("ritPlay")->setEnabled(line->systemFlag() && line->ritEnabled());
      for (const auto& name : {"ritTargetMode", "ritTarget", "ritCurve", "ritStartBpm"}) findChild<QWidget*>(name)->setEnabled(line->ritEnabled());
      findChild<QPushButton*>("ritReplaceRange")->setEnabled(line->systemFlag());
      QString issue = TempoExpression::conflict(line->score(), line->tick().ticks(), line->tick2().ticks(), line);
      _ritStatus->setText(!line->systemFlag() ? tr("先勾选系统文字线；普通文字线不改变播放。") : !issue.isEmpty() ? issue : line->ritPlay()
            ? tr("%1 → %2 BPM；结束后保持终速。范围端点可在谱面或演奏编辑器拖动。")
                  .arg(line->ritDerivedStart() * 60, 0, 'f', 2).arg(TempoExpression::target(line, line->ritDerivedStart()) * 60, 0, 'f', 2)
            : tr("播放关闭；渐变速度线仍保留精确范围，普通文字线保留原解释。"));
      }
}
