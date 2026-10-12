// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QString>
class QWidget;
namespace Ms {
class Element;
void showTapMarkDialog(Element*);
QString tapTempoText(const QString& original, double quarterBpm, bool appendNumber = true);
}
