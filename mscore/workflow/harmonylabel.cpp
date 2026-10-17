// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "mscore/plugin/api/scoreobserver.h"
#include "libmscore/harmony.h"
#include "harmonytypography.h"
#include <QBuffer>
#include <QCache>
#include <QFontMetricsF>
#include <QImage>
#include <QJsonDocument>
#include <QPainter>
#include <QScopedValueRollback>
#include <QtMath>

namespace Ms { namespace PluginAPI {
QVariantMap ScoreObserver::harmonyLabel(const QString& chordText,const QString& degreeText,const QVariantMap& options) const
      {
      if(!_score)return {};
      const QString chord=chordText.left(64),degree=degreeText.left(32);
      if(chord.isEmpty()&&degree.isEmpty())return {};
      static QCache<QString,QVariantMap> cache(96);
      const QString key=analysisScoreId()+analysisContentRevision()+chord+"\t"+degree+QString::fromUtf8(QJsonDocument::fromVariant(options).toJson(QJsonDocument::Compact));
      if(auto found=cache.object(key))return *found;
      const qreal pixels=qBound(12.,options.value("pixels",20.).toDouble(),40.);
      const qreal ratio=qBound(.6,options.value("degreeRatio",1.).toDouble(),1.5);
      const qreal gap=qBound(0.,options.value("gap",6.).toDouble(),24.);
      const int order=qBound(0,options.value("order",0).toInt(),3);
      const QColor ink(options.value("color","#343a3f").toString());
      MasterScore scratch(_score->style());scratch.style().checkChordList();
      // This display uses the same baseline for root, slash and bass. The score style stays unchanged.
      alignTemporaryHarmonyBass(scratch);
      Harmony harmony(&scratch);
      const QString font=options.value("font").toString();
      if(!font.isEmpty())harmony.setProperty(Pid::FONT_FACE,font);
      const bool symbol=!chord.isEmpty() && QString("ABCDEFGabcdefg").contains(chord.front());
      if(symbol){harmony.setHarmony(chord);harmony.calculateBoundingRect();}
      auto chordBox=chord.isEmpty()?QRectF():harmony.bbox();
      // Use the native chord's normal capital height as the common visual unit.
      QFont reference(font.isEmpty()?_score->styleSt(Sid::chordSymbolAFontFace):font);
      reference.setPointSizeF(_score->styleD(Sid::chordSymbolAFontSize));
      const qreal capital=qMax(1.,QFontMetricsF(reference).capHeight());
      Harmony capitalHarmony(&scratch);if(!font.isEmpty())capitalHarmony.setProperty(Pid::FONT_FACE,font);capitalHarmony.setHarmony("C");capitalHarmony.calculateBoundingRect();
      const qreal scale=pixels/qMax(1.,symbol?capitalHarmony.bbox().height():capital);
      const bool plain=(!chord.isEmpty() && (!symbol || chordBox.isEmpty()));
      if(plain)chordBox=QFontMetricsF(reference).boundingRect(chord);
      QString degreeFamily=options.value("degreeFont").toString();
      if(degreeFamily.isEmpty())degreeFamily="Edwin";
      QFont degreeFont(degreeFamily);degreeFont.setPixelSize(100);const qreal degreeCapital=qMax(1.,QFontMetricsF(degreeFont).capHeight());degreeFont.setPixelSize(qRound(100*pixels*ratio/degreeCapital));
      QFontMetricsF metrics(degreeFont);
      const QRectF degreeBox=metrics.tightBoundingRect(degree);
      const QSizeF a=chordBox.size()*scale,b=degree.isEmpty()?QSizeF():QSizeF(metrics.horizontalAdvance(degree),degreeBox.height());
      const bool vertical=order>=2,reverse=order==1||order==3;
      const qreal spacing=a.isEmpty()||b.isEmpty()?0:gap;
      const qreal ascent=qMax(-chordBox.top()*scale,-degreeBox.top()),descent=qMax(chordBox.bottom()*scale,degreeBox.bottom());
      const QSizeF combined=vertical?QSizeF(qMax(a.width(),b.width()),a.height()+b.height()+spacing):QSizeF(a.width()+b.width()+spacing,ascent+descent);
      const QSize size(qMax(1,qCeil(combined.width()+4)),qMax(1,qCeil(combined.height()+4)));
      QImage image(size*2,QImage::Format_ARGB32_Premultiplied);image.fill(Qt::transparent);
      QScopedValueRollback<qreal> fontDevice(MScore::pixelRatio,DPI/qreal(image.logicalDpiX()));
      QPainter painter(&image);painter.scale(2,2);painter.setRenderHint(QPainter::Antialiasing);painter.setRenderHint(QPainter::TextAntialiasing);
      QPointF chordAt(2,2),degreeAt(2,2);
      if(vertical){chordAt+=QPointF((combined.width()-a.width())/2,reverse?b.height()+spacing:0);degreeAt+=QPointF((combined.width()-b.width())/2,reverse?0:a.height()+spacing);}
      else {chordAt+=QPointF(reverse?b.width()+spacing:0,(combined.height()-a.height())/2);degreeAt+=QPointF(reverse?0:a.width()+spacing,(combined.height()-b.height())/2);}
      if(!vertical){chordAt.setY(2+ascent+chordBox.top()*scale);degreeAt.setY(2+ascent+degreeBox.top());}
      if(!chord.isEmpty()){painter.save();painter.translate(chordAt);painter.scale(scale,scale);painter.translate(-chordBox.topLeft());if(plain){painter.setPen(ink);painter.setFont(reference);painter.drawText(QPointF(),chord);}else{harmony.setColor(ink);static_cast<const Element&>(harmony).draw(&painter);}painter.restore();}
      if(!degree.isEmpty()){painter.setPen(ink);painter.setFont(degreeFont);painter.drawText(degreeAt-QPointF(0,degreeBox.top()),degree);}
      painter.end();QByteArray png;QBuffer buffer(&png);buffer.open(QIODevice::WriteOnly);image.save(&buffer,"PNG");
      QVariantMap result{{"source","data:image/png;base64,"+QString::fromLatin1(png.toBase64())},{"width",size.width()},{"height",size.height()},{"baseline",2+ascent},{"ascent",ascent},{"descent",descent},{"font",reference.family()},{"degreeFont",degreeFamily}};
      cache.insert(key,new QVariantMap(result));return result;
      }
}}
