// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
class QMenu;
namespace Ms { QObject* createReferenceMedia(QObject* parent); QObject* referenceProject(); void addReferenceActions(QMenu*); }
