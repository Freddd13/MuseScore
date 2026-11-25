#include "mscore/workflow/documentsettings.h"
#include "mscore/performanceeditor/spectrumdisplay.h"
#include <QSlider>
#include <QTableWidget>

void TestPluginHost::documentToolState171()
{
    auto main=Ms::mscore;
    guiScaling=1.;MuseScore::updateUiStyleAndTheme();
    if(!WorkspacesManager::currentWorkspace())WorkspacesManager::initCurrentWorkspace();
    main->resize(1360,920);main->show();
    auto first=performanceScore123(_settings.filePath("ui-a171.mscx"),8);
    auto second=performanceScore123(_settings.filePath("ui-b171.mscx"),8);
    QVERIFY(first);QVERIFY(second);
    auto firstView=main->appendScore(first);main->setCurrentScoreView(firstView);
    auto firstCanvas=main->currentScoreView();
    main->findChild<QAction*>("kumo-workspace-arrange")->trigger();
    main->showPerformanceEditor(true);QTest::qWait(60);
    auto editor=main->performanceEditor();
    auto scope=editor->findChild<QComboBox*>("performanceScope");
    auto axis=editor->findChild<QComboBox*>("performanceAxis");
    auto gain=editor->findChild<QSlider*>("spectrumDisplay_gain");
    QVERIFY(scope);QVERIFY(axis);QVERIFY(gain);
    const auto globalWheel=QSettings().value("performanceEditor/wheelVelocity");
    const auto globalBand=QSettings().value("performanceEditor/bandHeight");
    auto wheel=editor->findChild<QToolButton*>("performanceWheelVelocity");
    scope->setCurrentIndex(2);axis->setCurrentIndex(1);wheel->setChecked(false);
    gain->setValue(qMin(gain->maximum(),gain->value()+7));
    const int firstGain=gain->value();
    auto dock=main->findChild<QDockWidget*>("performanceEditorDock");QVERIFY(dock);
    showReferencePanel(true);
    auto reference=main->findChild<QDockWidget*>("kumoPanel/ReferencePlayer_MS3");QVERIFY(reference);
    reference->setFloating(true);reference->resize(640,420);reference->move(100,80);
    main->showPerformanceEditor(true);QTest::qWait(30);
    main->resizeDocks({dock},{430},Qt::Vertical);QTest::qWait(30);
    PluginAPI::Score wrapped(first);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
    QVERIFY(observer.saveConfiguration("state171",{{"expanded",true},{"filter","voice2"}}));
    auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs");QVERIFY(tabs);
    tabs->setCurrentIndex(3);
    auto draft=editor->findChild<QTableWidget*>("rhythmDraftTable");QVERIFY(draft);
    QTRY_VERIFY_WITH_TIMEOUT(draft->rowCount()>0,12000);
    draft->item(0,4)->setText("73");QTest::qWait(60);
    tabs->setCurrentIndex(0);QTest::qWait(30);
    const int firstHeight=dock->height();
    auto zoom=firstCanvas->logicalZoom();zoom.level=1.15;firstCanvas->setLogicalZoom(zoom);
    firstCanvas->setOffset(-20,-40);QTest::qWait(20);
    const auto firstZoom=firstCanvas->logicalZoom().level;
    const auto firstX=firstCanvas->xoffset(),firstY=firstCanvas->yoffset();
    const auto music=first->state();
    const auto fileA=_settings.filePath("ui-a171.mscz");
    QVERIFY(main->saveAs(first,false,fileA,"mscz"));
    qInfo()<<"A save extent"<<firstHeight<<dock->size()<<main->size()<<DocumentSettings(first).value("personalWorkspaceViews/arrange/editorExtent");
    QVERIFY(!first->metaTag("kumo.toolState").isEmpty());
    QVERIFY(first->state()==music);

    auto secondView=main->appendScore(second);main->setCurrentScoreView(secondView);QTest::qWait(90);
    showReferencePanel(false);reference->setFloating(false);
    scope->setCurrentIndex(1);axis->setCurrentIndex(0);wheel->setChecked(true);
    gain->setValue(qMax(gain->minimum(),firstGain-11));const int secondGain=gain->value();
    main->resizeDocks({dock},{260},Qt::Vertical);QTest::qWait(30);
    const int secondHeight=dock->height();
    QVERIFY(main->saveAs(second,false,_settings.filePath("ui-b171.mscz"),"mscz"));

    main->setCurrentScoreView(firstView);QTest::qWait(90);
    QCOMPARE(scope->currentIndex(),2);QCOMPARE(axis->currentIndex(),1);
    QVERIFY(!wheel->isChecked());QCOMPARE(gain->value(),firstGain);
    QVERIFY(first->state()==music);
    QVERIFY(reference->isFloating());QCOMPARE(reference->width(),640);
    QCOMPARE(firstCanvas->logicalZoom().level,firstZoom);
    QCOMPARE(firstCanvas->xoffset(),firstX);QCOMPARE(firstCanvas->yoffset(),firstY);
    qInfo()<<"A restored extent"<<firstHeight<<dock->size()<<main->size()<<dock->minimumSizeHint()<<DocumentSettings(first).value("personalWorkspaceViews/arrange/editorExtent");
    main->grab().save(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS")+"/document-a171.png");
    QVERIFY2(qAbs(dock->height()-firstHeight)<5,"First score restores its editor height");
    tabs->setCurrentIndex(3);QTRY_VERIFY_WITH_TIMEOUT(draft->rowCount()>0,12000);
    QTRY_COMPARE(draft->item(0,4)->text(),QString("73"));tabs->setCurrentIndex(0);QTest::qWait(30);
    main->setCurrentScoreView(secondView);QTest::qWait(90);
    QCOMPARE(scope->currentIndex(),1);QCOMPARE(axis->currentIndex(),0);
    QVERIFY(wheel->isChecked());QCOMPARE(gain->value(),secondGain);
    QVERIFY(reference->isHidden());
    QVERIFY2(qAbs(dock->height()-secondHeight)<5,"Second score restores its editor height");

    auto reopened=main->readScore(fileA);QVERIFY(reopened);
    main->setCurrentScoreView(main->appendScore(reopened));QTest::qWait(90);
    QCOMPARE(scope->currentIndex(),2);QCOMPARE(axis->currentIndex(),1);
    QVERIFY(!wheel->isChecked());QCOMPARE(gain->value(),firstGain);
    QVERIFY(reference->isFloating());
    tabs->setCurrentIndex(3);QTRY_VERIFY_WITH_TIMEOUT(draft->rowCount()>0,12000);
    QTRY_COMPARE(draft->item(0,4)->text(),QString("73"));tabs->setCurrentIndex(0);
    PluginAPI::Score rewrapped(reopened);observer.setScore(&rewrapped);
    QCOMPARE(observer.loadConfiguration("state171").value("filter").toString(),QString("voice2"));
    QCOMPARE(QSettings().value("performanceEditor/wheelVelocity"),globalWheel);
    QCOMPARE(QSettings().value("performanceEditor/bandHeight"),globalBand);
    QCOMPARE(toChord(reopened->tick2segment(Fraction::fromTicks(0),false,SegmentType::ChordRest)->element(0))->upNote()->pitch(),72);
    qInfo()<<"Each score restored its own display, spectrum controls, dock size and plugin configuration after native Save As and reopen";
}
