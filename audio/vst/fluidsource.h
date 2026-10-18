// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "plugin.h"
namespace Ms {
namespace VstHost {
std::unique_ptr<Plugin> createFluidSource(const Descriptor &, QString *);
}
} // namespace Ms
