// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include <functional>
class QWidget;
namespace Ms {
class Score;
void showPerformanceSettings(Score*, QWidget*, std::function<void()> saved);
}
