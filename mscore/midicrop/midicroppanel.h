// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QWidget>
#include <QMap>
#include <functional>
#include "audio/exports/midigate.h"
class QCheckBox;
namespace Ms {
class Score;
class MidiCropPanel : public QWidget {
      Q_OBJECT
      QCheckBox* _enabled;
      QCheckBox* _original;
      MidiGateOptions _gate;
      std::function<QList<Score*>()> _scores;
      QMap<Score*,QSet<QString>> _exclusions;
      QMap<Score*,QSet<int>> _staves;
      void settings();
      void compare();
   public:
      MidiCropPanel(QWidget*,std::function<QList<Score*>()>);
      bool enabled() const;
      bool includeOriginal() const;
      void clearSessions() { _exclusions.clear(); _staves.clear(); }
      void savePreferences() const;
      MidiExportOptions options(Score*) const;
      bool write(Score*,const QString&,const QString& original);
      };
}
