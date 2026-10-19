// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "fixedchanges.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/gui/iplugviewcontentscalesupport.h"
#include <QWidget>
namespace Ms {
namespace VstHost {
class Vst3Editor final : public QWidget,
                         public BorrowedInterface<Steinberg::IPlugFrame> {
  Steinberg::IPtr<Steinberg::IPlugView> view;
  bool attached = false;
  void resizeEvent(QResizeEvent *) override;
  void showEvent(QShowEvent *) override;

public:
  Vst3Editor(Steinberg::IPlugView *, QWidget *);
  ~Vst3Editor() override;
  bool attach(QString *);
  Steinberg::tresult PLUGIN_API resizeView(Steinberg::IPlugView *,
                                           Steinberg::ViewRect *) override;
};
} // namespace VstHost
} // namespace Ms
