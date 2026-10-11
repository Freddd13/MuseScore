// SPDX-License-Identifier: GPL-2.0-or-later
#include "referencefactory.h"
#include "../referenceaudio/mediafactory.h"
#include <QCoreApplication>
#include <QPluginLoader>
#include <QDir>
namespace Ms {
namespace {
class MissingMedia : public QObject {
 Q_OBJECT
 Q_PROPERTY(bool available READ available CONSTANT)
 Q_PROPERTY(QString status READ status CONSTANT)
 QString _error;
public:
 MissingMedia(QObject* p,QString error):QObject(p),_error(error){}
 bool available()const{return false;}
 QString status()const{return tr("Reference audio module unavailable: %1").arg(_error);}
};
}
QObject* createReferenceMedia(QObject* parent) {
 static QPluginLoader* loader=nullptr;
 if(!loader) {
  loader=new QPluginLoader(QDir(QCoreApplication::applicationDirPath()).filePath("kumo_referenceaudio"),qApp);
  loader->setLoadHints(QLibrary::PreventUnloadHint);
 }
 auto factory=qobject_cast<KumoMediaFactory*>(loader->instance());
 return factory?factory->create(parent):new MissingMedia(parent,loader->errorString());
}
}
#include "referencefactory.moc"
