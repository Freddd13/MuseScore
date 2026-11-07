// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include <QVector>
namespace Ms
{
class Note;
class Score;
class XmlReader;
class XmlWriter;
namespace PerformanceTiming
{
struct Adjustment {
    int index = 0, count = 1, pitch = 0, onset = 0, release = 0;
    bool operator==(const Adjustment& other) const;
};
using Adjustments = QVector<Adjustment>;
Adjustments values(const Note*);
bool matches(const Note*, const Adjustment&);
Adjustment value(const Note*, int index);
void apply(const Note*, int index, int& onset, int& release);
int lookahead(Score*);
bool change(Score*, Note*, const Adjustments&, bool linked = true);
void copy(const Note* source, const Note* destination);
void forget(const Note*);
void write(const Note*, XmlWriter&);
bool read(Note*, XmlReader&);
} // namespace PerformanceTiming
} // namespace Ms
