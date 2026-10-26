// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#ifndef KUMO_TEMPORARY_SCORE_H
#define KUMO_TEMPORARY_SCORE_H

#include "libmscore/score.h"
#include "libmscore/undo.h"
#include <memory>

namespace Ms {
// For a detached, independently loaded/cloned score used only by a workflow.
// MasterScore does not own Movements. Undo-owned elements must be released
// while the score and its style are still alive.
struct TemporaryScoreDeleter {
      void operator()(MasterScore* score) const {
            if (!score)
                  return;
            auto movements = score->movements();
            movements->undo()->clearAfterLoad();
            delete score;
            delete movements;
            }
      };
using TemporaryScore = std::unique_ptr<MasterScore, TemporaryScoreDeleter>;
}

#endif
