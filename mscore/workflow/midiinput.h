// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QVariantMap>
#include <QString>
#include <QByteArray>
#include <atomic>
namespace Ms
{
QVariantMap readPerformanceMidi(const QString&, const std::atomic_bool&);
QVariantMap readPerformanceMidiData(const QByteArray&, const std::atomic_bool&);
} // namespace Ms
