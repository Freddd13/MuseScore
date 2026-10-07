// Copyright (C) 2026 Freddd13 and contributors; GPL version 2, see LICENCE.GPL.
#include "performanceselection.h"
#include "libmscore/score.h"
#include "libmscore/select.h"
#include "mscore/musescore.h"
namespace Ms {
bool PerformanceSelection::apply(Score* score, const QList<Note*>& notes, Note* focus)
      {
      if (!score || score->noteEntryMode() || !score->selectNoteList(notes, focus)) return false;
      score->update();
      if (mscore && mscore->currentScore() == score) {
            score->setSelectionChanged(false);
            mscore->selectionChanged(score->selection().state());
            }
      return true;
      }
}
