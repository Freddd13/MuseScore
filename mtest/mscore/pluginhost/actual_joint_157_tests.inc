// SPDX-License-Identifier: GPL-2.0-or-later
// Exercise the reference panel with the real sound-card clock and block size.
void TestPluginHost::actualJointOutput157()
{
#if defined(USE_PORTAUDIO) && defined(KUMO_VST_HOST)
    if (!qEnvironmentVariableIsSet("P2_ACTUAL_AUDIO_DEVICE"))
        QSKIP("Explicit local sound-card selection required");
    auto main = Ms::mscore;
    QVERIFY(main && !Ms::seq);
    SourceTimerResolution timer;
    auto score = main->readScore(qEnvironmentVariable("P2_ACTUAL_AUDIO_SCORE"));
    QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    main->resize(1100,800);
    main->show();
    const auto close = qScopeGuard([&] {
        releaseHostPanels();
        score->undoStack()->setClean();
        main->closeScore(score);
    });

    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());
    QScopedValueRollback<MasterSynthesizer*> current(Ms::synti,synth.get());
    QScopedValueRollback<bool> sequencing(noSeq,false);
    auto sequence = new Seq;
    sequence->setMasterSynthesizer(synth.get());
    const auto stop = qScopeGuard([&] {
        QMetaObject::invokeMethod(referenceProject(),"stop");
        workflowContext()->finishPlaybackSession();
        sequence->stopWait();
        sequence->synchronizeStoppedGui();
        sequence->waitForStoppedRendering();
        sequence->setScoreView(nullptr);
        delete sequence;
        Ms::seq = nullptr;
    });
    auto driver = new ActualOutput157(sequence);
    sequence->setDriver(driver);
    QVERIFY2(driver->init(),qPrintable(driver->error));
    synth->setSampleRate(driver->sampleRate());
    synth->init();
    // Isolate the reference tones while measuring each track's amplitude.
    synth->setGain(0.f);
    QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
    Ms::seq = sequence;
    QScopedValueRollback<Sequencer*> events(MScore::seq,sequence);
    QVERIFY2(sequence->init(),qPrintable(driver->error));
    sequence->setScoreView(main->currentScoreView());
    QObject::connect(sequence,SIGNAL(started()),main,SLOT(seqStarted()));
    QObject::connect(sequence,SIGNAL(stopped()),main,SLOT(seqStopped()));
    QSignalSpy starts(sequence,&Seq::started);

    showReferencePanel(true);
    QPointer<QmlPlugin> panel;
    for (auto window : QGuiApplication::allWindows())
        if (auto view=qobject_cast<QQuickView*>(window))
            if (auto root=qobject_cast<QmlPlugin*>(view->rootObject()))
                if (root->menuPath()=="Plugins.Reference Player") panel=root;
    QVERIFY(panel);
    panel->setPanelFloating(true);
    panel->resizePanel(680,800);
    auto media = referenceProject();
    QVERIFY(media->property("available").toBool());

    const QVector<double> frequencies{311.,541.,773.,1009.,1237.,1471.,1699.,1931.};
    QVariantList tracks;
    for (int index=0; index<frequencies.size(); ++index) {
        const auto path=_settings.filePath(QString("actual-joint-157-%1.wav").arg(index));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QDataStream data(&file);
        data.setByteOrder(QDataStream::LittleEndian);
        const int rate=22050, frames=rate*10;
        data.writeRawData("RIFF",4);
        data << quint32(36+frames*2);
        data.writeRawData("WAVEfmt ",8);
        data << quint32(16) << quint16(1) << quint16(1) << quint32(rate)
             << quint32(rate*2) << quint16(2) << quint16(16);
        data.writeRawData("data",4);
        data << quint32(frames*2);
        for (int frame=0; frame<frames; ++frame)
            data << qint16(4000*std::sin(2*3.14159265358979323846*frequencies[index]*frame/rate));
        file.close();
        const QVariantMap association{{"id",QString("association-%1").arg(index)},
            {"name","Sound-card passage"},{"start",480},{"end",8160},
            {"from",500.},{"until",8500.},{"active",true},{"occurrence",1}};
        tracks.append(QVariantMap{{"id",QString("audio-%1").arg(index)},
            {"name",QString("Track %1").arg(index+1)},{"path",path},{"duration",10000.},
            {"volume",.08},{"participate",true},{"fade",false},
            {"associations",QVariantList{association}},{"selectedAssociation",0}});
    }
    QVERIFY(QMetaObject::invokeMethod(panel,"restoreTracks",
        Q_ARG(QVariant,QVariant(QVariantMap{{"tracks",tracks},{"selectedTrack",0}})),
        Q_ARG(QVariant,QVariant(QString()))));
    QTRY_VERIFY_WITH_TIMEOUT(panel->property("audioReady").toBool()
        && panel->property("readingAudioCount").toInt()==0,15000);
    panel->setProperty("jointVolume",1.);
    panel->setProperty("fade",false);
    panel->setProperty("looping",false);
    QVERIFY(QMetaObject::invokeMethod(panel,"saveScoreProject"));
    auto invoke = [&](const char* method) { return QMetaObject::invokeMethod(panel,method); };
    std::function<QQuickItem*(QQuickItem*,const QString&)> findItem;
    findItem = [&](QQuickItem* parent,const QString& name) -> QQuickItem* {
        if (parent->objectName()==name) return parent;
        for (auto child : parent->childItems())
            if (auto item=findItem(child,name)) return item;
        return nullptr;
    };
    auto click = [&](const char* name) {
        auto item=findItem(panel,QString::fromUtf8(name));
        if (!item || !item->isVisible() || !item->isEnabled()) {
            qInfo()<<"Reference control unavailable"<<name<<item;
            return false;
        }
        if (auto scroll=panel->findChild<QQuickItem*>("referencePanelScroll")) {
            const auto value=scroll->property("contentItem");
            auto object=value.canConvert<QJSValue>() ? value.value<QJSValue>().toQObject() : value.value<QObject*>();
            if (auto flick=qobject_cast<QQuickItem*>(object)) {
                const auto point=item->mapToItem(flick,QPointF());
                flick->setProperty("contentY",qBound(0.,point.y()-30.,
                    qMax(0.,flick->property("contentHeight").toDouble()-flick->height())));
            }
        }
        QTest::qWait(40);
        const auto point=item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint();
        if (!panel->window()->geometry().size().isValid()
            || !QRect(QPoint(),panel->window()->size()).contains(point)) return false;
        QTest::mouseClick(panel->window(),Qt::LeftButton,Qt::NoModifier,point);
        QTest::qWait(40);
        return true;
    };
    auto diagnostics = [&] { return media->property("diagnostics").toMap(); };
    int alignmentMaximumTicks = 0;
    auto levels = [&] {
        QVector<double> result;
        if (!driver->beginCapture()) return result;
        QElapsedTimer wait;
        wait.start();
        while (!driver->captureReady() && wait.elapsed()<2500) QTest::qWait(10);
        const auto samples=driver->capturedAudio();
        if (samples.isEmpty()) return result;
        const auto outputFrames=diagnostics().value("outputFrames").toLongLong();
        const auto expectedTick=score->utime2utick(PlaybackTiming::time(score,480)
            + double(outputFrames)/driver->sampleRate());
        alignmentMaximumTicks=qMax(alignmentMaximumTicks,qAbs(sequence->getCurTick()-expectedTick));
        for (double frequency : frequencies) {
            double real=0,imaginary=0;
            for (int frame=0; frame<samples.size(); ++frame) {
                const double angle=2*3.14159265358979323846*frequency*frame/driver->sampleRate();
                real+=samples[frame]*std::cos(angle);
                imaginary+=samples[frame]*std::sin(angle);
            }
            result.append(2*std::hypot(real,imaginary)/samples.size());
        }
        return result;
    };
    auto allAudible = [](const QVector<double>& values) {
        if (values.size()!=8) return false;
        for (double value : values) if (value<.0085 || value>.011) return false;
        return true;
    };
    auto asList = [](const QVector<double>& values) {
        QVariantList result;
        for (double value : values) result.append(value);
        return result;
    };

    QVariantMap report;
    driver->resetMetrics();
    QVERIFY(invoke("startJoint"));
    QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying()&&media->property("playing").toBool(),15000);
    QCOMPARE(diagnostics().value("audioTracks").toInt(),8);
    auto all=levels();
    qInfo()<<"real eight-track levels"<<all;
    QVERIFY(allAudible(all));
    report["allLevels"]=asList(all);
    const int firstStart=starts.count();
    QVERIFY(click("referenceTrackMute"));
    auto muted=levels();
    qInfo()<<"real eight-track muted"<<muted;
    QCOMPARE(muted.size(),8);
    QVERIFY(muted.front()<.0005&&muted.back()>.0085);
    report["mutedLevels"]=asList(muted);
    QVERIFY(click("referenceTrackMute"));
    QVERIFY(click("referenceTrackSolo"));
    auto solo=levels();
    qInfo()<<"real eight-track solo"<<solo;
    QCOMPARE(solo.size(),8);
    QVERIFY(solo.front()>.0085);
    for (int i=1;i<solo.size();++i) QVERIFY(solo[i]<.0005);
    report["soloLevels"]=asList(solo);
    QVERIFY(click("referenceTrackSolo"));
    QVERIFY(allAudible(levels()));
    QVERIFY2(alignmentMaximumTicks<=26,qPrintable(QString("Score/reference difference: %1 ticks").arg(alignmentMaximumTicks)));
    report["alignmentMaximumTicks"]=alignmentMaximumTicks;
    QCOMPARE(starts.count(),firstStart);
    QCOMPARE(diagnostics().value("underruns").toULongLong(),qulonglong(0));
    report["mixedPlayback"]=driver->metrics();
    report["media"]=diagnostics();
    QVERIFY(invoke("stopJoint"));
    QTRY_VERIFY_WITH_TIMEOUT(!sequence->isPlaying()&&!media->property("playing").toBool(),3000);
    QVERIFY(!getAction("play")->isChecked());
    QVERIFY(!QWidget::mouseGrabber());

    // Turn the real piano back on, then let the reference panel repeat itself.
    synth->setGain(.08f);
    QVERIFY(click("referenceLoop"));
    QVERIFY(panel->property("looping").toBool());
    const int beforeLoop=starts.count();
    driver->resetMetrics();
    QVERIFY(invoke("startJoint"));
    QTRY_VERIFY_WITH_TIMEOUT(starts.count()>=beforeLoop+2,20000);
    QTRY_VERIFY(sequence->isPlaying()&&media->property("playing").toBool());
    QTRY_VERIFY(sequence->getCurTick()>480&&sequence->getCurTick()<8160);
    QCOMPARE(diagnostics().value("audioTracks").toInt(),8);
    QCOMPARE(diagnostics().value("underruns").toULongLong(),qulonglong(0));
    report["loopPlayback"]=driver->metrics();
    report["loopMedia"]=diagnostics();
    report["starts"]=starts.count();
    QVERIFY(invoke("stopJoint"));
    QTRY_VERIFY_WITH_TIMEOUT(!sequence->isPlaying()&&!media->property("playing").toBool(),3000);
    QTest::qWait(500);
    QVERIFY(!sequence->isPlaying()&&!media->property("playing").toBool());
    QVERIFY(!getAction("play")->isChecked());
    QVERIFY(!QWidget::mouseGrabber());
    QVERIFY(click("referenceLoop"));
    QVERIFY(!panel->property("looping").toBool());

    driver->resetMetrics();
    QVERIFY(invoke("startJoint"));
    QTRY_VERIFY_WITH_TIMEOUT(sequence->isPlaying()&&media->property("playing").toBool(),15000);
    QTRY_VERIFY_WITH_TIMEOUT(!sequence->isPlaying()&&!media->property("playing").toBool(),15000);
    QVERIFY(!getAction("play")->isChecked());
    QVERIFY(!QWidget::mouseGrabber());
    report["naturalEnd"]=driver->metrics();
    report["finalStarts"]=starts.count();
    for (const auto key : {"mixedPlayback","loopPlayback","naturalEnd"}) {
        const auto metrics=report.value(key).toMap();
        QVERIFY(metrics.value("frames").toULongLong()>qulonglong(driver->sampleRate()));
        QCOMPARE(metrics.value("lateBlocks").toULongLong(),qulonglong(0));
        QCOMPARE(metrics.value("outputUnderflows").toULongLong(),qulonglong(0));
    }
    const auto folder=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path());
    QFile result(folder+"/actual-joint-157.json");
    QVERIFY(result.open(QIODevice::WriteOnly));
    result.write(QJsonDocument::fromVariant(report).toJson());
    qInfo()<<"real joint audio"<<report;
#else
    QSKIP("Windows audio and VST host");
#endif
}
