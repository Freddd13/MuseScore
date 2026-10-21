static MasterScore* expressionScore080(MuseScore* main, const QString& path)
{
    const int pitches[]{60,62,64,65,67,69,71,72,74,72,71,69,67,65,64,60};
    const int spelling[]{14,21,16,11,18,13,20,15,22,17,12,19};
    QString xml="<museScore version=\"3.02\"><Score><Division>480</Division><Part><Staff id=\"1\"><StaffType group=\"pitched\"><name>stdNormal</name></StaffType></Staff><trackName>Piano</trackName><Instrument id=\"piano\"><instrumentId>keyboard.piano</instrumentId><Channel/></Instrument></Part><Staff id=\"1\">";
    for(int bar=0;bar<4;++bar){
        xml+="<Measure><voice>";
        if(!bar)xml+="<Clef><concertClefType>G</concertClefType></Clef><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>";
        for(int i=0;i<4;++i){const int pitch=pitches[bar*4+i];xml+=QString("<Chord><durationType>quarter</durationType><Note><pitch>%1</pitch><tpc>%2</tpc></Note></Chord>").arg(pitch).arg(spelling[pitch%12]);}
        xml+="</voice></Measure>";
    }
    xml+="</Staff></Score></museScore>";
    QFile file(path);if(!file.open(QIODevice::WriteOnly))return nullptr;file.write(xml.toUtf8());file.close();
    auto score=main->readScore(path);if(score)main->setCurrentScoreView(main->appendScore(score));return score;
}
static void expressionHost080(const QString& settings)
{
    if(Ms::mscore)return;
    QCoreApplication::setOrganizationName("PerformanceRegression");QCoreApplication::setApplicationName("Expression080");
    QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings);
    dataPath=settings;MScore::noGui=true;noSeq=true;converterMode=true;initMuseScoreResources();QStringList args;MuseScore::init(args);
}
static QmlPlugin* expressionRoot080(MuseScore* main)
{
    showExpressionPanel();auto editor=main->performanceEditor();if(!editor)return nullptr;
    auto host=editor->findChild<QWidget*>("embeddedExpressionPage");return host?qobject_cast<QmlPlugin*>(host->property("root").value<QObject*>()):nullptr;
}
static QVariantMap expressionMap080(QmlPlugin* root,const char* name)
{
    const auto value=root->property(name);return value.canConvert<QJSValue>()?value.value<QJSValue>().toVariant().toMap():value.toMap();
}
static bool expressionClick080(QmlPlugin* root,const char* name)
{
    auto item=root->findChild<QQuickItem*>(name);if(!item||!item->isVisible()||!item->isEnabled())return false;
    const auto point=item->mapToScene(QPointF(item->width()/2,item->height()/2));
    if(!QRectF(0,0,root->width(),root->height()).contains(root->mapFromScene(point))||!QRectF(0,0,item->window()->width(),item->window()->height()).contains(point))return false;
    QTest::mouseClick(item->window(),Qt::LeftButton,Qt::NoModifier,point.toPoint());return true;
}
static void expressionShowMain080(MuseScore* main)
{
    const auto available=main->screen()->availableGeometry().size()-QSize(16,48);
    main->resize(QSize(1280,960).boundedTo(available));main->show();QTest::qWait(100);
}
