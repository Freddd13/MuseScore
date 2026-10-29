// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#pragma once
#include <QPointF>
#include <QHash>
#include "sym.h"
namespace Ms {
class Symbol;
class Score;
class Note;
class Segment;
namespace HandBracket {
struct Style { double horizontal=2,vertical=-1,width=.12;bool text=false; };
Style factoryDefaults(SymId);
Style defaults(SymId);
bool setDefaults(SymId,const Style&);
QPointF position(const Symbol*);
void layout(Segment*);
QHash<const Note*,int> assignments(const Score*,bool foldTiedChanges=true);
}
}
