// GPL-2.0-or-later. Resize gestures followed by actual reference/palette input.
#include <QSplitter>
void TestPluginHost::resizePanelsAfterPlayback121()
{
    auto main=Ms::mscore;
    guiScaling=1.;MuseScore::updateUiStyleAndTheme();
    if(!WorkspacesManager::currentWorkspace())WorkspacesManager::initCurrentWorkspace();
    auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());synth->setSampleRate(44100);synth->init();
    QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
    QScopedValueRollback<MasterSynthesizer*> currentSynth(Ms::synti,synth.get());QScopedValueRollback<bool> sequencing(noSeq,false);
    QVERIFY(!Ms::seq);auto sequence=new Seq;Ms::seq=sequence;
    std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* s){workflowContext()->finishPlaybackSession();s->stopWait();s->synchronizeStoppedGui();s->waitForStoppedRendering();s->setScoreView(nullptr);delete s;Ms::seq=nullptr;});
    auto driver=new ReferenceCallbackDriver120(sequence);sequence->setDriver(driver);sequence->setMasterSynthesizer(synth.get());QVERIFY(sequence->init());sequence->setScoreView(main->currentScoreView());
    QObject::connect(sequence,SIGNAL(started()),main,SLOT(seqStarted()));QObject::connect(sequence,SIGNAL(stopped()),main,SLOT(seqStopped()));
    main->findChild<QAction*>("kumo-workspace-arrange")->trigger();main->resize(1360,920);main->show();
    if(!main->getPaletteWidget()||main->getPaletteWidget()->isHidden())getAction("toggle-palette")->trigger();
    auto palette=main->getPaletteWidget();QVERIFY(palette);main->resizeDocks({palette},{240},Qt::Horizontal);
    auto paletteWindow=const_cast<QQuickView*>(palette->view());QVERIFY(paletteWindow&&paletteWindow->rootObject());
    {QList<QQuickItem*> pending{paletteWindow->rootObject()};while(!pending.isEmpty()){auto item=pending.takeFirst();pending.append(item->childItems());if(!item->property("placeholderText").toString().isEmpty())item->setProperty("text",QString());}}
    main->showPerformanceEditor(true);showReferencePanel(true);QTest::qWait(160);
    QmlPlugin* root=nullptr;
    for(auto w:QGuiApplication::allWindows())if(auto v=qobject_cast<QQuickView*>(w))if(auto p=qobject_cast<QmlPlugin*>(v->rootObject()))if(p->menuPath()=="Plugins.Reference Player")root=p;
    QVERIFY(root);
    QVERIFY(QMetaObject::invokeMethod(root,"setPanelFloating",Q_ARG(bool,true)));
    QVERIFY(QMetaObject::invokeMethod(root,"resizePanel",Q_ARG(int,920),Q_ARG(int,680)));
    QTemporaryDir audio(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("resize121-XXXXXX"));QVERIFY(audio.isValid());
    const auto path=audio.filePath("reference.wav");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));
    const int frames=22050*4;QDataStream data(&file);data.setByteOrder(QDataStream::LittleEndian);data.writeRawData("RIFF",4);data<<quint32(36+frames*2);data.writeRawData("WAVEfmt ",8);data<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(22050)<<quint32(44100)<<quint16(2)<<quint16(16);data.writeRawData("data",4);data<<quint32(frames*2);for(int i=0;i<frames;++i)data<<qint16(0);file.close();
    QVERIFY(QMetaObject::invokeMethod(root,"openAudio",Q_ARG(QVariant,QUrl::fromLocalFile(path).toString())));
    auto object=[&](const char* property){auto v=root->property(property);return v.canConvert<QJSValue>()?v.value<QJSValue>().toQObject():v.value<QObject*>();};
    QTRY_VERIFY_WITH_TIMEOUT(object("media")&&object("media")->property("duration").toDouble()>3900,10000);
    object("media")->setProperty("volume",0.);root->setProperty("jointVolume",0.);auto project=referenceProject();QVERIFY(project->property("available").toBool());
    auto click=[&](const char* name){auto item=root->findChild<QQuickItem*>(name);if(!item&&QByteArray(name)=="referenceLoop")for(auto candidate:root->findChildren<QQuickItem*>())if(candidate->property("text").toString()==QString::fromUtf8("循环")&&candidate->isVisible()){item=candidate;break;}if(!item||!item->isVisible()||!item->isEnabled())return false;auto point=item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint();QTest::mouseClick(root->window(),Qt::LeftButton,Qt::NoModifier,point);return true;};
    auto clickHeader=[&]{QList<QQuickItem*> queue{paletteWindow->rootObject()};while(!queue.isEmpty()){auto item=queue.takeFirst();queue.append(item->childItems());if(!QByteArray(item->metaObject()->className()).startsWith("TreePaletteHeader")||!item->isVisible())continue;auto point=item->mapToScene(QPointF(10,item->height()/2)).toPoint();if(!paletteWindow->rootObject()->contains(point))continue;bool before=item->property("expanded").toBool();QTest::mouseClick(paletteWindow,Qt::LeftButton,Qt::NoModifier,point);QTest::qWait(50);return item->property("expanded").toBool()!=before;}return false;};
    auto dump=[&]{qInfo()<<"resize121"<<main->state()<<main->currentScoreView()->mscoreState()<<"palette"<<palette->isEnabled()<<"grab"<<QWidget::mouseGrabber()<<"quick"<<root->window()->mouseGrabberItem()<<paletteWindow->mouseGrabberItem()<<"modal"<<QGuiApplication::modalWindow()<<"audio"<<project->property("playing")<<"music"<<sequence->isPlaying()<<"cursor"<<main->currentScoreView()->cursor().shape();};

    auto editor=main->performanceEditor();auto view=main->currentScoreView();
    editor->findChild<QAction*>("performanceBandPosition0")->trigger();
    editor->setScoreBandEnabled(true);
    main->showPerformanceEditor(false);QTest::qWait(180);view->repaint();
    auto edge=[&]() {
        const auto image=view->grab().toImage();int top=image.height(),left=image.width(),right=0;
        for(int y=0;y<image.height();++y){int count=0,l=image.width(),r=0;for(int x=0;x<image.width();++x)if(image.pixelColor(x,y).rgb()==QColor("#f1f2ef").rgb()){++count;l=qMin(l,x);r=qMax(r,x);}if(count>100){top=y;left=l;right=r;break;}}
        image.save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("resize-before-121.png"));
        qInfo()<<"strip edge"<<left<<right<<top<<view->size()<<"cursor"<<view->cursor().shape();
        return QPoint(qRound((left+right)/2/image.devicePixelRatio()),qRound(top/image.devicePixelRatio()));
    };
    const auto hoverEdge=edge();
    // The floating player can cover this part of the score; deliver the two
    // local hover events to the score rather than moving the desktop pointer.
    for(const auto point:{hoverEdge,hoverEdge+QPoint(0,40)}) {
        QMouseEvent hover(QEvent::MouseMove,point,view->mapToGlobal(point),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
        QCoreApplication::sendEvent(view,&hover);
        QCOMPARE(view->cursor().shape(),point==hoverEdge?Qt::SizeVerCursor:Qt::CrossCursor);
    }
    for(int round=0;round<4;++round){
        root->setProperty("transportTarget",0);root->setProperty("looping",false);
        QVERIFY(click("referenceJointPlay"));QTRY_VERIFY(project->property("playing").toBool());
        const auto point=edge();QVERIFY(view->rect().contains(point));
        QTest::mousePress(view,Qt::LeftButton,Qt::NoModifier,point);dump();QCOMPARE(QWidget::mouseGrabber(),view);
        QMouseEvent move(QEvent::MouseMove,point+QPoint(0,-12),view->mapToGlobal(point+QPoint(0,-12)),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
        QCoreApplication::sendEvent(view,&move);QTest::qWait(20);
        if(round==0){
            // A later native move reports the button already released. The pending resize must end.
            QMouseEvent releasedMove(QEvent::MouseMove,point,view->mapToGlobal(point),Qt::NoButton,Qt::NoButton,Qt::NoModifier);
            QCoreApplication::sendEvent(view,&releasedMove);
        } else if(round==1){
            // A QWindow-backed panel receives the next press after the interrupted gesture.
            bool before=root->property("looping").toBool();QVERIFY(click("referenceLoop"));QCOMPARE(root->property("looping").toBool(),!before);
        } else if(round==2){
            view->window()->activateWindow();QVERIFY(QTest::qWaitForWindowActive(view->window(),2000));
            root->focusPanel();
        } else QTest::mouseRelease(view,Qt::LeftButton,Qt::NoModifier,point+QPoint(0,-12));
        dump();QTRY_VERIFY2(QWidget::mouseGrabber()==nullptr,"尺寸拖动结束后释放鼠标，无需切换工作视图");
        QVERIFY(view->cursor().shape()!=Qt::SizeVerCursor);QVERIFY(clickHeader());
        QVERIFY(click("referenceJointPlay"));QTRY_VERIFY(!project->property("playing").toBool());
        if(round<3)QTest::mouseRelease(view,Qt::LeftButton,Qt::NoModifier,point);
    }
    editor->setScoreBandEnabled(false);main->showPerformanceEditor(true);
    main->resizeDocks({main->findChild<QDockWidget*>("performanceEditorDock")},{580},Qt::Vertical);QTest::qWait(150);
    auto split=editor->findChild<QSplitter*>("performanceMainSplitter");QVERIFY(split);
    auto handle=split->handle(1);QVERIFY(handle&&handle->isVisible());
    split->setSizes({200,200,0,0,0,0});QTest::qWait(80);
    const auto sizes=split->sizes();const auto center=handle->rect().center();
    qInfo()<<"native splitter before"<<sizes<<handle->geometry()<<split->size();
    QTest::mousePress(handle,Qt::LeftButton,Qt::NoModifier,center);
    QMouseEvent move(QEvent::MouseMove,center+QPoint(0,-25),handle->mapToGlobal(center+QPoint(0,-25)),Qt::NoButton,Qt::LeftButton,Qt::NoModifier);
    QCoreApplication::sendEvent(handle,&move);QTest::mouseRelease(handle,Qt::LeftButton,Qt::NoModifier,center+QPoint(0,-25));
    qInfo()<<"native splitter after"<<split->sizes();
    QVERIFY(split->sizes()!=sizes);QVERIFY(clickHeader());
    bool before=root->property("looping").toBool();QVERIFY(click("referenceLoop"));QCOMPARE(root->property("looping").toBool(),!before);
    QVERIFY(!QWidget::mouseGrabber());
    QVERIFY(main->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("resize-panels-121.png")));
    showReferencePanel(false);
}

void TestPluginHost::personalToolbarIcons121()
{
    auto main=Ms::mscore;
    auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    main->resize(1360,920);main->show();QTest::qWait(100);
    main->populateAlternativeOperations();
    auto toolbar=main->findChild<QToolBar*>("kumoPersonalTools");QVERIFY(toolbar);
    main->insertToolBarBreak(toolbar);QTest::qWait(80);
    const QList<QPair<QString,QString>> keys{
      {"harmony-assistant","和弦助手"},{"reference-player","原曲参考"},
      {"performance-expression","演奏表达"},{"kumo-spectrum","频谱扒谱"},
      {"arrangement-suggestions","编配建议"},{"kumo-vst","钢琴音源"},
      {"kumo-rhythm","写节奏"},{"kumo-roll-writing","卷帘写音"},
      {"kumo-system-lock","锁定谱行"},{"kumo-ai-connection","AI连接"}};
    QImage gallery(800,152,QImage::Format_ARGB32_Premultiplied);gallery.fill(qApp->palette().color(QPalette::Window));
    QPainter painter(&gallery);painter.setPen(qApp->palette().color(QPalette::WindowText));
    QList<QImage> seen;int center=-1;
    for(int i=0;i<keys.size();++i){
      const auto icon=personalToolIcon(keys[i].first);const auto pixels=icon.pixmap(24,24).toImage();QVERIFY(!pixels.isNull());
      QRect bounds;for(int y=0;y<pixels.height();++y)for(int x=0;x<pixels.width();++x)if(qAlpha(pixels.pixel(x,y)))bounds|=QRect(x,y,1,1);
      QVERIFY(!bounds.isEmpty());
      qInfo()<<keys[i].first<<pixels.size()<<bounds;
      QVERIFY(qAbs(bounds.center().x()-(pixels.width()-1)/2.)<=1.);
      QVERIFY(qAbs(bounds.center().y()-(pixels.height()-1)/2.)<=1.);
      for(const auto& other:seen)QVERIFY(pixels!=other);seen.append(pixels);
      painter.drawPixmap(QPoint(i*80+28,12),icon.pixmap(24,24));
      painter.drawText(QRect(i*80,42,80,24),Qt::AlignCenter,keys[i].second);
      painter.drawPixmap(QPoint(i*80+32,80),icon.pixmap(16,16));
      if(i<6){
        auto button=toolbar->findChild<QToolButton*>(keys[i].first+"-button");QVERIFY(button);
        QVERIFY(button->isVisible());
        QCOMPARE(button->iconSize(),toolbar->iconSize());QVERIFY(!button->toolTip().isEmpty());
        if(center<0)center=button->geometry().center().y();else QCOMPARE(button->geometry().center().y(),center);
      }
    }
    painter.end();const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));
    QVERIFY(gallery.save(output.filePath("personal-icons-121.png")));
    QVERIFY(toolbar->grab().save(output.filePath("personal-toolbar-121.png")));
}
