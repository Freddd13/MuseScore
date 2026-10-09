// Copyright (C) 2026 Freddd13 and contributors; GPL version 2, see LICENCE.GPL.
#ifndef MS_PERFORMANCESETTINGS_H
#define MS_PERFORMANCESETTINGS_H
#include <QColor>
#include <array>
class QWidget;
namespace Ms {
struct PerformanceAppearance {
      enum Role { Background, NaturalRow, AccidentalRow, Grid, Text, Selection, Playhead, Preview, Tempo, Pedal, RoleCount };
      std::array<QColor, RoleCount> colors;
      std::array<QColor, 4> gradient, voices;
      std::array<QColor, 8> staves;
      std::array<int, 4> stops {{1, 43, 85, 127}};
      int mode = 0;
      int velocityWidth = 3;
      PerformanceAppearance();
      QColor noteColor(int midiVelocity, int track) const;
      void load();
      void save() const;
      bool edit(QWidget* parent);
      };
}
#endif
