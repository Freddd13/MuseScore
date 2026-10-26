// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "config.h"
#include "toolicons.h"
#include "workspaceviews.h"
#include "mscore/icons.h"
#include <QAbstractButton>
#include <QApplication>
#include <QEvent>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QToolButton>
#ifdef SCRIPT_INTERFACE
#include <QQmlEngine>
#include <QQuickImageProvider>
#endif

namespace Ms {
namespace {
QIcon buttonIcon(QAbstractButton* button, const QString& key) {
      auto icon = componentToolIcon(key);
      if (icon.isNull() || key == "record") return icon;
      auto image = icon.pixmap(32, 32);
      QPainter p(&image); p.setCompositionMode(QPainter::CompositionMode_SourceIn);
      p.fillRect(image.rect(), button->palette().color(QPalette::ButtonText));
      return QIcon(image);
}
class ButtonIcons final : public QObject {
      QString _key;
      bool eventFilter(QObject* watched, QEvent* event) override {
            if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)
                  static_cast<QAbstractButton*>(watched)->setIcon(buttonIcon(static_cast<QAbstractButton*>(watched), _key));
            return false;
      }
public:
      explicit ButtonIcons(QAbstractButton* button) : QObject(button) {
            setObjectName("componentToolIconBinding"); button->installEventFilter(this);
      }
      void setKey(const QString& key) { _key = key; }
};
#ifdef SCRIPT_INTERFACE
class ToolIconProvider final : public QQuickImageProvider {
public:
      ToolIconProvider() : QQuickImageProvider(QQuickImageProvider::Pixmap) {}
      QPixmap requestPixmap(const QString& id, QSize* size, const QSize& requested) override {
            const auto parts = id.split('/');
            const QSize target = requested.isValid() ? requested.boundedTo(QSize(128, 128)) : QSize(16, 16);
            auto icon = componentToolIcon(parts.value(0));
            if (icon.isNull()) return {};
            auto image = icon.pixmap(target);
            const QColor color("#" + parts.value(1));
            if (color.isValid()) {
                  QPainter p(&image); p.setCompositionMode(QPainter::CompositionMode_SourceIn);
                  p.fillRect(image.rect(), color);
            }
            if (size) *size = image.size();
            return image;
      }
};
#endif
}

QIcon componentToolIcon(const QString& key) {
      if (key == "waveform") return personalToolIcon("reference-player");
      if (key == "chord") return personalToolIcon("harmony-assistant");
      if (key == "velocity") return personalToolIcon("performance-expression");
      // Reuse the score editor's established shapes and theme handling.
      static const QHash<QString, Icons> stock {
            {"open", Icons::fileOpen_ICON}, {"save", Icons::fileSave_ICON},
            {"copy", Icons::copy_ICON}, {"delete", Icons::bin_ICON},
            {"edit", Icons::edit_ICON}, {"undo", Icons::undo_ICON},
            {"redo", Icons::redo_ICON}, {"reset", Icons::reset_ICON},
            {"close", Icons::close_ICON}, {"back", Icons::goPrevious_ICON},
            {"refresh", Icons::viewRefresh_ICON}, {"play", Icons::play_ICON},
            {"loop", Icons::loop_ICON}, {"loop-in", Icons::loopIn_ICON},
            {"loop-out", Icons::loopOut_ICON}, {"midi", Icons::midiin_ICON},
            {"audition", Icons::speaker_ICON}, {"locate", Icons::playback_highlight_ICON},
            {"apply", Icons::checkmark_ICON},
            {"up", Icons::arrowUp_ICON}, {"down", Icons::arrowDown_ICON},
            {"keyboard", Icons::onscreenKeyboard_ICON},
            {"tempo", Icons::independentMetronome_ICON}, {"palette", Icons::noteheadColor_ICON},
            {"score", Icons::note4_ICON}, {"list", Icons::formatListUnordered_ICON},
            {"pencil", Icons::edit_ICON}
      };
      const auto item = stock.constFind(key);
      if (item != stock.cend() && icons[int(item.value())]) return *icons[int(item.value())];
      if (!QStringList{"add", "record", "stop", "pause", "mute", "solo", "fit", "link", "filter", "line", "panel", "settings", "select", "range", "hand"}.contains(key)) return {};
      QPixmap image(64, 64); image.setDevicePixelRatio(4); image.fill(Qt::transparent);
      QPainter p(&image); p.setRenderHint(QPainter::Antialiasing);
      const auto ink = qApp->palette().color(QPalette::WindowText);
      p.setPen(QPen(ink, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      if (key == "add") { p.drawLine(3, 8, 13, 8); p.drawLine(8, 3, 8, 13); }
      else if (key == "record") { p.setPen(Qt::NoPen); p.setBrush(QColor("#c54c50")); p.drawEllipse(QRectF(3, 3, 10, 10)); }
      else if (key == "stop") { p.setPen(Qt::NoPen); p.setBrush(ink); p.drawRoundedRect(QRectF(3, 3, 10, 10), 1, 1); }
      else if (key == "pause") { p.fillRect(QRectF(3, 3, 3, 10), ink); p.fillRect(QRectF(10, 3, 3, 10), ink); }
      else if (key == "mute") {
            QPolygonF speaker; speaker << QPointF(2,6) << QPointF(5,6) << QPointF(8,3) << QPointF(8,13) << QPointF(5,10) << QPointF(2,10);
            p.drawPolygon(speaker); p.drawLine(11,6,15,10); p.drawLine(11,10,15,6);
      } else if (key == "solo") {
            p.drawArc(QRectF(3,2,10,13),0,180*16);
            p.drawRoundedRect(QRectF(2,7,3,6),1,1); p.drawRoundedRect(QRectF(11,7,3,6),1,1);
      } else if (key == "fit") {
            p.drawLine(2,3,2,13); p.drawLine(14,3,14,13); p.drawLine(5,8,11,8);
            p.drawLine(5,8,7,6); p.drawLine(5,8,7,10); p.drawLine(11,8,9,6); p.drawLine(11,8,9,10);
      } else if (key == "link") {
            p.save(); p.translate(8,8); p.rotate(-40);
            p.drawRoundedRect(QRectF(-7,-3,8,6),2,2); p.drawRoundedRect(QRectF(-1,-3,8,6),2,2); p.restore();
      } else if (key == "select") {
            QPolygonF arrow; arrow << QPointF(3,1) << QPointF(3,13) << QPointF(6,10)
                << QPointF(9,15) << QPointF(11,14) << QPointF(8,9) << QPointF(13,9);
            p.drawPolygon(arrow);
      } else if (key == "range") {
            p.setPen(QPen(ink, 1.2, Qt::DashLine));p.drawRect(QRectF(3, 4, 10, 8));
            p.setPen(QPen(ink, 1.8));p.drawLine(3,2,3,14);p.drawLine(13,2,13,14);
      } else if (key == "hand") {
            QPainterPath hand; hand.moveTo(6,14); hand.lineTo(2.5,9);
            hand.quadTo(2,7,3.5,7.5); hand.lineTo(5,9); hand.lineTo(5,4);
            hand.quadTo(6,2.5,7,4); hand.lineTo(7,7); hand.lineTo(7,2.5);
            hand.quadTo(8,1,9,2.5); hand.lineTo(9,7); hand.lineTo(9,3.5);
            hand.quadTo(10,2,11,3.5); hand.lineTo(11,7); hand.lineTo(11,5.5);
            hand.quadTo(12,4,13,5.5); hand.lineTo(13,10); hand.lineTo(11,14); hand.closeSubpath();
            p.drawPath(hand);
      } else if (key == "panel") {
            p.drawRoundedRect(QRectF(1.5,2.5,13,11),1,1); p.drawLine(2,6,14,6);
            p.drawLine(4,4.5,5,4.5); p.drawLine(8,10,12,10);
      } else if (key == "settings") {
            for (int y : {4,8,12}) p.drawLine(2,y,14,y);
            p.setBrush(qApp->palette().color(QPalette::Window));
            p.drawRoundedRect(QRectF(4,2,3,4),.5,.5); p.drawRoundedRect(QRectF(9,6,3,4),.5,.5);
            p.drawRoundedRect(QRectF(5,10,3,4),.5,.5);
      } else if (key == "line") {
            p.drawLine(3,13,13,3); p.drawEllipse(QRectF(1.5,11.5,3,3)); p.drawEllipse(QRectF(11.5,1.5,3,3));
      } else {
            QPolygonF funnel; funnel << QPointF(2,3) << QPointF(14,3) << QPointF(10,8) << QPointF(10,12) << QPointF(6,14) << QPointF(6,8);
            p.drawPolygon(funnel);
      }
      return QIcon(image);
}
void setComponentToolIcon(QAbstractButton* button, const QString& key) {
      if (!button) return;
      if (button->property("componentToolIcon").toString() == key && !button->icon().isNull()) return;
      auto binding = static_cast<ButtonIcons*>(button->findChild<QObject*>("componentToolIconBinding", Qt::FindDirectChildrenOnly));
      if (!binding) binding = new ButtonIcons(button);
      binding->setKey(key); button->setProperty("componentToolIcon", key);
      button->setIconSize(QSize(16, 16)); button->setIcon(buttonIcon(button, key));
      if (auto tool = qobject_cast<QToolButton*>(button)) tool->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
}
#ifdef SCRIPT_INTERFACE
void installComponentToolIcons(QQmlEngine* engine) { engine->addImageProvider("kumo-tools", new ToolIconProvider); }
#endif
}
