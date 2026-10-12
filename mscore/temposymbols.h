// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
class QPointF;
namespace Ms {
class PalettePanel;
class Element;
void appendPlayableTempoSymbols(PalettePanel*);
bool isPlayableTempoSymbol(const Element*);
bool applyPlayableTempoSymbol(const Element*);
bool applyPlayableTempoDrop(const Element*, const QPointF&);
}
