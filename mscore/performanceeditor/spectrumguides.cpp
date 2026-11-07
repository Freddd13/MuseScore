// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "spectrumpane.h"
#include "performanceeditor.h"
#include "spectrumdisplay.h"
#include "miditrackpane.h"
#include "mscore/seq.h"
#include "libmscore/tempo.h"
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QSpinBox>
#include <QTabBar>
#include <QToolButton>
#include <cmath>
namespace Ms
{
QVariantMap SpectrumPane::viewInfo() const
{
    return {{"from", timeAtX(PerformanceViewport::gutter)},
            {"until", timeAtX(sourceWidth() - PerformanceViewport::rightMargin)},
            {"lowPitch", _low},
            {"highPitch", _high},
            {"hoverPitch", _hoverKey},
            {"overlay", _display->currentIndex() == 1},
            {"sourcePosition", sourceInPlayback() ? _lastAudioMs : _cursorMs}};
}
bool SpectrumPane::usesScoreTimeline() const
{
    return !_clip.isEmpty() || _display->currentIndex() == 1;
}
bool SpectrumPane::showMidiNotes() const
{
    return !isVisible() || _display->currentIndex() != 1 ||
           _displayControls->values().value("showMidi", true).toBool();
}
int SpectrumPane::timelineEndTick() const
{
    if (!isVisible() || _display->currentIndex() != 1 || !_clip.isEmpty() || !_editor->_score ||
        !_audioLoaded || !_audio)
        return 0;
    return _editor->_score->tempomap()->time2tick(_audio->property("duration").toDouble() / 1000.);
}
void SpectrumPane::hoverChanged(QPointF point, bool overlay)
{
    const int height = overlay ? _editor->_noteCanvas->height() : _canvas->height();
    int pitch = -1;
    if (point.y() >= 0 && point.y() < height) {
        if (point.x() < PerformanceViewport::gutter && !overlay)
            pitch =
                PerformanceKeyboard::pitchAt(point, _high - .5 + _transpose->value(),
                                             height / (_high - _low), PerformanceViewport::gutter);
        else
            pitch = qBound(0, qRound(pitchAtY(point.y(), overlay)) + _transpose->value(), 127);
    }
    if (pitch == _hoverKey && overlay == _hoverOnRoll)
        return;
    _hoverKey = pitch;
    _hoverOnRoll = overlay;
    _canvas->setProperty("hoverPitch", pitch);
    _canvas->update(QRect(0, 0, PerformanceViewport::gutter, height));
    _editor->_noteCanvas->update(
        QRect(0, 0, PerformanceViewport::gutter, _editor->_noteCanvas->height()));
}
void SpectrumPane::clearHover()
{
    if (_hoverKey < 0)
        return;
    _hoverKey = -1;
    _hoverOnRoll = false;
    _canvas->setProperty("hoverPitch", -1);
    _canvas->update();
    _editor->_noteCanvas->update();
}
void SpectrumPane::paintKeyboardHover(QPainter& p, QWidget* widget) const
{
    if (!isVisible() || _display->currentIndex() != 1 || !_hoverOnRoll || _hoverKey < 0 ||
        !_editor->_viewport.showsPitch(_hoverKey))
        return;
    p.save();
    p.setClipRect(QRect(0, 0, PerformanceViewport::gutter, widget->height()), Qt::IntersectClip);
    const auto& view = _editor->_viewport;
    const bool black = PerformanceKeyboard::isBlack(_hoverKey);
    auto fill = _editor->_appearance.colors[PerformanceAppearance::Selection];
    p.setBrush(fill);
    p.setPen(QPen(fill.darker(140), 1));
    QRectF bounds;
    if (_editor->_keyboardMode == 2 && !view.pitchRows.isEmpty()) {
        bounds = QRectF(0, view.pitchY(_hoverKey), PerformanceViewport::gutter * (black ? .58 : 1.),
                        view.rowHeight);
        p.drawRect(bounds);
    } else {
        const auto shape = PerformanceKeyboard::shape(_hoverKey, view.topPitch, view.rowHeight,
                                                      PerformanceViewport::gutter);
        bounds = shape.boundingRect();
        p.drawPolygon(shape);
    }
    if (_editor->_keyboardNames && (view.rowHeight >= 10 || _hoverKey % 12 == 0)) {
        static const char* names[] = {"C",  "C♯", "D",  "E♭", "E",  "F",
                                      "F♯", "G",  "A♭", "A",  "B♭", "B"};
        auto font = p.font();
        font.setPixelSize(qBound(7, int(view.rowHeight * .82), 12));
        p.setFont(font);
        p.setPen(fill.lightness() > 145 ? Qt::black : Qt::white);
        p.drawText(QRectF(black ? 2 : PerformanceViewport::gutter * .58 + 2,
                          bounds.center().y() - p.fontMetrics().height() / 2.,
                          PerformanceViewport::gutter * (black ? .58 : .42) - 4,
                          p.fontMetrics().height()),
                   Qt::AlignCenter,
                   QString::fromUtf8(names[_hoverKey % 12]) + QString::number(_hoverKey / 12 - 1));
    }
    p.restore();
}
void SpectrumPane::paintReferences(QPainter& p, const QRectF& bounds, bool overlay)
{
    if (!_harmonics->isChecked() || !_hasPick)
        return;
    const auto values = _displayControls->values();
    const int count = qBound(1, values.value("harmonicCount", 10).toInt(), 16);
    auto color = QColor(values.value("harmonicColor", "#ffdd88").toString());
    const double opacity = qBound(0., values.value("harmonicOpacity", .85).toDouble(), 1.);
    if (opacity <= 0)
        return;
    p.save();
    auto font = p.font();
    font.setPixelSize(qMax(9, p.fontMetrics().height() - 2));
    p.setFont(font);
    double labelTop[2] = {bounds.height() + 20, bounds.height() + 20};
    for (int harmonic = 1; harmonic <= count; ++harmonic) {
        const double y = yAtPitch(_cursorPitch + 12. * std::log2(harmonic), overlay);
        if (y < 0 || y > bounds.bottom())
            continue;
        const QString label = harmonic == 1 ? tr("1 · 基音") : tr("%1倍").arg(harmonic);
        const double width = p.fontMetrics().horizontalAdvance(label) + 8;
        color.setAlphaF(opacity * (harmonic == 1 ? 1. : .8));
        const auto style = harmonic == 1 ? Qt::SolidLine : Qt::DashLine;
        p.setPen(QPen(QColor(0, 0, 0, qRound(190 * opacity)), 3.5, style));
        p.drawLine(QPointF(PerformanceViewport::gutter, y), QPointF(bounds.right(), y));
        p.setPen(QPen(color, harmonic == 1 ? 1.7 : 1.2, style));
        p.drawLine(QPointF(PerformanceViewport::gutter, y), QPointF(bounds.right(), y));
        const double height = p.fontMetrics().height() + 2;
        int column = harmonic == 1 ? 0 : harmonic % 2;
        double top = qMin(y - height / 2, labelTop[column] - height - 2);
        if (top < 0) {
            column = 1 - column;
            top = qMin(y - height / 2, labelTop[column] - height - 2);
        }
        if (top < 0)
            continue;
        labelTop[column] = top;
        const QRectF badge(PerformanceViewport::gutter + 3 + column * 52, top, width, height);
        p.fillRect(badge, QColor(12, 16, 22, 220));
        p.setPen(color);
        p.drawText(badge, Qt::AlignCenter, label);
        if (qAbs(badge.center().y() - y) > height / 2)
            p.drawLine(QPointF(badge.right(), badge.center().y()), QPointF(badge.right() + 5, y));
    }
    p.restore();
}
void SpectrumPane::paintMidi(QPainter& p, const QRectF& bounds, bool overlay)
{
    // The overlaid roll already draws MIDI with its own selection and editing targets.
    if (overlay || !_editor->_midiTracks ||
        !_displayControls->values().value("showMidi", true).toBool())
        return;
    const auto notes = _editor->_midiTracks->spectrumNotes(
        _track.value("id").toString(), timeAtX(PerformanceViewport::gutter),
        timeAtX(bounds.right() - PerformanceViewport::rightMargin),
        qMax(0, int(std::floor(_low)) + _transpose->value()),
        qMin(127, int(std::ceil(_high)) + _transpose->value()));
    p.save();
    QSet<quint64> occupied;
    for (const auto& note : notes) {
        const double x = xAtTime(note.from), right = xAtTime(note.until),
                     y = yAtPitch(note.pitch - _transpose->value(), false);
        const double height = qMax(3., _canvas->height() / (_high - _low) * .8);
        const quint64 cell = (quint64(qMax(0, int(x) / 2)) << 8) | note.pitch;
        if (!note.selected && occupied.contains(cell))
            continue;
        occupied.insert(cell);
        auto color = palette().highlight().color();
        color.setAlpha(note.current ? (note.muted ? 120 : 175) : 65);
        p.setBrush(color);
        p.setPen(QPen(note.selected ? palette().highlightedText().color() : color.lighter(150),
                      note.selected ? 2 : 1));
        p.drawRect(QRectF(x, y - height / 2, qMax(2., right - x), height));
    }
    p.restore();
}
bool SpectrumPane::rollEvent(QEvent* event, bool ruler)
{
    if (!isVisible())
        return false;
    if (event->type() == QEvent::Leave || event->type() == QEvent::Hide) {
        clearHover();
        return false;
    }
    const auto tabs = _editor->findChild<QTabBar*>("workflowExpressionTabs");
    const bool active = tabs ? tabs->currentIndex() == 2 : _display->currentIndex() == 1;
    if (!active)
        return false;
    if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) {
        const auto key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Space &&
            (key->modifiers() == Qt::NoModifier || key->modifiers() == Qt::ShiftModifier)) {
            event->accept();
            if (event->type() == QEvent::KeyPress && !key->isAutoRepeat()) {
                if (key->modifiers() == Qt::ShiftModifier)
                    tapGrid();
                else
                    playSource();
            }
            return true;
        }
    }
    if (ruler && event->type() == QEvent::MouseButtonPress) {
        const auto mouse = static_cast<QMouseEvent*>(event);
        if (mouse->button() == Qt::LeftButton && mouse->modifiers() == Qt::NoModifier &&
            _editor->_score) {
            const int tick = _editor->tickForX(mouse->x(), false);
            const double time = _clip.isEmpty()
                                    ? _editor->_score->tempomap()->tick2time(tick) * 1000.
                                    : timeAtX(mouse->x());
            seekSource(qMax(0., time));
            _editor->seek(tick);
            if (_clip.isEmpty() && _display->currentIndex() == 0 &&
                (time < _from || time > _from + _span))
                _from = qMax(0., time - _span * .2);
            viewChanged();
            event->accept();
            return true;
        }
    }
    return !ruler && overlayEvent(event);
}
bool SpectrumPane::paintRollPlayhead(QPainter& p, QWidget* widget) const
{
    const auto tabs = _editor->findChild<QTabBar*>("workflowExpressionTabs");
    if (!isVisible() || (tabs && tabs->currentIndex() != 2) || !sourceInPlayback() ||
        !_editor->_score)
        return false;
    const double x =
        _clip.isEmpty()
            ? _editor->xForTick(_editor->_score->tempomap()->time2tick(_lastAudioMs / 1000.), false)
            : xAtTime(_lastAudioMs);
    const auto color = palette().highlight().color();
    p.save();
    p.setPen(QPen(color, 1.5));
    p.drawLine(QPointF(x, 0), QPointF(x, widget->height()));
    if (widget == _editor->_ruler || widget->property("performanceExtraRuler").toBool()) {
        p.setBrush(color);
        QPolygonF triangle;
        triangle << QPointF(x - 3, 0) << QPointF(x + 3, 0) << QPointF(x, 5);
        p.drawPolygon(triangle);
    }
    p.restore();
    return !seq || !seq->isPlaying();
}
} // namespace Ms
