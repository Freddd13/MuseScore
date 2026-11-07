// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include <QtPlugin>
class QObject;
class KumoMediaExportFactory
{
  public:
    virtual ~KumoMediaExportFactory() = default;
    virtual QObject* createAudioExport(QObject* parent) = 0;
};
#define KUMO_MEDIA_EXPORT_FACTORY_IID "org.freddd13.musescore.ReferenceAudioExport/1.0"
Q_DECLARE_INTERFACE(KumoMediaExportFactory, KUMO_MEDIA_EXPORT_FACTORY_IID)
