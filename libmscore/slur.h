//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Copyright (C) 2002-2016 Werner Schweer
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

#ifndef __SLUR_H__
#define __SLUR_H__

#include "slurtie.h"
#include "property.h"
#include "freeslur.h"
#include <array>

namespace Ms {

//---------------------------------------------------------
//   @@ SlurSegment
///    a single segment of slur; also used for Tie
//---------------------------------------------------------

class SlurSegment final : public SlurTieSegment {

   protected:
      qreal _extraHeight = 0.0;
      void changeAnchor(EditData&, Element*) override;
      QVector<QLineF> gripAnchorLines(Grip) const override;
      struct FreeGrips { int index; QPointF point,left,right; double leftScale,rightScale; };
      QVector<FreeGrips> _freeGrips;
      QVector<FreeSlur::CurvePoint> _freeCurvePoints;
      QString _freeDragOriginal;
      bool _freeDragging = false;
      Pid _freeDragProperty = Pid::FREE_SLUR_NODES;
      bool freeEndpointGrip(Grip) const;
      QPointF freeAutoEndpoint(bool start) const;
      double freeGripScale(bool start) const;
      std::array<QPointF,4> freeDrawingControls(bool adjustments = true) const;
      int _freeCollisions = 0;
      bool _freeLimited = false;
      int freeEndpointSide(bool start) const;
      bool computeFreeBezier(QPointF);
      bool editFreeNode(EditData&);

   public:
      SlurSegment(Score* s) : SlurTieSegment(s) {}
      SlurSegment(const SlurSegment& ss) : SlurTieSegment(ss) {}

      SlurSegment* clone() const override  { return new SlurSegment(*this); }
      ElementType type() const override    { return ElementType::SLUR_SEGMENT; }
      int subtype() const override         { return static_cast<int>(spanner()->type()); }
      void draw(QPainter*) const override;

      void layoutSegment(const QPointF& p1, const QPointF& p2);

      bool isEdited() const;
      const QVector<FreeSlur::CurvePoint>& freeCurvePoints() const { return _freeCurvePoints; }
      int freeCollisions() const { return _freeCollisions; }
      bool freeLimited() const { return _freeLimited; }
      bool edit(EditData&) override;
      std::array<QPointF,4> freeBaseControls() const;
      bool displayedFreeNode(int index,FreeSlurNode*) const;
      bool displayedFreeNode(double position,FreeSlurNode*) const;

      Slur* slur() const { return toSlur(spanner()); }

      void computeBezier(QPointF so = QPointF()) override;
      int gripsCount() const override;
      std::vector<QPointF> gripsPositions(const EditData& = EditData()) const override;
      void startEditDrag(EditData&) override;
      void editDrag(EditData&) override;
      void endEditDrag(EditData&) override;
      void endEdit(EditData&) override;
      void drawEditMode(QPainter*,EditData&) override;
      Element* propertyDelegate(Pid) override;
      QVariant getProperty(Pid) const override;
      QVariant propertyDefault(Pid) const override;
      bool setProperty(Pid,const QVariant&) override;
      };

//---------------------------------------------------------
//   @@ Slur
//---------------------------------------------------------

class Slur final : public SlurTie {

      void slurPosChord(SlurPos*);
      bool _freeMode = false;
      bool _freeAuto = false;
      int _freeStartSide=0,_freeEndSide=0;
      QVector<FreeSlurNode> _freeNodes;
      QString _freeData = "[]";
      std::array<FreeSlurEndpoint,2> _freeEndpoints;
      QString _freeEndpointData = "{}";

   public:
      Slur(Score* = 0);
      Slur(const Slur&);
      ~Slur() {}

      Slur* clone() const override        { return new Slur(*this); }
      ElementType type() const override { return ElementType::SLUR; }
      void write(XmlWriter& xml) const override;
      bool readProperties(XmlReader&) override;
      void layout() override;
      SpannerSegment* layoutSystem(System*) override;
      void setTrack(int val) override;
      void slurPos(SlurPos*) override;
      SlurPos freeEndpointPositions(bool up);

      SlurSegment* frontSegment()               { return toSlurSegment(Spanner::frontSegment()); }
      const SlurSegment* frontSegment() const   { return toSlurSegment(Spanner::frontSegment()); }
      SlurSegment* backSegment()                { return toSlurSegment(Spanner::backSegment());  }
      const SlurSegment* backSegment() const    { return toSlurSegment(Spanner::backSegment());  }
      SlurSegment* segmentAt(int n)             { return toSlurSegment(Spanner::segmentAt(n));   }
      const SlurSegment* segmentAt(int n) const { return toSlurSegment(Spanner::segmentAt(n));   }

      SlurTieSegment* newSlurTieSegment() override { return new SlurSegment(score()); }
      void reset() override;
      const std::array<FreeSlurEndpoint,2>& freeEndpoints() const { return _freeEndpoints; }
      bool freeEndpointsEdited() const { return _freeEndpointData != "{}"; }
      bool freeMode() const { return _freeMode; }
      bool freeAuto() const { return _freeAuto; }
      int freeStartSide()const{return _freeStartSide;}
      int freeEndSide()const{return _freeEndSide;}
      const QVector<FreeSlurNode>& freeNodes() const { return _freeNodes; }
      QVariant getProperty(Pid) const override;
      QVariant propertyDefault(Pid) const override;
      bool setProperty(Pid,const QVariant&) override;
      };

}     // namespace Ms
#endif
