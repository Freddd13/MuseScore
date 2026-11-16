// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QVariantMap>

class QPainter;

namespace Ms {
class MuseScore;
class Score;
class ScoreView;
void openEngravingTools(MuseScore*);
void paintEngravingHandle(ScoreView*, QPainter&);
void refreshEngravingSelection(MuseScore*);
bool cancelEngravingDrag(ScoreView*);
QVariantMap engravingLayout(Score*, const QVariantMap& scope);
}
