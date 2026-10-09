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

#ifndef __SYMBOL_H__
#define __SYMBOL_H__

#include "bsymbol.h"
#include "sym.h"

namespace Ms {

class Segment;
class ScoreFont;

//---------------------------------------------------------
//   @@ Symbol
///    Symbol constructed from builtin symbol.
//
//   @P symbol       string       the SMuFL name of the symbol
//---------------------------------------------------------

class Symbol : public BSymbol {
   protected:
      SymId _sym;
      const ScoreFont* _scoreFont = nullptr;
      bool _handMode = false, _handText = false, _handDragging = false;
      qreal _handH = 2, _handV = -1, _handWidth = .12;
      QPointF _handDragOriginal;
      QPainterPath handPath() const;
      QFont handFont() const;
      QPointF handTextPosition() const;
      QRectF handTextRect() const;


   public:
      Symbol(Score* s, ElementFlags f = ElementFlag::MOVABLE);
      Symbol(const Symbol&);

      Symbol &operator=(const Symbol&) = delete;

      Symbol* clone() const override     { return new Symbol(*this); }
      ElementType type() const override  { return ElementType::SYMBOL; }

      void setSym(SymId s, const ScoreFont* sf = nullptr) {
            const bool fresh = !handEligible() && !_handMode && _handH == 2;
            _sym = s; _scoreFont = sf;
            if (fresh && handEnd()) _handH = -2;
            }
      SymId sym() const                  { return _sym;  }
      QString symName() const;

      QString accessibleInfo() const override;

      void draw(QPainter*) const override;
      void write(XmlWriter& xml) const override;
      void read(XmlReader&) override;
      void layout() override;

      QVariant getProperty(Pid) const override;
      bool setProperty(Pid, const QVariant&) override;
      QVariant propertyDefault(Pid) const override;
      bool handEligible() const;
      bool handBracket() const { return _handMode && handEligible(); }
      bool handLeft() const;
      bool handEnd() const;
      void applyHandPreset(); // only explicit creation/conversion, never read/clone
      Shape shape() const override;
      int gripsCount() const override { return handBracket() ? 2 : 0; }
      Grip initialEditModeGrip() const override { return handBracket() ? Grip::START : Grip::NO_GRIP; }
      Grip defaultGrip() const override { return initialEditModeGrip(); }
      std::vector<QPointF> gripsPositions(const EditData& = EditData()) const override;
      void startEditDrag(EditData&) override;
      void editDrag(EditData&) override;
      void endEditDrag(EditData&) override;
      bool edit(EditData&) override;


      qreal baseLine() const override    { return 0.0; }
      virtual Segment* segment() const;
      };

//---------------------------------------------------------
//   @@ FSymbol
///    Symbol constructed from a font glyph.
//---------------------------------------------------------

class FSymbol final : public BSymbol {
      QFont _font;
      int _code;

   public:
      FSymbol(Score* s);
      FSymbol(const FSymbol&);

      FSymbol* clone() const override   { return new FSymbol(*this); }
      ElementType type() const override { return ElementType::FSYMBOL; }

      void draw(QPainter*) const override;
      void write(XmlWriter& xml) const override;
      void read(XmlReader&) override;
      void layout() override;

      qreal baseLine() const override{ return 0.0; }
      Segment* segment() const       { return (Segment*)parent(); }
      QFont font() const             { return _font; }
      int code() const               { return _code; }
      void setFont(const QFont& f);
      void setCode(int val)          { _code = val; }
      };

}     // namespace Ms
#endif

