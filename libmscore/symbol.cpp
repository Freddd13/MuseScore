 //=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Copyright (C) 2002-2011 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#include "chord.h"
#include "note.h"
#include <cmath>
#include <QPainterPathStroker>
#include "image.h"
#include "measure.h"
#include "page.h"
#include "score.h"
#include "staff.h"
#include "system.h"
#include "symbol.h"
#include "sym.h"
#include "xml.h"

namespace Ms {

//---------------------------------------------------------
//   Symbol
//---------------------------------------------------------

Symbol::Symbol(Score* s, ElementFlags f)
   : BSymbol(s, f)
      {
      _sym = SymId::accidentalSharp;        // arbitrary valid default
      }

Symbol::Symbol(const Symbol& s)
   : BSymbol(s)
      {
      _sym       = s._sym;
      _scoreFont = s._scoreFont;
      _handMode = s._handMode;
      _handText = s._handText;
      _handH = s._handH;
      _handV = s._handV;
      _handWidth = s._handWidth;
      }

//---------------------------------------------------------
//   symName
//---------------------------------------------------------

QString Symbol::symName() const
      {
      return Sym::id2name(_sym);
      }

//---------------------------------------------------------
//   accessibleInfo
//---------------------------------------------------------

QString Symbol::accessibleInfo() const
      {
      return QString("%1: %2").arg(Element::accessibleInfo(), Sym::id2name(_sym));
      }


//---------------------------------------------------------
//   layout
//    height() and width() should return sensible
//    values when calling this method
//---------------------------------------------------------

bool Symbol::handEligible() const
      {
      return _sym == SymId::keyboardPlayWithRH || _sym == SymId::keyboardPlayWithRHEnd
         || _sym == SymId::keyboardPlayWithLH || _sym == SymId::keyboardPlayWithLHEnd;
      }

bool Symbol::handLeft() const
      {
      return _sym == SymId::keyboardPlayWithLH || _sym == SymId::keyboardPlayWithLHEnd;
      }

bool Symbol::handEnd() const
      {
      return _sym == SymId::keyboardPlayWithRHEnd || _sym == SymId::keyboardPlayWithLHEnd;
      }

void Symbol::applyHandPreset()
      {
      if (!handEligible()) return;
      _handMode = true; _handH = handEnd() ? -2 : 2; _handV = -1;
      _handWidth = .12; _handText = false;
      }

Segment* Symbol::segment() const
      {
      if (parent() && parent()->isNote()) return toNote(parent())->chord()->segment();
      if (parent() && parent()->isChord()) return toChord(parent())->segment();
      return parent() && parent()->isSegment() ? toSegment(parent()) : nullptr;
      }

QPainterPath Symbol::handPath() const
      {
      const qreal unit = spatium() * mag();
      const qreal y = handLeft() ? _handV * unit : 0;
      QPainterPath path;
      path.moveTo(0, handLeft() ? 0 : _handV * unit);
      path.lineTo(0, y); path.lineTo(_handH * unit, y);
      return path;
      }

QFont Symbol::handFont() const
      {
      QFont font("FreeSerif"); font.setPixelSize(qMax(1,qRound(.9 * spatium() * mag())));
      return font;
      }

QPointF Symbol::handTextPosition() const
      {
      const qreal unit = spatium() * mag();
      const qreal width = QFontMetricsF(handFont(), MScore::paintDevice()).horizontalAdvance(handLeft() ? "LH" : "RH");
      return QPointF(_handH * unit + (_handH >= 0 ? .3 * unit : -.3 * unit - width),
         (handLeft() ? _handV * unit : 0) + .3 * unit);
      }

QRectF Symbol::handTextRect() const
      {
      return _handText ? QFontMetricsF(handFont(), MScore::paintDevice()).boundingRect(handLeft() ? "LH" : "RH").translated(handTextPosition()) : QRectF();
      }

Shape Symbol::shape() const
      {
      if (!handBracket()) return BSymbol::shape();
      const qreal unit = spatium() * mag(), pad = (_handWidth / 2 + .12) * unit;
      const qreal x = _handH * unit, y = _handV * unit, corner = handLeft() ? y : 0;
      Shape result;
      result.add(QRectF(-pad,qMin(qreal(0),y)-pad,2*pad,std::abs(y)+2*pad));
      result.add(QRectF(qMin(qreal(0),x)-pad,corner-pad,std::abs(x)+2*pad,2*pad));
      if (_handText) result.add(handTextRect());
      return result;
      }

std::vector<QPointF> Symbol::gripsPositions(const EditData&) const
      {
      if (!handBracket()) return {};
      const qreal unit = spatium() * mag();
      return {pagePos()+QPointF(_handH*unit,handLeft() ? _handV*unit : 0),pagePos()+QPointF(0,_handV*unit)};
      }

void Symbol::startEditDrag(EditData& ed)
      {
      Element::startEditDrag(ed);
      if (handBracket() && int(ed.curGrip)>=0 && int(ed.curGrip)<2) {
            auto data=ed.getData(this); data->pushProperty(Pid::HAND_BRACKET_H); data->pushProperty(Pid::HAND_BRACKET_V);
            _handDragOriginal=QPointF(_handH,_handV); _handDragging=true;
            }
      }

void Symbol::editDrag(EditData& ed)
      {
      if (!handBracket() || int(ed.curGrip)<0 || int(ed.curGrip)>=2) {BSymbol::editDrag(ed);return;}
      if (!_handDragging) return; // cancelled preview stays cancelled until release
      const qreal unit=spatium()*mag();
      const Pid id=int(ed.curGrip)==0 ? Pid::HAND_BRACKET_H : Pid::HAND_BRACKET_V;
      const qreal delta=int(ed.curGrip)==0 ? ed.delta.x() : ed.delta.y();
      score()->addRefresh(canvasBoundingRect());
      setProperty(id,qBound(-40.0,getProperty(id).toDouble()+delta/unit,40.0));
      layout(); score()->addRefresh(canvasBoundingRect());
      }

void Symbol::endEditDrag(EditData& ed)
      {
      Element::endEditDrag(ed); _handDragging=false;
      }

bool Symbol::edit(EditData& ed)
      {
      if (handBracket() && int(ed.curGrip)>=0 && int(ed.curGrip)<2) {
            if (ed.key==Qt::Key_Escape && _handDragging) {
                  score()->addRefresh(canvasBoundingRect());
                  setProperty(Pid::HAND_BRACKET_H,_handDragOriginal.x());
                  setProperty(Pid::HAND_BRACKET_V,_handDragOriginal.y());
                  _handDragging=false; layout(); score()->addRefresh(canvasBoundingRect()); return true;
                  }
            if (ed.key==Qt::Key_Home) {
                  const Pid id=int(ed.curGrip)==0 ? Pid::HAND_BRACKET_H : Pid::HAND_BRACKET_V;
                  undoChangeProperty(id,propertyDefault(id));return true;
                  }
            }
      return BSymbol::edit(ed);
      }

void Symbol::layout()
      {
      if (handBracket()) {
            BSymbol::layout();
            if (parent() && (parent()->isNote() || parent()->isChord())) setMag(parent()->mag());
            QPainterPathStroker stroke; stroke.setWidth(_handWidth*spatium()*mag());
            stroke.setJoinStyle(Qt::MiterJoin); stroke.setCapStyle(Qt::SquareCap);
            QRectF box=stroke.createStroke(handPath()).boundingRect();
            if (_handText) box |= handTextRect(); setbbox(box);
            QPointF position;
            if (parent() && (parent()->isNote() || parent()->isChord())) {
                  position.setX(-2.5 * spatium() * mag());
                  if (parent()->isChord()) position += toChord(parent())->downNote()->pos();
                  }
            setPos(position); return;
            }
      // foreach(Element* e, leafs())     done in BSymbol::layout() ?
      //      e->layout();
      setbbox(_scoreFont ? _scoreFont->bbox(_sym, magS()) : symBbox(_sym));
      qreal w = width();
      QPointF p;
      if (align() & Align::BOTTOM)
            p.setY(- height());
      else if (align() & Align::VCENTER)
            p.setY((- height()) * .5);
      else if (align() & Align::BASELINE)
            p.setY(-baseLine());
      if (align() & Align::RIGHT)
            p.setX(-w);
      else if (align() & Align::HCENTER)
            p.setX(-(w * .5));
      setPos(p);
      BSymbol::layout();
      }

//---------------------------------------------------------
//   Symbol::draw
//---------------------------------------------------------

void Symbol::draw(QPainter* p) const
      {
      if (handBracket()) {
            p->save();p->setBrush(Qt::NoBrush);
            p->setPen(QPen(curColor(),_handWidth*spatium()*mag(),Qt::SolidLine,Qt::SquareCap,Qt::MiterJoin));
            p->drawPath(handPath());
            if (_handText) {p->setPen(curColor());p->setFont(handFont());p->drawText(handTextPosition(),handLeft() ? "LH" : "RH");}
            p->restore();return;
            }
      if (!isNoteDot() || !staff()->isTabStaff(tick())) {
            p->setPen(curColor());
            if (_scoreFont)
                  _scoreFont->draw(_sym, p, magS(), QPointF());
            else
                  drawSymbol(_sym, p);
            }
      }

//---------------------------------------------------------
//   Symbol::write
//---------------------------------------------------------

void Symbol::write(XmlWriter& xml) const
      {
      xml.stag(this);
      xml.tag("name", Sym::id2name(_sym));
      if (_scoreFont)
            xml.tag("font", _scoreFont->name());
      for (Pid id : {Pid::HAND_BRACKET_MODE,Pid::HAND_BRACKET_H,Pid::HAND_BRACKET_V,Pid::HAND_BRACKET_WIDTH,Pid::HAND_BRACKET_TEXT})
            writeProperty(xml,id);
      BSymbol::writeProperties(xml);
      xml.etag();
      }

//---------------------------------------------------------
//   Symbol::read
//---------------------------------------------------------

void Symbol::read(XmlReader& e)
      {
      QPointF pos;
      while (e.readNextStartElement()) {
            const QStringRef& tag(e.name());
            if (tag == "name") {
                  QString val(e.readElementText());
                  SymId symId = Sym::name2id(val);
                  if (val != "noSym") {
                        if (symId == SymId::noSym) {
                              // if symbol name not found, fall back to user names
                              // TODO : does it make sense? user names are probably localized
                              symId = Sym::userName2id(val);
                              if (symId == SymId::noSym) {
                                    qDebug("unknown symbol <%s>, falling back to no symbol", qPrintable(val));
                                    // set a default symbol, or layout() will crash
                                    symId = SymId::noSym;
                                    }
                              }
                        }
                  setSym(symId);
                  }
            else if (tag == "font")
                  _scoreFont = ScoreFont::fontFactory(e.readElementText());
            else if (tag == "Symbol") {
                  Symbol* s = new Symbol(score());
                  s->read(e);
                  add(s);
                  }
            else if (tag == "Image") {
                  if (MScore::noImages)
                        e.skipCurrentElement();
                  else {
                        Image* image = new Image(score());
                        image->read(e);
                        add(image);
                        }
                  }
            else if (tag == "small" || tag == "subtype")    // obsolete
                  e.skipCurrentElement();
            else if (tag == propertyName(Pid::HAND_BRACKET_MODE)) readProperty(e,Pid::HAND_BRACKET_MODE);
            else if (tag == propertyName(Pid::HAND_BRACKET_H)) readProperty(e,Pid::HAND_BRACKET_H);
            else if (tag == propertyName(Pid::HAND_BRACKET_V)) readProperty(e,Pid::HAND_BRACKET_V);
            else if (tag == propertyName(Pid::HAND_BRACKET_WIDTH)) readProperty(e,Pid::HAND_BRACKET_WIDTH);
            else if (tag == propertyName(Pid::HAND_BRACKET_TEXT)) readProperty(e,Pid::HAND_BRACKET_TEXT);
            else if (!BSymbol::readProperties(e))
                  e.unknown();
            }
      setPos(pos);
      }

//---------------------------------------------------------
//   Symbol::getProperty
//---------------------------------------------------------

QVariant Symbol::getProperty(Pid propertyId) const
      {
      switch (propertyId) {
            case Pid::HAND_BRACKET_MODE: return _handMode;
            case Pid::HAND_BRACKET_H: return _handH;
            case Pid::HAND_BRACKET_V: return _handV;
            case Pid::HAND_BRACKET_WIDTH: return _handWidth;
            case Pid::HAND_BRACKET_TEXT: return _handText;
            case Pid::SYMBOL:
                  return QVariant::fromValue(_sym);
            default:
                  break;
            }
      return BSymbol::getProperty(propertyId);
      }

//---------------------------------------------------------
//   Symbol::setProperty
//---------------------------------------------------------

bool Symbol::setProperty(Pid id, const QVariant& value)
      {
      const qreal number=value.toDouble();
      switch (id) {
            case Pid::HAND_BRACKET_MODE: _handMode=value.toBool();break;
            case Pid::HAND_BRACKET_TEXT: _handText=value.toBool();break;
            case Pid::HAND_BRACKET_H:
            case Pid::HAND_BRACKET_V:
                  if (!std::isfinite(number) || std::abs(number)>40) return false;
                  if (id==Pid::HAND_BRACKET_H) _handH=number; else _handV=number;
                  break;
            case Pid::HAND_BRACKET_WIDTH:
                  if (!std::isfinite(number) || number<.02 || number>2) return false;
                  _handWidth=number;break;
            case Pid::SYMBOL: _sym=value.value<SymId>();break;
            default: return BSymbol::setProperty(id,value);
            }
      triggerLayout(); return true;
      }

QVariant Symbol::propertyDefault(Pid id) const
      {
      switch (id) {
            case Pid::HAND_BRACKET_MODE: return false;
            case Pid::HAND_BRACKET_TEXT: return false;
            case Pid::HAND_BRACKET_H: return handEnd() ? -2.0 : 2.0;
            case Pid::HAND_BRACKET_V: return -1.0;
            case Pid::HAND_BRACKET_WIDTH: return .12;
            default: return BSymbol::propertyDefault(id);
            }
      }

//---------------------------------------------------------
//   FSymbol
//---------------------------------------------------------

FSymbol::FSymbol(Score* s)
  : BSymbol(s)
      {
      _code = 0;
      _font.setStyleStrategy(QFont::NoFontMerging);
      }

FSymbol::FSymbol(const FSymbol& s)
  : BSymbol(s)
      {
      _font = s._font;
      _code = s._code;
      }

//---------------------------------------------------------
//   draw
//---------------------------------------------------------

void FSymbol::draw(QPainter* painter) const
      {
      QString s;
      QFont f(_font);
      f.setPointSizeF(f.pointSizeF() * MScore::pixelRatio);
      painter->setFont(f);
      if (_code & 0xffff0000) {
            s = QChar(QChar::highSurrogate(_code));
            s += QChar(QChar::lowSurrogate(_code));
            }
      else
            s = QChar(_code);
      painter->setPen(curColor());
      painter->drawText(QPointF(0, 0), s);
      }

//---------------------------------------------------------
//   write
//---------------------------------------------------------

void FSymbol::write(XmlWriter& xml) const
      {
      xml.stag(this);
      xml.tag("font",     _font.family());
      xml.tag("fontsize", _font.pointSizeF());
      xml.tag("code",     _code);
      BSymbol::writeProperties(xml);
      xml.etag();
      }

//---------------------------------------------------------
//   read
//---------------------------------------------------------

void FSymbol::read(XmlReader& e)
      {
      while (e.readNextStartElement()) {
            const QStringRef& tag(e.name());
            if (tag == "font")
                  _font.setFamily(e.readElementText());
            else if (tag == "fontsize")
                  _font.setPointSizeF(e.readDouble());
            else if (tag == "code")
                  _code = e.readInt();
            else if (!BSymbol::readProperties(e))
                  e.unknown();
            }
      setPos(QPointF());
      }

//---------------------------------------------------------
//   layout
//---------------------------------------------------------

void FSymbol::layout()
      {
      QString s;
      if (_code & 0xffff0000) {
            s = QChar(QChar::highSurrogate(_code));
            s += QChar(QChar::lowSurrogate(_code));
            }
      else
            s = QChar(_code);
      QFontMetricsF fm(_font, MScore::paintDevice());
      setbbox(fm.boundingRect(s));
      }

//---------------------------------------------------------
//   setFont
//---------------------------------------------------------

void FSymbol::setFont(const QFont& f)
      {
      _font = f;
      _font.setStyleStrategy(QFont::NoFontMerging);
      }

}

