// Copyright (C) 2026 Freddd13 and contributors; GPL version 2, see LICENCE.GPL.
#ifndef MS_PERFORMANCESELECTION_H
#define MS_PERFORMANCESELECTION_H
#include <QList>
namespace Ms {
class Score;
class Note;
namespace PerformanceSelection {
// GUI-only selection notifications; no audition, transport, score command or undo.
bool apply(Score*, const QList<Note*>&, Note* focus = nullptr);
}
}
#endif
