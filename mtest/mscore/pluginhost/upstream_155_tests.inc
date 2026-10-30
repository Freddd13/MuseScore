// GPL-2.0-or-later. Upstream playback and file compatibility regressions.
#include "libmscore/repeatlist.h"
#include "libmscore/xml.h"
#include "libmscore/text.h"
#include "libmscore/articulation.h"
#include "importexport/guitarpro/importgtp.h"
#include "mscore/workflow/referencebus.h"
#include <QTextDocument>
#include <QDomDocument>

namespace {
void fastTempo155(MasterScore* score)
{
    auto tempo = new TempoText(score);
    tempo->setTrack(0);
    tempo->setParent(score->firstSegment(SegmentType::ChordRest));
    tempo->setXmlText("<sym>metNoteQuarterUp</sym> = 480");
    tempo->setTempo(8.);
    score->startCmd(); score->undoAddElement(tempo); score->endCmd();
    Ms::mscore->endCmd(); score->setPlaylistDirty();
}
struct GpifReader155 : GuitarPro7 {
    explicit GpifReader155(MasterScore* score) : GuitarPro7(score) {}
    using GuitarPro6::readGpif;
};
}

void TestPluginHost::upstreamLoopRequests155_data()
{
    QTest::addColumn<bool>("preview");QTest::newRow("score")<<false;QTest::newRow("preview")<<true;
}
void TestPluginHost::upstreamLoopRequests155()
{
    QFETCH(bool,preview);
    auto main=Ms::mscore; QVERIFY(!Ms::seq);
    auto score=performanceScore123(_settings.filePath("upstream-loop-155.mscx"),2); QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score)); main->show();
    const auto close=qScopeGuard([&]{score->undoStack()->setClean();main->closeScore(score);});
    fastTempo155(score);
    score->setLoopInTick(Fraction::fromTicks(60));
    score->setLoopOutTick(Fraction::fromTicks(960));
    score->setPlayPos(Fraction());
    const bool oldLoop=getAction("loop")->isChecked();getAction("loop")->setChecked(true);
    const auto loop=qScopeGuard([&]{getAction("loop")->setChecked(oldLoop);});
    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());synth->setSampleRate(44100);synth->init();
    QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
    QScopedValueRollback<MasterSynthesizer*> currentSynth(Ms::synti,synth.get());
    QScopedValueRollback<bool> sequencing(noSeq,false);
    std::atomic<int> requests{0};
    auto sequence=new Seq;Ms::seq=sequence;
    const auto stop=qScopeGuard([&]{stopPlaybackPreview(true);workflowContext()->finishPlaybackSession();sequence->stopWait();sequence->synchronizeStoppedGui();sequence->waitForStoppedRendering();sequence->setScoreView(nullptr);delete sequence;Ms::seq=nullptr;});
    auto driver=new SourcePlaybackDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(synth.get());
    QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
    connect(sequence,SIGNAL(started()),main,SLOT(seqStarted()));connect(sequence,SIGNAL(stopped()),main,SLOT(seqStopped()));
    const auto connection=connect(sequence,&Seq::toGui,sequence,[&](int message,int){if(message=='3')++requests;},Qt::DirectConnection);
    const auto disconnect=qScopeGuard([&]{QObject::disconnect(connection);});
    if(preview){
        auto material=renderPlaybackPreview(score,score);preparePlaybackPreview(score,"upstream155",material,material);
        const auto started=startPlaybackPreview(score,false,{{"start",0},{"end",score->lastMeasure()->endTick().ticks()},{"exactStart",true}});
        QVERIFY2(!started.contains("error"),qPrintable(started.value("error").toString()));getAction("loop")->setChecked(true);
    }else getAction("play")->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying(),5000);
    QElapsedTimer blocked;blocked.start();
    while(requests.load()==0 && blocked.elapsed()<3000)QTest::qSleep(2);
    QVERIFY(requests.load()>0);QTest::qSleep(100);
    QCOMPARE(requests.load(),1); // A busy GUI must receive one pending restart.
    QTest::qWait(60);
    for(int i=0;i<12;++i){sequence->seek(60+(i*60)%780);QTest::qWait(25);QVERIFY(sequence->isPlaying());}
    QTRY_VERIFY_WITH_TIMEOUT(requests.load()>=8,8000);
    const auto audio=driver->metrics();QVERIFY(audio["finite"].toBool());QVERIFY(audio["peak"].toDouble()>0.000001);
    getAction("play")->trigger();QTRY_VERIFY_WITH_TIMEOUT(!sequence->isPlaying(),3000);
    QTest::qWait(150);QVERIFY(!sequence->isPlaying());QVERIFY(!getAction("play")->isChecked());
    getAction("play")->trigger();QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying(),3000);
    getAction("play")->trigger();QTRY_VERIFY_WITH_TIMEOUT(!sequence->isPlaying(),3000);
    qInfo()<<"155 loop requests"<<preview<<requests.load()<<"busy GUI milliseconds"<<blocked.elapsed()<<"audio"<<audio;
}

void TestPluginHost::upstreamRepeatTempo155_data()
{
    QTest::addColumn<bool>("referenceClock");
    QTest::newRow("score")<<false;QTest::newRow("reference-clock")<<true;
}
void TestPluginHost::upstreamRepeatTempo155()
{
    QFETCH(bool,referenceClock);auto main=Ms::mscore;QVERIFY(!Ms::seq);
    auto score=performanceScore123(_settings.filePath(referenceClock?"upstream-tempo-reference-155.mscx":"upstream-tempo-155.mscx"),2);QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));main->show();
    const auto close=qScopeGuard([&]{score->undoStack()->setClean();main->closeScore(score);});
    score->firstMeasure()->setRepeatStart(true);score->lastMeasure()->setRepeatEnd(true);score->lastMeasure()->setRepeatCount(2);
    fastTempo155(score);score->setExpandRepeats(true);score->setPlaylistDirty();
    QVERIFY(score->repeatList().ticks()>score->lastMeasure()->endTick().ticks());
    const bool oldLoop=getAction("loop")->isChecked();getAction("loop")->setChecked(false);
    const auto loop=qScopeGuard([&]{getAction("loop")->setChecked(oldLoop);});
    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());synth->setSampleRate(44100);synth->init();
    QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
    QScopedValueRollback<MasterSynthesizer*> currentSynth(Ms::synti,synth.get());QScopedValueRollback<bool> sequencing(noSeq,false);
    std::atomic<int> changedPosition{-1};std::atomic<double> expectedRatio{-1.};
    auto sequence=new Seq;Ms::seq=sequence;
    const auto stop=qScopeGuard([&]{sequence->stopWait();sequence->synchronizeStoppedGui();sequence->waitForStoppedRendering();setReferenceClock({});sequence->setScoreView(nullptr);delete sequence;Ms::seq=nullptr;});
    auto driver=new SourcePlaybackDriver(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(synth.get());QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
    connect(sequence,SIGNAL(started()),main,SLOT(seqStarted()));connect(sequence,SIGNAL(stopped()),main,SLOT(seqStopped()));
    connect(sequence,&Seq::tempoChanged,sequence,[&]{if(qAbs(score->tempomap()->relTempo()-expectedRatio.load())<.00001){int empty=-1;changedPosition.compare_exchange_strong(empty,sequence->getCurEventTick());}},Qt::DirectConnection);
    if(referenceClock)setReferenceClock({{0.,0.},{20.,10.}});
    getAction("play")->trigger();QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying(),5000);
    const int secondPass=score->lastMeasure()->endTick().ticks();
    QTRY_VERIFY_WITH_TIMEOUT(sequence->getCurEventTick()>secondPass+240,5000);
    for(double ratio:{1.5,.75,1.}){
        changedPosition=-1;expectedRatio=ratio;const int before=sequence->getCurEventTick();
        sequence->setRelTempo(ratio);QTRY_VERIFY_WITH_TIMEOUT(changedPosition.load()>=0,1000);
        const int after=changedPosition.load();
        qInfo()<<"155 repeat tempo"<<referenceClock<<ratio<<before<<after<<"GUI observed later"<<sequence->getCurEventTick();
        QVERIFY2(after>=before-60 && after-before<360,"A tempo change must retain the current repeat occurrence and beat");
    }
    sequence->stopWait();sequence->synchronizeStoppedGui();sequence->waitForStoppedRendering();
    const auto audio=driver->metrics();QVERIFY(audio["finite"].toBool());QVERIFY(audio["peak"].toDouble()>0.000001);
}

void TestPluginHost::upstreamRecentFiles155()
{
    auto main=Ms::mscore;const auto previous=main->recentScores();
    const auto remember=[&](const QString& path){MasterScore record(MScore::baseStyle());record.fileInfo()->setFile(path);main->addRecentScore(&record);};
    QVector<MasterScore*> opened;
    const auto close=qScopeGuard([&]{for(auto score:opened){score->undoStack()->setClean();main->closeScore(score);}for(auto file:previous)remember(file.absoluteFilePath());});
    QFile original(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(original.open(QIODevice::ReadOnly));const auto fixture=original.readAll();
    QStringList files;
    for(int i=0;i<65;++i){const auto name=_settings.filePath(QString("recent-155-%1.mscx").arg(i));QFile f(name);QVERIFY(f.open(QIODevice::WriteOnly));f.write(fixture);f.close();files.append(name);remember(name);}
    for(int i=40;i<65;++i){auto score=main->readScore(files[i]);QVERIFY(score);opened.append(score);main->appendScore(score);}
    const auto recent=main->recentScores();QCOMPARE(recent.size(),20);QCOMPARE(recent.front().absoluteFilePath(),QFileInfo(files[39]).absoluteFilePath());
    auto score=opened.takeLast();score->undoStack()->setClean();main->closeScore(score);QVERIFY(!main->scores().contains(score));
    QCOMPARE(main->recentScores().front().absoluteFilePath(),QFileInfo(files[64]).absoluteFilePath());
    QVERIFY(QMetaObject::invokeMethod(main,"openRecentMenu"));
    qInfo()<<"155 recent files"<<main->recentScores().size()<<"after closing"<<main->recentScores().front().fileName();
    for(int i=5;i<=64;++i)if(i<40||i==64){auto more=main->readScore(files[i]);QVERIFY(more);opened.append(more);main->appendScore(more);}
    QVERIFY(main->recentScores().isEmpty());
    QTranslator chinese;QVERIFY(chinese.load(qEnvironmentVariable("P2_UPSTREAM_ZH_QM")));
    qApp->installTranslator(&chinese);const auto remove=qScopeGuard([&]{qApp->removeTranslator(&chinese);});
    QCOMPARE(chinese.translate("Ms::MuseScore","All recent files are already open"),QString::fromUtf8("最近文件均已打开"));
    QVERIFY(QMetaObject::invokeMethod(main,"openRecentMenu"));
    QMenu* hint=nullptr;for(auto menu:main->findChildren<QMenu*>())for(auto action:menu->actions())if(action->text()==QString::fromUtf8("最近文件均已打开")){hint=menu;QVERIFY(!action->isEnabled());}
    QVERIFY(hint);hint->popup(main->mapToGlobal(QPoint(80,60)));QTest::qWait(80);
    QVERIFY(hint->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("upstream-recent-all-open-155.png")));hint->hide();
}

void TestPluginHost::upstreamTextAndSections155()
{
    QByteArray modern("<text>First<br/>Second &amp; third<br/>Fourth</text>");
    XmlReader reader(modern);QVERIFY(reader.readNextStartElement());const auto value=reader.readXml();
    QCOMPARE(value,QString("First\nSecond &amp; third\nFourth"));
    QByteArray legacy("<html-data><html><body><p>Old<br/>line</p></body></html></html-data>");
    XmlReader old(legacy);QVERIFY(old.readNextStartElement());const auto html=old.readXml();
    QVERIFY(html.contains("<br>"));QTextDocument text;text.setHtml(html);QCOMPARE(text.toPlainText(),QString("Old\nline"));
    auto main=Ms::mscore;
    QFile legacySource(QString(TESTROOT)+"/mtest/libmscore/compat114/title.mscx");QVERIFY(legacySource.open(QIODevice::ReadOnly));
    auto legacyXml=legacySource.readAll();QVERIFY(legacyXml.contains(">title</p>"));legacyXml.replace(">title</p>",">Old<br/>line</p>");
    const auto legacyFile=_settings.filePath("legacy-lines-155.mscx");QFile legacyOutput(legacyFile);QVERIFY(legacyOutput.open(QIODevice::WriteOnly));legacyOutput.write(legacyXml);legacyOutput.close();
    std::unique_ptr<MasterScore> legacyScore(main->readScore(legacyFile));QVERIFY(legacyScore);
    QStringList legacyTexts;legacyScore->scanElements(&legacyTexts,[](void* data,Element* element){if(element->isTextBase())static_cast<QStringList*>(data)->append(toTextBase(element)->plainText());},true);
    QVERIFY(legacyTexts.contains("Old\nline"));
    legacyScore->scanElements(nullptr,[](void*,Element* element){if(element->isTextBase()){auto t=toTextBase(element);if(t->plainText().contains("Old")){for(const auto& block:t->textBlockList())for(const auto& fragment:block.fragments()){auto f=fragment.font(t);QFontMetricsF fm(f,MScore::paintDevice());qInfo()<<"155 legacy fragment"<<fragment.text<<f.family()<<f.pointSizeF()<<QFontInfo(f).family()<<fm.lineSpacing()<<block.y()<<block.lineSpacing();}}}},true);

    legacyScore->scanElements(nullptr,[](void*,Element* element){if(element->isTextBase()){auto t=toTextBase(element);if(t->plainText().contains("Old"))qInfo()<<"155 legacy title metrics"<<t->size()<<t->textLineSpacing()<<t->lineSpacing()<<t->rows()<<t->bbox()<<t->xmlText();}},true);
    auto score=performanceScore123(_settings.filePath("upstream-section-155.mscx"),4);QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    const auto close=qScopeGuard([&]{score->undoStack()->setClean();main->closeScore(score);});
    score->setExpandRepeats(false);const int second=score->firstMeasure()->nextMeasure()->tick().ticks()+120;
    const double before=score->repeatList().utick2utime(second);
    score->startCmd();
    for(auto row:{qMakePair(score->firstMeasure(),1.25),qMakePair(score->lastMeasure(),.75)}){
        auto section=new LayoutBreak(score);section->setLayoutBreakType(LayoutBreak::Type::SECTION);section->setPause(row.second);section->setPropertyFlags(Pid::PAUSE,PropertyFlags::UNSTYLED);section->setParent(row.first);section->setTrack(0);score->undoAddElement(section);
    }
    score->endCmd();main->endCmd();score->setPlaylistDirty();
    const auto& list=score->repeatList();QCOMPARE(list.size(),2);QVERIFY(!list.back()->isEmpty());
    const double after=list.utick2utime(second);QVERIFY(qAbs(after-before-1.25)<.00001);
    QCOMPARE(list.ticks(),score->lastMeasure()->endTick().ticks());
    const auto file=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("upstream-section-155.mscz");QFileInfo saved(file);QVERIFY(score->saveCompressedFile(saved,false,false));
    std::unique_ptr<MasterScore> reopened(main->readScore(file));QVERIFY(reopened);reopened->setExpandRepeats(false);
    QCOMPARE(reopened->repeatList().size(),2);QVERIFY(qAbs(reopened->repeatList().utick2utime(second)-after)<.00001);
    qInfo()<<"155 section pause seconds"<<after-before<<"segments"<<list.size();
}

void TestPluginHost::upstreamGp6Files155()
{
    auto main=Ms::mscore;
    for(auto item:{qMakePair(QString("fermata.gpx"),4),qMakePair(QString("repeats.gpx"),3),qMakePair(QString("multivoices.gpx"),3)}){
        std::unique_ptr<MasterScore> score(main->readScore(QString(TESTROOT)+"/mtest/guitarpro/"+item.first));QVERIFY(score);
        int bars=0;for(auto m=score->firstMeasure();m;m=m->nextMeasure())++bars;
        QCOMPARE(bars,item.second);qInfo()<<"155 GP6"<<item.first<<bars;
    }
    QFile data(QString(TESTROOT)+"/mtest/mscore/pluginhost/data/gpif-fermata-155.xml");QVERIFY(data.open(QIODevice::ReadOnly));const auto xml=data.readAll();
    QDomDocument commented;QVERIFY(commented.setContent(xml));auto masterBars=commented.documentElement().firstChildElement("MasterBars");
    masterBars.insertBefore(commented.createComment("order is important"),masterBars.firstChild());masterBars.insertBefore(commented.createComment("between bars"),masterBars.lastChild());
    for(auto input:{xml,commented.toByteArray(-1)}){
        std::unique_ptr<MasterScore> score(new MasterScore(MScore::baseStyle()));GpifReader155 importer(score.get());importer.readGpif(&input);
        int bars=0;for(auto m=score->firstMeasure();m;m=m->nextMeasure())++bars;QCOMPARE(bars,4);
        bool absentPrevious=false,fermata=false;
        for(auto segment=score->firstSegment(SegmentType::ChordRest);segment;segment=segment->next1(SegmentType::ChordRest)){
            if(segment->tick().ticks()==480)absentPrevious=segment->element(1)==nullptr;
            if(segment->tick().ticks()==960)if(auto cr=segment->cr(1))if(cr->isChord())for(auto articulation:toChord(cr)->articulations())fermata|=articulation->symId()==SymId::fermataAbove;
        }
        QVERIFY(absentPrevious);QVERIFY(fermata);qInfo()<<"155 GPIF empty previous voice fermata"<<bars;
    }
}
