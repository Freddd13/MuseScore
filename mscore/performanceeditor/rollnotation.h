// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QVariantList>
#include <QVariantMap>
#include <atomic>

namespace Ms {
// Input and output contain values only; usable by preview and score
// application.
int rollQuantizeTick(int tick, int origin, int quantum);
int rollGridQuantum(int tick, int quantum, bool triplet, int track,
                    const QVariantList &tuplets);
int rollSnapInScore(int tick, int quantum, bool triplet, int track,
                    int fallback, const QVariantList &measures,
                    const QVariantList &tuplets);
QVariantMap
rollAffectedScope(const QVariantList &current, const QVariantList &previous,
                  const QVariantMap &scope, const QVariantList &measures,
                  const QVariantList &tuplets, const QVariantList &tracks);
QVariantMap
buildRollEdit(const QVariantList &current, const QVariantList &previous,
              const QVariantMap &scope, const QVariantList &measures,
              const QVariantList &tuplets, const QVariantMap &settings,
              bool local, const std::atomic_bool *cancel = nullptr);
QVariantMap buildRollNotation(const QVariantList &notes,
                              const QVariantMap &scope,
                              const QVariantList &measures,
                              const QVariantList &tuplets,
                              const QVariantMap &settings,
                              const std::atomic_bool *cancel = nullptr);
} // namespace Ms
