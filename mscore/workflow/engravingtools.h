// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QVariantMap>

namespace Ms {
class MuseScore;
class Score;
void openEngravingTools(MuseScore*);
QVariantMap engravingLayout(Score*, const QVariantMap& scope);
}
