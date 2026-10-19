// SPDX-License-Identifier: GPL-2.0-or-later
#include "vst2editor.h"
#include "plugin.h"
#include <QThread>
namespace Ms {
namespace VstHost {
Vst2Editor::Vst2Editor(QWidget *parent, std::function<void()> onIdle,
                       std::function<void()> onClose)
    : QWidget(parent, Qt::Tool), detach(std::move(onClose)) {
  setObjectName("kumoVstPluginEditor");
  setAttribute(Qt::WA_DeleteOnClose);
  setAttribute(Qt::WA_NativeWindow);
  QObject::connect(&idle, &QTimer::timeout, this, std::move(onIdle));
  idle.start(40);
}
Vst2Editor::~Vst2Editor() {
  idle.stop();
  if (detach)
    detach();
}
void Vst2Editor::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  QTimer::singleShot(0, this, [this] { placeEditorOnScreen(this); });
}
void Vst2Editor::disableCallbacks() {
  idle.stop();
  detach = {};
}
void Vst2Editor::setPluginSize(const QSize &pixels) {
  if (pixels.width() < 1 || pixels.height() < 1 || pixels.width() > 16384 ||
      pixels.height() > 16384)
    return;
  const QSize logical(qRound(pixels.width() / devicePixelRatioF()),
                      qRound(pixels.height() / devicePixelRatioF()));
  if (QThread::currentThread() == thread())
    setFixedSize(logical);
  else
    QMetaObject::invokeMethod(
        this, [this, logical] { setFixedSize(logical); }, Qt::QueuedConnection);
}
} // namespace VstHost
} // namespace Ms
