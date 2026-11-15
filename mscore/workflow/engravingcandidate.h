// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QVariantMap>

namespace Ms {
class Score;

// The caller owns the undo command. All addresses and values are resolved
// before the score is changed. A successful result reports old and new values.
QVariantMap applyEngravingChanges(Score*, const QVariantMap& scope, const QVariantList& operations,
                                 const QVariantList& expected = {});
QVariantMap renderArrangementImage(Score*, const QVariantMap& scope, const QString& snapshot,
                                   const QVariantMap& request);
QVariantMap saveEngravingCopy(Score* original, Score* candidate, const QVariantMap& request);
}
