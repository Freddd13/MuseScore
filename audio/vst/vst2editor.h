// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QTimer>
#include <QWidget>
#include <functional>
namespace Ms {
namespace VstHost {
class Vst2Editor final : public QWidget {
  QTimer idle;
  std::function<void()> detach;
  void showEvent(QShowEvent *) override;

public:
  Vst2Editor(QWidget *, std::function<void()> onIdle,
             std::function<void()> onClose);
  ~Vst2Editor() override;
  void disableCallbacks();
  void setPluginSize(const QSize &);
};
} // namespace VstHost
} // namespace Ms
