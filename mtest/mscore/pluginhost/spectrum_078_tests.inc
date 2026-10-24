static QString spectrumAudio078(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))return QString();
    const int rate=22050, frames=24*rate;
    QDataStream data(&file);data.setByteOrder(QDataStream::LittleEndian);
    data.writeRawData("RIFF",4);data<<quint32(36+frames*2);data.writeRawData("WAVEfmt ",8);
    data<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(rate)<<quint32(rate*2)<<quint16(2)<<quint16(16);
    data.writeRawData("data",4);data<<quint32(frames*2);
    for(int i=0;i<frames;++i){
        const double frequency=440.*std::pow(2.,(i/(2*rate)%4)/12.);
        data<<qint16(14000*std::sin(2*3.14159265358979323846*frequency*i/rate));
    }
    return path;
}

static MasterScore* spectrumScore078()
{
    auto main=Ms::mscore;
    auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");
    if(!score)return nullptr;
    score->startCmd();
    for(int i=0;i<7;++i)score->insertMeasure(ElementType::MEASURE,score->firstMeasure());
    score->endCmd();score->doLayout();
    main->setCurrentScoreView(main->appendScore(score));
    main->resize(1280,960);main->show();
    return score;
}

static bool spectrumTracks078(Score* score,const QString& path)
{
    workflowContext()->setReferenceScore(score);
    QVariantMap response;
    const QVariantMap association{{"id","spectrum-range-078"},{"name",QString::fromUtf8("第3—4小节")},
        {"start",3840},{"end",7680},{"from",8000.},{"until",12000.},{"active",true}};
    const QVariantList tracks{QVariantMap{{"id","spectrum-source-078"},{"name","Pitch reference"},
        {"path",path},{"associations",QVariantList{association}}}};
    return QMetaObject::invokeMethod(referenceProject(),"setTracks",Q_RETURN_ARG(QVariantMap,response),
        Q_ARG(QVariantList,tracks))&&response.value("ok").toBool();
}

static void spectrumWheel078(QWidget* widget,const QPoint& point,int delta)
{
    QWheelEvent event(QPointF(point),QPointF(widget->mapToGlobal(point)),QPoint(),QPoint(0,delta),
                      Qt::NoButton,Qt::ControlModifier,Qt::NoScrollPhase,false);
    QApplication::sendEvent(widget,&event);
}

void TestPluginHost::spectrumInitialSourceAndChoice()
{
    auto score=spectrumScore078();QVERIFY(score);
    const auto path=spectrumAudio078(_settings.filePath("spectrum-source-078.wav"));QVERIFY(!path.isEmpty());
    showReferencePanel(true);
    QPointer<QmlPlugin> reference;
    for(auto window:QGuiApplication::allWindows())
        if(auto view=qobject_cast<QQuickView*>(window))
            if(auto root=qobject_cast<QmlPlugin*>(view->rootObject()))
                if(root->menuPath()=="Plugins.Reference Player")reference=root;
    QVERIFY(reference);
    const QVariant url=QUrl::fromLocalFile(path).toString();
    QVERIFY(QMetaObject::invokeMethod(reference,"openAudio",Q_ARG(QVariant,url)));
    auto fileSession=[&]() -> QObject* {
        const auto value=reference->property("media");
        return value.canConvert<QJSValue>()?value.value<QJSValue>().toQObject():value.value<QObject*>();
    };
    QTRY_VERIFY_WITH_TIMEOUT(fileSession()&&fileSession()->property("duration").toDouble()>23990.,10000);
    score->selection().setRangeTicks(Fraction::fromTicks(3840),Fraction::fromTicks(7680),0,score->nstaves());
    score->selection().updateSelectedElements();
    reference->setProperty("a",8000.);reference->setProperty("b",12000.);
    QVERIFY(QMetaObject::invokeMethod(reference,"associate"));
    QTRY_COMPARE(reference->property("selectedAssociation").toInt(),0);
    QVERIFY(QMetaObject::invokeMethod(reference,"selectAssociation",Q_ARG(QVariant,QVariant(0))));
    QCOMPARE(reference->property("a").toDouble(),8000.);
    QCOMPARE(reference->property("b").toDouble(),12000.);
    auto projectAssociation=[&] {
        return referenceProject()->property("tracks").toList().value(0).toMap()
            .value("associations").toList().value(0).toMap();
    };
    QVERIFY(!projectAssociation().value("id").toString().isEmpty());
    // Restore a real older project, whose associations did not have IDs.
    auto legacyTrack=referenceProject()->property("tracks").toList().front().toMap();
    auto legacyAssociation=projectAssociation();legacyAssociation.remove("id");
    legacyTrack["associations"]=QVariantList{legacyAssociation};
    const QVariant legacyProject=QVariantMap{{"tracks",QVariantList{legacyTrack}},{"selectedTrack",0}};
    const QVariant emptyUrl=QString();
    QVERIFY(QMetaObject::invokeMethod(reference,"restoreTracks",Q_ARG(QVariant,legacyProject),Q_ARG(QVariant,emptyUrl)));
    QTRY_VERIFY_WITH_TIMEOUT(fileSession()&&fileSession()->property("duration").toDouble()>23990.,10000);
    const QString selectedClipId="reference-"+legacyTrack.value("id").toString()+"-0";
    QTRY_COMPARE(projectAssociation().value("id").toString(),selectedClipId);
    QCOMPARE(projectAssociation().value("from").toDouble(),8000.);
    QCOMPARE(projectAssociation().value("until").toDouble(),12000.);
    showReferencePanel(false);
    auto main=Ms::mscore;main->showPerformanceEditor(true);auto editor=main->performanceEditor();
    auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs");QVERIFY(tabs);tabs->setCurrentIndex(2);
    auto pane=editor->findChild<QWidget*>("transcriptionSpectrumPane");QVERIFY(pane);
    QTRY_VERIFY(pane->isVisible());
    auto choice=pane->findChild<QComboBox*>("transcriptionClips");QVERIFY(choice);
    QTRY_COMPARE(choice->count(),2);
    QCOMPARE(choice->currentIndex(),0);
    auto session=pane->findChild<QObject*>("transcriptionSpectrumSession");QVERIFY(session);
    QTRY_COMPARE_WITH_TIMEOUT(session->property("info").toMap().value("sourcePath").toString(),path,10000);
    QTRY_VERIFY_WITH_TIMEOUT(!qvariant_cast<QImage>(session->property("image")).isNull(),10000);
    QVERIFY(session->property("info").toMap().value("from").toDouble()<1000);
    auto fit=pane->findChild<QToolButton*>("spectrumFitTime");QVERIFY(fit);
    QTRY_VERIFY(fit->isEnabled());
    QTest::mouseClick(fit,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(session->property("info").toMap().value("until").toDouble()>=23990.,10000);
    QVERIFY(!qvariant_cast<QImage>(session->property("image")).isNull());
    choice->setCurrentIndex(1);QTest::qWait(100);
    QCOMPARE(choice->currentData().toMap().value("id").toString(),selectedClipId);
    tabs->setCurrentIndex(0);tabs->setCurrentIndex(2);QTest::qWait(150);
    QCOMPARE(choice->currentIndex(),1);
    choice->setCurrentIndex(0);QTest::qWait(100);
    auto media=referenceProject();auto tracks=media->property("tracks").toList();
    auto other=tracks.front().toMap();other["id"]="spectrum-other-078";tracks.append(other);
    QVariantMap result;QVERIFY(QMetaObject::invokeMethod(media,"setTracks",Q_RETURN_ARG(QVariantMap,result),Q_ARG(QVariantList,tracks)));
    auto trackChoice=pane->findChild<QComboBox*>("transcriptionTracks");QVERIFY(trackChoice);trackChoice->setCurrentIndex(1);
    QCOMPARE(choice->currentIndex(),0);choice->setCurrentIndex(1);trackChoice->setCurrentIndex(0);
    QCOMPARE(choice->currentIndex(),0);trackChoice->setCurrentIndex(1);QCOMPARE(choice->currentIndex(),1);
    tabs->setCurrentIndex(0);
}

void TestPluginHost::spectrumLinkedZoomAndScroll()
{
    auto score=spectrumScore078();QVERIFY(score);
    const auto path=spectrumAudio078(_settings.filePath("spectrum-zoom-078.wav"));QVERIFY(!path.isEmpty());
    QVERIFY(spectrumTracks078(score,path));
    auto main=Ms::mscore;main->showPerformanceEditor(true);auto editor=main->performanceEditor();
    auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs");QVERIFY(tabs);tabs->setCurrentIndex(2);
    auto pane=editor->findChild<QWidget*>("transcriptionSpectrumPane");QVERIFY(pane);
    auto choice=pane->findChild<QComboBox*>("transcriptionClips");QVERIFY(choice);
    QTRY_COMPARE(choice->count(),2);choice->setCurrentIndex(1);
    auto display=pane->findChild<QComboBox*>("transcriptionDisplay");QVERIFY(display);display->setCurrentIndex(0);
    editor->findChild<QAction*>("editorArea/float/transcriptionSpectrumPane")->trigger();
    auto dock=main->findChild<QDockWidget*>("kumoEditorArea/transcriptionSpectrumPane");QVERIFY(dock);
    dock->resize(900,640);QTest::qWait(200);
    auto canvas=pane->findChild<QWidget*>("transcriptionSpectrumCanvas");QVERIFY(canvas);
    auto session=pane->findChild<QObject*>("transcriptionSpectrumSession");QVERIFY(session);
    QTRY_VERIFY_WITH_TIMEOUT(!qvariant_cast<QImage>(session->property("image")).isNull(),10000);
    QScrollBar* scroll=nullptr;
    for(auto bar:editor->findChildren<QScrollBar*>(QString(),Qt::FindDirectChildrenOnly))
        if(bar->orientation()==Qt::Horizontal)scroll=bar;
    QVERIFY(scroll);scroll->setValue(0);editor->fit(false);QTest::qWait(200);
    const int before=scroll->pageStep();
    spectrumWheel078(canvas,QPoint(PerformanceViewport::gutter+120,canvas->height()/2),120);
    QTest::qWait(150);
    qInfo("spectrum zoom pageStep: %d -> %d",before,scroll->pageStep());
    QVERIFY2(scroll->pageStep()<before,"频谱缩放后，编辑器滚动条必须一起更新可见拍数");
    auto fit=pane->findChild<QToolButton*>("spectrumFitTime");QVERIFY(fit&&fit->isEnabled());
    QTest::mouseClick(fit,Qt::LeftButton);QTest::qWait(200);
    QTRY_VERIFY_WITH_TIMEOUT(!session->property("busy").toBool(),10000);
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));
    QVERIFY(dock->grab().save(output.filePath("spectrum-linked-fit-078.png")));
    const auto linkedInfo=session->property("info").toMap();
    QCOMPARE(linkedInfo.value("from").toDouble(),8000.);
    QCOMPARE(linkedInfo.value("until").toDouble(),12000.);
    const int center=PerformanceViewport::gutter+(canvas->width()-PerformanceViewport::gutter-PerformanceViewport::rightMargin)/2;
    const int step=scroll->pageStep();
    spectrumWheel078(canvas,QPoint(center,canvas->height()/2),120);
    QTRY_VERIFY_WITH_TIMEOUT(session->property("info").toMap().value("from").toDouble()>8000.,10000);
    QTRY_VERIFY_WITH_TIMEOUT(!session->property("busy").toBool(),10000);
    auto zoomed=session->property("info").toMap();
    QVERIFY(zoomed.value("until").toDouble()<12000.);
    QVERIFY(qAbs((zoomed.value("from").toDouble()+zoomed.value("until").toDouble())/2.-10000.)<30.);
    QVERIFY(scroll->pageStep()<step);
    QCOMPARE(zoomed.value("computedBlocks").toInt(),0);
    QVERIFY(zoomed.value("cachedBlocks").toInt()>0);
    auto spectrumScroll=pane->findChild<QScrollBar*>("spectrumTimeScroll");QVERIFY(spectrumScroll);
    QCOMPARE(spectrumScroll->pageStep(),scroll->pageStep());QCOMPARE(spectrumScroll->maximum(),scroll->maximum());
    QVERIFY(dock->grab().save(output.filePath("spectrum-linked-zoom-078.png")));
    const int current=scroll->value();spectrumScroll->setValue(current+500);QCOMPARE(scroll->value(),current+500);
    QTRY_VERIFY_WITH_TIMEOUT(session->property("info").toMap().value("from").toDouble()>zoomed.value("from").toDouble(),10000);
    scroll->setValue(scroll->maximum());QTest::qWait(150);
    QVERIFY(dock->grab().save(output.filePath("spectrum-linked-outside-078.png")));
    const int outsideUndo=score->undoStack()->getCurIdx();
    QTest::mouseClick(canvas,Qt::LeftButton,Qt::NoModifier,QPoint(center,canvas->height()/2));
    auto outsideMark=pane->findChild<QToolButton*>("transcriptionMark");QVERIFY(outsideMark&&!outsideMark->isEnabled());
    QCOMPARE(score->undoStack()->getCurIdx(),outsideUndo);
    QTest::mouseClick(fit,Qt::LeftButton);QTest::qWait(150);
    QTRY_VERIFY_WITH_TIMEOUT(!session->property("busy").toBool(),10000);
    auto roll=editor->findChild<QWidget*>("performanceNoteCanvas");QVERIFY(roll);
    const int rollStep=scroll->pageStep();
    const auto beforeRollZoom=session->property("info").toMap();
    spectrumWheel078(roll,QPoint(center,roll->height()/2),120);QTest::qWait(150);
    QVERIFY(scroll->pageStep()<rollStep);
    QTRY_VERIFY_WITH_TIMEOUT(session->property("info").toMap().value("from").toDouble()>8000.,10000);
    QTRY_VERIFY_WITH_TIMEOUT(session->property("info").toMap().value("until").toDouble()-
        session->property("info").toMap().value("from").toDouble()<
        beforeRollZoom.value("until").toDouble()-beforeRollZoom.value("from").toDouble()-100.,10000);
    QCOMPARE(spectrumScroll->pageStep(),scroll->pageStep());
    QVERIFY(dock->grab().save(output.filePath("spectrum-editor-zoom-split-082.png")));
    auto media=referenceProject();auto anchoredTracks=media->property("tracks").toList();
    auto anchoredTrack=anchoredTracks.front().toMap();auto associations=anchoredTrack.value("associations").toList();
    auto association=associations.front().toMap();association["anchors"]=QVariantList{QVariantMap{{"tick",5760},{"ms",11000.}}};
    associations[0]=association;anchoredTrack["associations"]=associations;anchoredTracks[0]=anchoredTrack;
    QVariantMap anchorResponse;QVERIFY(QMetaObject::invokeMethod(media,"setTracks",Q_RETURN_ARG(QVariantMap,anchorResponse),Q_ARG(QVariantList,anchoredTracks)));
    QTest::mouseClick(fit,Qt::LeftButton);QTest::qWait(150);
    QTRY_VERIFY_WITH_TIMEOUT(!session->property("busy").toBool(),10000);
    auto mark=pane->findChild<QToolButton*>("transcriptionMark");QVERIFY(mark);
    const QPoint pick(center,qRound((108.5-69)/88.*canvas->height()));
    QTest::mouseClick(canvas,Qt::LeftButton,Qt::NoModifier,pick);QTest::mouseClick(mark,Qt::LeftButton);
    auto pitches=QJsonDocument::fromJson(score->metaTag("kumo.transcription").toUtf8()).toVariant().toMap().value("pitches").toList();
    QVERIFY(!pitches.isEmpty());QVERIFY(qAbs(pitches.back().toMap().value("ms").toDouble()-11000.)<20.);
    const int markUndo=score->undoStack()->getCurIdx();
    spectrumWheel078(canvas,pick,120);QTest::qWait(150);
    QTest::mouseClick(canvas,Qt::LeftButton,Qt::NoModifier,pick);QTest::mouseClick(mark,Qt::LeftButton);
    pitches=QJsonDocument::fromJson(score->metaTag("kumo.transcription").toUtf8()).toVariant().toMap().value("pitches").toList();
    QVERIFY(qAbs(pitches.back().toMap().value("ms").toDouble()-11000.)<20.);
    QCOMPARE(score->undoStack()->getCurIdx(),markUndo+1);
    main->cmd(getAction("undo"));
    QVERIFY(dock->grab().save(output.filePath("spectrum-linked-anchor-078.png")));
    display->setCurrentIndex(1);QTest::qWait(150);
    QTest::mouseClick(fit,Qt::LeftButton);QTest::qWait(200);
    QTRY_VERIFY_WITH_TIMEOUT(!session->property("busy").toBool(),10000);
    QVERIFY(editor->grab().save(output.filePath("spectrum-linked-overlay-078.png")));
    display->setCurrentIndex(0);choice->setCurrentIndex(0);QTest::qWait(150);
    QTest::mouseClick(fit,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(session->property("info").toMap().value("until").toDouble()>23000.,10000);
    QTRY_VERIFY_WITH_TIMEOUT(!session->property("busy").toBool(),10000);
    QVERIFY(dock->grab().save(output.filePath("spectrum-whole-source-078.png")));
    const double sourceSpan=session->property("info").toMap().value("until").toDouble()-session->property("info").toMap().value("from").toDouble();
    spectrumWheel078(canvas,QPoint(center,canvas->height()/2),120);
    QTRY_VERIFY_WITH_TIMEOUT(session->property("info").toMap().value("until").toDouble()-session->property("info").toMap().value("from").toDouble()<sourceSpan,10000);
    display->setCurrentIndex(1);QTest::qWait(150);
    const auto rawInfo=session->property("info").toMap();
    const double rawSpan=rawInfo.value("until").toDouble()-rawInfo.value("from").toDouble();
    spectrumWheel078(roll,QPoint(center,roll->height()/2),120);
    QTRY_VERIFY_WITH_TIMEOUT(session->property("info").toMap().value("until").toDouble()-session->property("info").toMap().value("from").toDouble()<rawSpan-100,10000);
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(session->property("info").toMap().value("until").toDouble()-
        session->property("info").toMap().value("from").toDouble()-rawSpan/1.2)<50.,10000);
    // A source overlay must follow the editor's other zoom controls too.
    tabs->setCurrentIndex(0);QTest::qWait(150);
    QVERIFY(pane->isVisible());
    QTRY_VERIFY_WITH_TIMEOUT(!session->property("busy").toBool(),10000);
    const auto beforeFit=session->property("info").toMap();
    const double beforeFitSpan=beforeFit.value("until").toDouble()-beforeFit.value("from").toDouble();
    QPushButton* whole=nullptr;
    for(auto button:editor->findChildren<QPushButton*>())
        if(button->text()==QString::fromUtf8("全曲"))whole=button;
    QVERIFY(whole);
    auto toolbar=editor->findChild<QScrollArea*>("performanceControlScroll");QVERIFY(toolbar);
    toolbar->ensureWidgetVisible(whole);QTRY_VERIFY(whole->isVisible());
    QTest::mouseClick(whole,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(session->property("info").toMap().value("until").toDouble()-
        session->property("info").toMap().value("from").toDouble()>beforeFitSpan+100,10000);
    QTRY_VERIFY_WITH_TIMEOUT(!session->property("busy").toBool(),10000);
    QVERIFY(editor->grab().save(output.filePath("spectrum-editor-fit-overlay-084.png")));
    QTest::mouseClick(fit,Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(session->property("info").toMap().value("until").toDouble()-24000.)<50.,10000);
    auto parameter=editor->findChild<QWidget*>("performanceParameterCanvas");QVERIFY(parameter&&parameter->isVisible());
    const int zoomUndo=score->undoStack()->getCurIdx();
    spectrumWheel078(parameter,QPoint(PerformanceViewport::gutter,parameter->height()/2),120);
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(session->property("info").toMap().value("until").toDouble()-
        session->property("info").toMap().value("from").toDouble()-20000.)<50.,10000);
    QTRY_VERIFY_WITH_TIMEOUT(!session->property("busy").toBool(),10000);
    QCOMPARE(score->undoStack()->getCurIdx(),zoomUndo);
    QCOMPARE(session->property("info").toMap().value("computedBlocks").toInt(),0);
    QVERIFY(editor->grab().save(output.filePath("spectrum-parameter-zoom-overlay-084.png")));
    display->setCurrentIndex(0);
    QTest::qWait(150);
    const auto independent=session->property("info").toMap();
    QTest::mouseClick(whole,Qt::LeftButton);QTest::qWait(150);
    QCOMPARE(session->property("info").toMap().value("from"),independent.value("from"));
    QCOMPARE(session->property("info").toMap().value("until"),independent.value("until"));
    editor->findChild<QAction*>("editorArea/restore/transcriptionSpectrumPane")->trigger();
    tabs->setCurrentIndex(0);
}
