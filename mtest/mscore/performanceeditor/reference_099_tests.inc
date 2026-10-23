// GPL-2.0-or-later. Track controls are checked against the actual mixed PCM.
static QString referenceTone099(const QString& path,double frequency)
{
    QFile file(path);if(!file.open(QIODevice::WriteOnly))return {};
    const int rate=22050,frames=rate*5;QDataStream data(&file);data.setByteOrder(QDataStream::LittleEndian);
    data.writeRawData("RIFF",4);data<<quint32(36+frames*2);data.writeRawData("WAVEfmt ",8);
    data<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(rate)<<quint32(2*rate)<<quint16(2)<<quint16(16);
    data.writeRawData("data",4);data<<quint32(frames*2);
    for(int i=0;i<frames;++i)data<<qint16(4000*std::sin(2*3.14159265358979323846*frequency*i/rate));
    return path;
}
static QQuickItem* referenceItem099(QQuickItem* parent,const QString& name)
{
    if(parent->objectName()==name)return parent;
    for(auto child:parent->childItems())if(auto found=referenceItem099(child,name))return found;
    return nullptr;
}
static bool referenceClick099(QmlPlugin* panel,const QString& name)
{
    auto item=referenceItem099(panel,name);if(!item||!item->isVisible()||!item->isEnabled())return false;
    const auto point=item->mapToScene(QPointF(item->width()/2,item->height()/2));
    if(!QRectF(0,0,panel->window()->width(),panel->window()->height()).contains(point))return false;
    QTest::mouseClick(panel->window(),Qt::LeftButton,Qt::NoModifier,point.toPoint());return true;
}
void TestPerformanceEditor::referenceLiveTrackControls()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(!Ms::seq);
    auto score=expressionScore080(main,_directory.filePath("reference-mix-099.mscx"));QVERIFY(score);expressionShowMain080(main);
    MasterSynthesizer synth;MScore::sampleRate=44100;auto recording=new PerformanceRecordingSynth;synth.registerSynthesizer(recording);synth.setSampleRate(44100);synth.init();
    auto sequence=new PerformanceAuditionSeq;Ms::seq=sequence;
    std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* s){workflowContext()->finishPlaybackSession();s->stopWait();s->waitForStoppedRendering();s->setScoreView(nullptr);delete s;Ms::seq=nullptr;});
    auto driver=new PerformanceTestDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(&synth);QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
    const auto a=referenceTone099(_directory.filePath("低音-099.wav"),330.),b=referenceTone099(_directory.filePath("旋律-099.wav"),550.);QVERIFY(!a.isEmpty()&&!b.isEmpty());
    ReferencePanel081 panel{referenceOpen081(a)};auto root=panel.root.data();QVERIFY(root);
    QTRY_VERIFY_WITH_TIMEOUT(referenceMedia081(root)&&referenceMedia081(root)->property("duration").toDouble()>4900,10000);
    auto selectRange=[&]{score->selection().setRangeTicks(Fraction(),Fraction::fromTicks(3840),0,1);score->selection().updateSelectedElements();};
    selectRange();root->setProperty("a",0.);root->setProperty("b",4000.);referenceMedia081(root)->setProperty("volume",.5);QVERIFY(referenceClick081(root,"referenceAssociate"));
    root->setProperty("addingAudio",true);QVERIFY(QMetaObject::invokeMethod(root,"openAudio",Q_ARG(QVariant,QUrl::fromLocalFile(b).toString())));
    QTRY_VERIFY_WITH_TIMEOUT(referenceMedia081(root)->property("path").toString().contains("旋律-099")&&referenceMedia081(root)->property("duration").toDouble()>4900,10000);
    selectRange();root->setProperty("a",0.);root->setProperty("b",4000.);referenceMedia081(root)->setProperty("volume",.5);QVERIFY(referenceClick081(root,"referenceAssociate"));
    root->setProperty("transportTarget",2);root->setProperty("jointVolume",1.);root->setProperty("fade",false);auto project=referenceProject();QSignalSpy starts(sequence,&Seq::started);
    QVERIFY(referenceClick081(root,"referenceJointPlay"));QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying()&&project->property("playing").toBool(),10000);
    auto amplitudes=[&]{QVector<float> values;for(int n=0;n<10;++n){driver->pulse();for(int i=0;i<2048;++i)values.append(driver->output()[2*i]);QTest::qWait(3);}QVector<double> result;
        for(double frequency:{330.,550.}){double real=0,imag=0;for(int i=2048;i<values.size();++i){const double angle=2*3.14159265358979323846*frequency*i/44100.;real+=values[i]*std::cos(angle);imag+=values[i]*std::sin(angle);}result<<2*std::hypot(real,imag)/(values.size()-2048);}qInfo()<<"live track amplitudes"<<result;return result;};
    auto levels=amplitudes();QVERIFY(levels[0]>.04&&levels[1]>.04);
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));QVERIFY(root->window()->grabWindow().save(output.filePath("reference-live-tracks-099.png")));
    auto mute=referenceItem099(root,"referenceTrackMute");QVERIFY(mute);QVERIFY2(mute->isEnabled(),"播放中应能直接静音参加播放的音轨");
    QVERIFY(referenceClick099(root,"referenceTrackMute"));levels=amplitudes();QVERIFY(levels[0]<.001&&levels[1]>.04);QVERIFY(sequence->isPlaying());QCOMPARE(starts.count(),1);
    QVERIFY(referenceClick099(root,"referenceTrackMute"));levels=amplitudes();QVERIFY(levels[0]>.04&&levels[1]>.04);
    QVERIFY(referenceClick099(root,"referenceTrackSolo"));levels=amplitudes();QVERIFY(levels[0]>.04&&levels[1]<.001);
    QVERIFY(referenceClick099(root,"referenceTrackSolo"));levels=amplitudes();QVERIFY(levels[0]>.04&&levels[1]>.04);
    auto slider=root->findChild<QQuickItem*>("referenceVolumeSlider");QVERIFY(slider&&slider->isEnabled());
    QTest::mouseClick(root->window(),Qt::LeftButton,Qt::NoModifier,slider->mapToScene(QPointF(slider->width()*.25,slider->height()/2)).toPoint());levels=amplitudes();
    const double expected=referenceMedia081(root)->property("volume").toDouble()*4000/32768.;QVERIFY(levels[0]>.04&&levels[1]<.05);QVERIFY(qAbs(levels[1]-expected)<.001);QCOMPARE(starts.count(),1);
    QVERIFY(QMetaObject::invokeMethod(root,"selectTrack",Q_ARG(QVariant,0)));QCOMPARE(project->property("selectedTrack").toInt(),0);QVERIFY(sequence->isPlaying());levels=amplitudes();QVERIFY(levels[0]>.04&&qAbs(levels[1]-expected)<.001);QCOMPARE(starts.count(),1);
    const auto diagnostics=project->property("diagnostics").toMap();QCOMPARE(diagnostics.value("underruns").toInt(),0);qInfo()<<"live mix buffers"<<diagnostics;
    QVERIFY(referenceClick081(root,"referenceJointPlay"));QTRY_VERIFY(!sequence->isPlaying());QVERIFY(!project->property("playing").toBool());
    QVERIFY(root->window()->grabWindow().save(output.filePath("reference-live-tracks-099.png")));
}
void TestPerformanceEditor::referenceOverlappingAssociationChoice()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;auto score=expressionScore080(main,_directory.filePath("reference-overlap-099.mscx"));QVERIFY(score);expressionShowMain080(main);
    const auto wav=referenceTone099(_directory.filePath("reference-overlap-099.wav"),330.);ReferencePanel081 panel{referenceOpen081(wav)};auto root=panel.root.data();QVERIFY(root);
    QTRY_VERIFY_WITH_TIMEOUT(referenceMedia081(root)&&referenceMedia081(root)->property("duration").toDouble()>4900,10000);
    score->selection().setRangeTicks(Fraction::fromTicks(480),Fraction::fromTicks(1920),0,1);score->selection().updateSelectedElements();
    root->setProperty("a",200.);root->setProperty("b",1700.);QVERIFY(referenceClick081(root,"referenceAssociate"));QCOMPARE(referenceAssociations081(root).size(),1);
    root->setProperty("a",2200.);root->setProperty("b",3700.);QVERIFY(referenceClick081(root,"referenceAssociate"));auto links=referenceAssociations081(root);QCOMPARE(links.size(),2);
    int active=0;for(auto link:links)if(link.toMap().value("active",true).toBool())++active;
    QCOMPARE(active,1);QVERIFY(!links.front().toMap().value("active",true).toBool());QVERIFY(links.back().toMap().value("active",true).toBool());
    QVERIFY(QMetaObject::invokeMethod(root,"undoAssociation"));links=referenceAssociations081(root);QCOMPARE(links.size(),1);QVERIFY(links.front().toMap().value("active",true).toBool());
    root->setProperty("a",2200.);root->setProperty("b",3700.);QVERIFY(referenceClick081(root,"referenceAssociate"));
    QVERIFY(QMetaObject::invokeMethod(root,"setAssociationOccurrence",Q_ARG(QVariant,1),Q_ARG(QVariant,2)));
    QVERIFY(QMetaObject::invokeMethod(root,"useAssociation",Q_ARG(QVariant,0),Q_ARG(QVariant,true)));links=referenceAssociations081(root);QVERIFY(links[0].toMap().value("active").toBool());QVERIFY(links[1].toMap().value("active").toBool());
    score->select(toChord(score->findCR(Fraction::fromTicks(960),0))->upNote(),SelectType::SINGLE);
    QVERIFY(QMetaObject::invokeMethod(referenceMedia081(root),"seek",Q_ARG(double,3000.)));QVERIFY(QMetaObject::invokeMethod(root,"addAnchor"));
    links=referenceAssociations081(root);QCOMPARE(links[1].toMap().value("anchors").toList().size(),1);
    QVERIFY(referenceClick081(root,"referenceAlignmentPoints"));auto details=root->findChild<QQuickWindow*>("referenceAssociationDetails");QVERIFY(details);QTRY_VERIFY(details->isVisible());
    auto time=referenceItem099(details->contentItem(),"referenceAlignmentTime0");QVERIFY(time);QTest::mouseClick(details,Qt::LeftButton,Qt::NoModifier,time->mapToScene(QPointF(time->width()/2,time->height()/2)).toPoint());
    QTest::keyClick(details,Qt::Key_A,Qt::ControlModifier);for(char key:QByteArray("0:02.900"))QTest::keyClick(details,key);QTest::keyClick(details,Qt::Key_Return);links=referenceAssociations081(root);QCOMPARE(links[1].toMap().value("anchors").toList()[0].toMap().value("ms").toDouble(),2900.);
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));QVERIFY(details->grabWindow().save(output.filePath("reference-alignment-point-099.png")));details->hide();
    QVERIFY(QMetaObject::invokeMethod(root,"beginSlice"));root->setProperty("a",2250.);root->setProperty("b",3750.);QVERIFY(QMetaObject::invokeMethod(root,"finishSlice"));
    PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
    QVERIFY(QMetaObject::invokeMethod(root,"storeTrack"));QVERIFY(QMetaObject::invokeMethod(root,"saveScoreProject"));
    auto saved=QJsonDocument::fromJson(score->metaTag("kumo.reference").toUtf8()).toVariant().toMap();auto savedLinks=saved.value("tracks").toList()[0].toMap().value("associations").toList();QCOMPARE(savedLinks.size(),2);QCOMPARE(savedLinks[1].toMap().value("from").toDouble(),2250.);
    const auto scorePath=output.filePath("reference-associations-099.mscz");
    QVERIFY(observer.copyWorkflowScore(scorePath,observer.analysisRevision(),true).value("ok").toBool());auto reopened=main->readScore(scorePath);QVERIFY(reopened);main->setCurrentScoreView(main->appendScore(reopened));
    QTRY_COMPARE(referenceAssociations081(root).size(),2);QTRY_COMPARE(root->property("a").toDouble(),2250.);QTRY_VERIFY_WITH_TIMEOUT(referenceMedia081(root)&&referenceMedia081(root)->property("duration").toDouble()>4900,10000);
    links=referenceAssociations081(root);QCOMPARE(links[1].toMap().value("anchors").toList()[0].toMap().value("ms").toDouble(),2900.);QCOMPARE(links[1].toMap().value("occurrence").toInt(),2);
    QVERIFY(referenceClick099(root,"referenceTrackDelete"));QCOMPARE(referenceAssociations081(root).size(),0);QVERIFY(QMetaObject::invokeMethod(root,"saveScoreProject"));
    saved=QJsonDocument::fromJson(reopened->metaTag("kumo.reference").toUtf8()).toVariant().toMap();QCOMPARE(saved.value("tracks").toList().size(),0);QVERIFY(root->window()->grabWindow().save(output.filePath("reference-empty-recover-099.png")));
    QVERIFY(referenceClick081(root,"referenceRestoreTrack"));QTRY_COMPARE(referenceAssociations081(root).size(),2);QTRY_VERIFY_WITH_TIMEOUT(referenceMedia081(root)&&referenceMedia081(root)->property("duration").toDouble()>4900,10000);
    QVERIFY(root->window()->grabWindow().save(output.filePath("reference-associations-099.png")));
    QVERIFY(referenceClick099(root,"referenceTrackDelete"));auto open=root->findChild<QQuickItem*>("referenceOpenAudio");QVERIFY(open&&open->isEnabled());auto add=root->findChild<QQuickItem*>("referenceAddAudio");QVERIFY(add&&add->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(root,"openAudio",Q_ARG(QVariant,QUrl::fromLocalFile(wav).toString())));QTRY_VERIFY_WITH_TIMEOUT(referenceMedia081(root)&&referenceMedia081(root)->property("duration").toDouble()>4900,10000);QCOMPARE(referenceAssociations081(root).size(),0);
    QVERIFY(referenceClick081(root,"referenceRestoreTrack"));QTRY_COMPARE(referenceAssociations081(root).size(),2);QTRY_VERIFY_WITH_TIMEOUT(referenceMedia081(root)&&referenceMedia081(root)->property("duration").toDouble()>4900,10000);
}
void TestPerformanceEditor::referenceLiveMixLoopAndPositions()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;auto score=expressionScore080(main,_directory.filePath("reference-live-loop-099.mscx"));QVERIFY(score);expressionShowMain080(main);QVERIFY(!Ms::seq);
    MasterSynthesizer synth;MScore::sampleRate=44100;auto recording=new PerformanceRecordingSynth;synth.registerSynthesizer(recording);synth.setSampleRate(44100);synth.init();auto sequence=new PerformanceAuditionSeq;Ms::seq=sequence;
    std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* s){workflowContext()->finishPlaybackSession();s->stopWait();s->waitForStoppedRendering();s->setScoreView(nullptr);delete s;Ms::seq=nullptr;});
    auto driver=new PerformanceTestDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(&synth);QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
    const auto wav=referenceTone099(_directory.filePath("reference-shared-file-099.wav"),330.);PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);auto project=referenceProject();
    QVariantList tracks;
    for(int i=0;i<2;++i)tracks<<QVariantMap{{"id",QString("loop-%1").arg(i)},{"name",QString("Track %1").arg(i)},{"path",wav},{"volume",.5},{"mute",i==0},{"participate",true},{"associations",QVariantList{QVariantMap{{"start",0},{"end",1920},{"from",i*2000.},{"until",i*2000.+2000.}}}}};
    QVERIFY(observer.setReferenceTracks(tracks).value("ok").toBool());auto plan=observer.referencePlan({{"revision",observer.analysisRevision()},{"start",0},{"end",1920},{"tracks",tracks},{"volume",1.},{"mode",0},{"fade",false}});QVERIFY2(!plan.contains("error"),qPrintable(plan.value("error").toString()));QCOMPARE(plan.value("clips").toList().size(),2);plan["loop"]=true;
    QSignalSpy starts(sequence,&Seq::started);QVERIFY(observer.startReferencePlayback(plan).value("ok").toBool());QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying(),10000);
    for(int n=0;n<5;++n){driver->pulse();QTest::qWait(3);}double first=-1,second=-1;
    QVERIFY(QMetaObject::invokeMethod(project,"sourcePosition",Q_RETURN_ARG(double,first),Q_ARG(QString,wav),Q_ARG(QString,QString("loop-0"))));QVERIFY(QMetaObject::invokeMethod(project,"sourcePosition",Q_RETURN_ARG(double,second),Q_ARG(QString,wav),Q_ARG(QString,QString("loop-1"))));QVERIFY(qAbs(second-first-2000)<.1);
    auto mix=plan.value("mixTracks").toList();auto changed=mix[1].toMap();changed["mute"]=true;mix[1]=changed;changed=mix[0].toMap();changed["mute"]=false;changed["volume"]=.25;mix[0]=changed;
    QVariantMap reply;QVERIFY(QMetaObject::invokeMethod(project,"setTrackMix",Q_RETURN_ARG(QVariantMap,reply),Q_ARG(QVariantList,mix),Q_ARG(QString,QString("loop-0"))));QVERIFY(reply.value("ok").toBool());
    for(int n=0;n<180&&starts.count()<2;++n){driver->pulse();QTest::qWait(3);}QVERIFY(starts.count()>=2);
    auto current=project->property("playbackPlan").toMap().value("mixTracks").toList();QCOMPARE(current[0].toMap().value("volume").toDouble(),.25);QVERIFY(current[1].toMap().value("mute").toBool());
    for(int n=0;n<8;++n){driver->pulse();QTest::qWait(3);}double power=0;for(int n=0;n<2048;++n)power+=driver->output()[2*n]*driver->output()[2*n];const double rms=std::sqrt(power/2048);qInfo()<<"loop output RMS"<<rms;QVERIFY(rms>.020&&rms<.024);
    observer.stopReferencePlayback();QTRY_VERIFY(!sequence->isPlaying());QVERIFY(!project->property("playing").toBool());
    auto silent=tracks[0].toMap();silent["mute"]=true;silent["path"]=_directory.filePath("missing-099.wav");tracks[0]=silent;
    plan=observer.referencePlan({{"revision",observer.analysisRevision()},{"start",0},{"end",1920},{"tracks",tracks},{"volume",1.},{"mode",0}});QVERIFY(!plan.contains("error"));QCOMPARE(plan.value("clips").toList().size(),1);
    QVERIFY(observer.startReferencePlayback(plan).value("ok").toBool());QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying(),10000);mix=plan.value("mixTracks").toList();changed=mix[0].toMap();changed["mute"]=false;mix[0]=changed;
    QVERIFY(QMetaObject::invokeMethod(project,"setTrackMix",Q_RETURN_ARG(QVariantMap,reply),Q_ARG(QVariantList,mix),Q_ARG(QString,QString("loop-0"))));QVERIFY(reply.contains("error"));QVERIFY(sequence->isPlaying());observer.stopReferencePlayback();
}
