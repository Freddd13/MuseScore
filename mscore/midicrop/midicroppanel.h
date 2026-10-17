// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QWidget>
#include <QMap>
#include <functional>
#include "audio/exports/midigate.h"
#include "libmscore/score.h"
class QCheckBox;
namespace Ms {
class Score;
class MidiCropPanel : public QWidget {
      Q_OBJECT
      QCheckBox* _enabled;
      QCheckBox* _original;
      MidiGateOptions _gate;
      int _fromMeasure=0,_untilMeasure=0;
      std::function<QList<Score*>()> _scores;
      QMap<Score*,QSet<QString>> _exclusions;
      QMap<Score*,QMap<QString,int>> _releaseOverrides;
      QMap<Score*,ScoreContentState> _releaseStates;
      QMap<Score*,QSet<int>> _staves;
      QMap<Score*,QSet<int>> _tracks,_protectedTracks;
      void settings();
      void compare();
      void voiceSelection(Score*);
   public:
      MidiCropPanel(QWidget*,std::function<QList<Score*>()>);
      bool enabled() const;
      bool includeOriginal() const;
      void clearSessions() { _exclusions.clear(); _releaseOverrides.clear(); _releaseStates.clear(); _staves.clear(); _tracks.clear(); _protectedTracks.clear(); }
      void savePreferences() const;
      MidiExportOptions options(Score*) const;
      bool write(Score*,const QString&,const QString& original);
      };
}
