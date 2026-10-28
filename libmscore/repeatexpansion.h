// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "score.h"
#include <QPointer>

namespace Ms {
// A temporary rendering/preview policy; the user's ordinary playback choice survives.
class RepeatExpansionScope {
      QPointer<MasterScore> _score;
      bool _previous;
   public:
      RepeatExpansionScope(MasterScore* score, bool expand)
            : _score(score), _previous(score && score->expandRepeats()) {
            if (_score) _score->setExpandRepeats(expand);
            }
      ~RepeatExpansionScope() { if (_score) _score->setExpandRepeats(_previous); }
      RepeatExpansionScope(const RepeatExpansionScope&) = delete;
      RepeatExpansionScope& operator=(const RepeatExpansionScope&) = delete;
      };
}
