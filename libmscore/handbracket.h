// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#pragma once
#include <QPointF>
#include "sym.h"
namespace Ms {
class Symbol;
namespace HandBracket {
struct Style { double horizontal=2,vertical=-1,width=.12;bool text=false; };
Style defaults(SymId);
bool setDefaults(SymId,const Style&);
QPointF position(const Symbol*);
}
}
