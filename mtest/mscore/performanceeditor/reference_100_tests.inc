// GPL-2.0-or-later. Read failures keep the working audio and its associations.
#include <QtConcurrent>
#include <QSemaphore>
#include <QMessageBox>
#include "libmscore/playbacktiming.h"
void TestPerformanceEditor::referenceFileRelocationAndFailures()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(!Ms::seq);QVERIFY(QDir().mkpath(_directory.filePath("原项目/音频")));
    auto score=expressionScore080(main,_directory.filePath("原项目/乐谱.mscx"));QVERIFY(score);expressionShowMain080(main);
    const auto source=referenceTone099(_directory.filePath("原项目/音频/伴奏.wav"),330.);ReferencePanel081 panel{referenceOpen081(source)};auto root=panel.root.data();QVERIFY(root);
    QTRY_VERIFY_WITH_TIMEOUT(referenceMedia081(root)&&referenceMedia081(root)->property("duration").toDouble()>4900,10000);
    score->selection().setRangeTicks(Fraction::fromTicks(480),Fraction::fromTicks(1920),0,1);score->selection().updateSelectedElements();root->setProperty("a",500.);root->setProperty("b",2000.);QVERIFY(referenceClick081(root,"referenceAssociate"));
    auto media=referenceMedia081(root);const auto originalLinks=referenceAssociations081(root);const auto bad=_directory.filePath("无法读取.wav");QFile broken(bad);QVERIFY(broken.open(QIODevice::WriteOnly));broken.write("not an audio file");broken.close();
    root->setProperty("relocating",true);QVERIFY(QMetaObject::invokeMethod(root,"openAudio",Q_ARG(QVariant,QUrl::fromLocalFile(bad).toString())));
    QTRY_VERIFY_WITH_TIMEOUT(!media->property("status").toString().contains("Reading")&&!media->property("status").toString().contains(QString::fromUtf8("正在读取")),10000);
    QCOMPARE(media->property("path").toString(),source);QVERIFY(media->property("duration").toDouble()>4900);QCOMPARE(referenceAssociations081(root),originalLinks);QVERIFY(!root->property("relocating").toBool());
    root->setProperty("addingAudio",true);QVERIFY(QMetaObject::invokeMethod(root,"openAudio",Q_ARG(QVariant,QUrl::fromLocalFile(bad).toString())));
    QTRY_VERIFY_WITH_TIMEOUT(referenceMedia081(root)==media,10000);QCOMPARE(referenceAssociations081(root),originalLinks);QCOMPARE(root->property("selectedTrack").toInt(),0);
    const auto found=_directory.filePath("原项目/音频/已找回.wav");QVERIFY(QFile::copy(source,found));QFile copied(found);QVERIFY(copied.open(QIODevice::ReadWrite));QVERIFY(copied.setFileTime(QFileInfo(source).lastModified(),QFileDevice::FileModificationTime));copied.close();
    root->setProperty("relocating",true);QVERIFY(QMetaObject::invokeMethod(root,"openAudio",Q_ARG(QVariant,QUrl::fromLocalFile(found).toString())));QTRY_COMPARE_WITH_TIMEOUT(media->property("path").toString(),found,10000);QCOMPARE(referenceAssociations081(root),originalLinks);QVERIFY(QMetaObject::invokeMethod(root,"saveScoreProject"));
    PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);auto saved=QJsonDocument::fromJson(score->metaTag("kumo.reference").toUtf8()).toVariant().toMap();QCOMPARE(saved.value("tracks").toList()[0].toMap().value("relativePath").toString(),QString::fromUtf8("音频/已找回.wav"));
    QVERIFY(QDir().mkpath(_directory.filePath("新位置/音频")));const auto movedAudio=_directory.filePath("新位置/音频/已找回.wav"),movedScore=_directory.filePath("新位置/乐谱.mscz");QVERIFY(QFile::copy(found,movedAudio));QFile moved(movedAudio);QVERIFY(moved.open(QIODevice::ReadWrite));QVERIFY(moved.setFileTime(QFileInfo(found).lastModified(),QFileDevice::FileModificationTime));moved.close();
    QVERIFY(observer.copyWorkflowScore(movedScore,observer.analysisRevision(),true).value("ok").toBool());QVERIFY(QFile::rename(found,_directory.filePath("已归档.wav")));
    auto reopened=main->readScore(movedScore);QVERIFY(reopened);main->setCurrentScoreView(main->appendScore(reopened));
    QTRY_VERIFY_WITH_TIMEOUT(referenceMedia081(root)&&referenceMedia081(root)->property("path").toString()==movedAudio,10000);QCOMPARE(referenceAssociations081(root),originalLinks);
    PluginAPI::Score movedWrapped(reopened);PluginAPI::ScoreObserver movedObserver;movedObserver.setScore(&movedWrapped);QVERIFY(QMetaObject::invokeMethod(root,"saveScoreProject"));
    QVERIFY(QDir().mkpath(_directory.filePath("缺失位置")));const auto missingScore=_directory.filePath("缺失位置/乐谱.mscz");QVERIFY(movedObserver.copyWorkflowScore(missingScore,movedObserver.analysisRevision(),true).value("ok").toBool());QVERIFY(QFile::rename(movedAudio,_directory.filePath("再次归档.wav")));
    auto missing=main->readScore(missingScore);QVERIFY(missing);main->setCurrentScoreView(main->appendScore(missing));
    auto recovery=[&]{return referenceItem099(root,"referenceLocateAudio0");};QTRY_VERIFY_WITH_TIMEOUT(recovery()&&recovery()->isVisible()&&recovery()->isEnabled(),10000);QCOMPARE(referenceAssociations081(root),originalLinks);
    QTRY_COMPARE(root->property("readingAudioCount").toInt(),0);
    QVERIFY(QMetaObject::invokeMethod(root,"saveScoreProject"));const auto missingData=QJsonDocument::fromJson(missing->metaTag("kumo.reference").toUtf8()).toVariant().toMap();QCOMPARE(missingData.value("tracks").toList()[0].toMap().value("relativePath").toString(),QString::fromUtf8("音频/已找回.wav"));QVERIFY(workflowContext()->referenceCurrent(missing));
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));QVERIFY(root->window()->grabWindow().save(output.filePath("reference-missing-audio-100.png")));
    root->setProperty("relocating",true);QVERIFY(QMetaObject::invokeMethod(root,"openAudio",Q_ARG(QVariant,QUrl::fromLocalFile(source).toString())));QTRY_VERIFY_WITH_TIMEOUT(referenceMedia081(root)->property("path").toString()==source&&referenceMedia081(root)->property("duration").toDouble()>4900,10000);QCOMPARE(referenceAssociations081(root),originalLinks);
    QVERIFY(root->window()->grabWindow().save(output.filePath("reference-relocated-100.png")));
}
void TestPerformanceEditor::referenceRepeatedOccurrencePlayback()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(!Ms::seq);auto score=expressionScore080(main,_directory.filePath("reference-repeat-100.mscx"));QVERIFY(score);expressionShowMain080(main);
    score->firstMeasure()->setRepeatStart(true);auto second=score->firstMeasure()->nextMeasure();second->setRepeatEnd(true);second->setRepeatCount(2);score->setPlaylistDirty();score->setExpandRepeats(true);
    MasterSynthesizer synth;MScore::sampleRate=44100;auto recording=new PerformanceRecordingSynth;synth.registerSynthesizer(recording);synth.setSampleRate(44100);synth.init();auto sequence=new PerformanceAuditionSeq;Ms::seq=sequence;
    std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* s){workflowContext()->finishPlaybackSession();s->stopWait();s->waitForStoppedRendering();s->setScoreView(nullptr);delete s;Ms::seq=nullptr;});auto driver=new PerformanceTestDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(&synth);QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
    const auto wave=referenceTone099(_directory.filePath("reference-repeat-100.wav"),550.);PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);auto project=referenceProject();
    QVariantList links;for(int occurrence=1;occurrence<=2;++occurrence)links<<QVariantMap{{"start",480},{"end",1440},{"occurrence",occurrence},{"from",occurrence==1?500.:2500.},{"until",occurrence==1?1500.:3500.},{"active",true}};
    const QVariantList tracks{QVariantMap{{"id","repeat-audio"},{"name","Reference"},{"path",wave},{"volume",1.},{"associations",links}}};QVERIFY(observer.setReferenceTracks(tracks).value("ok").toBool());
    for(int mode=0;mode<3;++mode){auto plan=observer.referencePlan({{"revision",observer.analysisRevision()},{"start",480},{"end",1440},{"occurrence",2},{"mode",mode},{"tracks",tracks},{"leader",links[1]},{"volume",1.},{"fade",false}});QVERIFY2(!plan.contains("error"),qPrintable(plan.value("error").toString()));QCOMPARE(plan.value("startUtick").toInt(),4320);QCOMPARE(plan.value("untilUtick").toInt(),5280);
        auto pieces=plan.value("clips").toList()[0].toMap().value("pieces").toList();QCOMPARE(pieces[0].toMap().value("from").toDouble(),2500.);recording->onPitches.clear();QVERIFY(observer.startReferencePlayback(plan).value("ok").toBool());QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying(),10000);QVERIFY(sequence->getCurTick()>=4320);double first=-1;QVERIFY(QMetaObject::invokeMethod(project,"sourcePosition",Q_RETURN_ARG(double,first),Q_ARG(QString,wave)));QVERIFY(qAbs(first-2500)<1);
        for(int n=0;n<70&&(sequence->isPlaying()||project->property("playing").toBool());++n){driver->pulse();QTest::qWait(3);}QTRY_VERIFY(!sequence->isPlaying());QTRY_VERIFY(!project->property("playing").toBool());QVERIFY(!recording->onPitches.isEmpty());qInfo()<<"repeat occurrence mode"<<mode<<"first source ms"<<first<<"first note"<<recording->onPitches.front();
    }
    const auto absent=observer.referencePlan({{"revision",observer.analysisRevision()},{"start",480},{"end",1440},{"occurrence",3},{"tracks",tracks}});QVERIFY(absent.contains("error"));QVERIFY(absent.value("error").toString().contains(QString::fromUtf8("第 3 遍")));
}
void TestPerformanceEditor::referenceLongAudioTiming()
{
    const auto audioDirectory=qEnvironmentVariable("P2_REFERENCE_LONG_AUDIO");QVERIFY2(!audioDirectory.isEmpty(),"Provide the generated three-format reference audio directory.");
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(!Ms::seq);auto score=expressionScore095(main,_directory.filePath("reference-long-100.mscx"),40);QVERIFY(score);expressionShowMain080(main);
    MasterSynthesizer synth;MScore::sampleRate=44100;auto recording=new PerformanceRecordingSynth;synth.registerSynthesizer(recording);synth.setSampleRate(44100);synth.init();auto sequence=new PerformanceAuditionSeq;Ms::seq=sequence;
    std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* s){workflowContext()->finishPlaybackSession();s->stopWait();s->waitForStoppedRendering();s->setScoreView(nullptr);delete s;Ms::seq=nullptr;});auto driver=new PerformanceTestDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(&synth);QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
    ReferencePanel081 panel{referenceOpen081(QDir(audioDirectory).filePath("long.wav"))};auto root=panel.root.data();QVERIFY(root);auto project=referenceProject();
    for(const auto& extension:{QString("wav"),QString("mp3"),QString("flac")}){
        const auto path=QDir(audioDirectory).filePath("long."+extension);QElapsedTimer timer;timer.start();if(extension!="wav")QVERIFY(QMetaObject::invokeMethod(root,"openAudio",Q_ARG(QVariant,QUrl::fromLocalFile(path).toString())));
        QTRY_VERIFY_WITH_TIMEOUT(referenceMedia081(root)&&referenceMedia081(root)->property("path").toString()==path&&referenceMedia081(root)->property("duration").toDouble()>179900,15000);const qint64 readMs=timer.elapsed();
        score->selection().setRangeTicks(Fraction(),score->lastMeasure()->endTick(),0,score->nstaves());score->selection().updateSelectedElements();root->setProperty("a",90000.);root->setProperty("b",170000.);root->setProperty("jointVolume",1.);root->setProperty("fade",false);referenceMedia081(root)->setProperty("volume",1.);QVERIFY(referenceClick081(root,"referenceAssociate"));root->setProperty("playbackMode",1);root->setProperty("transportTarget",2);timer.restart();QVERIFY(referenceClick081(root,"referenceJointPlay"));QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying(),15000);const auto prepareMs=timer.elapsed();
        qint64 rendered=0,first=-1,last=-1;QVector<double> callbackMs;double energy=0;const qint64 expectedFrames=80*44100;
        for(int block=0;block<2100&&(sequence->isPlaying()||project->property("playing").toBool());++block){timer.restart();driver->pulse();callbackMs.append(timer.nsecsElapsed()/1000000.);for(int frame=0;frame<2048;++frame){const float value=driver->output()[2*frame];if(qAbs(value)>.0001){if(first<0)first=rendered+frame;last=rendered+frame;}energy+=value*value;}rendered+=2048;QTest::qWait(1);}
        QTRY_VERIFY_WITH_TIMEOUT(!sequence->isPlaying()&&!project->property("playing").toBool(),1500);QVERIFY(first>=0&&first<4410);QVERIFY(qAbs(last-expectedFrames)<4096);QVERIFY(energy/rendered>.004);
        double source=-1;QVERIFY(QMetaObject::invokeMethod(project,"sourcePosition",Q_RETURN_ARG(double,source),Q_ARG(QString,path)));QVERIFY(qAbs(source-170000.)<1.);std::sort(callbackMs.begin(),callbackMs.end());
        qInfo()<<"long reference"<<extension<<"read ms"<<readMs<<"prepare ms"<<prepareMs<<"first frame"<<first<<"last frame"<<last<<"expected"<<expectedFrames<<"callback p95 ms"<<callbackMs[int(callbackMs.size()*.95)]<<"max ms"<<callbackMs.back();
    }
}
void TestPerformanceEditor::referenceCancelReadKeepsCurrent()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;auto score=expressionScore080(main,_directory.filePath("reference-cancel-100.mscx"));QVERIFY(score);expressionShowMain080(main);
    const auto source=referenceTone099(_directory.filePath("reference-cancel-100.wav"),330.);ReferencePanel081 panel{referenceOpen081(source)};auto root=panel.root.data();QVERIFY(root);QTRY_VERIFY_WITH_TIMEOUT(referenceMedia081(root)&&referenceMedia081(root)->property("duration").toDouble()>4900,10000);auto media=referenceMedia081(root);
    auto pool=QThreadPool::globalInstance();QVERIFY(pool->waitForDone(5000));const int threads=pool->maxThreadCount();pool->setMaxThreadCount(1);QSemaphore occupied,release;
    auto blocker=QtConcurrent::run([&]{occupied.release();release.acquire();});
    struct RestorePool {QThreadPool* pool;int count;QSemaphore& release;QFuture<void>& future;~RestorePool(){release.release();future.waitForFinished();pool->setMaxThreadCount(count);}} restore{pool,threads,release,blocker};QVERIFY(occupied.tryAcquire(1,5000));
    root->setProperty("relocating",true);QVERIFY(QMetaObject::invokeMethod(root,"openAudio",Q_ARG(QVariant,QUrl::fromLocalFile(source).toString())));QVERIFY(media->property("reading").toBool());QCoreApplication::processEvents();
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_directory.path()));QVERIFY(root->window()->grabWindow().save(output.filePath("reference-reading-cancel-100.png")));
    QVERIFY(referenceClick081(root,"referenceCancelAudioRead"));QVERIFY(!media->property("reading").toBool());QCOMPARE(media->property("path").toString(),source);QVERIFY(media->property("duration").toDouble()>4900);QVERIFY(!root->property("relocating").toBool());
}
void TestPerformanceEditor::referenceReadingStaysWithTrack()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;auto score=expressionScore080(main,_directory.filePath("reference-load-target-100.mscx"));QVERIFY(score);expressionShowMain080(main);
    const auto a=referenceTone099(_directory.filePath("reference-A-100.wav"),330.),b=referenceTone099(_directory.filePath("reference-B-100.wav"),550.),found=_directory.filePath("reference-C-100.wav");QVERIFY(QFile::copy(a,found));QFile copied(found);QVERIFY(copied.open(QIODevice::ReadWrite));QVERIFY(copied.setFileTime(QFileInfo(a).lastModified(),QFileDevice::FileModificationTime));copied.close();
    ReferencePanel081 panel{referenceOpen081(a)};auto root=panel.root.data();QVERIFY(root);QTRY_VERIFY_WITH_TIMEOUT(referenceMedia081(root)&&referenceMedia081(root)->property("duration").toDouble()>4900,10000);
    score->selection().setRangeTicks(Fraction::fromTicks(480),Fraction::fromTicks(1920),0,1);score->selection().updateSelectedElements();root->setProperty("a",500.);root->setProperty("b",2000.);QVERIFY(referenceClick081(root,"referenceAssociate"));const auto originalA=referenceAssociations081(root);
    root->setProperty("addingAudio",true);QVERIFY(QMetaObject::invokeMethod(root,"openAudio",Q_ARG(QVariant,QUrl::fromLocalFile(b).toString())));QCOMPARE(referenceAssociations081(root).size(),0);QTRY_COMPARE_WITH_TIMEOUT(referenceMedia081(root)->property("path").toString(),b,10000);root->setProperty("a",1500.);root->setProperty("b",3000.);QVERIFY(referenceClick081(root,"referenceAssociate"));const auto originalB=referenceAssociations081(root);auto project=referenceProject();
    QVERIFY(QMetaObject::invokeMethod(root,"selectTrack",Q_ARG(QVariant,0)));root->setProperty("relocating",true);QVERIFY(QMetaObject::invokeMethod(root,"openAudio",Q_ARG(QVariant,QUrl::fromLocalFile(found).toString())));QVERIFY(!root->property("relocating").toBool());QVERIFY(QMetaObject::invokeMethod(root,"selectTrack",Q_ARG(QVariant,1)));
    auto tracks=[&]{return project->property("tracks").toList();};QTRY_COMPARE_WITH_TIMEOUT(tracks()[0].toMap().value("path").toString(),found,10000);QCOMPARE(tracks()[0].toMap().value("associations").toList(),originalA);QCOMPARE(referenceAssociations081(root),originalB);QCOMPARE(referenceMedia081(root)->property("path").toString(),b);
    QVERIFY(QMetaObject::invokeMethod(root,"selectTrack",Q_ARG(QVariant,0)));QVERIFY(QMetaObject::invokeMethod(root,"openAudio",Q_ARG(QVariant,QUrl::fromLocalFile(a).toString())));QVERIFY(QMetaObject::invokeMethod(root,"selectTrack",Q_ARG(QVariant,1)));QTRY_COMPARE_WITH_TIMEOUT(tracks()[0].toMap().value("path").toString(),a,10000);QCOMPARE(tracks()[0].toMap().value("associations").toList().size(),0);QCOMPARE(referenceAssociations081(root),originalB);QCOMPARE(referenceMedia081(root)->property("path").toString(),b);
    const auto legacyPath=_directory.filePath("reference-legacy-100.json");const QVariantMap legacy{{"schema",1},{"reference",QVariantMap{{"file",b},{"a",1500.},{"b",3000.},{"associations",originalB},{"identity",tracks()[1].toMap().value("identity")},{"volume",.37},{"speed",1.}}}};QFile file(legacyPath);QVERIFY(file.open(QIODevice::WriteOnly));file.write(QJsonDocument::fromVariant(legacy).toJson());file.close();
    QVERIFY(QMetaObject::invokeMethod(root,"loadProject",Q_ARG(QVariant,QUrl::fromLocalFile(legacyPath).toString())));QTRY_VERIFY_WITH_TIMEOUT(referenceMedia081(root)->property("path").toString()==b&&referenceMedia081(root)->property("duration").toDouble()>4900,10000);QCOMPARE(referenceAssociations081(root),originalB);QCOMPARE(referenceMedia081(root)->property("volume").toDouble(),.37);QCOMPARE(project->property("tracks").toList().size(),1);
}
void TestPerformanceEditor::referenceVoltaAndJumpLocations()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;QVERIFY(!Ms::seq);MasterSynthesizer synth;MScore::sampleRate=44100;auto recording=new PerformanceRecordingSynth;synth.registerSynthesizer(recording);synth.setSampleRate(44100);synth.init();auto sequence=new PerformanceAuditionSeq;Ms::seq=sequence;
    std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* s){workflowContext()->finishPlaybackSession();s->stopWait();s->waitForStoppedRendering();s->setScoreView(nullptr);delete s;Ms::seq=nullptr;});auto driver=new PerformanceTestDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(&synth);QVERIFY(sequence->init());
    const auto wave=referenceTone099(_directory.filePath("reference-jumps-100.wav"),330.);auto project=referenceProject();
    for(const auto& name:{QString("repeat06"),QString("repeat07")}){
        auto score=main->readScore(QString(TESTROOT)+"/mtest/libmscore/repeat/"+name+".mscx");QVERIFY(score);score->setPlaylistDirty();score->setExpandRepeats(true);main->setCurrentScoreView(main->appendScore(score));expressionShowMain080(main);sequence->setScoreView(main->currentScoreView());PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);int checked=0;QVariantMap audition;
        for(auto segment:score->repeatList()){
            if(segment->len()<480)continue;const int start=segment->tick,end=start+480;int occurrence=0;
            for(auto previous:score->repeatList()){if(start>=previous->tick&&start<previous->tick+previous->len())++occurrence;if(previous==segment)break;}
            const int from=segment->utick,until=from+480;const double duration=(PlaybackTiming::time(score,until)-PlaybackTiming::time(score,from))*1000;QVERIFY(duration>0&&duration<4500);
            const QVariantMap link{{"start",start},{"end",end},{"occurrence",occurrence},{"from",500.},{"until",500.+duration}};
            const QVariantList tracks{QVariantMap{{"id","jump-track"},{"path",wave},{"name",name},{"volume",1.},{"associations",QVariantList{link}}}};
            auto plan=observer.referencePlan({{"revision",observer.analysisRevision()},{"start",start},{"end",end},{"occurrence",occurrence},{"mode",1},{"leader",link},{"tracks",tracks},{"volume",1.},{"fade",false}});QVERIFY2(!plan.contains("error"),qPrintable(plan.value("error").toString()));QCOMPARE(plan.value("startUtick").toInt(),from);QCOMPARE(plan.value("untilUtick").toInt(),until);++checked;if(occurrence>1&&audition.isEmpty())audition=plan;
        }
        QVERIFY(checked>=2);QVERIFY(!audition.isEmpty());QVERIFY(observer.startReferencePlayback(audition).value("ok").toBool());QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying(),10000);QVERIFY(sequence->getCurTick()>=audition.value("startUtick").toInt());
        double peak=0;for(int n=0;n<4;++n){driver->pulse();for(int i=0;i<4096;++i)peak=qMax(peak,qAbs(double(driver->output()[i])));QTest::qWait(3);}QVERIFY(peak>.05);qInfo()<<"volta/jump"<<name<<"checked segments"<<checked<<"audition utick"<<audition.value("startUtick");observer.stopReferencePlayback();QTRY_VERIFY(!sequence->isPlaying());sequence->waitForStoppedRendering();
    }
}
void TestPerformanceEditor::referenceImmediateSaveIncludesAssociations()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;const auto path=_directory.filePath("reference-immediate-100.mscx");auto score=expressionScore080(main,path);QVERIFY(score);expressionShowMain080(main);
    const auto audio=referenceTone099(_directory.filePath("reference-immediate-100.wav"),330.);ReferencePanel081 panel{referenceOpen081(audio)};auto root=panel.root.data();QVERIFY(root);QTRY_VERIFY_WITH_TIMEOUT(referenceMedia081(root)&&referenceMedia081(root)->property("duration").toDouble()>4900,10000);
    score->selection().setRangeTicks(Fraction::fromTicks(480),Fraction::fromTicks(1920),0,1);score->selection().updateSelectedElements();root->setProperty("a",500.);root->setProperty("b",2000.);QVERIFY(referenceClick081(root,"referenceAssociate"));
    auto save=getAction("file-save");QVERIFY(save&&save->isEnabled());save->trigger();
    std::unique_ptr<MasterScore> reopened(main->readScore(path));QVERIFY(reopened);const auto data=QJsonDocument::fromJson(reopened->metaTag("kumo.reference").toUtf8()).toVariant().toMap();const auto tracks=data.value("tracks").toList();QCOMPARE(tracks.size(),1);QCOMPARE(tracks[0].toMap().value("associations").toList().size(),1);QCOMPARE(tracks[0].toMap().value("associations").toList()[0].toMap().value("start").toInt(),480);
    QTest::qWait(300);QVERIFY(!score->dirty());QVERIFY(QMetaObject::invokeMethod(root,"deleteAssociation",Q_ARG(QVariant,0)));
    const auto copy=_directory.filePath("reference-immediate-copy-100");QVERIFY(main->saveAs(score,true,copy,"mscz"));std::unique_ptr<MasterScore> copied(main->readScore(copy+".mscz"));QVERIFY(copied);auto copiedData=QJsonDocument::fromJson(copied->metaTag("kumo.reference").toUtf8()).toVariant().toMap();QCOMPARE(copiedData.value("tracks").toList()[0].toMap().value("associations").toList().size(),0);
    save->trigger();QTest::qWait(300);QVERIFY(!score->dirty());root->setProperty("a",1000.);root->setProperty("b",2500.);QVERIFY(referenceClick081(root,"referenceAssociate"));
    QScopedValueRollback<bool> normalConversion(converterMode,false);
    bool prompted=false;QTimer answer;answer.setInterval(10);QObject::connect(&answer,&QTimer::timeout,[&]{if(auto dialog=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))if(auto button=dialog->button(QMessageBox::Save)){prompted=true;QTest::mouseClick(button,Qt::LeftButton);answer.stop();}});answer.start();QTimer::singleShot(5000,&answer,[]{if(auto dialog=qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))dialog->reject();});auto close=getAction("file-close");QVERIFY(close&&close->isEnabled());close->trigger();answer.stop();QVERIFY(prompted);QVERIFY(main->currentScore()!=score);
    std::unique_ptr<MasterScore> closed(main->readScore(path));QVERIFY(closed);const auto closedData=QJsonDocument::fromJson(closed->metaTag("kumo.reference").toUtf8()).toVariant().toMap();QCOMPARE(closedData.value("tracks").toList()[0].toMap().value("associations").toList().size(),1);QCOMPARE(closedData.value("tracks").toList()[0].toMap().value("associations").toList()[0].toMap().value("from").toDouble(),1000.);
}
void TestPerformanceEditor::referenceWithoutScoreReopens()
{
    expressionHost080(_directory.path());auto main=Ms::mscore;struct RestoreView {MuseScore* main;QPointer<ScoreView> view;~RestoreView(){main->setCurrentScoreView(view);}} restore{main,main->currentScoreView()};main->setCurrentScoreView(nullptr);QVERIFY(!main->currentScore());expressionShowMain080(main);
    const auto audio=referenceTone099(_directory.filePath("reference-no-score-100.wav"),330.);ReferencePanel081 panel{referenceOpen081(audio)};auto root=panel.root.data();QVERIFY(root);QTRY_VERIFY_WITH_TIMEOUT(referenceMedia081(root)&&referenceMedia081(root)->property("duration").toDouble()>4900,10000);root->setProperty("a",1000.);root->setProperty("b",3000.);QVERIFY(QMetaObject::invokeMethod(root,"storeTrack"));root->disposePanelForReload();panel.root=nullptr;QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);showReferencePanel(true);
    QmlPlugin* opened=nullptr;for(auto window:QGuiApplication::allWindows())if(auto view=qobject_cast<QQuickView*>(window))if(auto item=qobject_cast<QmlPlugin*>(view->rootObject()))if(item->menuPath()=="Plugins.Reference Player")opened=item;QVERIFY(opened);panel.root=opened;QTRY_VERIFY_WITH_TIMEOUT(referenceMedia081(opened)&&referenceMedia081(opened)->property("path").toString()==audio&&referenceMedia081(opened)->property("duration").toDouble()>4900,10000);QTRY_VERIFY(opened->property("viewEnd").toDouble()>4900);QCOMPARE(opened->property("a").toDouble(),1000.);QCOMPARE(opened->property("b").toDouble(),3000.);
}
