static MasterScore* layoutScore079()
{
    auto main=Ms::mscore;
    auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");
    if(score)main->setCurrentScoreView(main->appendScore(score));
    main->resize(1280,960);main->show();
    return score;
}

void TestPluginHost::editorAreaSizeRoundTrip()
{
    auto main=Ms::mscore;
    auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));main->resize(1280,960);main->show();
    main->findChild<QAction*>("kumo-workspace-arrange")->trigger();
    main->showPerformanceEditor(true);auto editor=main->performanceEditor();
    auto dock=main->findChild<QDockWidget*>("performanceEditorDock");QVERIFY(dock);
    dock->setFloating(false);main->resizeDocks({dock},{550},Qt::Vertical);
    auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs");QVERIFY(tabs);tabs->setCurrentIndex(0);
    auto split=editor->findChild<QSplitter*>("performanceMainSplitter");QVERIFY(split);
    auto notes=editor->findChild<QWidget*>("performanceNotesArea");QVERIFY(notes);
    auto parameters=editor->findChild<QWidget*>("performanceParametersArea");QVERIFY(parameters);
    QTest::qWait(200);
    auto handle=split->handle(split->indexOf(parameters));QVERIFY(handle&&handle->isVisible());
    const auto point=handle->mapTo(handle->window(),handle->rect().center());
    auto window=handle->window()->windowHandle();
    QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,point);
    QTest::mouseMove(window,point-QPoint(0,35),30);
    QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,point-QPoint(0,35));
    QTest::qWait(100);
    const double ratio=double(notes->height())/(notes->height()+parameters->height());
    const int dockHeight=dock->height();
    QVERIFY(ratio>.05&&ratio<.95);
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));
    QVERIFY(editor->grab().save(output.filePath("layout-area-before-079.png")));
    tabs->setCurrentIndex(2);QTest::qWait(100);tabs->setCurrentIndex(0);QTest::qWait(150);
    const double tabRatio=double(notes->height())/(notes->height()+parameters->height());
    qInfo("area ratio before %.3f, after tool page %.3f",ratio,tabRatio);
    QVERIFY2(qAbs(tabRatio-ratio)<.035,"切换工具页后保留已调好的区域比例");
    for(int repeat=0;repeat<3;++repeat){
        main->findChild<QAction*>("kumo-workspace-daw")->trigger();QTest::qWait(150);
        main->findChild<QAction*>("kumo-workspace-engrave")->trigger();QTest::qWait(150);
        main->findChild<QAction*>("kumo-workspace-arrange")->trigger();QTest::qWait(150);
        QVERIFY(dock->isVisible());
        const double actual=double(notes->height())/(notes->height()+parameters->height());
        qInfo("workspace return %d, dock %d/%d, ratio %.3f/%.3f",repeat,dock->height(),dockHeight,actual,ratio);
        QVERIFY2(qAbs(actual-ratio)<.035,"回到编曲后保留区域比例");
        QVERIFY2(qAbs(dock->height()-dockHeight)<12,"回到编曲后保留整个编辑器高度");
    }
    QVERIFY(editor->grab().save(output.filePath("layout-area-after-079.png")));
}

void TestPluginHost::engravingFirstUseLeavesScoreSpace()
{
    QVERIFY(layoutScore079());
    auto main=Ms::mscore;
    main->findChild<QAction*>("kumo-workspace-arrange")->trigger();
    QSettings().remove("personalWorkspaceViews/engrave/layout");
    main->showPerformanceEditor(true);
    auto input=main->findChild<QToolBar*>("entry-tools");QVERIFY(input);input->show();
    main->findChild<QAction*>("kumo-workspace-engrave")->trigger();QTest::qWait(150);
    auto dock=main->findChild<QDockWidget*>("performanceEditorDock");QVERIFY(dock);
    QVERIFY(!main->centralWidget()->isHidden());
    QVERIFY2(!dock->isVisible(),"初次进入雕版时收起演奏编辑器，优先显示乐谱");
    QVERIFY(!input->isVisible());
    main->currentScoreView()->setBackground(main->palette().color(QPalette::Window));
    QTest::qWait(80);
    QVERIFY(main->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("layout-engraving-first-079.png")));
    main->showPerformanceEditor(true);input->toggleViewAction()->trigger();
    QTRY_VERIFY(input->isVisible());
    main->findChild<QAction*>("kumo-workspace-arrange")->trigger();QTest::qWait(100);
    QTRY_VERIFY(dock->isVisible());QVERIFY(input->isVisible());
    main->findChild<QAction*>("kumo-workspace-engrave")->trigger();QTest::qWait(100);
    QVERIFY(dock->isVisible());QVERIFY(input->isVisible());
    main->findChild<QAction*>("kumo-workspace-arrange")->trigger();
}

void TestPluginHost::legacyEditorAreaStateStaysUsable()
{
    QVERIFY(layoutScore079());
    auto main=Ms::mscore;main->findChild<QAction*>("kumo-workspace-arrange")->trigger();
    main->resize(1280,960);main->showPerformanceEditor(true);
    auto editor=main->performanceEditor();auto dock=main->findChild<QDockWidget*>("performanceEditorDock");QVERIFY(dock);
    dock->setFloating(false);main->resizeDocks({dock},{650},Qt::Vertical);
    auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs");QVERIFY(tabs);tabs->setCurrentIndex(0);
    auto split=editor->findChild<QSplitter*>("performanceMainSplitter");QVERIFY(split);QTest::qWait(120);
    const auto normal=split->saveState();auto sizes=split->sizes();
    for(int i=0;i<split->count();++i)sizes[i]=split->widget(i)->objectName()=="performanceNotesArea"?1:700;
    split->setSizes(sizes);const auto legacy=split->saveState();split->restoreState(normal);
    QSettings().setValue("performanceEditor/modeLayout/2",legacy);
    QSettings().setValue("performanceEditor/areaHeights/arrange/2",QVariantMap{{"performanceNotesArea",160},{"transcriptionSpectrumPane",240}});
    tabs->setCurrentIndex(2);QTest::qWait(150);
    auto notes=editor->findChild<QWidget*>("performanceNotesArea"),spectrum=editor->findChild<QWidget*>("transcriptionSpectrumPane");
    QVERIFY(notes&&spectrum);QVERIFY(notes->isVisible()&&spectrum->isVisible());
    const double ratio=double(notes->height())/(notes->height()+spectrum->height());
    qInfo("mixed legacy area ratio %.3f",ratio);QVERIFY(qAbs(ratio-.4)<.06);
    for(const QSize size:{QSize(1120,850),QSize(1280,960)}){
        main->resize(size);QTest::qWait(100);QVERIFY(notes->height()>30);QVERIFY(spectrum->height()>=180);
        main->showPerformanceEditor(false);main->showPerformanceEditor(true);QTest::qWait(100);
        QVERIFY(notes->isVisible()&&spectrum->isVisible());QVERIFY(notes->height()>30);
    }
    tabs->setCurrentIndex(0);
}

void TestPluginHost::rhythmPageKeepsNavigationAndHeight()
{
    QVERIFY(layoutScore079());
    auto main=Ms::mscore;main->findChild<QAction*>("kumo-workspace-arrange")->trigger();
    main->resize(1280,960);main->showPerformanceEditor(true);auto editor=main->performanceEditor();
    auto dock=main->findChild<QDockWidget*>("performanceEditorDock");QVERIFY(dock);
    dock->setFloating(false);
    auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs");QVERIFY(tabs);tabs->setCurrentIndex(0);QTest::qWait(150);
    main->resizeDocks({dock},{550},Qt::Vertical);QTest::qWait(100);
    const int height=dock->height();
    QTest::mouseClick(tabs,Qt::LeftButton,Qt::NoModifier,tabs->tabRect(3).center());QTest::qWait(150);
    QCOMPARE(tabs->currentIndex(),3);
    qInfo("rhythm page visible %d, dock height %d -> %d",tabs->isVisible(),height,dock->height());
    QVERIFY2(tabs->isVisible(),"写节奏中可直接看见并点击其它工具页");
    auto area=editor->findChild<QToolButton*>("performanceAreaMenu");QVERIFY(area&&area->isVisible());
    qInfo("rhythm dock height %d -> %d",height,dock->height());QVERIFY(qAbs(dock->height()-height)<12);
    QVERIFY(editor->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("layout-rhythm-navigation-079.png")));
    QTest::mouseClick(tabs,Qt::LeftButton,Qt::NoModifier,tabs->tabRect(0).center());QTest::qWait(100);
    QCOMPARE(tabs->currentIndex(),0);QVERIFY(editor->findChild<QWidget*>("performanceParametersArea")->isVisible());
}

void TestPluginHost::expressionOpeningKeepsDockHeight()
{
    QVERIFY(layoutScore079());
    auto main=Ms::mscore;main->findChild<QAction*>("kumo-workspace-arrange")->trigger();
    main->resize(1280,960);main->showPerformanceEditor(true);auto editor=main->performanceEditor();
    auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs");QVERIFY(tabs);tabs->setCurrentIndex(0);
    auto dock=main->findChild<QDockWidget*>("performanceEditorDock");QVERIFY(dock);
    QTest::qWait(150);
    for(int requested:{600,740}){
        tabs->setCurrentIndex(0);main->resizeDocks({dock},{requested},Qt::Vertical);QTest::qWait(120);
        const int height=dock->height();
        QVERIFY2(height>main->height()*.5,"编辑器能够向上展开到超过窗口一半");
        showExpressionPanel();QTest::qWait(200);QCOMPARE(tabs->currentIndex(),1);
        qInfo("expression dock requested %d, height %d -> %d",requested,height,dock->height());
        QVERIFY2(qAbs(dock->height()-height)<12,"打开演奏表达保留已调整的编辑器高度");
    }
    auto host=editor->findChild<QWidget*>("embeddedExpressionPage");QVERIFY(host&&host->isVisible());
    auto root=qobject_cast<QmlPlugin*>(host->property("root").value<QObject*>());QVERIFY(root&&root->window());
    QTRY_VERIFY(root->window()->isVisible());QVERIFY(root->width()>200&&root->height()>=180);
    QTest::qWait(100);
    const auto panelImage=root->window()->grabWindow();QVERIFY(!panelImage.isNull());
    QVERIFY(panelImage.save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("layout-expression-content-079.png")));
    QVERIFY(editor->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("layout-expression-size-079.png")));
    QTest::mouseClick(tabs,Qt::LeftButton,Qt::NoModifier,tabs->tabRect(0).center());
}

void TestPluginHost::rapidToolChangesKeepWindowSize()
{
    QVERIFY(layoutScore079());auto main=Ms::mscore;
    main->findChild<QAction*>("kumo-workspace-arrange")->trigger();main->showPerformanceEditor(true);
    auto editor=main->performanceEditor();auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs");QVERIFY(tabs);
    tabs->setCurrentIndex(0);QTest::qWait(180);
    tabs->setCurrentIndex(3);tabs->setCurrentIndex(1);tabs->setCurrentIndex(0);
    const auto available=main->screen()->availableGeometry().size()-QSize(20,60);
    const QSize desired=QSize(1360,1000).boundedTo(available);main->resize(desired);QTest::qWait(100);
    qInfo("rapid tools desired %dx%d, actual %dx%d",desired.width(),desired.height(),main->width(),main->height());
    QCOMPARE(main->size(),desired);QVERIFY(tabs->isVisible());
    QVERIFY(editor->findChild<QWidget*>("performanceParametersArea")->isVisible());
}

void TestPluginHost::existingEditorProfileFirstOpen()
{
    if(qEnvironmentVariableIsEmpty("P2_LAYOUT_PROFILE"))QSKIP("Set P2_LAYOUT_PROFILE to check an existing layout copy");
    QVERIFY(layoutScore079());auto main=Ms::mscore;
    const int page=QSettings().value("performanceEditor/activePage/arrange",0).toInt();
    main->showPerformanceEditor(true);auto editor=main->performanceEditor();QVERIFY(editor);
    auto dock=main->findChild<QDockWidget*>("performanceEditorDock");QVERIFY(dock);
    const auto state=QSettings().value("personalWorkspaceViews/arrange/layout").toByteArray();
    if(!state.isEmpty())QVERIFY(main->restoreState(state,1));
    settleEditorAreaLayout(editor);main->showPerformanceEditor(true);QTest::qWait(200);
    auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs");QVERIFY(tabs);
    QCOMPARE(tabs->currentIndex(),page);QVERIFY(dock->isVisible());QVERIFY(tabs->isVisible());
    auto split=editor->findChild<QSplitter*>("performanceMainSplitter");QVERIFY(split);
    qInfo("profile dock %dx%d editor %dx%d split %dx%d minimum %d minimumForWidth %d",dock->width(),dock->height(),editor->width(),editor->height(),split->width(),split->height(),editor->minimumSizeHint().height(),editor->layout()->minimumHeightForWidth(editor->width()));
    editor->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("layout-profile-before-check-079.png"));
    for(int i=0;i<split->count();++i)
        if(!split->widget(i)->isHidden()){
            const auto rect=split->widget(i)->geometry();
            qInfo("profile child %s: %d,%d %dx%d",qPrintable(split->widget(i)->objectName()),rect.x(),rect.y(),rect.width(),rect.height());
            QVERIFY2(split->sizes()[i]>1,qPrintable(split->widget(i)->objectName()));
            QTRY_VERIFY_WITH_TIMEOUT(split->rect().adjusted(-1,-1,1,1).contains(split->widget(i)->geometry()),5000);
        }
    qInfo("profile after layout: dock %dx%d editor %dx%d split %dx%d",dock->width(),dock->height(),editor->width(),editor->height(),split->width(),split->height());
    if(dock->isFloating())QVERIFY(dock->screen()->availableGeometry().contains(dock->frameGeometry()));
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));
    QVERIFY(editor->grab().save(output.filePath("layout-existing-profile-079.png")));
    if(page==1){
        auto host=editor->findChild<QWidget*>("embeddedExpressionPage");QVERIFY(host&&host->isVisible());
        auto root=qobject_cast<QmlPlugin*>(host->property("root").value<QObject*>());QVERIFY(root&&root->window());
        QTRY_VERIFY(root->window()->isVisible());QVERIFY(root->height()>=180);
        QTest::qWait(100);QVERIFY(root->window()->grabWindow().save(output.filePath("layout-existing-expression-079.png")));
    }
    const int height=dock->height();
    QTest::mouseClick(tabs,Qt::LeftButton,Qt::NoModifier,tabs->tabRect(0).center());QTest::qWait(100);
    QTest::mouseClick(tabs,Qt::LeftButton,Qt::NoModifier,tabs->tabRect(page).center());QTest::qWait(100);
    QCOMPARE(tabs->currentIndex(),page);QVERIFY(qAbs(dock->height()-height)<12);
}

void TestPluginHost::rhythmControlsFitNarrowPanel()
{
    QVERIFY(layoutScore079());auto main=Ms::mscore;main->showPerformanceEditor(true);
    auto editor=main->performanceEditor();auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs");QVERIFY(tabs);
    tabs->setCurrentIndex(3);QTest::qWait(120);
    auto pane=editor->findChild<QWidget*>("rhythmWritingPane");QVERIFY(pane);
    auto audition=pane->findChild<QPushButton*>("rhythm-audition");QVERIFY(audition);
    qInfo("rhythm action width %d, preferred %d",audition->width(),audition->sizeHint().width());
    QVERIFY2(audition->width()<=audition->sizeHint().width()+1,"试听按钮采用文字所需宽度，保留画面空间");
    editor->findChild<QAction*>("editorArea/float/rhythmWritingPane")->trigger();
    auto dock=main->findChild<QDockWidget*>("kumoEditorArea/rhythmWritingPane");QVERIFY(dock);
    QCOMPARE(pane->palette().color(QPalette::Window),qApp->palette().color(QPalette::Window));
    QCOMPARE(pane->palette().color(QPalette::WindowText),qApp->palette().color(QPalette::WindowText));
    const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));
    for(int width:{320,720,1200}){
        dock->resize(width,640);QTest::qWait(100);
        for(const auto& name:{"rhythmSettingsTools","rhythmRecordingTools","rhythmActionTools"}){
            auto host=pane->findChild<QWidget*>(name);QVERIFY2(host,name);
            const auto controls=host->findChildren<QWidget*>(QString(),Qt::FindDirectChildrenOnly);
            for(int i=0;i<controls.size();++i){
                if(controls[i]->isHidden())continue;
                QVERIFY2(host->rect().contains(controls[i]->geometry()),qPrintable(controls[i]->objectName()));
                for(int j=i+1;j<controls.size();++j)
                    if(!controls[j]->isHidden())QVERIFY(!controls[i]->geometry().intersects(controls[j]->geometry()));
            }
        }
        QVERIFY(dock->grab().save(output.filePath(QString("rhythm-controls-%1-079.png").arg(width))));
    }
    editor->findChild<QAction*>("editorArea/restore/rhythmWritingPane")->trigger();tabs->setCurrentIndex(0);
}

void TestPluginHost::detachedToolsKeepEditorWorking()
{
    QVERIFY(layoutScore079());auto main=Ms::mscore;main->showPerformanceEditor(true);
    auto editor=main->performanceEditor();auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs");QVERIFY(tabs);
    for(const auto& tool:QList<QPair<QString,int>>{{"rhythmWritingPane",3},{"transcriptionSpectrumPane",2},{"embeddedExpressionPage",1}}){
        tabs->setCurrentIndex(tool.second);QTest::qWait(100);
        editor->findChild<QAction*>("editorArea/float/"+tool.first)->trigger();QTest::qWait(100);
        auto dock=main->findChild<QDockWidget*>("kumoEditorArea/"+tool.first);QVERIFY(dock&&dock->isVisible());
        QCOMPARE(tabs->currentIndex(),0);
        auto toolbar=editor->findChild<QWidget*>("performanceControlScroll");QVERIFY(toolbar&&toolbar->isVisible());
        QScrollBar* scroll=nullptr;for(auto bar:editor->findChildren<QScrollBar*>(QString(),Qt::FindDirectChildrenOnly))if(bar->orientation()==Qt::Horizontal)scroll=bar;
        QVERIFY(scroll);QVERIFY2(scroll->isVisible(),qPrintable(tool.first+"移出后，普通编辑器保留时间滚动条"));
        const int inlinePage=tool.second==1?2:1;
        tabs->setCurrentIndex(inlinePage);QTest::qWait(100);
        QTest::mouseClick(tabs,Qt::LeftButton,Qt::NoModifier,tabs->tabRect(tool.second).center());QTest::qWait(100);
        QCOMPARE(tabs->currentIndex(),inlinePage);QVERIFY(dock->isVisible());
        editor->findChild<QAction*>("editorArea/restore/"+tool.first)->trigger();QTest::qWait(100);
        QCOMPARE(tabs->currentIndex(),tool.second);
        QTest::mouseClick(tabs,Qt::LeftButton,Qt::NoModifier,tabs->tabRect(0).center());
    }
}
