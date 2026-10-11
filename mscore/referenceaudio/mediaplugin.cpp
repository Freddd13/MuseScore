// SPDX-License-Identifier: GPL-2.0-or-later
#include "mediafactory.h"
#include "mediasession.h"
class ReferenceAudioPlugin:public QObject,public KumoMediaFactory {
 Q_OBJECT
 Q_PLUGIN_METADATA(IID KUMO_MEDIA_FACTORY_IID)
 Q_INTERFACES(KumoMediaFactory)
public:QObject* create(QObject* p)override{return new ReferenceMediaSession(p);}
};
#include "mediaplugin.moc"
