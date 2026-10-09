// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#ifndef MS_PLAYBACKENVELOPE_H
#define MS_PLAYBACKENVELOPE_H
#include <QVariant>
#include <QMap>
#include <QStringRef>
#include <initializer_list>
#include "property.h"
namespace Ms {
class Element;
class Note;
class NoteEvent;
class XmlWriter;
class XmlReader;

// Stored notation parameters; factors on NoteEvent are derived, never saved.
class PlaybackEnvelope {
      bool _ornament;
      int _mode = 0; // legacy, soft, crescendo, diminuendo, custom
      double _start = 100, _end, _curve = 1, _alternate;
   public:
      explicit PlaybackEnvelope(bool ornament = false) : _ornament(ornament), _end(ornament ? 100 : 90), _alternate(ornament ? 15 : 5) {}
      int mode() const { return _mode; }
      bool operator==(const PlaybackEnvelope&) const;
      double factor(double progress, bool first, bool alternate) const;
      static std::initializer_list<Pid> properties();
      static bool handles(Pid);
      QVariant property(Pid) const;
      QVariant defaultProperty(Pid) const;
      bool setProperty(Pid, const QVariant&);
      static bool read(Element*, XmlReader&);
      static void write(const Element*, XmlWriter&);
      static QMap<Pid,QVariant> preset(int mode, bool ornament);
      static const Note* eventSource(const Note* owner, const NoteEvent&);
      static double eventFactor(const Note* owner, const NoteEvent&);
      };
}
#endif
