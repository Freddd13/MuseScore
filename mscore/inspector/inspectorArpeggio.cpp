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

#include "inspectorArpeggio.h"
#include "libmscore/arpeggio.h"
#include "libmscore/playbacktiming.h"
#include "scrubproperty.h"
#include "../musescore.h"
#include "../performanceeditor/performanceeditor.h"
#include <QPushButton>

namespace Ms {

//---------------------------------------------------------
//   InspectorArpeggio
//---------------------------------------------------------

InspectorArpeggio::InspectorArpeggio(QWidget* parent)
   : InspectorElementBase(parent)
      {
      g.setupUi(addWidget());

      auto mode = new QComboBox(g.panel);
      mode->setObjectName("arpeggioTimingMode");
      mode->addItems({tr("原伸缩比"), tr("首音落正拍"), tr("末音落正拍")});
      auto grid = qobject_cast<QGridLayout*>(g.panel->layout());
      int row = grid->rowCount();
      grid->addWidget(new QLabel(tr("对拍方式"), g.panel), row, 0); grid->addWidget(mode, row, 1, 1, 2);
      std::vector<InspectorItem> iiList = {
            { Pid::ARP_TIMING_MODE, 0, mode, nullptr },
            { Pid::PLAY,            0,    g.playArpeggio, g.resetPlayArpeggio},
            { Pid::TIME_STRETCH,    0,    g.stretch,      g.resetStretch }
            };
      iiList.push_back(scrubProperty(g.panel, tr("每音间隔"), Pid::ARP_INTERVAL_MS, 1, 1000, 1, " ms"));
      iiList.push_back(scrubProperty(g.panel, tr("整体偏移"), Pid::ARP_OFFSET_MS, -1000, 1000, 1, " ms"));
      auto preset = new QPushButton(tr("应用末音对拍预设"), g.panel);
      preset->setObjectName("arpeggioApplyPreset");
      grid->addWidget(preset, grid->rowCount(), 0, 1, 3);
      connect(preset, &QPushButton::clicked, this, [this] {
            for (auto element : *inspector->el()) {
                  auto editor = mscore->performanceEditor();
                  editor->queueProperty(element, Pid::ARP_TIMING_MODE, 2);
                  editor->queueProperty(element, Pid::ARP_INTERVAL_MS, 65.0);
                  editor->queueProperty(element, Pid::ARP_OFFSET_MS, 0.0);
                  }
            });
      _actualTiming = new QLabel(g.panel);
      _actualTiming->setWordWrap(true);
      grid->addWidget(_actualTiming, grid->rowCount(), 0, 1, 3);
      const std::vector<InspectorPanel> ppList = {
            { g.title, g.panel }
            };

      mapSignals(iiList, ppList);
      }
void InspectorArpeggio::postInit()
      {
      auto a = toArpeggio(inspector->element());
      g.stretch->setEnabled(a->timingMode() == 0);
      auto interval = findChild<QDoubleSpinBox*>("arpIntervalMs");
      interval->setEnabled(a->timingMode() != 0);
      findChild<QDoubleSpinBox*>("arpOffsetMs")->setEnabled(a->timingMode() != 0);
      _actualTiming->setText(a->timingMode() ? tr("实际每音间隔：%1 ms（短和弦自动压缩）").arg(PlaybackTiming::intervalMs(a), 0, 'f', 2) : tr("旧谱使用原伸缩比播放。"));
      }
}
