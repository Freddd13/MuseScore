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

#ifndef __TEXTLINE_H__
#define __TEXTLINE_H__

#include "textlinebase.h"

namespace Ms {

class Note;

//---------------------------------------------------------
//   @@ TextLineSegment
//---------------------------------------------------------

class TextLineSegment final : public TextLineBaseSegment {

      Sid getTextLinePos(bool above) const;
      Sid getPropertyStyle(Pid) const override;

   public:
      TextLineSegment(Spanner* sp, Score* s, bool system=false);

      ElementType type() const override       { return ElementType::TEXTLINE_SEGMENT; }
      TextLineSegment* clone() const override { return new TextLineSegment(*this); }

      virtual Element* propertyDelegate(Pid) override;

      TextLine* textLine() const              { return toTextLine(spanner()); }
      void layout() override;
      };

//---------------------------------------------------------
//   @@ TextLine
//---------------------------------------------------------

class TextLine final : public TextLineBase {

      Sid getTextLinePos(bool above) const;
      Sid getPropertyStyle(Pid) const override;
      bool _ritMode = false;
      bool _ritPlay = false;
      int _ritTargetMode = 0; // 0 percentage, 1 BPM
      double _ritTarget = 80.0;
      double _ritCurve = 1.0;
      double _ritStartBpm = 0.0; // zero follows the preceding tempo
      double _ritDerivedStart = 2.0; // transient display cache, not a score property

   public:
      using TextLineBase::undoChangeProperty;
      bool ritPlay() const { return _ritMode && _ritPlay && systemFlag(); }
      bool ritEnabled() const { return _ritMode; }
      int ritTargetMode() const { return _ritTargetMode; }
      double ritTarget() const { return _ritTarget; }
      double ritCurve() const { return _ritCurve; }
      double ritStartBpm() const { return _ritStartBpm; }
      double ritDerivedStart() const { return _ritDerivedStart; }
      void setRitDerivedStart(double value) { _ritDerivedStart = value; }
      TextLine(Score* s, bool system=false);
      TextLine(const TextLine&);
      ~TextLine() {}

      virtual void undoChangeProperty(Pid id, const QVariant&, PropertyFlags ps) override;
      virtual SpannerSegment* layoutSystem(System*) override;
      QPointF linePos(Grip, System**) const override;

      TextLine* clone() const override   { return new TextLine(*this); }
      ElementType type() const override  { return ElementType::TEXTLINE; }

      void write(XmlWriter&) const override;
      void read(XmlReader&) override;
      bool readProperties(XmlReader&) override;
      QVariant getProperty(Pid) const override;

      void initStyle();

      LineSegment* createLineSegment() override;
      QVariant propertyDefault(Pid) const override;
      bool setProperty(Pid propertyId, const QVariant&) override;
      };

}     // namespace Ms
#endif
