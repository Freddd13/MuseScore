// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include <QVariantMap>
#include <atomic>
namespace KumoAudio {
QVariantMap estimateTempo(const QVariantMap &, const std::atomic_bool &);
}
