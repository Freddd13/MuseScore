// Only new personal inspector fields opt in to name scrubbing.
#ifndef MS_SCRUBPROPERTY_H
#define MS_SCRUBPROPERTY_H
#include <QLabel>
#include <QDoubleSpinBox>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QPointer>
#include <QGridLayout>
#include <QToolButton>
#include "inspectorBase.h"
namespace Ms {
class ScrubPropertyLabel : public QLabel {
      QPointer<QDoubleSpinBox> _spin;
      QPoint _origin;
      double _original = 0;
      bool _dragging = false, _blocked = false;
      void finish(bool cancel)
            {
            if (!_dragging) return;
            _dragging = false; setProperty("pianoParameterScrubbing", false);
            if (!_spin) return;
            double value = _spin->value();
            _spin->setValue(_original);
            _spin->blockSignals(_blocked);
            if (!cancel) _spin->setValue(value); // one native inspector transaction
            }
      bool event(QEvent* e) override
            {
            // Esc must cancel the scrub before the application's deselect shortcut.
            if (_dragging && e->type() == QEvent::ShortcutOverride && static_cast<QKeyEvent*>(e)->key() == Qt::Key_Escape) {
                  e->accept(); return true;
                  }
            return QLabel::event(e);
            }
      void mousePressEvent(QMouseEvent* e) override
            {
            if (e->button() != Qt::LeftButton || !_spin || !_spin->isEnabled()) { QLabel::mousePressEvent(e); return; }
            _origin = e->globalPos(); _original = _spin->value(); _dragging = true; setProperty("pianoParameterScrubbing", true);
            _blocked = _spin->blockSignals(true); setFocus(); e->accept();
            }
      void mouseMoveEvent(QMouseEvent* e) override
            {
            if (!_dragging || !_spin) { QLabel::mouseMoveEvent(e); return; }
            double step = _spin->singleStep() * (e->modifiers() & Qt::ShiftModifier ? 0.1 : 1.0);
            _spin->setValue(_original + (e->globalPos().x() - _origin.x()) * step); e->accept();
            }
      void mouseReleaseEvent(QMouseEvent* e) override { finish(false); e->accept(); }
      void keyPressEvent(QKeyEvent* e) override
            { if (e->key() == Qt::Key_Escape) { finish(true); e->accept(); } else QLabel::keyPressEvent(e); }
      void hideEvent(QHideEvent* e) override { finish(true); QLabel::hideEvent(e); }
   public:
      ScrubPropertyLabel(const QString& text, QDoubleSpinBox* spin, QWidget* parent)
            : QLabel(text, parent), _spin(spin)
            { setFocusPolicy(Qt::StrongFocus); setCursor(Qt::SizeHorCursor); setBuddy(spin); setToolTip(QObject::tr("Drag horizontally; Shift: fine adjustment; Esc: cancel")); }
      ~ScrubPropertyLabel() { finish(true); }
      };
inline InspectorItem scrubProperty(QWidget* host, const QString& name, Pid pid, double minimum, double maximum, double step, const QString& suffix = {}, int parent = 0)
      {
      auto layout = qobject_cast<QGridLayout*>(host->layout());
      if (!layout) layout = new QGridLayout(host);
      int row = layout->rowCount();
      auto spin = new QDoubleSpinBox(host);
      spin->setObjectName(QString::fromLatin1(propertyName(pid)));
      spin->setRange(minimum, maximum); spin->setSingleStep(step); spin->setDecimals(2); spin->setSuffix(suffix); spin->setKeyboardTracking(false);
      auto label = new ScrubPropertyLabel(name, spin, host);
      label->setObjectName(spin->objectName() + "Label");
      auto reset = new QToolButton(host); reset->setText(QString::fromUtf8("\xE2\x86\xBA")); reset->setToolTip(QObject::tr("Reset"));
      layout->addWidget(label, row, 0); layout->addWidget(spin, row, 1); layout->addWidget(reset, row, 2);
      return {pid, parent, spin, reset};
      }
}
#endif
