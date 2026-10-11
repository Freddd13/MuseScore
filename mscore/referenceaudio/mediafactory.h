// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QObject>
#include <QtPlugin>
class KumoMediaFactory {
public:
 virtual ~KumoMediaFactory() {}
 virtual QObject* create(QObject* parent)=0;
};
#define KUMO_MEDIA_FACTORY_IID "org.freddd13.kumo.ReferenceMedia/1.0"
Q_DECLARE_INTERFACE(KumoMediaFactory,KUMO_MEDIA_FACTORY_IID)
