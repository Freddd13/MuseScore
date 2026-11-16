// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QPointF>
#include <QVariant>

namespace Ms {
class Element;
class Measure;
class Score;
class Segment;
class System;

enum class EngravingTool { Symbol, Beat, Measure, Staff, System };

struct EngravingTarget {
    EngravingTool tool = EngravingTool::Symbol;
    Element* symbol = nullptr;
    Segment* segment = nullptr;
    Measure* measure = nullptr;
    Measure* rangeEnd = nullptr;
    System* system = nullptr;
    int staff = -1;

    explicit operator bool() const;
};

bool engravingMovableSymbol(const Element*);
EngravingTarget engravingTarget(Score*, EngravingTool);
QVariant engravingValue(const EngravingTarget&);
bool changeEngravingValue(const EngravingTarget&, const QVariant& value, bool restoreAutomatic = false);
}
