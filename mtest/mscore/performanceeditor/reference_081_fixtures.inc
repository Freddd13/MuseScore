// GPL-2.0-or-later. Small shared helpers for the native reference-panel operations.
static QObject* referenceMedia081(QmlPlugin* root) {
    auto value=root->property("media");return value.canConvert<QJSValue>()?value.value<QJSValue>().toQObject():value.value<QObject*>();
}
static QString referenceWave081(const QString& path) {
    QFile file(path);if(!file.open(QIODevice::WriteOnly))return {};
    const int rate=22050,frames=rate*4;QDataStream data(&file);data.setByteOrder(QDataStream::LittleEndian);
    data.writeRawData("RIFF",4);data<<quint32(36+frames*2);data.writeRawData("WAVEfmt ",8);
    data<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(rate)<<quint32(2*rate)<<quint16(2)<<quint16(16);
    data.writeRawData("data",4);data<<quint32(frames*2);
    for(int i=0;i<frames;++i)data<<qint16(4000*std::sin(2*3.14159265358979323846*440*i/rate));
    return path;
}
static QmlPlugin* referenceOpen081(const QString& path) {
    showReferencePanel(true);QmlPlugin* root=nullptr;
    for(auto window:QGuiApplication::allWindows())if(auto view=qobject_cast<QQuickView*>(window))if(auto panel=qobject_cast<QmlPlugin*>(view->rootObject()))if(panel->menuPath()=="Plugins.Reference Player")root=panel;
    if(!root)return nullptr;
    if(!QMetaObject::invokeMethod(root,"setPanelFloating",Q_ARG(bool,true))||!QMetaObject::invokeMethod(root,"resizePanel",Q_ARG(int,920),Q_ARG(int,680)))return nullptr;
    if(!QMetaObject::invokeMethod(root,"openAudio",Q_ARG(QVariant,QUrl::fromLocalFile(path).toString())))return nullptr;
    return root;
}
struct ReferencePanel081 {
    QPointer<QmlPlugin> root;
    ~ReferencePanel081(){if(root)root->disposePanelForReload();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}
};
static bool referenceClick081(QmlPlugin* root,const char* name) {
    auto item=root->findChild<QQuickItem*>(name);if(!item||!item->isVisible()||!item->isEnabled()||!root->window())return false;
    auto point=[&]{return item->mapToScene(QPointF(item->width()/2,item->height()/2));};
    const QRectF bounds(8,8,root->window()->width()-16,root->window()->height()-16);
    if(!bounds.contains(point())){
        auto scroll=root->findChild<QQuickItem*>("referencePanelScroll");
        auto body=scroll?scroll->property("contentItem").value<QObject*>():nullptr;
        if(!body)return false;
        const double change=point().y()<bounds.top()?point().y()-bounds.top():point().y()-bounds.bottom();
        body->setProperty("contentY",qBound(0.,body->property("contentY").toDouble()+change+30.,qMax(0.,body->property("contentHeight").toDouble()-body->property("height").toDouble())));
        QTest::qWait(40);
    }
    if(!bounds.contains(point()))return false;
    QTest::mouseClick(root->window(),Qt::LeftButton,Qt::NoModifier,point().toPoint());return true;
}
static QVariantList referenceAssociations081(QmlPlugin* root) {
    auto value=root->property("associations");return value.canConvert<QJSValue>()?value.value<QJSValue>().toVariant().toList():value.toList();
}
