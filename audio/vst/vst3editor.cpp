// SPDX-License-Identifier: GPL-2.0-or-later
#include "vst3editor.h"
#include "plugin.h"
#include <QResizeEvent>
#include <QThread>
#include <QTimer>
namespace Ms {
namespace VstHost {
using namespace Steinberg;
Vst3Editor::Vst3Editor(IPlugView *plugin, QWidget *parent)
    : QWidget(parent, Qt::Tool), view(plugin, false) {
  setObjectName("kumoVstPluginEditor");
  setAttribute(Qt::WA_DeleteOnClose);
  setAttribute(Qt::WA_NativeWindow);
}
Vst3Editor::~Vst3Editor() {
  if (view) {
    if (attached)
      view->removed();
    view->setFrame(nullptr);
  }
}
void Vst3Editor::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  QTimer::singleShot(0, this, [this] { placeEditorOnScreen(this); });
}
bool Vst3Editor::attach(QString *error) {
  if (view->isPlatformTypeSupported(kPlatformTypeHWND) != kResultTrue) {
    if (error)
      *error = tr("这个插件未提供 Windows 界面。");
    return false;
  }
  view->setFrame(this);
  FUnknownPtr<IPlugViewContentScaleSupport> scaling(view);
  if (scaling)
    scaling->setContentScaleFactor(float(devicePixelRatioF()));
  ViewRect rect;
  if (view->getSize(&rect) != kResultOk || rect.getWidth() <= 0 ||
      rect.getHeight() <= 0) {
    if (error)
      *error = tr("插件界面尚未提供尺寸，请重新打开。");
    return false;
  }
  resize(qRound(rect.getWidth() / devicePixelRatioF()),
         qRound(rect.getHeight() / devicePixelRatioF()));
  if (view->canResize() != kResultTrue)
    setFixedSize(size());
  if (view->attached(reinterpret_cast<void *>(winId()), kPlatformTypeHWND) !=
      kResultOk) {
    if (error)
      *error = tr("插件界面打开失败。");
    return false;
  }
  attached = true;
  show();
  return true;
}
void Vst3Editor::resizeEvent(QResizeEvent *event) {
  QWidget::resizeEvent(event);
  if (!view || !attached)
    return;
  ViewRect rect(0, 0, qRound(width() * devicePixelRatioF()),
                qRound(height() * devicePixelRatioF()));
  view->checkSizeConstraint(&rect);
  const QSize constrained(qRound(rect.getWidth() / devicePixelRatioF()),
                          qRound(rect.getHeight() / devicePixelRatioF()));
  if (constrained != size()) {
    resize(constrained);
    return;
  }
  view->onSize(&rect);
}
tresult PLUGIN_API Vst3Editor::resizeView(IPlugView *source, ViewRect *rect) {
  if (source != view || !rect || rect->getWidth() < 1 ||
      rect->getHeight() < 1 || rect->getWidth() > 16384 ||
      rect->getHeight() > 16384)
    return kInvalidArgument;
  const QSize next(qRound(rect->getWidth() / devicePixelRatioF()),
                   qRound(rect->getHeight() / devicePixelRatioF()));
  if (QThread::currentThread() == thread())
    resize(next);
  else
    QMetaObject::invokeMethod(
        this, [this, next] { resize(next); }, Qt::QueuedConnection);
  return kResultOk;
}
} // namespace VstHost
} // namespace Ms
