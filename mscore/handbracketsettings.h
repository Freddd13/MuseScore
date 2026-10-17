// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#pragma once
class QWidget;
class QGridLayout;
namespace Ms {class Inspector;class PalettePanel;class Symbol;
void loadHandBracketDefaults();
void addHandBracketDefaultControls(QWidget*,QGridLayout*,Inspector*);
void completeHandBracketPalette(PalettePanel*);
void addHandTextControls(QWidget*,QGridLayout*,Inspector*);
void syncHandTextControls(QWidget*,Symbol*);
}
