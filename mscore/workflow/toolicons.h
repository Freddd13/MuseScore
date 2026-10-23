// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include <QIcon>
#include <QString>
class QAbstractButton;
class QQmlEngine;
namespace Ms {
QIcon componentToolIcon(const QString& key);
void setComponentToolIcon(QAbstractButton* button, const QString& key);
#ifdef SCRIPT_INTERFACE
void installComponentToolIcons(QQmlEngine* engine);
#endif
}
