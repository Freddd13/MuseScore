// SPDX-License-Identifier: GPL-2.0-or-later
#include "mediafactory.h"
#include "mediasession.h"
#include "projectfactory.h"
#include "mediaproject.h"
class ReferenceAudioPlugin:public QObject,public KumoMediaFactory,public KumoMediaProjectFactory {
 Q_OBJECT
 Q_PLUGIN_METADATA(IID KUMO_MEDIA_FACTORY_IID)
 Q_INTERFACES(KumoMediaFactory KumoMediaProjectFactory)
public:QObject* create(QObject* p)override{return new ReferenceMediaSession(p);}
 QObject* createProject(QObject* p,const KumoReferenceHost& host)override{return new ReferenceMediaProject(p,host);}
};
#include "mediaplugin.moc"
