// Personal playback timing; score ticks and written durations remain unchanged.
#ifndef MS_PLAYBACKTIMING_H
#define MS_PLAYBACKTIMING_H
#include <QList>
namespace Ms {
class Chord;
class Arpeggio;
class Note;
class Score;
class EventMap;
class NPlayEvent;
namespace PlaybackTiming {
Arpeggio* arpeggio(const Chord*);
QList<Note*> arpeggioNotes(const Arpeggio*);
double intervalMs(const Arpeggio*);
// Signed time before score tick zero uses the initial tempo, not a repeat lookup.
double time(const Score*, int utick);
int startTick(const EventMap&, int target, const Score*);
bool belongsToStart(const NPlayEvent&, int target);
int exportOffset(const EventMap&);
}
}
#endif
