// Copyright (C) 2026 Freddd13 and contributors; GPL version 2, see LICENCE.GPL.
#include "mscore/scoreview.h"
#include "libmscore/score.h"
#include <QPainter>
namespace Ms {
void ScoreView::paintTopChordPreview(QPainter& painter)
      {
      if(!score()||score()->printing()||fotoMode())return;
      const QRectF visible=matrix().inverted().mapRect(QRectF(rect()));
      const auto protectedArea=previewEditBounds();
      painter.save();painter.setTransform(matrix());painter.setClipping(false);
      _notePreviewLayers.forEachChord([&](const NotePreviewEntry& entry,bool active){
            if(!entry.onTop)return;const QRectF box=entry.chordBox.translated(entry.anchor);
            if(!box.intersects(visible)||(entry.avoidScoreContent&&box.intersects(protectedArea)))return;
            painter.save();painter.translate(box.topLeft());
            if(entry.chordMask){painter.setPen(Qt::NoPen);painter.setBrush(active?entry.highlightBackground:QColor(255,255,255,235));
                  painter.drawRoundedRect(QRectF(QPointF(),box.size()),entry.spatium*.18,entry.spatium*.18);}
            painter.setPen(active?entry.highlightColor:entry.chordColor);
            if(!entry.chordPicture.isNull()){painter.save();painter.translate(entry.primaryBox.topLeft());painter.drawPicture(QPointF(),active?entry.activeChordPicture:entry.chordPicture);painter.restore();}
            else {painter.setFont(entry.chordFont);painter.drawText(entry.primaryBox,Qt::AlignCenter,entry.chord);}
            painter.setFont(entry.degreeFont);painter.drawText(entry.secondaryBox,Qt::AlignCenter,entry.degree);painter.restore();
            });
      painter.restore();
      }
}
