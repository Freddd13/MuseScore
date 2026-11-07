// SPDX-License-Identifier: GPL-2.0-or-later
#include "mediafactory.h"
#include "mediaexportfactory.h"
#include "mediaexport.h"
#include "mediasession.h"
#include "projectfactory.h"
#include "mediaproject.h"
#include "spectrumsession.h"
#include "spectrumfactory.h"
class ReferenceAudioPlugin:public QObject,public KumoMediaFactory,public KumoMediaProjectFactory,public KumoSpectrumFactory,public KumoMediaExportFactory {
 Q_OBJECT
 Q_PLUGIN_METADATA(IID KUMO_MEDIA_FACTORY_IID)
 Q_INTERFACES(KumoMediaFactory KumoMediaProjectFactory KumoSpectrumFactory KumoMediaExportFactory)
public:QObject* createAudioExport(QObject* p)override{return new ReferenceAudioExport(p);}
 QObject* createSpectrum(QObject* p)override{return new ReferenceSpectrumSession(p);}
 QObject* create(QObject* p)override{return new ReferenceMediaSession(p);}
 QObject* createProject(QObject* p,const KumoReferenceHost& host)override{return new ReferenceMediaProject(p,host);}
};
#include "mediaplugin.moc"
