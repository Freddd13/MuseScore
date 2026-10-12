// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace Ms {
class Measure;
class ChordRest;
namespace InputNotation {
void setTiedBeams(bool);
void setCoherentStems(bool);
bool tiedBeams();
bool coherentStems();
bool joinTiedChords(const ChordRest* previous, const ChordRest* current);
void coordinateTieStems(Measure*);
}
}
