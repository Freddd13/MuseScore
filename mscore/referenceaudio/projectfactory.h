// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#pragma once
#include "referencehost.h"
#include <QObject>
#include <QtPlugin>
class KumoMediaProjectFactory {
public:
      virtual ~KumoMediaProjectFactory() {}
      virtual QObject* createProject(QObject*,const KumoReferenceHost&)=0;
};
#define KUMO_MEDIA_PROJECT_IID "org.freddd13.kumo.ReferenceProject/1.0"
Q_DECLARE_INTERFACE(KumoMediaProjectFactory,KUMO_MEDIA_PROJECT_IID)
