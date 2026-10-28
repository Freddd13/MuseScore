// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
class QPointF;
namespace Ms {
class PalettePanel;struct PaletteTree;
class Element;
class TextLine;
void applyPlayableTempoTextStyle(TextLine*,bool styledOnly=false);
bool usesPlayableTempoTextStyle(const TextLine*);
void appendPlayableTempoSymbols(PalettePanel*);
void deduplicatePlayableTempoSymbols(PaletteTree*);
bool isPlayableTempoSymbol(const Element*);
bool applyPlayableTempoSymbol(const Element*);
bool applyPlayableTempoDrop(const Element*, const QPointF&);
}
