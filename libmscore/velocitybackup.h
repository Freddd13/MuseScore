// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#pragma once
#include <QPair>
#include <QList>
namespace Ms {
class Note;
class Score;
class XmlReader;
class XmlWriter;
namespace VelocityBackup {
QPair<int, int> original(const Note*);
QList<Note*> notes(Score*);
void remember(Score*, Note*, bool replace = false);
void copy(const Note* source, const Note* destination);
void forget(const Note*);
bool read(Note*, XmlReader&);
void write(const Note*, XmlWriter&);
}
}
