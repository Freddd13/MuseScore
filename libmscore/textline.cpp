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

#include "musescoreCore.h"
#include "score.h"
#include "staff.h"
#include "system.h"
#include "textline.h"
#include "undo.h"
#include "tempoexpression.h"
#include "measure.h"
#include "segment.h"
#include <cmath>
#include <algorithm>

namespace Ms {


//---------------------------------------------------------
//   textLineSegmentStyle
//---------------------------------------------------------

static const ElementStyle textLineSegmentStyle {
      { Sid::textLinePosAbove,      Pid::OFFSET       },
      { Sid::textLineMinDistance,   Pid::MIN_DISTANCE },
      };

//---------------------------------------------------------
//   systemTextLineSegmentStyle
//---------------------------------------------------------

      static const ElementStyle systemTextLineSegmentStyle {
      { Sid::systemTextLinePosAbove,      Pid::OFFSET       },
      { Sid::systemTextLineMinDistance,   Pid::MIN_DISTANCE },
      };

//---------------------------------------------------------
//   textLineStyle
//---------------------------------------------------------

static const ElementStyle textLineStyle {
//       { Sid::textLineSystemFlag,                 Pid::SYSTEM_FLAG             },
      { Sid::textLineFontFace,                   Pid::BEGIN_FONT_FACE         },
      { Sid::textLineFontFace,                   Pid::CONTINUE_FONT_FACE      },
      { Sid::textLineFontFace,                   Pid::END_FONT_FACE           },
      { Sid::textLineFontSize,                   Pid::BEGIN_FONT_SIZE         },
      { Sid::textLineFontSize,                   Pid::CONTINUE_FONT_SIZE      },
      { Sid::textLineFontSize,                   Pid::END_FONT_SIZE           },
      { Sid::textLineFontStyle,                  Pid::BEGIN_FONT_STYLE        },
      { Sid::textLineFontStyle,                  Pid::CONTINUE_FONT_STYLE     },
      { Sid::textLineFontStyle,                  Pid::END_FONT_STYLE          },
      { Sid::textLineTextAlign,                  Pid::BEGIN_TEXT_ALIGN        },
      { Sid::textLineTextAlign,                  Pid::CONTINUE_TEXT_ALIGN     },
      { Sid::textLineTextAlign,                  Pid::END_TEXT_ALIGN          },
      { Sid::textLinePlacement,                  Pid::PLACEMENT               },
      { Sid::textLinePosAbove,                   Pid::OFFSET                  },
      };

//---------------------------------------------------------
//   systemTextLineStyle
//---------------------------------------------------------

static const ElementStyle systemTextLineStyle {
//       { Sid::systemTextLineSystemFlag,           Pid::SYSTEM_FLAG             },
      { Sid::systemTextLineFontFace,             Pid::BEGIN_FONT_FACE         },
      { Sid::systemTextLineFontFace,             Pid::CONTINUE_FONT_FACE      },
      { Sid::systemTextLineFontFace,             Pid::END_FONT_FACE           },
      { Sid::systemTextLineFontSize,             Pid::BEGIN_FONT_SIZE         },
      { Sid::systemTextLineFontSize,             Pid::CONTINUE_FONT_SIZE      },
      { Sid::systemTextLineFontSize,             Pid::END_FONT_SIZE           },
      { Sid::systemTextLineFontStyle,            Pid::BEGIN_FONT_STYLE        },
      { Sid::systemTextLineFontStyle,            Pid::CONTINUE_FONT_STYLE     },
      { Sid::systemTextLineFontStyle,            Pid::END_FONT_STYLE          },
      { Sid::systemTextLineTextAlign,            Pid::BEGIN_TEXT_ALIGN        },
      { Sid::systemTextLineTextAlign,            Pid::CONTINUE_TEXT_ALIGN     },
      { Sid::systemTextLineTextAlign,            Pid::END_TEXT_ALIGN          },
      { Sid::systemTextLinePlacement,            Pid::PLACEMENT               },
      { Sid::systemTextLinePosAbove,             Pid::OFFSET                  },
      };

//---------------------------------------------------------
//   TextLineSegment
//---------------------------------------------------------

TextLineSegment::TextLineSegment(Spanner* sp, Score* s, bool system)
   : TextLineBaseSegment(sp, s, ElementFlag::MOVABLE | ElementFlag::ON_STAFF)
      {
      setSystemFlag(system);
      if (systemFlag())
            initElementStyle(&systemTextLineSegmentStyle);
      else
            initElementStyle(&textLineSegmentStyle);
      }

//---------------------------------------------------------
//   propertyDelegate
//---------------------------------------------------------

Element* TextLineSegment::propertyDelegate(Pid pid)
      {
      if (pid == Pid::SYSTEM_FLAG || pid == Pid::RIT_MODE || pid == Pid::RIT_PLAY || pid == Pid::RIT_TARGET_MODE || pid == Pid::RIT_TARGET || pid == Pid::RIT_CURVE || pid == Pid::RIT_START_BPM)
            return static_cast<TextLine*>(spanner());
      return TextLineBaseSegment::propertyDelegate(pid);
      }

//---------------------------------------------------------
//   layout
//---------------------------------------------------------

void TextLineSegment::layout()
      {
      TextLineBaseSegment::layout();
      if (isStyled(Pid::OFFSET))
            roffset() = textLine()->propertyDefault(Pid::OFFSET).toPointF();
      autoplaceSpannerSegment();
      }

//---------------------------------------------------------
//   TextLine
//---------------------------------------------------------

TextLine::TextLine(Score* s, bool system)
   : TextLineBase(s)
      {
      setSystemFlag(system);

      initStyle();

      setBeginText("");
      setContinueText("");
      setEndText("");
      setBeginTextOffset(QPointF(0,0));
      setContinueTextOffset(QPointF(0,0));
      setEndTextOffset(QPointF(0,0));
      setLineVisible(true);

      setBeginHookType(HookType::NONE);
      setEndHookType(HookType::NONE);
      setBeginHookHeight(Spatium(1.5));
      setEndHookHeight(Spatium(1.5));

      initElementStyle(&textLineStyle);

      resetProperty(Pid::BEGIN_TEXT_PLACE);
      resetProperty(Pid::CONTINUE_TEXT_PLACE);
      resetProperty(Pid::END_TEXT_PLACE);
      }

TextLine::TextLine(const TextLine& tl)
   : TextLineBase(tl), _ritMode(tl._ritMode), _ritPlay(tl._ritPlay), _ritTargetMode(tl._ritTargetMode), _ritTarget(tl._ritTarget), _ritCurve(tl._ritCurve), _ritStartBpm(tl._ritStartBpm)
      {
      }

//---------------------------------------------------------
//   initStyle
//---------------------------------------------------------

void TextLine::initStyle()
      {
      if (systemFlag())
            initElementStyle(&systemTextLineStyle);
      else
            initElementStyle(&textLineStyle);
      }

//---------------------------------------------------------
//   write
//---------------------------------------------------------

void TextLine::write(XmlWriter& xml) const
      {
      if (!xml.canWrite(this))
            return;
      if (systemFlag())
            xml.stag(QString("TextLine"), this, QString("system=\"1\""));
      else
            xml.stag(this);
      // other styled properties are included in TextLineBase pids list
      for (auto pid : {Pid::RIT_MODE, Pid::RIT_PLAY, Pid::RIT_TARGET_MODE, Pid::RIT_TARGET, Pid::RIT_CURVE, Pid::RIT_START_BPM}) writeProperty(xml, pid);
      writeProperty(xml, Pid::PLACEMENT);
      writeProperty(xml, Pid::OFFSET);
      TextLineBase::writeProperties(xml);
      xml.etag();
      }

//---------------------------------------------------------
//   read
//---------------------------------------------------------

void TextLine::read(XmlReader& e)
      {
      bool system =  e.intAttribute("system", 0) == 1;
      setSystemFlag(system);
      initStyle();
      TextLineBase::read(e);
      }

//---------------------------------------------------------
//   createLineSegment
//---------------------------------------------------------

bool TextLine::readProperties(XmlReader& e)
      {
      for (auto pid : {Pid::RIT_MODE, Pid::RIT_PLAY, Pid::RIT_TARGET_MODE, Pid::RIT_TARGET, Pid::RIT_CURVE, Pid::RIT_START_BPM})
            if (e.name() == propertyName(pid)) { readProperty(e, pid); return true; }
      return TextLineBase::readProperties(e);
      }

QVariant TextLine::getProperty(Pid id) const
      {
      switch (id) {
            case Pid::RIT_MODE: return _ritMode;
            case Pid::RIT_PLAY: return _ritPlay;
            case Pid::RIT_TARGET_MODE: return _ritTargetMode;
            case Pid::RIT_TARGET: return _ritTarget;
            case Pid::RIT_CURVE: return _ritCurve;
            case Pid::RIT_START_BPM: return _ritStartBpm;
            default: return TextLineBase::getProperty(id);
            }
      }

LineSegment* TextLine::createLineSegment()
      {
      TextLineSegment* seg = new TextLineSegment(this, score(), systemFlag());
      seg->setTrack(track());
      // note-anchored line segments are relative to system not to staff
      if (anchor() == Spanner::Anchor::NOTE)
            seg->setFlag(ElementFlag::ON_STAFF, false);

      if (systemFlag())
            seg->initElementStyle(&systemTextLineSegmentStyle);
      else
            seg->initElementStyle(&textLineSegmentStyle);

      return seg;
      }

//---------------------------------------------------------
//   getTextLinePos
//---------------------------------------------------------

Sid TextLineSegment::getTextLinePos(bool above) const
      {
      if (systemFlag())
            return above ? Sid::systemTextLinePosAbove : Sid::systemTextLinePosBelow;
      else
            return above ? Sid::textLinePosAbove : Sid::textLinePosBelow;
      }

Sid TextLine::getTextLinePos(bool above) const
      {
      if (systemFlag())
            return above ? Sid::systemTextLinePosAbove : Sid::systemTextLinePosBelow;
      else
            return above ? Sid::textLinePosAbove : Sid::textLinePosBelow;
      }

//---------------------------------------------------------
//   getPropertyStyle
//---------------------------------------------------------

Sid TextLineSegment::getPropertyStyle(Pid pid) const
      {
      if (pid == Pid::OFFSET) {
            if (spanner()->anchor() == Spanner::Anchor::NOTE)
                  return Sid::NOSTYLE;
            else
                  return getTextLinePos(spanner()->placeAbove());
            }
      return TextLineBaseSegment::getPropertyStyle(pid);
      }

Sid TextLine::getPropertyStyle(Pid pid) const
      {
      if (pid == Pid::OFFSET) {
            if (anchor() == Spanner::Anchor::NOTE)
                  return Sid::NOSTYLE;
            else
                  return getTextLinePos(placeAbove());
            }
      return TextLineBase::getPropertyStyle(pid);
      }

//---------------------------------------------------------
//   propertyDefault
//---------------------------------------------------------

QVariant TextLine::propertyDefault(Pid propertyId) const
      {
      switch (propertyId) {
            case Pid::RIT_MODE: return false;
            case Pid::RIT_PLAY: return false;
            case Pid::RIT_TARGET_MODE: return 0;
            case Pid::RIT_TARGET: return 80.0;
            case Pid::RIT_CURVE: return 1.0;
            case Pid::RIT_START_BPM: return 0.0;
            case Pid::PLACEMENT:
                  if (systemFlag())
                        return score()->styleV(Sid::textLinePlacement);
                  else
                        return score()->styleV(Sid::systemTextLinePlacement);
            case Pid::BEGIN_TEXT:
            case Pid::CONTINUE_TEXT:
            case Pid::END_TEXT:
                  return "";
            case Pid::LINE_VISIBLE:
                  return true;
            case Pid::BEGIN_TEXT_OFFSET:
            case Pid::CONTINUE_TEXT_OFFSET:
            case Pid::END_TEXT_OFFSET:
                  return QPointF(0,0);
            case Pid::BEGIN_HOOK_TYPE:
            case Pid::END_HOOK_TYPE:
                  return int(HookType::NONE);
            case Pid::BEGIN_TEXT_PLACE:
            case Pid::CONTINUE_TEXT_PLACE:
            case Pid::END_TEXT_PLACE:
                  return int(PlaceText::LEFT);
            case Pid::BEGIN_HOOK_HEIGHT:
            case Pid::END_HOOK_HEIGHT:
                  return Spatium(1.5);
            default:
                  return TextLineBase::propertyDefault(propertyId);
            }
      }

//---------------------------------------------------------
//   setProperty
//---------------------------------------------------------

bool TextLine::setProperty(Pid id, const QVariant& v)
      {
      if (((id == Pid::RIT_PLAY && _ritMode) || (id == Pid::RIT_MODE && _ritPlay)) && v.toBool() && score()) {
            const auto& spanners = score()->spannerMap().map();
            const bool registered = std::any_of(spanners.begin(), spanners.end(), [this](const auto& item) { return item.second == this; });
            if (registered && !TempoExpression::conflict(score(), tick().ticks(), tick2().ticks(), this).isEmpty()) return false;
            }
      switch (id) {
            case Pid::RIT_MODE: _ritMode = v.toBool(); break;
            case Pid::RIT_PLAY: _ritPlay = v.toBool(); break;
            case Pid::RIT_START_BPM:
                  if (!std::isfinite(v.toDouble()) || v.toDouble() < 0 || v.toDouble() > 999 || (v.toDouble() > 0 && v.toDouble() < 5)) return false;
                  _ritStartBpm = v.toDouble(); break;
            case Pid::RIT_TARGET_MODE:
                  if (v.toInt() < 0 || v.toInt() > 1) return false;
                  _ritTargetMode = v.toInt(); break;
            case Pid::RIT_TARGET:
                  if (!std::isfinite(v.toDouble()) || v.toDouble() < 1 || v.toDouble() > 999) return false;
                  _ritTarget = v.toDouble(); break;
            case Pid::RIT_CURVE:
                  if (!std::isfinite(v.toDouble()) || v.toDouble() < 0.1 || v.toDouble() > 8) return false;
                  _ritCurve = v.toDouble(); break;
            case Pid::PLACEMENT:
                  setPlacement(Placement(v.toInt()));
                  break;
            default:
                  if (!TextLineBase::setProperty(id, v)) return false;
                  break;
            }
      triggerLayout();
      if (score() && (id == Pid::RIT_MODE || id == Pid::RIT_PLAY || ritPlay())) {
            const auto& spanners = score()->spannerMap().map();
            bool registered = std::any_of(spanners.begin(), spanners.end(), [this](const auto& item) { return item.second == this; });
            if (registered) {
                  score()->masterScore()->_tempoExpressionsPresent = true;
                  score()->requestTempoMapRebuild(); score()->setPlaylistDirty();
                  }
            }
      return true;
      }

// Exact tempo-curve endpoints use notation only as a layout context, like
// native pedal controllers. Disabling playback keeps the saved curve range.
QPointF TextLine::linePos(Grip grip, System** system) const
      {
      if (!ritEnabled()) return TextLineBase::linePos(grip, system);
      const Fraction point = grip == Grip::START ? tick() : tick2();
      Measure* measure = score()->tick2measureMM(point);
      if (!measure && score()->lastMeasure() && point == score()->lastMeasure()->endTick()) measure = score()->lastMeasure();
      if (!measure) { *system = nullptr; return {}; }
      const Fraction local = point - measure->tick();
      Fraction before(0,1), after = measure->ticks() * (measure->isMMRest() ? measure->mmRestCount() : 1);
      qreal x0 = 0, x1 = measure->width();
      for (auto segment = measure->first(SegmentType::ChordRest); segment; segment = segment->next(SegmentType::ChordRest)) {
            if (segment->rtick() > local) { after = segment->rtick(); x1 = segment->x(); break; }
            before = segment->rtick(); x0 = segment->x();
            }
      *system = measure->system();
      const qreal fraction = after > before ? qreal((local - before).ticks()) / (after - before).ticks() : 0;
      return QPointF(measure->pos().x() + x0 + (x1 - x0) * fraction, 0);
      }

//---------------------------------------------------------
//   undoChangeProperty
//---------------------------------------------------------

void TextLine::undoChangeProperty(Pid id, const QVariant& v, PropertyFlags ps)
      {
      if (id == Pid::SYSTEM_FLAG) {
            score()->undo(new ChangeTextLineProperty(this, v));
            for (SpannerSegment* s : spannerSegments()) {
                  score()->undo(new ChangeTextLineProperty(s, v));
                  triggerLayout();
                  }
            MuseScoreCore::mscoreCore->updateInspector();
            return;
            }
      TextLineBase::undoChangeProperty(id, v, ps);
      }

//---------------------------------------------------------
//   layoutSystem
//    layout spannersegment for system
//---------------------------------------------------------

SpannerSegment* TextLine::layoutSystem(System* system)
      {
      if (ritPlay()) score()->masterScore()->_tempoExpressionsPresent = true;
      TextLineSegment* tls = toTextLineSegment(TextLineBase::layoutSystem(system));

      if (tls->spanner()) {
            for (SpannerSegment* ss : tls->spanner()->spannerSegments()) {
                  ss->setFlag(ElementFlag::SYSTEM, systemFlag());
                  ss->setTrack(systemFlag() ? 0 : track());
            }
            tls->spanner()->setFlag(ElementFlag::SYSTEM, systemFlag());
            tls->spanner()->setTrack(systemFlag() ? 0 : track());
            }

      return tls;
      }

}     // namespace Ms
