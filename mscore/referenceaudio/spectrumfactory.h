// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include <QObject>
#include <QtPlugin>
class KumoSpectrumFactory {
  public:
    virtual ~KumoSpectrumFactory() {}
    virtual QObject *createSpectrum(QObject *) = 0;
};
#define KUMO_SPECTRUM_FACTORY_IID "org.freddd13.kumo.ReferenceSpectrum/1.0"
Q_DECLARE_INTERFACE(KumoSpectrumFactory, KUMO_SPECTRUM_FACTORY_IID)
