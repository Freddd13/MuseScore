// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QVariantMap>
#include <QString>
#include <atomic>
namespace Ms { QVariantMap readPerformanceMidi(const QString&,const std::atomic_bool&); }
