// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QMap>
#include <QScopedValueRollback>
#include <QVariant>
#include "property.h"

namespace Ms {

class ScoreElement;
using SavePropertyOverrides = QMap<const ScoreElement*, QMap<Pid, QVariant>>;

// Native serialization runs synchronously, including linked parts and
// regional fragments. Leave the playing score untouched.
class ScopedSavePropertyOverrides {
      inline static thread_local const SavePropertyOverrides* _active = nullptr;
      QScopedValueRollback<const SavePropertyOverrides*> _restore;

   public:
      explicit ScopedSavePropertyOverrides(const SavePropertyOverrides& properties)
         : _restore(_active, properties.isEmpty() ? nullptr : &properties) {}

      static QVariant value(const ScoreElement* element, Pid pid)
            {
            if (!_active)
                  return {};
            auto entry = _active->constFind(element);
            return entry == _active->cend() ? QVariant() : entry->value(pid);
            }
      };

}
