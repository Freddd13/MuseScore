// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "velocitybackup.h"
#include "note.h"
#include "chord.h"
#include "measure.h"
#include "segment.h"
#include "score.h"
#include "undo.h"
#include "xml.h"
#include <QHash>
#include <QGlobalStatic>
namespace Ms {
namespace VelocityBackup {
namespace {
using Value = QPair<int, int>;
using Values = QHash<const Note*, Value>;
Q_GLOBAL_STATIC(Values, storedValues)
Values& values()
      {
      return *storedValues;
      }
void store(const Note* note, const Value& value)
      {
      if (storedValues.isDestroyed())
            return;
      if (value.first < 0)
            values().remove(note);
      else
            values().insert(note, value);
      }
class Change : public UndoCommand {
      const Note* _note;
      Value _next;
      void flip(EditData*) override
            {
            const auto previous = original(_note);
            store(_note, _next);
            _next = previous;
            }
   public:
      Change(const Note* note, const Value& value) : _note(note), _next(value) {}
      const char* name() const override { return "ChangeOriginalVelocity"; }
      };
}
QPair<int, int> original(const Note* note)
      {
      if (storedValues.isDestroyed())
            return {-1, 0};
      return values().value(note, {-1, 0});
      }
QList<Note*> notes(Score* score)
      {
      QList<Note*> result;
      if (!score || !storedValues.exists() || storedValues.isDestroyed() || values().isEmpty())
            return result;
      // Undo and temporary notes may outlive their old parents. Start from the
      // score's live chords, then look up saved values without following those parents.
      for (auto segment = score->firstSegment(SegmentType::ChordRest); segment;
           segment = segment->next1(SegmentType::ChordRest)) {
            for (int track = 0; track < score->ntracks(); ++track) {
                  const auto element = segment->element(track);
                  if (!element || !element->isChord())
                        continue;
                  const auto chord = toChord(element);
                  auto chords = chord->graceNotes();
                  chords.append(chord);
                  for (auto current : chords)
                        for (auto note : current->notes())
                              if (values().contains(note))
                                    result.append(note);
                  }
            }
      return result;
      }
void remember(Score* score, Note* note, bool replace)
      {
      const Value next{int(note->veloType()), note->veloOffset()};
      const auto previous = original(note);
      if ((!replace && previous.first >= 0) || previous == next)
            return;
      score->undo(new Change(note, next));
      }
void copy(const Note* source, const Note* destination)
      {
      store(destination, original(source));
      }
void forget(const Note* note)
      {
      if (storedValues.exists() && !storedValues.isDestroyed())
            values().remove(note);
      }
void write(const Note* note, XmlWriter& xml)
      {
      const auto value = original(note);
      if (value.first >= 0)
            xml.tag("kumoOriginalVelocity", QString("%1:%2").arg(value.first).arg(value.second));
      }
bool read(Note* note, XmlReader& xml)
      {
      if (xml.name() != "kumoOriginalVelocity")
            return false;
      const auto fields = xml.readElementText().split(':');
      if (fields.size() != 2)
            return true;
      bool typeOk = false, rawOk = false;
      const int type = fields[0].toInt(&typeOk), raw = fields[1].toInt(&rawOk);
      const bool relative = type == int(Note::ValueType::OFFSET_VAL);
      const bool absolute = type == int(Note::ValueType::USER_VAL);
      if (typeOk && rawOk && ((relative && raw >= -100 && raw <= 12600) ||
                              (absolute && raw >= 0 && raw <= 127)))
            store(note, {type, raw});
      return true;
      }
}
}
