// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QByteArray>
#include <QVariantMap>
namespace Ms {
QByteArray encodeVstPreset(const QByteArray &state, const QVariantMap &plugin);
QByteArray readVstPreset(const QString &path, const QVariantMap &plugin, bool *wrapped,
                         QString *error);
} // namespace Ms
