// GPL-2.0-or-later. Actual Qt Widgets / QML host regressions, isolated from user settings.
#include <QtTest/QtTest>
#include <QDockWidget>
#include <QMenuBar>
#include <QMenu>
#include <QTranslator>
#include <QQuickView>
#include <QJSValue>
#include <QTemporaryDir>
#include <QSettings>
#include <functional>
#include <QLabel>
#include <QMessageBox>
#include <QTimer>
#include <QScopedValueRollback>
#include <QtConcurrent>
#include "mscore/mssplashscreen.h"
#include "mscore/musescoredialogs.h"
#include "personalbranding.h"
#include "mscore/workflow/builtinpanels.h"
#include "mscore/workflow/systemlocks.h"
#include "mscore/workflow/vstpanel.h"
#include "audio/midi/msynthesizer.h"
#include "audio/midi/fluid/fluid.h"
#include "mscore/synthcontrol.h"
#ifdef KUMO_VST_HOST
#include "audio/vst/vstsynth.h"
#endif
#include "mscore/debuglog.h"
#include "mscore/pianoroll/pianoroll.h"
#include "libmscore/layoutbreak.h"
#include "mscore/workflow/featuregate.h"
#include "mscore/workflow/workspaceviews.h"
#include "mscore/workflow/editorareas.h"
#include "mscore/workflow/referencepanel.h"
#include "mscore/workflow/regionalpanel.h"
#include "mscore/workflow/aiconnection.h"
#include "mscore/workflow/referencefactory.h"
#include "mscore/workflow/workflowcontext.h"
#include <QDataStream>
#include <QToolButton>
#include <QListWidget>
#include <QPushButton>
#include <QTreeWidget>
#include "mscore/workflow/arrangement.h"
#include "mscore/workflow/localsuggestions.h"
#include "mscore/performanceeditor/rhythmpane.h"
#include "mscore/workflow/midicapture.h"
#include "audio/midi/event.h"
#include <QTableWidget>
#include <QScreen>
#include <QComboBox>
#include <cmath>
#include "mscore/performanceeditor/performanceeditor.h"
#include "mscore/performanceeditor/spectrumannotation.h"
#include "mscore/performanceeditor/rollnotation.h"
#include "mscore/performanceeditor/rollwriting.h"
#include <QTabBar>
#include <QTabWidget>
#include <QSlider>
#include <QScrollArea>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QLineEdit>
#include <QQuickItem>
#include <QQmlComponent>
#include <QQmlContext>
#include <QJsonDocument>

#include "mscore/plugin/qmlpluginengine.h"
#include "libmscore/notevelocity.h"
#include "libmscore/velocitybackup.h"
#include "libmscore/regionalvariants.h"
#include "libmscore/excerpt.h"
#include "libmscore/tie.h"
#include "libmscore/slur.h"
#include "libmscore/tuplet.h"
#include "libmscore/tempotext.h"
#include <QFontDatabase>
#include "mtest/testutils.h"
#include "mscore/musescore.h"
#include "mscore/scoreview.h"
#include "mscore/plugin/pluginManager.h"
#include "mscore/plugin/qmlplugin.h"
#include "mscore/plugin/api/score.h"
#include "mscore/plugin/api/util.h"
#include "mscore/plugin/api/scoreobserver.h"
#include "libmscore/chord.h"
#include "libmscore/harmony.h"
#include "libmscore/measure.h"
#include "libmscore/system.h"
#include "libmscore/page.h"
#include "libmscore/note.h"
#include "libmscore/fingering.h"
#include "libmscore/segment.h"
#include "libmscore/inputrhythm.h"
#include "mscore/inputrhythmpreference.h"
#include "libmscore/undo.h"
#include "mscore/preferences.h"
#include "mscore/pianotools.h"
#include "mscore/pianoroll/pianoview.h"
#include "mscore/workspace.h"
#include "mscore/shortcut.h"
#include "libmscore/xml.h"

static void initPluginHostResources() { Q_INIT_RESOURCE(musescorefonts_Petaluma); }

using namespace Ms;
static void releaseHostPanels(){
 QList<QPointer<QmlPlugin>> panels;for(auto window:QGuiApplication::allWindows())if(auto view=qobject_cast<QQuickView*>(window))if(auto root=qobject_cast<QmlPlugin*>(view->rootObject()))panels.append(root);
 for(auto panel:panels)if(panel)panel->disposePanelForReload();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
}

class TestPluginHost : public QObject {
      Q_OBJECT
      QTemporaryDir _settings;
   private slots:
      void spectrumInitialSourceAndChoice();
      void spectrumLinkedZoomAndScroll();
      void harmonyRangeWithoutSettings();
      void harmonyManualSurvivesPitchEdit();
      void harmonyStyleSurfacesAndRestore();
      void nearestWheelOnBandAxis();
      void scoreBandStaysNearSelectedSystem();
      void harmonyDisplayAndHold();
      void chordHandlesStayAtMusicRange();
      void legacyVariantsSaveBeforeSwitch();
      void metadataSkipsSupportComponents();
      void contentWidthControlsWrapWithoutOverlap();
      void restoredWorkspaceKeepsPersonalTools();
      void vstFirstUseControls();
      void toolWindowScreenAndFullscreenRoundTrip();
      void spectrumButtonsAndFirstOpenContent();
      void cfxLiveStateKeepsEditorInstance();
      void personalToolWindowsRemainIndependent();
      void metronomeFontCompatibility() {
            QVERIFY(QFontDatabase().families().contains("Metronome Text 3"));
            auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);
            score->style().set(Sid::musicalTextFont,QString("Leland Text"));
            TempoText tempo(score);TextFragment fragment;
            fragment.format.setFontFamily("ScoreText");fragment.format.setFontSize(12);
            fragment.text=QString(QChar(0xECA5))+QChar(0xECB7);
            const auto font=fragment.font(&tempo);QCOMPARE(font.family(),QString("Metronome Text 3"));
            QFont current("Leland Text");current.setPointSizeF(font.pointSizeF());
            const QString quarter(QChar(0xECA5));
            QVERIFY(QFontMetricsF(font).tightBoundingRect(quarter).height()>QFontMetricsF(current).tightBoundingRect(quarter).height()*1.5);
            fragment.text=QString(QChar(0xE520));QCOMPARE(fragment.font(&tempo).family(),QString("Leland Text"));
            score->style().set(Sid::musicalTextFont,QString("Bravura Text"));fragment.text=quarter;
            QCOMPARE(fragment.font(&tempo).family(),QString("Bravura Text"));delete score;
      }
      void expressionTargetAndInputControls() {
            auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);
            main->setCurrentScoreView(main->appendScore(score));main->resize(1250,940);main->show();
            auto start=score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest);
            auto end=score->tick2segment(Fraction::fromTicks(960),false,SegmentType::ChordRest);QVERIFY(start);QVERIFY(end);
            score->selection().setRange(start,end,0,1);score->selection().updateSelectedElements();
            QVERIFY(score->selection().isRange());QCOMPARE(score->selection().tickStart().ticks(),480);QCOMPARE(score->selection().tickEnd().ticks(),960);
            getAction("performance-expression")->trigger();
            QVERIFY(score->selection().isRange());QCOMPARE(score->selection().tickStart().ticks(),480);
            auto editor=main->performanceEditor();QVERIFY(editor);auto host=editor->findChild<QWidget*>("embeddedExpressionPage");QVERIFY(host);
            auto root=qobject_cast<QmlPlugin*>(host->property("root").value<QObject*>());QVERIFY(root);
            QVERIFY(QMetaObject::invokeMethod(root,"chooseTarget",Q_ARG(QVariant,false)));
            auto scope=[root]{auto v=root->property("retainedScope");return v.userType()==qMetaTypeId<QJSValue>()?v.value<QJSValue>().toVariant().toMap():v.toMap();};
            QCOMPARE(scope().value("start").toInt(),480);QCOMPARE(scope().value("end").toInt(),960);QCOMPARE(scope().value("firstBeat").toDouble(),2.);QCOMPARE(scope().value("endBeat").toDouble(),3.);
            score->deselectAll();QVERIFY(QMetaObject::invokeMethod(root,"updateScoreContext"));QCOMPARE(scope().value("start").toInt(),480);
            editor->findChild<QAction*>("editorArea/float/embeddedExpressionPage")->trigger();QTRY_VERIFY(!root->property("withinEditor").toBool());
            auto click=[root](const char* name){auto item=root->findChild<QQuickItem*>(name);if(!item||!item->isVisible()||!item->isEnabled())return false;QTest::mouseClick(item->window(),Qt::LeftButton,Qt::NoModifier,item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint());return true;};
            QTest::qWait(150);QVERIFY(click("expressionTargetWhole"));QCOMPARE(scope().value("start").toInt(),0);QCOMPARE(scope().value("end").toInt(),1920);
            const bool enabled=main->isMidiInEnabled();QVERIFY(click("expressionMidiInput"));QTRY_COMPARE(main->isMidiInEnabled(),!enabled);QVERIFY(click("expressionMidiInput"));QTRY_COMPARE(main->isMidiInEnabled(),enabled);
            QVERIFY(main->isEnabled());QCOMPARE(root->window()->modality(),Qt::NonModal);
            QVERIFY(root->window()->grabWindow().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("expression-scope-065.png")));
            editor->findChild<QAction*>("editorArea/restore/embeddedExpressionPage")->trigger();
      }
      void workspaceViewsAndFirstOpen() {
            auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);
            main->setCurrentScoreView(main->appendScore(score));main->resize(1280,960);main->show();main->showPerformanceEditor(true);
            auto editor=main->performanceEditor();auto split=editor->findChild<QSplitter*>("performanceMainSplitter");QVERIFY(split);
            auto parameters=editor->findChild<QWidget*>("performanceParametersArea");QVERIFY(parameters);
            QTRY_VERIFY(main->height()<=1000);
            const int index=split->indexOf(parameters);QVERIFY(index>=0);
            split->setCollapsible(index,true);auto sizes=split->sizes();sizes[index]=0;split->setSizes(sizes);
            main->showPerformanceEditor(false);main->resize(1260,940);main->showPerformanceEditor(true);
            QTRY_VERIFY(parameters->height()>30);
            main->findChild<QAction*>("kumo-workspace-daw")->trigger();
            QTRY_COMPARE(main->property("kumoWorkspaceView").toString(),QString("daw"));
            QVERIFY(main->centralWidget()->isHidden());
            auto dock=main->findChild<QDockWidget*>("performanceEditorDock");QVERIFY(dock);
            QTRY_VERIFY(dock->height()>main->height()*2/3);
            QVERIFY2(main->height()<=1000,qPrintable(QString("DAW window grew to %1 (minimum %2, hint %3)").arg(main->height()).arg(main->minimumHeight()).arg(main->minimumSizeHint().height())));
            QVERIFY(main->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("workspace-daw-064.png")));
            main->findChild<QAction*>("kumo-workspace-score")->trigger();QVERIFY(!main->centralWidget()->isHidden());
            main->findChild<QAction*>("kumo-workspace-arrange")->trigger();QCOMPARE(main->property("kumoWorkspaceView").toString(),QString("arrange"));
            dock->setFloating(true);dock->show();auto back=dock->findChild<QToolButton*>("performanceReturnToScore");QVERIFY(back);QTRY_VERIFY(back->isVisible());
            QTest::mouseClick(back,Qt::LeftButton);QTRY_VERIFY(!dock->isVisible());QVERIFY(!main->centralWidget()->isHidden());dock->setFloating(false);
      }
      void detachedEditorAreasAndTheme() {
            auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);
            main->setCurrentScoreView(main->appendScore(score));main->resize(1280,960);main->show();main->showPerformanceEditor(true);auto editor=main->performanceEditor();
            auto notes=editor->findChild<QWidget*>("performanceNotesArea");QVERIFY(notes);
            auto action=editor->findChild<QAction*>("editorArea/float/performanceNotesArea");QVERIFY(action);action->trigger();
            auto dock=main->findChild<QDockWidget*>("kumoEditorArea/performanceNotesArea");QVERIFY(dock);QTRY_VERIFY(dock->isVisible());QVERIFY(dock->isFloating());QCOMPARE(notes->parentWidget(),dock);
            main->showPerformanceEditor(false);QVERIFY(dock->isVisible());
            auto ruler=notes->findChild<QWidget*>("performanceDetachedNotesRuler");QVERIFY(ruler);QTRY_VERIFY(ruler->isVisible());QTest::qWait(100);
            QTest::mouseClick(ruler,Qt::LeftButton,Qt::NoModifier,QPoint(150,ruler->height()/2));QTRY_VERIFY(score->masterScore()->playPos().ticks()>0);
            editor->findChild<QAction*>("editorArea/restore/performanceNotesArea")->trigger();QTRY_VERIFY(!dock->isVisible());QTRY_VERIFY(notes->isVisible());
            auto expression=editor->findChild<QWidget*>("embeddedExpressionPage");QVERIFY(expression);
            editor->findChild<QAction*>("editorArea/float/embeddedExpressionPage")->trigger();
            auto root=expression->property("root").value<QObject*>();QVERIFY(root);QTRY_VERIFY(!root->property("withinEditor").toBool());QVERIFY(root->property("embeddedWorkflow").toBool());
            auto theme=root->findChild<QObject*>("workflowUiTheme");QVERIFY(theme);QTRY_COMPARE(theme->property("background").value<QColor>(),qApp->palette().color(QPalette::Window));
            editor->findChild<QAction*>("editorArea/restore/embeddedExpressionPage")->trigger();QTRY_VERIFY(root->property("withinEditor").toBool());
            QTRY_COMPARE(theme->property("background").value<QColor>(),editor->property("workflowPanelTheme").toMap().value("background").value<QColor>());
            auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs");QVERIFY(tabs);QTest::mouseDClick(tabs,Qt::LeftButton,Qt::NoModifier,tabs->tabRect(2).center());
            auto spectrumDock=main->findChild<QDockWidget*>("kumoEditorArea/transcriptionSpectrumPane");QVERIFY(spectrumDock);QTRY_VERIFY(spectrumDock->isVisible());
            editor->findChild<QAction*>("editorArea/restore/transcriptionSpectrumPane")->trigger();QTRY_VERIFY(!spectrumDock->isVisible());tabs->setCurrentIndex(0);
            QVERIFY(main->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("editor-area-controls-064.png")));
      }
      void workspaceAreaPlacements() {
            auto main=Ms::mscore;
            auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);
            main->setCurrentScoreView(main->appendScore(score));
            main->findChild<QAction*>("kumo-workspace-arrange")->trigger();
            QSettings().remove("performanceEditor/detached");
            main->show();main->showPerformanceEditor(true);QTest::qWait(50);
            auto editor=main->performanceEditor();
            auto notes=main->findChild<QWidget*>("performanceNotesArea");
            auto parameters=main->findChild<QWidget*>("performanceParametersArea");QVERIFY(notes);QVERIFY(parameters);
            editor->findChild<QAction*>("editorArea/float/performanceNotesArea")->trigger();
            QTRY_VERIFY(notes->property("kumoDetachedArea").toBool());
            main->findChild<QAction*>("kumo-workspace-daw")->trigger();
            QTRY_VERIFY(!notes->property("kumoDetachedArea").toBool());
            editor->findChild<QAction*>("editorArea/float/performanceParametersArea")->trigger();
            QTRY_VERIFY(parameters->property("kumoDetachedArea").toBool());
            main->findChild<QAction*>("kumo-workspace-arrange")->trigger();
            QTRY_VERIFY(notes->property("kumoDetachedArea").toBool());
            QTRY_VERIFY(!parameters->property("kumoDetachedArea").toBool());
            main->findChild<QAction*>("kumo-workspace-daw")->trigger();
            QTRY_VERIFY(!notes->property("kumoDetachedArea").toBool());
            QTRY_VERIFY(parameters->property("kumoDetachedArea").toBool());
            editor->findChild<QAction*>("editorArea/restore/performanceParametersArea")->trigger();
            main->findChild<QAction*>("kumo-workspace-arrange")->trigger();
            QTRY_VERIFY(notes->property("kumoDetachedArea").toBool());
            auto notesDock=qobject_cast<QDockWidget*>(notes->parentWidget());QVERIFY(notesDock);notesDock->close();
            main->findChild<QAction*>("kumo-workspace-daw")->trigger();QTest::qWait(50);
            main->findChild<QAction*>("kumo-workspace-arrange")->trigger();QTest::qWait(50);
            QVERIFY(notes->property("kumoDetachedArea").toBool());QVERIFY(!notesDock->isVisible());
            editor->findChild<QAction*>("editorArea/show/performanceNotesArea")->trigger();QTRY_VERIFY(notesDock->isVisible());
            editor->findChild<QAction*>("editorArea/restore/performanceNotesArea")->trigger();
      }
      void referenceTrackDeletionAndSliders() {
            auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
            main->resize(1280,960);main->show();auto action=getAction("reference-player");if(!action->isChecked())action->trigger();
            QmlPlugin* root=nullptr;for(auto window:QGuiApplication::allWindows())if(auto view=qobject_cast<QQuickView*>(window))if(auto candidate=qobject_cast<QmlPlugin*>(view->rootObject()))if(candidate->menuPath()=="Plugins.Reference Player")root=candidate;
            QVERIFY(root);
            QVERIFY(QMetaObject::invokeMethod(root,"setPanelFloating",Q_ARG(bool,true)));
            QVERIFY(QMetaObject::invokeMethod(root,"resizePanel",Q_ARG(int,1000),Q_ARG(int,720)));
            QTest::qWait(150);
            const auto path=_settings.filePath("reference-track.wav");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QDataStream data(&file);data.setByteOrder(QDataStream::LittleEndian);data.writeRawData("RIFF",4);data<<quint32(36+88200);data.writeRawData("WAVEfmt ",8);data<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(44100)<<quint32(88200)<<quint16(2)<<quint16(16);data.writeRawData("data",4);data<<quint32(88200);for(int i=0;i<44100;++i)data<<qint16(0);file.close();
            auto currentMedia=[root]{auto value=root->property("media");return value.canConvert<QJSValue>()?value.value<QJSValue>().toQObject():value.value<QObject*>();};
            for(int i=0;i<2;++i){root->setProperty("addingAudio",true);QVERIFY(QMetaObject::invokeMethod(root,"openAudio",Q_ARG(QVariant,QUrl::fromLocalFile(path).toString())));QTRY_VERIFY(currentMedia()&&currentMedia()->property("duration").toDouble()>900);}
            auto list=[root](const char* name){auto value=root->property(name);return value.canConvert<QJSValue>()?value.value<QJSValue>().toVariant().toList():value.toList();};
            QCOMPARE(list("tracks").size(),2);QVERIFY(root->findChild<QQuickItem*>("referenceTrackRows"));
            QTest::qWait(150);
            auto rows=root->findChild<QQuickItem*>("referenceTrackRows");
            std::function<QQuickItem*(QQuickItem*,const QString&)> findVisual=[&](QQuickItem* item,const QString& name)->QQuickItem*{if(item->objectName()==name)return item;for(auto child:item->childItems())if(auto found=findVisual(child,name))return found;return nullptr;};
            QTRY_VERIFY(findVisual(rows,"referenceTrackRow"));QPointer<QQuickItem> trackRow=findVisual(rows,"referenceTrackRow");const int trackIndex=trackRow->property("trackIndex").toInt();
            QTRY_VERIFY(trackRow->width()>100);QTest::qWait(100);
            for(const char* name:{"referenceTrackMute","referenceTrackSolo","referenceTrackParticipate"}){
                  auto button=findVisual(trackRow,name);QVERIFY(button);QVERIFY(button->height()<=trackRow->height());
                  QVERIFY(QRectF(0,0,trackRow->width(),trackRow->height()).adjusted(-1,-1,1,1).contains(button->mapRectToItem(trackRow,QRectF(0,0,button->width(),button->height()))));
                  }
            QPointer<QQuickItem> mute=findVisual(trackRow,"referenceTrackMute");QVERIFY(mute);
            QVERIFY(mute->isVisible()&&mute->isEnabled());
            QVERIFY(QRectF(QPointF(),root->window()->size()).contains(mute->mapRectToScene(QRectF(0,0,mute->width(),mute->height()))));
            QSignalSpy muteClicks(mute,SIGNAL(clicked()));
            QTest::mouseClick(root->window(),Qt::LeftButton,Qt::NoModifier,mute->mapToScene(QPointF(mute->width()/2,mute->height()/2)).toPoint());QTRY_VERIFY(list("tracks")[trackIndex].toMap()["mute"].toBool());
            QCOMPARE(muteClicks.count(),1);
            QTest::qWait(100);
            QTRY_VERIFY(findVisual(rows,"referenceTrackRow"));trackRow=findVisual(rows,"referenceTrackRow");mute=findVisual(trackRow,"referenceTrackMute");QVERIFY(mute);
            QTest::mouseClick(root->window(),Qt::LeftButton,Qt::NoModifier,mute->mapToScene(QPointF(mute->width()/2,mute->height()/2)).toPoint());QTRY_VERIFY(!list("tracks")[trackIndex].toMap()["mute"].toBool());
            for(const auto &option:QList<QPair<QString,QString>>{{"referenceTrackSolo","solo"},{"referenceTrackParticipate","participate"}}){
                  const bool original=list("tracks")[trackIndex].toMap()[option.second].toBool();
                  for(bool target:{!original,original}){
                        QTest::qWait(100);trackRow=findVisual(rows,"referenceTrackRow");QVERIFY(trackRow);QCOMPARE(trackRow->property("trackIndex").toInt(),trackIndex);
                        QPointer<QQuickItem> button=findVisual(trackRow,option.first);QVERIFY(button);
                        QTest::mouseClick(root->window(),Qt::LeftButton,Qt::NoModifier,button->mapToScene(QPointF(button->width()/2,button->height()/2)).toPoint());
                        QTRY_COMPARE(list("tracks")[trackIndex].toMap()[option.second].toBool(),target);
                        }
                  }
            auto number=root->findChild<QQuickItem*>("referenceSpeedValue");auto play=root->findChild<QQuickItem*>("referenceJointPlay");QVERIFY(number&&play);QCOMPARE(number->height(),play->height());
            auto slider=root->findChild<QQuickItem*>("referenceVolumeSlider");QVERIFY(slider);QTRY_VERIFY(slider->width()>20);
            QTest::mouseClick(root->window(),Qt::LeftButton,Qt::NoModifier,slider->mapToScene(QPointF(slider->width()*.3,slider->height()/2)).toPoint());
            auto mediaValue=root->property("media");auto media=mediaValue.canConvert<QJSValue>()?mediaValue.value<QJSValue>().toQObject():mediaValue.value<QObject*>();QVERIFY(media);QVERIFY(media->property("volume").toDouble()<.6);
            root->setProperty("transportTarget",0);QVERIFY(QMetaObject::invokeMethod(root,"toggleReferencePlayback"));QCOMPARE(root->property("lastSessionMode").toString(),QString("single"));
            QMetaObject::invokeMethod(media,"pause");
            QTest::qWait(350);const int history=score->undoStack()->getCurIdx();
            QVERIFY(QMetaObject::invokeMethod(root,"removeTrack",Q_ARG(QVariant,0)));QCOMPARE(list("tracks").size(),1);QCOMPARE(list("deletedTracks").size(),1);QCOMPARE(score->undoStack()->getCurIdx(),history+1);
            QVERIFY(QMetaObject::invokeMethod(root,"restoreDeletedTrack"));QCOMPARE(list("tracks").size(),2);QCOMPARE(list("deletedTracks").size(),0);
            getAction("undo")->trigger();QTRY_COMPARE(list("tracks").size(),1);
            getAction("redo")->trigger();QTRY_COMPARE(list("tracks").size(),2);
            QMetaObject::invokeMethod(root,"setPanelFloating",Q_ARG(bool,true));
            QMetaObject::invokeMethod(root,"resizePanel",Q_ARG(int,1000),Q_ARG(int,720));
            QTest::mouseMove(root->window(),QPoint(12,root->window()->height()-12));QTest::qWait(200);
            QVERIFY(root->window()->grabWindow().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("reference-tracks-064.png")));
            QVERIFY(QMetaObject::invokeMethod(root,"resizePanel",Q_ARG(int,420),Q_ARG(int,360)));QTest::qWait(180);
            auto scroll=root->findChild<QQuickItem*>("referencePanelScroll");QVERIFY(scroll);QVERIFY(scroll->property("contentHeight").toDouble()>scroll->height());
            auto content=scroll->property("contentItem").value<QQuickItem*>();QVERIFY(content);content->setProperty("contentY",0.);QTest::qWait(100);
            auto vertical=root->findChild<QQuickItem*>("referencePanelVerticalScroll");QVERIFY(vertical);QVERIFY(vertical->height()>scroll->height()/2);
            QVERIFY(QRectF(QPointF(),root->window()->size()).contains(vertical->mapRectToScene(QRectF(0,0,vertical->width(),vertical->height()))));
            QVERIFY(root->window()->grabWindow().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("reference-narrow-top-074.png")));
            QTest::mouseClick(root->window(),Qt::LeftButton,Qt::NoModifier,vertical->mapToScene(QPointF(vertical->width()/2,vertical->height()-4)).toPoint());QTRY_VERIFY(content->property("contentY").toDouble()>0);
            content->setProperty("contentY",scroll->property("contentHeight").toDouble()-scroll->property("availableHeight").toDouble());QTest::qWait(100);
            auto associate=root->findChild<QQuickItem*>("referenceAssociate");QVERIFY(associate);QVERIFY(QRectF(QPointF(),root->window()->size()).contains(associate->mapRectToScene(QRectF(0,0,associate->width(),associate->height()))));
            QVERIFY(root->window()->grabWindow().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("reference-narrow-074.png")));
      }
      void featureDisabledStartup() {
            if (qEnvironmentVariable("P2_FEATURE_GATE_OFF").isEmpty()) QSKIP("Run with P2_FEATURE_GATE_OFF=1");
            QVERIFY(!personalFeaturesEnabled());
            auto main=Ms::mscore;
            for (auto action:main->findChildren<QAction*>())
                  if (personalAction(action->objectName()) || action->property("kumoExperimental").toBool())
                        QVERIFY2(!action->isVisible() && !action->isEnabled(),qUtf8Printable(action->objectName()));
            QVERIFY(!InputRhythm::enabled());
            QVERIFY(getAction("toggle-piano")->isVisible());
            auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);
            main->setCurrentScoreView(main->appendScore(score));main->show();
            score->setMetaTag("kumo.reference","saved audio associations");
            QFileInfo info(_settings.filePath("extension-off.mscz"));QVERIFY(score->saveCompressedFile(info,false,false));
            std::unique_ptr<MasterScore> restored(main->readScore(info.absoluteFilePath()));QVERIFY(restored);
            QCOMPARE(restored->metaTag("kumo.reference"),QString("saved audio associations"));
            main->showPerformanceEditor(true);QVERIFY(!main->findChild<QDockWidget*>("performanceEditorDock"));
            showAIConnection();QVERIFY(!main->findChild<QDialog*>("kumoAIConnection"));
            QVERIFY(main->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("extension-off-063.png")));
      }
      void featureConfigurationAndHiddenActions() {
            QTemporaryDir folder; QVERIFY(folder.isValid());
            auto create=[&](const QString& name,const QByteArray& content){QFile f(folder.filePath(name));if(!f.open(QIODevice::WriteOnly))return QString();f.write(content);return f.fileName();};
            const auto off=create("off.ini","[features]\nenable_exp_feature=false\n");
            const auto on=create("on.ini","[features]\nenable_exp_feature=true\n");
            const auto invalid=create("invalid.ini","[features]\nenable_exp_feature=yes\n");
            qApp->setProperty("kumoFeatureConfiguration",off);
            QVERIFY(!personalFeaturesEnabled());
            QMainWindow local;auto action=new QAction(&local);action->setObjectName("performance-editor");
            applyPersonalFeatureVisibility(&local);QVERIFY(!action->isEnabled());QVERIFY(!action->isVisible());
            const auto before=Ms::mscore->findChildren<QDialog*>("kumoAIConnection").size();
            showAIConnection();QCOMPARE(Ms::mscore->findChildren<QDialog*>("kumoAIConnection").size(),before);
            auto media=createReferenceMedia(&local);QVERIFY(!media->property("available").toBool());
            qApp->setProperty("kumoFeatureConfiguration",on);QVERIFY(personalFeaturesEnabled());
            qApp->setProperty("kumoFeatureConfiguration",invalid);QVERIFY(!personalFeaturesEnabled());
            qApp->setProperty("kumoFeatureConfiguration",folder.filePath("missing.ini"));QVERIFY(!personalFeaturesEnabled());
            qApp->setProperty("kumoFeatureConfiguration",QVariant());QVERIFY(personalFeaturesEnabled());
      }
      void pluginSettingsSurviveExtensionSwitch() {
            QTemporaryDir folder;QFile file(folder.filePath("off.ini"));QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("[features]\nenable_exp_feature=false\n");file.close();
            PluginManager manager(Ms::mscore);manager.init();
            auto find=[&]()->PluginDescription* {
                  for(int i=0;i<manager.pluginCount();++i) {
                        auto plugin=manager.getPluginDescription(i);
                        if(QFileInfo(plugin->path).fileName()=="ArrangementAssistant_MS3.qml")return plugin;
                        }
                  return nullptr;
                  };
            auto plugin=find();QVERIFY(plugin);plugin->load=true;plugin->shortcut.setKeys({QKeySequence("Ctrl+Alt+F12")});
            qApp->setProperty("kumoFeatureConfiguration",file.fileName());
            manager.updatePluginList(true);plugin=find();QVERIFY(plugin);QVERIFY(plugin->load);
            QCOMPARE(plugin->shortcut.keys(),QList<QKeySequence>{QKeySequence("Ctrl+Alt+F12")});
            manager.init();
            for(int i=0;i<manager.pluginListWidget->count();++i)
                  QVERIFY(manager.pluginListWidget->item(i)->text()!="ArrangementAssistant_MS3");
            qApp->setProperty("kumoFeatureConfiguration",QVariant());manager.init();plugin=find();QVERIFY(plugin);QVERIFY(plugin->load);
      }
      void cleanup(){qApp->setProperty("kumoFeatureConfiguration",QVariant());releaseHostPanels();if(Ms::mscore)Ms::mscore->showPerformanceEditor(false);}
      void initTestCase()
            {
            QVERIFY(_settings.isValid());
            QCoreApplication::setOrganizationName("MuseScoreRegression");
            QCoreApplication::setApplicationName("PluginHost");
            QSettings::setDefaultFormat(QSettings::IniFormat);
            QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,_settings.path());
            dataPath=_settings.path();
            MScore::noGui=true; noSeq=true; converterMode=true;
            initMuseScoreResources(); initPluginHostResources();
            QStringList arguments;
            MuseScore::init(arguments);
            QVERIFY(Ms::mscore);
            }
      void automaticRhythmPreference()
            {
            auto action=Ms::mscore->findChild<QAction*>("auto-rhythmic-input");
            QVERIFY(action); QVERIFY(action->isCheckable()); QVERIFY(action->isChecked());
            QVERIFY(InputRhythm::enabled());
            action->trigger(); QVERIFY(!InputRhythm::enabled());
            preferences.save();
            QSettings stored; stored.sync();
            QVERIFY(stored.contains(PREF_SCORE_NOTE_INPUT_AUTO_RHYTHM));
            QVERIFY(!stored.value(PREF_SCORE_NOTE_INPUT_AUTO_RHYTHM).toBool());
            action->trigger(); QVERIFY(InputRhythm::enabled());
            }
      void restoredWorkspaceKeepsRhythmMenu()
            {
            auto main=Ms::mscore;
            auto action=main->findChild<QAction*>("auto-rhythmic-input");
            QVERIFY(action);
            QDir().mkpath(dataPath+"/workspaces/global");
            Workspace::writeGlobalMenuBar(main->menuBar());
            QFile saved(dataPath+"/workspaces/global/menubar.xml");
            QVERIFY(saved.open(QIODevice::ReadOnly));
            QByteArray xml=saved.readAll();saved.close();
            QVERIFY(xml.contains("auto-rhythmic-input"));
            // Simulate a saved menu from before the personal action existed.
            xml.replace("<action>auto-rhythmic-input</action>","");
            QVERIFY(!xml.contains("auto-rhythmic-input"));
            QVERIFY(saved.open(QIODevice::WriteOnly|QIODevice::Truncate));
            QCOMPARE(saved.write(xml),qint64(xml.size()));saved.close();
            XmlReader reader(xml);QVERIFY(reader.readNextStartElement());
            Workspace workspace;workspace.read(reader);
            auto tools=Workspace::findMenuFromString("menu-tools");
            QVERIFY(tools);QVERIFY(tools->actions().contains(action));
            QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
            QCOMPARE(main->findChild<QAction*>("auto-rhythmic-input"),action);
            QTranslator chinese;
            QVERIFY(chinese.load(QCoreApplication::applicationDirPath()+"/../locale/mscore_zh_CN.qm"));
            QCoreApplication::installTranslator(&chinese);
            QEvent language(QEvent::LanguageChange);QCoreApplication::sendEvent(main,&language);
            main->updateMenus();main->updateMenus();
            QCOMPARE(tools->actions().count(action),1);
            QString expected;
            for (ushort code : {0x81ea,0x52a8,0x89c4,0x8303,0x8f93,0x5165,0x65f6,0x503c})
                  expected+=QChar(code);
            QCOMPARE(action->text(),expected);
            QCOMPARE(tools->actions().indexOf(action),
                  tools->actions().indexOf(getAction("reset-groupings"))+1);
            QVERIFY(action->isChecked());action->trigger();QVERIFY(!InputRhythm::enabled());
            action->trigger();QVERIFY(InputRhythm::enabled());
            QCoreApplication::removeTranslator(&chinese);main->updateMenus();
            }
      void cleanupTestCase()
            {
            // Destroy font selectors before QApplication removes application
            // fonts and emits fontDatabaseChanged during its own destruction.
            delete Ms::mscore;Ms::mscore=nullptr;
            }
      void pianoRollInputGrouping()
            {
            auto main=Ms::mscore;
            auto score=main->readScore(QString(TESTROOT)+"/mtest/libmscore/inputrhythm/blank.mscx");
            QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
            Pos locators[3];for(auto& locator:locators) locator.setContext(score->tempomap(),score->sigmap());
            PianoView view;view.setStaff(score->staff(0),locators);
            const QString xml="<notes firstN=\"3\" firstD=\"8\"><note startN=\"3\" startD=\"8\" lenN=\"1\" lenD=\"4\" pitch=\"60\" voice=\"0\" staff=\"0\" veloOff=\"0\" veloType=\"o\" rhythmEligible=\"1\"/></notes>";
            score->startCmd();auto plain=view.pasteNotes(xml,Fraction(3,8),Fraction(),0);score->endCmd();
            QVERIFY(!plain.isEmpty());QCOMPARE(score->findCR(Fraction(3,8),0)->ticks(),Fraction(1,4));
            EditData ed;score->undoStack()->undo(&ed);
            score->startCmd();auto grouped=view.pasteNotes(xml,Fraction(3,8),Fraction(),0,false,true);score->endCmd();
            QCOMPARE(grouped.size(),2);QCOMPARE(grouped.front()->playTicksFraction(),Fraction(1,4));
            QCOMPARE(grouped.front()->chord()->ticks(),Fraction(1,8));QCOMPARE(grouped.back()->tick(),Fraction(1,2));
            view.updateNotes();
            score->undoStack()->undo(&ed);QVERIFY(score->firstMeasure()->first(SegmentType::ChordRest)->cr(0)->isRest());
            score->undoStack()->redo(&ed);view.updateNotes();
            QCOMPARE(toChord(score->findCR(Fraction(3,8),0))->upNote()->playTicksFraction(),Fraction(1,4));
            view.setStaff(nullptr,nullptr);
            }

      void reloadActiveWorkflowPanels()
            {
            auto main=Ms::mscore;
            auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");
            QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
            main->resize(1100,800);main->show();
            PluginManager manager(main);manager.init();
            const QString path=QString(TESTROOT)+"/share/plugins/HarmonyAssistant/HarmonyAssistant_MS3.qml";
            main->pluginTriggered(path);QTest::qWait(100);
            auto live=[&](){int count=0;for(auto window:QGuiApplication::allWindows())if(auto view=qobject_cast<QQuickView*>(window))if(auto root=qobject_cast<QmlPlugin*>(view->rootObject()))if(root->menuPath()=="Plugins.Harmony Assistant")++count;return count;};
            QCOMPARE(live(),1);
            for(int i=0;i<3;++i){
                  QTimer dismiss;dismiss.setInterval(50);connect(&dismiss,&QTimer::timeout,this,[]{for(auto w:QApplication::topLevelWidgets())if(auto box=qobject_cast<QMessageBox*>(w))box->accept();});dismiss.start();
                  QVERIFY(QMetaObject::invokeMethod(&manager,"reloadPluginsClicked",Qt::DirectConnection));
                  QTest::qWait(100);QCOMPARE(live(),1);
                  }
            releaseHostPanels();
            }
      void builtinToolEntryPoints()
            {
            auto main=Ms::mscore;
            auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");
            QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->resize(1280,900);main->show();
            auto harmony=getAction("harmony-assistant");auto expression=getAction("performance-expression");
            QVERIFY(harmony);QVERIFY(expression);
            showHarmonyPanel();QTest::qWait(120);
            auto dock=main->findChild<QDockWidget*>("kumoPanel/HarmonyAssistant_MS3");QVERIFY(dock);
            auto original=dock;
            showHarmonyPanel(false);QVERIFY(!dock->isVisible());showHarmonyPanel();QCOMPARE(main->findChild<QDockWidget*>(dock->objectName()),original);
            expression->trigger();QTest::qWait(120);
            auto editor=main->performanceEditor();auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs");QVERIFY(tabs);QCOMPARE(tabs->currentIndex(),1);
            auto page=editor->findChild<QWidget*>("embeddedExpressionPage");QVERIFY(page);QVERIFY(page->isVisible());QVERIFY(page->property("loaded").toBool());
            auto root=qobject_cast<QmlPlugin*>(page->property("root").value<QObject*>());QVERIFY(root);QVERIFY(root->property("embeddedWorkflow").toBool());
            auto loaded=root;tabs->setCurrentIndex(0);QVERIFY(!page->isVisible());showExpressionPanel();QCOMPARE(page->property("root").value<QObject*>(),static_cast<QObject*>(loaded));QVERIFY(page->isVisible());
            editor->findChild<QAction*>("editorArea/float/embeddedExpressionPage")->trigger();QTest::qWait(100);
            // Use the real import button and file picker, then adopt through the page.
            QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
            root->window()->requestActivate();QTest::qWait(100);
            QVERIFY(root->window()->grabWindow().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("builtin-import-before.png")));
            auto importButton=root->findChild<QQuickItem*>("embeddedExpressionImport");QVERIFY(importButton);QSignalSpy clicked(importButton,SIGNAL(clicked()));
            const QByteArray track=QByteArray::fromHex("00ff510307a120009034640090376483608034000080370000903b6400903e64009045648360803b0000803e000080450000ff2f00");
            QByteArray midi=QByteArray::fromHex("4d546864000000060000000101e0");midi.append("MTrk");for(int i=3;i>=0;--i)midi.append(char(track.size()>>(i*8)));midi.append(track);
            const auto midiPath=_settings.filePath("native-expression.mid");QFile midiFile(midiPath);QVERIFY(midiFile.open(QIODevice::WriteOnly));QCOMPARE(midiFile.write(midi),qint64(midi.size()));midiFile.close();
            auto click=[root](const char* name){auto item=root->findChild<QQuickItem*>(name);if(!item||!root->window())return false;QTest::mouseClick(root->window(),Qt::LeftButton,Qt::NoModifier,item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint());return true;};
            QTimer chooseMidi;bool picked=false;int pickerTries=0;QPointer<QFileDialog> picker;
            chooseMidi.setInterval(100);connect(&chooseMidi,&QTimer::timeout,this,[&]{
                  ++pickerTries;
                  auto dialog=qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
                  if(!dialog){if(picker){picked=true;chooseMidi.stop();}return;}
                  if(!picker){picker=dialog;dialog->setDirectory(QFileInfo(midiPath).absolutePath());dialog->selectFile(QFileInfo(midiPath).fileName());return;}
                  if(auto edit=dialog->findChild<QLineEdit*>("fileNameEdit")){edit->setFocus();edit->setText(midiPath);}
                  QMetaObject::invokeMethod(dialog,"accept",Qt::DirectConnection);
                  if(!dialog->isVisible()){picked=true;chooseMidi.stop();}else if(pickerTries>50){dialog->reject();chooseMidi.stop();}
                  });chooseMidi.start();
            QVERIFY(click("embeddedExpressionImport"));QTRY_VERIFY_WITH_TIMEOUT(clicked.count()>0,1500);QTRY_VERIFY_WITH_TIMEOUT(picked,6000);chooseMidi.stop();
            auto candidate=[root](){auto value=root->property("candidate");return value.canConvert<QJSValue>()?value.value<QJSValue>().toVariant().toMap():value.toMap();};
            QTRY_VERIFY2_WITH_TIMEOUT(!candidate().value("velocity").toList().isEmpty(),qUtf8Printable(root->property("status").toString()),10000);
            auto resultList=root->findChild<QQuickItem*>("embeddedExpressionResults");QVERIFY(resultList);
            QTRY_VERIFY(resultList->property("count").toInt()>0);
            std::function<QQuickItem*(QQuickItem*)> findResultRow=[&](QQuickItem* item)->QQuickItem*{
                  if(item->objectName()=="expressionResultRow")return item;
                  for(auto child:item->childItems())if(auto found=findResultRow(child))return found;
                  return nullptr;
                  };
            QTRY_VERIFY(findResultRow(resultList));
            root->setProperty("selected",-1);
            auto firstRow=findResultRow(resultList);QVERIFY(firstRow->height()>0&&firstRow->isVisible());
            QTest::mouseClick(root->window(),Qt::LeftButton,Qt::NoModifier,firstRow->mapToScene(QPointF(firstRow->width()/2,firstRow->height()/2)).toPoint());
            QTRY_COMPARE(root->property("selected").toInt(),0);
            QVERIFY(!score->selection().noteList().empty());
            QTest::qWait(80);QVERIFY(root->window()->grabWindow().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("builtin-expression-ready-058.png")));
            auto chord=score->findCR(Fraction::fromTicks(480),0);QVERIFY(chord&&chord->isChord());auto first=toChord(chord)->notes().front();
            const auto previousType=first->veloType();const int previousRaw=first->veloOffset();const int beforeUndo=score->undoStack()->getCurIdx();
            QVERIFY(click("expressionApplyVelocity"));QTRY_COMPARE(score->undoStack()->getCurIdx(),beforeUndo+1);
            QCOMPARE(first->veloType(),Note::ValueType::USER_VAL);QCOMPARE(NoteVelocity::effective(NoteVelocity::referenceBase(first),first->veloType(),first->veloOffset()),100);
            EditData undo;score->undoRedo(true,&undo);QCOMPARE(first->veloType(),previousType);QCOMPARE(first->veloOffset(),previousRaw);
            QVERIFY(editor->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("builtin-expression-058.png")));
            QVERIFY(main->currentScoreView()->height()>80);
            QVERIFY(root->window());QTest::qWait(80);QVERIFY(root->window()->grabWindow().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("builtin-expression-controls-058.png")));
            editor->findChild<QAction*>("editorArea/restore/embeddedExpressionPage")->trigger();tabs->setCurrentIndex(0);main->showPerformanceEditor(false);
            }
      void fluidResourceReleaseUnlocksMutex()
            {
            static std::atomic<int> warnings{0};
            static QtMessageHandler previous=nullptr;
            warnings=0;
            previous=qInstallMessageHandler([](QtMsgType type,const QMessageLogContext& context,const QString& message){
                  if(message.contains("QMutex: destroying locked mutex"))++warnings;
                  if(previous)previous(type,context,message);
                  });
            struct RestoreHandler {QtMessageHandler original;~RestoreHandler(){qInstallMessageHandler(original);}} restore{previous};
            for(int i=0;i<3;++i){FluidS::Fluid synth;synth.init(44100);}
            QCOMPARE(warnings.load(),0);
            }
      void rollWritingMouseAndDraft(){
            struct SettingsRestore{QVariantMap values;SettingsRestore(){QSettings settings;for(auto key:settings.allKeys())values[key]=settings.value(key);}~SettingsRestore(){QSettings settings;settings.clear();for(auto i=values.cbegin();i!=values.cend();++i)settings.setValue(i.key(),i.value());}} restoreSettings;
            auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->resize(1280,1000);main->show();score->select(score->findCR(Fraction(),0));main->showPerformanceEditor(true);QTest::qWait(100);
            auto editor=main->performanceEditor();auto bar=editor->findChild<RollWritingBar*>("rollWritingBar");auto canvas=editor->findChild<QWidget*>("performanceNoteCanvas");auto toggle=editor->findChild<QToolButton*>("rollWriteToggle");QVERIFY(bar&&canvas&&toggle);QVERIFY(!toggle->isChecked());
            editor->findChild<QAction*>("editorArea/float/performanceNotesArea")->trigger();auto area=qobject_cast<QDockWidget*>(canvas->parentWidget()->property("kumoDetachedDock").value<QObject*>());QVERIFY(area);area->resize(1000,720);QTest::qWait(120);QTest::mouseClick(toggle,Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(bar->state()["ready"].toBool(),5000);
            const auto scope=bar->state()["scope"].toMap();const auto original=rollNotationIdentity(score,scope);const int before=score->undoStack()->getCurIdx();const auto geometry=bar->state();const double step=geometry["quarterPixels"].toDouble(),x=geometry["fromX"].toDouble();const QPoint start(qRound(x+.5*step),canvas->height()/2),end(qRound(x+1.5*step),start.y());
            QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,start);QTest::mouseMove(canvas,end,30);QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,end);QTRY_VERIFY_WITH_TIMEOUT(!bar->state()["busy"].toBool()&&bar->state()["ready"].toBool(),5000);QTRY_COMPARE(score->undoStack()->getCurIdx(),before+1);QVERIFY(rollNotationIdentity(score,scope)!=original);QVERIFY(!bar->state()["selected"].toList().isEmpty());
            const auto afterDirect=rollNotationIdentity(score,scope);EditData edit;score->undoRedo(true,&edit);QTRY_COMPARE(rollNotationIdentity(score,scope),original);score->undoRedo(false,&edit);QTRY_COMPARE(rollNotationIdentity(score,scope),afterDirect);QTRY_VERIFY(bar->state()["ready"].toBool());
            auto mode=bar->findChild<QComboBox*>("rollWriteMode");QVERIFY(mode);mode->setCurrentIndex(1);QTRY_VERIFY(bar->state()["ready"].toBool());const int draftUndo=score->undoStack()->getCurIdx();const QPoint draftStart(qRound(x+2*step),qMax(8,start.y()-36)),draftEnd(qRound(x+2.75*step),draftStart.y());
            QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,draftStart);QTest::mouseMove(canvas,draftEnd,30);QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,draftEnd);QTRY_VERIFY_WITH_TIMEOUT(!bar->state()["busy"].toBool(),5000);QTRY_COMPARE(score->undoStack()->getCurIdx(),draftUndo+1);QCOMPARE(rollNotationIdentity(score,scope),afterDirect);
            auto apply=bar->findChild<QToolButton*>("rollWriteApply");QVERIFY(apply&&apply->isEnabled());const auto draftNotes=bar->state()["notes"].toList();QVERIFY(draftNotes.size()>=2);QTest::mouseClick(apply,Qt::LeftButton);QTRY_VERIFY(bar->state()["ready"].toBool());QVERIFY(rollNotationIdentity(score,scope)!=afterDirect);score->undoRedo(true,&edit);QTRY_COMPARE(rollNotationIdentity(score,scope),afterDirect);QTRY_COMPARE(bar->state()["sourceIdentity"].toString(),afterDirect);QTRY_VERIFY_WITH_TIMEOUT(bar->state()["ready"].toBool(),5000);QTRY_COMPARE(bar->state()["notes"].toList().size(),draftNotes.size());QTRY_VERIFY(apply->isEnabled());
            const int notesBeforeCopy=bar->state()["notes"].toList().size();QTest::mouseClick(canvas,Qt::LeftButton,Qt::NoModifier,QPoint(draftStart.x()+qRound(step*.25),draftStart.y()));canvas->setFocus();QTest::keyClick(canvas,Qt::Key_D,Qt::ControlModifier);QTRY_VERIFY_WITH_TIMEOUT(!bar->state()["busy"].toBool(),5000);QTRY_COMPARE(bar->state()["notes"].toList().size(),notesBeforeCopy+1);QCOMPARE(rollNotationIdentity(score,scope),afterDirect);QTest::keyClick(canvas,Qt::Key_Delete);QTRY_VERIFY_WITH_TIMEOUT(!bar->state()["busy"].toBool(),5000);QTRY_COMPARE(bar->state()["notes"].toList().size(),notesBeforeCopy);
            const auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QVERIFY(area->grab().save(output.filePath("roll-writing-067.png")));QFileInfo saved(_settings.filePath("roll-draft.mscz"));QVERIFY(score->saveCompressedFile(saved,false,false));std::unique_ptr<MasterScore> restored(main->readScore(saved.absoluteFilePath()));QVERIFY(restored);QCOMPARE(restored->metaTag("kumo.rollWriting"),score->metaTag("kumo.rollWriting"));
            area->resize(520,720);QTest::qWait(100);QVERIFY(bar->width()<=area->width());QVERIFY(bar->grab().save(output.filePath("roll-writing-narrow-067.png")));canvas->setFocus();QTest::keyClick(canvas,Qt::Key_Escape);QTRY_VERIFY(!toggle->isChecked());QVERIFY(!rollWritingAction()->isChecked());editor->findChild<QAction*>("editorArea/restore/performanceNotesArea")->trigger();
      }
#ifdef Q_MOC_RUN
      void vstNativeSelectionUndoAndSave();
      void vstDisabledRetainsStateAndUsesSF2();
      void systemLocksUndoAndFile();
      void upstreamLiveTranslationAndPreferences();
#else
      #include "vst_tests.inc"
      #include "system_lock_tests.inc"
#endif
      void rollLocalScopeCost(){
            QVariantList rows,measures;for(int i=0;i<10000;++i){rows.append(QVariantMap{{"id",QString::number(i)},{"track",0},{"pitch",60},{"start",1920*i},{"end",1920*i+480}});measures.append(QVariantMap{{"start",1920*i},{"end",1920*(i+1)}});}auto current=rows;auto changed=current[5000].toMap();changed["pitch"]=62;current[5000]=changed;const QVariantMap scope{{"start",0},{"end",19200000},{"firstTrack",0},{"endTrack",4}};QElapsedTimer time;time.start();const auto affected=rollAffectedScope(current,rows,scope,measures,{},QVariantList{0});qInfo()<<"Roll affected scope 10k notes ms"<<time.nsecsElapsed()/1e6;QCOMPARE(affected["start"].toInt(),9600000);QCOMPARE(affected["end"].toInt(),9601920);
      }
      void rollLargeEditWorkerAndCancel(){
            QVariantList rows,measures;for(int i=0;i<10000;++i){rows.append(QVariantMap{{"id",QString::number(i)},{"track",0},{"pitch",60},{"tpc1",14},{"tpc2",14},{"start",1920*i},{"end",1920*i+480}});measures.append(QVariantMap{{"start",1920*i},{"end",1920*(i+1)}});}auto current=rows;auto changed=current[5000].toMap();changed["pitch"]=62;changed["tpc1"]=16;changed["tpc2"]=16;current[5000]=changed;const QVariantMap scope{{"start",0},{"end",19200000},{"firstTrack",0},{"endTrack",4}};int pulses=0;QTimer heartbeat;connect(&heartbeat,&QTimer::timeout,this,[&]{++pulses;});heartbeat.start(1);auto result=QtConcurrent::run([=]{return buildRollEdit(current,rows,scope,measures,{},{{"quantum",120},{"suggestVoices",false}},true);});QTRY_VERIFY_WITH_TIMEOUT(result.isFinished(),5000);heartbeat.stop();QVERIFY(pulses>0);const auto edit=result.result();QVERIFY2(!edit.contains("error"),qPrintable(edit["error"].toString()));QCOMPARE(edit["scope"].toMap()["start"].toInt(),9600000);QCOMPARE(edit["scope"].toMap()["end"].toInt(),9601920);QCOMPARE(edit["voices"].toList().size(),1);std::atomic_bool cancelled{true};QVERIFY(buildRollEdit(current,rows,scope,measures,{},{{"quantum",120}},true,&cancelled)["cancelled"].toBool());
      }
      void rollCopyKeepsPersonalSettings(){
            auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));score->startCmd();auto rest=score->findCR(Fraction(),0);QVERIFY(rest&&rest->isRest());score->setNoteRest(rest->segment(),0,NoteVal(60),Fraction(1,4));auto chord=toChord(score->findCR(Fraction(),0));auto note=chord->notes().front();note->setSmall(true);note->setColor(QColor("#c08055"));note->setVeloType(Note::ValueType::OFFSET_VAL);note->setVeloOffset(12);NoteEventList events;events.clear();events.append(NoteEvent(0,17,830));note->setPlayEvents(events);chord->setPlayEventType(PlayEventType::User);auto finger=new Fingering(score);finger->setParent(note);finger->setTrack(0);finger->setPlainText("3");score->undoAddElement(finger);score->endCmd();
            PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);auto state=observer.arrangementState({{"firstMeasure",1},{"lastMeasure",1}});auto scope=state["scope"].toMap();scope["firstTrack"]=0;scope["endTrack"]=1;
            const QVariantMap original{{"id","original"},{"sourceId","0:0:0"},{"track",0},{"pitch",60},{"tpc1",14},{"tpc2",14},{"start",0},{"end",480},{"velocity",90}};auto copy=original;copy["id"]="copy";copy.remove("sourceId");copy["copySourceId"]="0:0:0";copy["start"]=480;copy["end"]=960;copy["new"]=true;auto values=buildRollNotation({original,copy},scope,{QVariantMap{{"start",0},{"end",1920}}},{},{{"quantum",120},{"suggestVoices",false}});values["snapshotId"]=observer.analysisContentRevision();const auto result=applyRollNotation(&observer,score,values);QVERIFY2(result["ok"].toBool(),qPrintable(result["error"].toString()));auto copied=toChord(score->findCR(Fraction::fromTicks(480),0))->notes().front();QVERIFY(copied->getProperty(Pid::SMALL).toBool());QCOMPARE(copied->color(),QColor("#c08055"));QCOMPARE(copied->veloType(),Note::ValueType::OFFSET_VAL);QCOMPARE(copied->veloOffset(),12);QCOMPARE(copied->playEvents().front().ontime(),17);QCOMPARE(copied->playEvents().front().len(),830);bool fingering=false;for(auto e:copied->el())if(e->isFingering())fingering|=toTextBase(e)->plainText()=="3";QVERIFY(fingering);
      }
      void rollNotationAndNativeApply(){
            auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->show();
            PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);auto state=observer.arrangementState({{"firstMeasure",1},{"lastMeasure",1}});QVERIFY(!state.contains("error"));auto scope=state["scope"].toMap();scope["firstTrack"]=0;scope["endTrack"]=1;
            auto row=[](QString id,int pitch,int begin,int end){return QVariantMap{{"id",id},{"pitch",pitch},{"tpc1",pitch==60?14:pitch==62?16:18},{"tpc2",pitch==60?14:pitch==62?16:18},{"start",begin},{"end",end},{"rawStart",begin},{"rawEnd",end},{"track",0},{"new",true}};};
            const QVariantList measures{QVariantMap{{"start",0},{"end",1920}}};auto values=buildRollNotation({row("low",60,0,960),row("upper",64,480,720)},scope,measures,{},{{"quantum",120},{"suggestVoices",false}});QVERIFY2(!values.contains("error"),qPrintable(values["error"].toString()));
            values["snapshotId"]=observer.analysisContentRevision();values["source"]=QVariantMap{{"kind","rollDirect"},{"allowVoiceReassignment",true}};const int undo=score->undoStack()->getCurIdx();auto applied=applyRollNotation(&observer,score,values);QVERIFY2(applied["ok"].toBool(),qPrintable(applied["error"].toString()));QCOMPARE(score->undoStack()->getCurIdx(),undo+1);
            auto first=toChord(score->findCR(Fraction(),0));QVERIFY(first&&first->notes().front()->tieFor());auto middle=toChord(score->findCR(Fraction::fromTicks(480),0));QVERIFY(middle);QCOMPARE(middle->notes().size(),size_t(2));for(auto note:middle->notes())if(note->pitch()==60){QVERIFY(note->tieBack());QVERIFY(note->tieFor());}else{QVERIFY(!note->tieBack());QVERIFY(!note->tieFor());}
            EditData edit;score->undoRedo(true,&edit);QVERIFY(score->findCR(Fraction(),0)->isRest());
            values=buildRollNotation({row("triplet1",60,0,160),row("triplet2",62,160,320),row("triplet3",64,320,480)},scope,measures,{},{{"quantum",160},{"triplet",true},{"suggestVoices",false}});QVERIFY(!values.contains("error"));
            // Mark the drawn notes so the triplet group is created.
            auto rows=values["notes"].toList();for(auto &v:rows){auto note=v.toMap();note["edited"]=true;v=note;}values=buildRollNotation(rows,scope,measures,{},{{"quantum",160},{"triplet",true},{"suggestVoices",false}});QCOMPARE(values["voices"].toList().front().toMap()["tuplets"].toList().size(),1);values["snapshotId"]=observer.analysisContentRevision();applied=applyRollNotation(&observer,score,values);QVERIFY2(applied["ok"].toBool(),qPrintable(applied["error"].toString()));QVERIFY(toChord(score->findCR(Fraction(),0))->tuplet());score->undoRedo(true,&edit);
            auto fine=row("fine",60,0,240);fine["rawStart"]=13;fine["rawEnd"]=207;fine["edited"]=true;values=buildRollNotation({fine},scope,measures,{},{{"quantum",120},{"suggestVoices",false},{"preserveTiming",true}});values["snapshotId"]=observer.analysisContentRevision();applied=applyRollNotation(&observer,score,values);QVERIFY2(applied["ok"].toBool(),qPrintable(applied["error"].toString()));EventMap rendered;score->renderMidi(&rendered,true,false,main->synthesizerState());int attack=-1,release=-1;for(const auto &event:rendered)if(event.second.type()==ME_NOTEON&&event.second.pitch()==60){if(event.second.velo()>0)attack=event.first;else release=event.first;}QVERIFY(qAbs(attack-13)<=1);QVERIFY(qAbs(release+1-207)<=1);score->undoRedo(true,&edit);
            QCOMPARE(rollSnapInScore(850,480,false,0,0,{QVariantMap{{"start",0},{"end",720}},QVariantMap{{"start",720},{"end",1440}}},{}),720);
            QCOMPARE(rollSnapInScore(85,120,false,0,0,measures,{QVariantMap{{"track",0},{"start",0},{"duration",480},{"ratio","3/2"}}}),80);
            auto allocated=buildRollNotation({row("a",60,0,960),row("b",64,480,720)},QVariantMap{{"start",0},{"end",1920},{"firstTrack",0},{"endTrack",4}},measures,{},{{"quantum",120},{"suggestVoices",true}});QVERIFY(!allocated.contains("error"));QCOMPARE(allocated["notes"].toList()[1].toMap()["track"].toInt(),1);QCOMPARE(rollQuantizeTick(119,0,120),120);QCOMPARE(rollQuantizeTick(61,0,120),120);
      }
      void floatingEditorShutdown(){
            struct SettingsRestore{QVariantMap values;SettingsRestore(){QSettings settings;for(auto key:settings.allKeys())values[key]=settings.value(key);}~SettingsRestore(){QSettings settings;settings.clear();for(auto i=values.cbegin();i!=values.cend();++i)settings.setValue(i.key(),i.value());}} restoreSettings;
            auto main=Ms::mscore;main->show();auto editor=new PerformanceEditor;
            auto pane=editor->findChild<QWidget*>("transcriptionSpectrumPane");QVERIFY(pane);editor->resize(900,600);editor->show();auto action=editor->findChild<QAction*>("editorArea/float/transcriptionSpectrumPane");QVERIFY(action);action->trigger();QTest::qWait(80);
            QPointer<QDockWidget> dock=qobject_cast<QDockWidget*>(pane->property("kumoDetachedDock").value<QObject*>());QVERIFY(dock&&dock->isFloating());delete editor;QVERIFY(dock.isNull());
      }
      void spectrumMarkExtentAndDuration(){
            QImage data(400,512,QImage::Format_Grayscale8);data.fill(0);const int row=qRound((128.-69)/128*512);for(int y=row-1;y<=row+1;++y)for(int x=100;x<200;++x)data.scanLine(y)[x]=220;
            const QVariantMap info{{"from",0.},{"until",4000.},{"lowPitch",0.},{"highPitch",128.},{"tuning",440.},{"fftSize",16384},{"sampleRate",44100}};
            auto value=estimateSpectrumExtent(data,info,1500.,440.);QVERIFY(!value.isEmpty());QVERIFY(qAbs(value["ms"].toDouble()-1000)<100);QVERIFY(qAbs(value["until"].toDouble()-2000)<100);QVERIFY(estimateSpectrumExtent(data,info,500.,440.).isEmpty());
            auto duration=spectrumInputDuration(1.5);QCOMPARE(duration.type(),TDuration::DurationType::V_QUARTER);QCOMPARE(duration.dots(),1);QCOMPARE(spectrumInputDuration(1.1).type(),TDuration::DurationType::V_QUARTER);
      }
      void transcriptionManualFlow()
            {
            auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->resize(1280,960);main->show();
            const auto path=_settings.filePath("spectral-a440.wav");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QDataStream data(&file);data.setByteOrder(QDataStream::LittleEndian);data.writeRawData("RIFF",4);data<<quint32(36+44100*4);data.writeRawData("WAVEfmt ",8);data<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(44100)<<quint32(88200)<<quint16(2)<<quint16(16);data.writeRawData("data",4);data<<quint32(44100*4);for(int i=0;i<88200;++i)data<<qint16(15000*std::sin(2*3.14159265358979323846*440*i/44100));file.close();
            auto media=referenceProject();QVariantMap response;const QVariantList tracks{QVariantMap{{"id","spectral-test"},{"name","440 Hz"},{"path",path},{"associations",QVariantList{QVariantMap{{"start",0},{"end",1920},{"from",0.},{"until",2000.},{"name","第1小节"},{"active",true}}}}}};
            workflowContext()->setReferenceScore(score);QVERIFY(QMetaObject::invokeMethod(media,"setTracks",Q_RETURN_ARG(QVariantMap,response),Q_ARG(QVariantList,tracks)));QVERIFY(response.value("ok").toBool());
            main->showPerformanceEditor(true);auto editor=main->performanceEditor();auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs");QVERIFY(tabs);QVERIFY(tabs->count()>=4);tabs->setCurrentIndex(2);QTest::qWait(150);
            auto pane=editor->findChild<QWidget*>("transcriptionSpectrumPane");QVERIFY(pane&&pane->isVisible());auto session=pane->findChild<QObject*>("transcriptionSpectrumSession");QVERIFY(session);QVERIFY(session->property("available").toBool());
            QTRY_VERIFY_WITH_TIMEOUT(!qvariant_cast<QImage>(session->property("image")).isNull(),10000);
            auto playback=pane->findChild<QToolButton*>("transcriptionPlay");QVERIFY(playback);QTRY_VERIFY(playback->isEnabled());const auto oldVolume=media->property("volume");media->setProperty("volume",0.);QSignalSpy playbackChanged(media,SIGNAL(changed()));QTest::mouseClick(playback,Qt::LeftButton);QVERIFY(playbackChanged.count()>0);QTRY_VERIFY_WITH_TIMEOUT(!media->property("playing").toBool()||media->property("diagnostics").toMap().value("outputFrames").toLongLong()>0,4000);if(media->property("playing").toBool()) {
                editor->findChild<QAction*>("editorArea/float/transcriptionSpectrumPane")->trigger();
                auto floating = main->findChild<QDockWidget*>("kumoEditorArea/transcriptionSpectrumPane");QVERIFY(floating);
                QTRY_VERIFY(floating->isVisible());QVERIFY(media->property("playing").toBool());
                auto full = floating->findChild<QAction*>("kumoPanelFullscreen");QVERIFY(full);
                full->trigger();QTest::qWait(30);QVERIFY(media->property("playing").toBool());
                full->trigger();QTest::qWait(30);QVERIFY(media->property("playing").toBool());
                editor->findChild<QAction*>("editorArea/restore/transcriptionSpectrumPane")->trigger();
                QTest::qWait(30);QVERIFY(media->property("playing").toBool());
                QTest::mouseClick(playback,Qt::LeftButton);
            }QTRY_VERIFY(!media->property("playing").toBool());media->setProperty("volume",oldVolume);
            auto canvas=pane->findChild<QWidget*>("transcriptionSpectrumCanvas");QVERIFY(canvas);auto piano=canvas->grab().toImage();int white=0,black=0;for(int y=0;y<piano.height();++y){auto color=piano.pixelColor(20,y);if(color.lightness()>200)++white;if(color.lightness()<40)++black;}QVERIFY(white>piano.height()/8);QVERIFY(black>piano.height()/8);auto mark=pane->findChild<QToolButton*>("transcriptionMark");auto input=pane->findChild<QToolButton*>("transcriptionInput");QVERIFY(mark&&input);
            auto marksToggle=pane->findChild<QToolButton*>("spectrumMarksToggle");QVERIFY(marksToggle);
            auto markList=pane->findChild<QListWidget*>("transcriptionMarks");QVERIFY(markList);
            QCOMPARE(markList->count(),0);QVERIFY(!markList->isVisible());QVERIFY(!marksToggle->isEnabled());
            auto sharedPlan=media->property("workflowPlaybackPlan").toMap();sharedPlan["owner"]="reference-panel";sharedPlan["volume"]=0.;QVERIFY(QMetaObject::invokeMethod(media,"start",Q_RETURN_ARG(QVariantMap,response),Q_ARG(QVariantMap,sharedPlan)));QVERIFY(!response.contains("error"));QTRY_VERIFY(playback->text().contains(QString::fromUtf8("暂停")));QTest::keyClick(canvas,Qt::Key_Space);QTRY_VERIFY(!media->property("playing").toBool());media->setProperty("volume",oldVolume);
            auto rest=score->findCR(Fraction(),0);QVERIFY(rest&&rest->isRest());score->select(rest);main->currentScoreView()->cmd("note-input");score->inputState().setDuration(TDuration(TDuration::DurationType::V_QUARTER));
            QTest::mouseClick(canvas,Qt::LeftButton,Qt::NoModifier,QPoint(gutterForTest()+100,qRound((108.5-69)/88.*canvas->height())));
            QTest::mouseMove(canvas,QPoint(canvas->width()-20,canvas->height()-2));const int before=score->undoStack()->getCurIdx();QTest::mouseClick(mark,Qt::LeftButton);QTRY_COMPARE(score->undoStack()->getCurIdx(),before+1);
            const auto saved=QJsonDocument::fromJson(score->masterScore()->metaTag("kumo.transcription").toUtf8()).toVariant().toMap();QCOMPARE(saved.value("pitches").toList().size(),1);QCOMPARE(saved.value("pitches").toList().front().toMap().value("pitch").toInt(),69);
            auto marks=pane->findChild<QListWidget*>("transcriptionMarks");QVERIFY(marks&&marks->count()==1);marks->item(0)->setSelected(true);QTest::mouseClick(input,Qt::LeftButton);QTRY_COMPARE(score->undoStack()->getCurIdx(),before+2);auto chord=score->findCR(Fraction(),0);QVERIFY(chord&&chord->isChord());QCOMPARE(toChord(chord)->notes().front()->pitch(),69);
            Shortcut::load();for(const char* key:{"undo","redo"}){auto shortcut=Shortcut::getShortcut(key);shortcut->setStandardKey(shortcut->standardKey());}QVERIFY(!getAction("undo")->shortcut().isEmpty());main->activateWindow();
            QApplication::setActiveWindow(main);canvas->setFocus();QTRY_VERIFY(canvas->hasFocus());QTest::keySequence(canvas,getAction("undo")->shortcut());QTRY_VERIFY(score->findCR(Fraction(),0)->isRest());QTest::keySequence(canvas,getAction("undo")->shortcut());QTRY_VERIFY(score->masterScore()->metaTag("kumo.transcription").isEmpty());QTRY_COMPARE(marks->count(),0);QTest::keySequence(canvas,getAction("redo")->shortcut());QTest::keySequence(canvas,getAction("redo")->shortcut());QTRY_VERIFY(score->findCR(Fraction(),0)->isChord());
            QFileInfo info(_settings.filePath("spectral-mark.mscz"));QVERIFY(score->saveCompressedFile(info,false,false));std::unique_ptr<MasterScore> restored(main->readScore(info.absoluteFilePath()));QVERIFY(restored);QCOMPARE(restored->metaTag("kumo.transcription"),score->masterScore()->metaTag("kumo.transcription"));
            const auto out=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));
            QFile savedInput(info.absoluteFilePath());QVERIFY(savedInput.open(QIODevice::ReadOnly));QFile savedOutput(out.filePath("transcription-compat-059.mscz"));QVERIFY(savedOutput.open(QIODevice::WriteOnly));QCOMPARE(savedOutput.write(savedInput.readAll()),savedInput.size());savedOutput.close();
            QVERIFY(pane->grab().save(out.filePath("transcription-separate-059.png")));
            auto display=pane->findChild<QComboBox*>("transcriptionDisplay");QVERIFY(display);display->setCurrentIndex(1);QTest::qWait(100);QVERIFY(editor->grab().save(out.filePath("transcription-overlay-059.png")));display->setCurrentIndex(0);
            editor->findChild<QAction*>("editorArea/float/transcriptionSpectrumPane")->trigger();auto separate=main->findChild<QDockWidget*>("kumoEditorArea/transcriptionSpectrumPane");QVERIFY(separate);separate->resize(920,700);QTest::qWait(180);
            if(!marksToggle->isChecked())QTest::mouseClick(marksToggle,Qt::LeftButton);
            QTRY_VERIFY(markList->isVisible());auto markSplit=pane->findChild<QSplitter*>("spectrumMarksSplitter");QVERIFY(markSplit);
            const int oldMarkHeight=markList->height();auto handle=markSplit->handle(1);QVERIFY(handle&&handle->isVisible());
            const auto center=handle->mapTo(handle->window(),handle->rect().center());auto window=handle->window()->windowHandle();
            QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,center);QTest::mouseMove(window,center-QPoint(0,50),30);QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,center-QPoint(0,50));
            QTRY_VERIFY(markList->height()>oldMarkHeight+20);QVERIFY(!QSettings().value("spectrum/marksSplitter").toByteArray().isEmpty());
            const int shownCanvasHeight=canvas->height();QTest::mouseClick(marksToggle,Qt::LeftButton);QTRY_VERIFY(!markList->isVisible());QTRY_VERIFY(canvas->height()>shownCanvasHeight);
            QTest::mouseClick(marksToggle,Qt::LeftButton);QTRY_VERIFY(markList->isVisible());QVERIFY(QSettings().value("spectrum/showMarks").toBool());QTest::qWait(150);
            QVERIFY(!markList->visibleRegion().isEmpty());QVERIFY(markList->height()>=28);
            QVERIFY(separate->grab().save(out.filePath("spectrum-mark-split-074.png")));
            auto timeScroll=pane->findChild<QScrollBar*>("spectrumTimeScroll"),pitchScroll=pane->findChild<QScrollBar*>("spectrumPitchScroll");QVERIFY(timeScroll&&pitchScroll);QVERIFY(timeScroll->isVisible());QVERIFY(pitchScroll->isVisible());
            auto optionsButton=pane->findChild<QToolButton*>("spectrumDisplaySettings");QVERIFY(optionsButton);QTest::mouseClick(optionsButton,Qt::LeftButton);QTest::qWait(150);QTRY_VERIFY(!session->property("busy").toBool());
            auto optionsScroll=pane->findChild<QScrollArea*>("spectrumOptionsScroll");QVERIFY(optionsScroll&&optionsScroll->isVisible());auto optionTabs=pane->findChild<QTabWidget*>("spectrumOptionsTabs");QVERIFY(optionTabs);
            auto gain=pane->findChild<QSlider*>("spectrumDisplay_gain");QVERIFY(gain);optionsScroll->ensureWidgetVisible(gain);QSignalSpy analysisChanges(session,SIGNAL(changed()));QTest::keyClick(gain,Qt::Key_Right);QTest::qWait(150);QCOMPARE(analysisChanges.count(),0);
            QVERIFY(separate->grab().save(out.filePath("spectrum-display-066.png")));
            media->setProperty("volume",0.);QSignalSpy spacePlayback(media,SIGNAL(changed()));canvas->setFocus();QTest::keyClick(canvas,Qt::Key_Space);QTRY_VERIFY(spacePlayback.count()>0);if(media->property("playing").toBool())QTest::keyClick(canvas,Qt::Key_Space);QTRY_VERIFY(!media->property("playing").toBool());media->setProperty("volume",oldVolume);
            optionTabs->setCurrentIndex(2);QTest::qWait(50);const int gridUndo=score->undoStack()->getCurIdx();
            for(int tap=0;tap<4;++tap){QTest::keyClick(canvas,Qt::Key_Space,Qt::ShiftModifier);if(tap<3)QTest::qWait(250);}
            auto bpm=pane->findChild<QDoubleSpinBox*>("spectrumGridBpm");QVERIFY(bpm);QVERIFY(bpm->value()>220&&bpm->value()<260);QCOMPARE(score->undoStack()->getCurIdx(),gridUndo);
            auto saveGrid=pane->findChild<QToolButton*>("spectrumSaveGrid");QVERIFY(saveGrid);optionsScroll->ensureWidgetVisible(saveGrid);QTest::mouseClick(saveGrid,Qt::LeftButton);QTRY_COMPARE(score->undoStack()->getCurIdx(),gridUndo+1);
            auto stored=QJsonDocument::fromJson(score->metaTag("kumo.transcription").toUtf8()).toVariant().toMap();QCOMPARE(stored["grids"].toMap()["spectral-test"].toMap()["bpm"].toDouble(),bpm->value());
            QVERIFY(separate->grab().save(out.filePath("spectrum-grid-066.png")));EditData gridEdit;score->undoRedo(true,&gridEdit);QTest::qWait(50);QVERIFY(!QJsonDocument::fromJson(score->metaTag("kumo.transcription").toUtf8()).toVariant().toMap()["grids"].toMap().contains("spectral-test"));
            auto drawTool=pane->findChild<QComboBox*>("transcriptionTool");QVERIFY(drawTool);drawTool->setCurrentIndex(3);optionTabs->setCurrentIndex(3);QTest::qWait(50);
            const int drawUndo=score->undoStack()->getCurIdx();auto range=pane->findChild<QComboBox*>("transcriptionClips");QVERIFY(range);range->setCurrentIndex(0);QTest::qWait(100);
            const int width=canvas->width()-gutterForTest()-PerformanceViewport::rightMargin;const QPoint start(gutterForTest()+qRound(width*.10),qRound((108.5-60)/88.*canvas->height())),end(start.x()+qRound(width*.375),start.y());
            QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,start);QTest::mouseMove(canvas,end,20);QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,end);QTRY_COMPARE(score->undoStack()->getCurIdx(),drawUndo+1);QCOMPARE(marks->count(),2);
            auto drawn=marks->selectedItems();QCOMPARE(drawn.size(),1);const auto drawnMark=drawn.front()->data(Qt::UserRole+1).toMap();QVERIFY(qAbs(drawnMark["until"].toDouble()-drawnMark["ms"].toDouble()-750)<10);
            const QPoint extended(end.x()+qRound(width*.1),start.y());QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,end);QTest::mouseMove(canvas,extended,20);QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,extended);QTRY_COMPARE(score->undoStack()->getCurIdx(),drawUndo+2);QCOMPARE(marks->count(),2);
            canvas->setFocus();QTest::keyClick(canvas,Qt::Key_Delete);QTRY_COMPARE(marks->count(),1);EditData markEdit;score->undoRedo(true,&markEdit);QTRY_COMPARE(marks->count(),2);score->undoRedo(true,&markEdit);QTest::qWait(50);QCOMPARE(marks->count(),2);marks->clearSelection();marks->item(1)->setSelected(true);
            auto durationChoice=pane->findChild<QCheckBox*>("spectrumInputMarkedDuration");QVERIFY(durationChoice);durationChoice->setChecked(true);bpm->setValue(120.);main->currentScoreView()->cmd("escape");score->select(score->findCR(Fraction(),0));main->currentScoreView()->cmd("note-input");score->inputState().setDuration(TDuration(TDuration::DurationType::V_QUARTER));QTest::mouseClick(input,Qt::LeftButton);
            auto estimated=score->findCR(Fraction(),0);QVERIFY(estimated&&estimated->isChord());QCOMPARE(estimated->durationType().type(),TDuration::DurationType::V_QUARTER);QCOMPARE(estimated->durationType().dots(),1);QCOMPARE(score->inputState().duration().dots(),0);durationChoice->setChecked(false);
            QVERIFY(separate->grab().save(out.filePath("spectrum-marks-066.png")));
            editor->findChild<QAction*>("editorArea/restore/transcriptionSpectrumPane")->trigger();
            auto clips=pane->findChild<QComboBox*>("transcriptionClips");QVERIFY(clips);
            auto updated=tracks.front().toMap();auto firstClip=updated.value("associations").toList().front().toMap();firstClip["id"]="clip-one";auto secondClip=firstClip;secondClip["id"]="clip-two";secondClip["name"]="第二个片段";secondClip["from"]=500.;secondClip["until"]=1500.;updated["associations"]=QVariantList{firstClip,secondClip};QVariantList changed{updated};
            QMetaObject::invokeMethod(media,"setTracks",Q_RETURN_ARG(QVariantMap,response),Q_ARG(QVariantList,changed));clips->setCurrentIndex(2);updated["volume"]=.5;changed=QVariantList{updated};QMetaObject::invokeMethod(media,"setTracks",Q_RETURN_ARG(QVariantMap,response),Q_ARG(QVariantList,changed));QCOMPARE(clips->currentData().toMap().value("id").toString(),QString("clip-two"));
            clips->setCurrentIndex(0);updated["volume"]=.6;changed=QVariantList{updated};QMetaObject::invokeMethod(media,"setTracks",Q_RETURN_ARG(QVariantMap,response),Q_ARG(QVariantList,changed));QCOMPARE(clips->currentIndex(),0);
            tabs->setCurrentIndex(0);QVERIFY(!pane->isVisible());main->currentScoreView()->cmd("escape");
            }
      static int gutterForTest(){return 76;}


      void nativeAiConnectionAndTransport() {
            const auto done=qEnvironmentVariable("P2_NATIVE_AI_DONE");
            if(done.isEmpty())QSKIP("Run P2/test_native_ai.py for packaged MCP and native connection");
            const auto state=qEnvironmentVariable("MCP_MUSESCORE_STATE_DIR");QVERIFY(!state.isEmpty());
            auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");
            QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
            initializeBuiltinAI();
            QTRY_VERIFY_WITH_TIMEOUT(!QDir(state).entryList({"instance-*.json"},QDir::Files).isEmpty(),10000);
            showAIConnection();auto box=main->findChild<QDialog*>("kumoAIConnection");QVERIFY(box&&box->isVisible());
            auto status=box->findChild<QLabel*>("aiConnectionStatus");QVERIFY(status);
            auto configure=box->findChild<QPushButton*>("aiConfigureCodex");QVERIFY(configure);
            QTest::mouseClick(configure,Qt::LeftButton);
            const auto config=QDir(qEnvironmentVariable("CODEX_HOME")).filePath("config.toml");
            QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(config),20000);
            auto check=box->findChild<QPushButton*>("aiCheckConnection");QVERIFY(check);
            QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QString::fromUtf8("已配置")),20000);
            QTest::mouseClick(check,Qt::LeftButton);
            QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QString::fromUtf8("连接成功")),20000);
            QDir output(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));
            QVERIFY(box->grab().save(output.filePath("ai-connection-063.png")));
            showPersonalGuide("docs/WORKSPACE_063_GUIDE.md");QTest::qWait(150);
            for(auto candidate:main->findChildren<QDialog*>())if(candidate->windowTitle()==QString::fromUtf8("个人功能说明")){QVERIFY(candidate->grab().save(output.filePath("ai-guide-063.png")));candidate->hide();}
            QFile ready(done+".ready");QVERIFY(ready.open(QIODevice::WriteOnly));ready.write("ready");ready.close();
            QElapsedTimer clock;clock.start();while(!QFile::exists(done)&&clock.elapsed()<60000)QTest::qWait(50);
            QVERIFY2(QFile::exists(done),"packaged MCP did not complete");box->hide();shutdownBuiltinAI();
      }
      void arrangementMcpTransport(){
            const auto bridge=qEnvironmentVariable("P2_CONTEXT_BRIDGE");const auto done=qEnvironmentVariable("P2_CONTEXT_DONE");
            if(bridge.isEmpty()||done.isEmpty())QSKIP("Run P2/test_context_host.py for the real MCP connection");
            auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
            QmlPluginEngine engine;QQmlComponent component(&engine,QUrl::fromLocalFile(bridge));QVERIFY2(!component.isError(),qUtf8Printable(component.errorString()));
            std::unique_ptr<QmlPlugin> root(qobject_cast<QmlPlugin*>(component.create()));QVERIFY2(root,qUtf8Printable(component.errorString()));root->runPlugin();
            QElapsedTimer clock;clock.start();bool reloaded=false;
            const auto restart=qEnvironmentVariable("P2_CONTEXT_RELOAD");
            while(!QFile::exists(done)&&clock.elapsed()<30000){
                if(!reloaded&&!restart.isEmpty()&&QFile::exists(restart)){
                    root.reset();root.reset(qobject_cast<QmlPlugin*>(component.create()));QVERIFY(root);root->runPlugin();reloaded=true;QFile ack(restart+".ready");QVERIFY(ack.open(QIODevice::WriteOnly));ack.write("ready");ack.close();
                }
                QTest::qWait(25);
            }
            QFile file(done);QVERIFY2(file.open(QIODevice::ReadOnly),"MCP client did not finish");auto report=QJsonDocument::fromJson(file.readAll()).toVariant().toMap();QCOMPARE(report.value("status").toString(),QString("passed"));showSuggestionPanel(false);
            root.reset();

      }

      void largeArrangementReadAndPreparation(){
            auto path=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("arrangement-4000-062.mscx");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));
            QByteArray xml("<?xml version=\"1.0\"?><museScore version=\"3.02\"><Score><Division>480</Division><Part><Staff id=\"1\"><StaffType group=\"pitched\"><name>stdNormal</name></StaffType></Staff><trackName>Piano</trackName><Instrument id=\"piano\"><instrumentId>keyboard.piano</instrumentId><Channel/></Instrument></Part><Staff id=\"1\">");
            for(int bar=0;bar<1000;++bar){xml+="<Measure><voice>";if(!bar)xml+="<TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>";for(int i=0;i<4;++i)xml+="<Chord><durationType>quarter</durationType><Note><pitch>60</pitch><tpc>14</tpc></Note></Chord>";xml+="</voice></Measure>";}xml+="</Staff></Score></museScore>";file.write(xml);file.close();
            auto main=Ms::mscore;auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);auto state=observer.arrangementState({{"whole",true}});int cursor=0,count=0,pages=0;double maximum=0,first=0;QElapsedTimer timer;timer.start();QVariantMap note;
            do{auto page=observer.arrangementRange({{"snapshotId",state["snapshotId"]},{"scope",state["scope"]},{"fromTick",cursor},{"limit",64}});QVERIFY2(!page.contains("error"),qUtf8Printable(page["error"].toString()));double ms=page["milliseconds"].toDouble();if(!pages)first=ms;maximum=qMax(maximum,ms);++pages;count+=page["notes"].toList().size();if(note.isEmpty()&&!page["notes"].toList().isEmpty())note=page["notes"].toList()[0].toMap();if(page["done"].toBool())break;QVERIFY(page["nextTick"].toInt()>cursor);cursor=page["nextTick"].toInt();}while(true);
            QCOMPARE(count,4000);QVERIFY(pages>1);const double readMs=timer.nsecsElapsed()/1000000.;auto local=observer.arrangementState({{"firstMeasure",1},{"lastMeasure",1}});const int undo=score->undoStack()->getCurIdx();
            auto result=observer.arrangementRequest("submitArrangementOptions",{{"snapshotId",state["snapshotId"]},{"showPanel",false},{"options",QVariantList{QVariantMap{{"title","大谱检查"},{"scope",local["scope"]},{"changes",QVariantList{QVariantMap{{"ref",note["ref"]},{"velocity",90}}}}}}}});QVERIFY2(!result.contains("error"),qUtf8Printable(result["error"].toString()));QCOMPARE(score->undoStack()->getCurIdx(),undo);
            QVariantMap report{{"notes",count},{"pages",pages},{"readMs",readMs},{"firstGuiBatchMs",first},{"maximumGuiBatchMs",maximum},{"preparation",result["options"].toList()[0].toMap()["preparationMilliseconds"]}};
            QFile out(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("arrangement-performance-062.json"));QVERIFY(out.open(QIODevice::WriteOnly));out.write(QJsonDocument::fromVariant(report).toJson());
      }

      void rhythmRecordDragApplyAndRestore(){
            auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/pluginhost/rhythm.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->resize(1300,1000);main->show();score->deselectAll();
            auto first=toChord(score->firstSegment(SegmentType::ChordRest)->element(0));auto last=toChord(score->tick2segment(Fraction::fromTicks(1440),false,SegmentType::ChordRest)->element(0));
            score->startCmd();last->undoChangeProperty(Pid::STAFF_MOVE,1);first->notes()[0]->undoChangeProperty(Pid::TUNING,15.);auto slur=new Slur(score);slur->setTrack(0);slur->setTrack2(0);slur->setTick(Fraction());slur->setTick2(Fraction::fromTicks(1440));slur->setStartElement(first);slur->setEndElement(last);score->undoAddElement(slur);score->endCmd();score->doLayout();
            main->showPerformanceEditor(true);auto editor=main->performanceEditor();auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs");QVERIFY(tabs&&tabs->count()>=4);tabs->setCurrentIndex(3);
            auto pane=editor->findChild<RhythmPane*>();QVERIFY(pane&&pane->isVisible());auto table=pane->findChild<QTableWidget*>("rhythmDraftTable");QVERIFY(table);auto label=pane->findChild<QLabel*>("rhythmStatus");QVERIFY(label);
            QTRY_COMPARE_WITH_TIMEOUT(table->rowCount(),4,10000);auto capture=pane->findChild<MidiCaptureSession*>();QVERIFY(capture);QSignalSpy finished(capture,&MidiCaptureSession::finished);
            const int before=score->undoStack()->getCurIdx();QVERIFY(!capture->begin({{"monitor",false},{"endMode","manual"}},false).contains("error"));
            for(int i=0;i<4;++i){QVERIFY(captureMidiInput(NPlayEvent(ME_NOTEON,0,30,90+i*5),midiInputTime()));QTest::qWait(30);QVERIFY(captureMidiInput(NPlayEvent(ME_NOTEOFF,0,30,0),midiInputTime()));if(i<3)QTest::qWait(95);}
            capture->stop();QTRY_COMPARE_WITH_TIMEOUT(finished.size(),1,5000);QTRY_VERIFY_WITH_TIMEOUT(label->text().contains("4次敲击"),10000);QCOMPARE(table->rowCount(),4);QCOMPARE(table->item(0,4)->text(),QString("90"));QVERIFY(!table->item(1,5)->text().contains("漏敲"));
            auto canvas=pane->findChild<QWidget*>("rhythmDraftCanvas");QVERIFY(canvas);QTest::qWait(50);
            QVERIFY(main->height()<=1000);
            qDebug()<<"Rhythm canvas"<<canvas->size()<<"Window"<<main->size()<<"Minimum"<<main->minimumSize()<<main->minimumSizeHint()<<"Screen"<<QGuiApplication::primaryScreen()->availableGeometry();for(int i=0;i<table->rowCount();++i)qDebug()<<"Draft row"<<i<<table->item(i,1)->text()<<table->item(i,2)->text()<<table->item(i,3)->text();
            main->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("rhythm-before-drag-062.png"));
            double step=qMax(2.,double(canvas->height()-30)/10.);int x=52+qRound(360./1920*(canvas->width()-64))+10,y=qRound(26+2*step+qMax(4.,step-1)/2);
            QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,QPoint(x,y));QTest::mouseMove(canvas->window()->windowHandle(),canvas->mapTo(canvas->window(),QPoint(x+qRound(120./1920*(canvas->width()-64)),y)),60);QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,QPoint(x+qRound(120./1920*(canvas->width()-64)),y));
            QTRY_VERIFY_WITH_TIMEOUT(table->item(3,2)->text().toDouble()>=1.99,5000);
            QVERIFY(table->visualItemRect(table->item(0,0)).top()>=0);
            main->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("rhythm-writing-062.png"));
            auto apply=pane->findChild<QPushButton*>("rhythmApply");QTRY_VERIFY(apply->isEnabled());QTest::mouseClick(apply,Qt::LeftButton);QVERIFY2(label->text().contains("已采用"),qUtf8Printable(label->text()));QCOMPARE(score->undoStack()->getCurIdx(),before+1);
            QVector<int> pitches;for(auto segment=score->firstSegment(SegmentType::ChordRest);segment;segment=segment->next1(SegmentType::ChordRest)){auto e=segment->element(0);if(e&&e->isChord())for(auto n:toChord(e)->notes())if(!n->tieBack())pitches.append(n->pitch());}QCOMPARE(pitches,QVector<int>({60,62,64,65}));
            auto head=toChord(score->firstSegment(SegmentType::ChordRest)->element(0))->notes()[0];QVERIFY(!head->el().empty());QCOMPARE(head->veloOffset(),90);QCOMPARE(head->tuning(),15.);QCOMPARE(toChord(score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest)->element(0))->staffMove(),1);QCOMPARE(VelocityBackup::original(head),(QPair<int,int>(int(Note::ValueType::OFFSET_VAL),0)));bool moved=false;for(auto row:score->spannerMap().map())if(row.second->isSlur()){QCOMPARE(row.second->tick2().ticks(),480);moved=true;}QVERIFY(moved);
            PluginAPI::ScoreObserver info;PluginAPI::Score wrapped(score);info.setScore(&wrapped);auto saved=info.workflowData("arrangement");QCOMPARE(saved["materials"].toList().size(),1);QCOMPARE(saved["materials"].toList()[0].toMap()["notes"].toList().size(),4);
            auto output=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("rhythm-applied-062.mscz");QFileInfo fileInfo(output);QVERIFY(score->saveCompressedFile(fileInfo,false,false));std::unique_ptr<MasterScore> reopened(main->readScore(output));QVERIFY(reopened);QCOMPARE(reopened->metaTag("kumo.arrangement"),score->metaTag("kumo.arrangement"));QCOMPARE(VelocityBackup::original(toChord(reopened->firstSegment(SegmentType::ChordRest)->element(0))->notes()[0]),(QPair<int,int>(int(Note::ValueType::OFFSET_VAL),0)));
            EditData edit;score->undoRedo(true,&edit);QCOMPARE(score->undoStack()->getCurIdx(),before);QCOMPARE(toChord(score->firstSegment(SegmentType::ChordRest)->element(0))->actualTicks().ticks(),480);tabs->setCurrentIndex(0);
      }
      void rhythmTripletsKeepLogicalNotesAndCanBeRewritten(){
            auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/pluginhost/rhythm.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
            auto q=[](int n,int d=1){return QVariantMap{{"numerator",n},{"denominator",d}};};
            auto tone=[](int p,int tpc,int old){return QVariantList{QVariantMap{{"pitch",p},{"tpc1",tpc},{"tpc2",tpc},{"velocity",99},{"sourceId",QString("%1:0:0").arg(old)}}};};
            QVariantList events{QVariantMap{{"quarterDuration",q(1,3)},{"pitches",tone(60,14,0)}},QVariantMap{{"quarterDuration",q(1,3)},{"pitches",tone(62,16,480)}},QVariantMap{{"quarterDuration",q(1,3)},{"pitches",tone(64,18,960)},{"tieNext",true}},QVariantMap{{"quarterDuration",q(1)},{"pitches",tone(64,18,960)}},QVariantMap{{"quarterDuration",q(2)},{"pitches",tone(65,13,1440)}}};
            auto state=observer.arrangementState({{"whole",true}});auto scope=state["scope"].toMap();
            QVariantMap voice{{"track",0},{"events",events},{"tuplets",QVariantList{QVariantMap{{"quarterTime",q(0)},{"quarterDuration",q(1)},{"actual",3},{"normal",2}}}}};
            auto submit=[&](const QVariantMap& v){return observer.arrangementRequest("submitArrangementOptions",{{"snapshotId",observer.analysisContentRevision()},{"options",QVariantList{QVariantMap{{"title","节奏检查"},{"scope",scope},{"voices",QVariantList{v}}}}},{"showPanel",false},{"taskBrief",QVariantMap{{"preserveTracks",QVariantList{}}}}});};
            auto result=submit(voice);QVERIFY2(!result.contains("error"),qUtf8Printable(result["error"].toString()));auto id=result["options"].toList()[0].toMap()["id"];auto midi=observer.arrangementRequest("arrangementOption",{{"optionId",id},{"action","performance"}});QCOMPARE(midi["proposed"].toList().size(),4);for(auto value:midi["proposed"].toList())QCOMPARE(value.toMap()["velocity"].toInt(),99);
            auto applied=observer.arrangementRequest("arrangementOption",{{"optionId",id},{"action","apply"}});QVERIFY2(!applied.contains("error"),qUtf8Printable(applied["error"].toString()));auto e=toChord(score->tick2segment(Fraction::fromTicks(320),false,SegmentType::ChordRest)->element(0))->notes()[0];QVERIFY(e->tieFor());QCOMPARE(e->tieFor()->endNote()->chord()->tick().ticks(),480);
            QVariantList regular;int ps[]={60,62,64,65},ts[]={14,16,18,13};for(int i=0;i<4;++i)regular.append(QVariantMap{{"quarterDuration",q(1)},{"pitches",QVariantList{QVariantMap{{"pitch",ps[i]},{"tpc1",ts[i]},{"tpc2",ts[i]}}}}});
            result=submit({{"track",0},{"events",regular}});QVERIFY2(!result.contains("error"),qUtf8Printable(result["error"].toString()));id=result["options"].toList()[0].toMap()["id"];applied=observer.arrangementRequest("arrangementOption",{{"optionId",id},{"action","apply"}});QVERIFY2(!applied.contains("error"),qUtf8Printable(applied["error"].toString()));QVERIFY(!toChord(score->firstSegment(SegmentType::ChordRest)->element(0))->tuplet());
            showSuggestionPanel(false);
      }

      void traditionalSuggestionsUseNativeWorker(){
            auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->resize(1280,960);main->show();score->deselectAll();
            showSuggestionPanel(true);auto panel=main->findChild<QWidget*>("arrangementSuggestionPanel");QVERIFY(panel);auto task=panel->findChild<LocalSuggestionTask*>();QVERIFY(task);
            const int before=score->undoStack()->getCurIdx();QSignalSpy completed(task,&LocalSuggestionTask::completed);QSignalSpy progress(task,&LocalSuggestionTask::progress);
            panel->findChild<QComboBox*>("localSuggestionKind")->setCurrentIndex(0);QTest::mouseClick(panel->findChild<QPushButton*>("localSuggestionStart"),Qt::LeftButton);QTRY_VERIFY_WITH_TIMEOUT(!task->running(),15000);
            QVERIFY2(completed.size()==1,qUtf8Printable(progress.isEmpty()?QString("No progress"):progress.last()[0].toString()));
            auto result=completed.front()[0].toMap();QVERIFY(!result["options"].toList().isEmpty());QCOMPARE(score->undoStack()->getCurIdx(),before);
            auto apply=panel->findChild<QPushButton*>("arrangement-apply");QVERIFY(apply&&apply->isEnabled());
            QVERIFY(panel->findChild<QPushButton*>("localSuggestionStart")->isEnabled());
            QTRY_VERIFY(main->currentScoreView()->findChild<QWidget*>("regionalDifferenceLayer")->isVisible());
            main->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("traditional-suggestions-061.png"));
            QTest::mouseClick(apply,Qt::LeftButton);QTRY_COMPARE(score->undoStack()->getCurIdx(),before+1);int fingers=0;
            for(auto segment=score->firstSegment(SegmentType::ChordRest);segment;segment=segment->next1(SegmentType::ChordRest))for(int track=0;track<score->ntracks();++track){auto e=segment->element(track);if(e&&e->isChord())for(auto n:toChord(e)->notes())for(auto mark:n->el())fingers+=mark->isFingering();}
            QVERIFY(fingers>0);EditData data;score->undoRedo(true,&data);QCOMPARE(score->undoStack()->getCurIdx(),before);showSuggestionPanel(false);
            completed.clear();task->start(score,"fingering",{});task->cancel();QTest::qWait(70);QCOMPARE(completed.size(),0);
      }
      void arrangementReadIncludesHeldEntryNotes(){
            auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
            PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
            auto state=observer.arrangementState({{"scope",QVariantMap{{"start",480},{"end",960},{"firstTrack",0},{"endTrack",8}}}});
            auto page=observer.arrangementRange({{"snapshotId",state["snapshotId"]},{"scope",state["scope"]},{"fromTick",480}});QVERIFY(!page.contains("error"));
            bool held=false;for(auto value:page["notes"].toList()){auto n=value.toMap();if(n["contextOnly"].toBool()){held=true;QVERIFY(n["tick"].toInt()<480);QVERIFY(n["logicalEnd"].toInt()>480);}}
            QVERIFY(held);
      }

      void arrangementAiWorkflow(){
            auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->resize(1280,960);main->show();
            PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
            auto n=toChord(score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest)->element(0))->notes()[0];
            score->select(n,SelectType::SINGLE);const int before=score->undoStack()->getCurIdx();
            const auto state=observer.arrangementState();QVERIFY(!state.contains("error"));const auto selected=state.value("scope").toMap();QCOMPARE(selected.value("start").toInt(),480);QCOMPARE(selected.value("end").toInt(),960);
            auto all=observer.arrangementState({{"whole",true}});const auto snapshot=all.value("snapshotId").toString();QVERIFY(!snapshot.isEmpty());
            auto page=observer.arrangementRange({{"snapshotId",snapshot},{"scope",all.value("scope")},{"fromTick",0},{"limit",1}});QVERIFY2(!page.contains("error"),qUtf8Printable(page.value("error").toString()));QCOMPARE(page.value("notes").toList().size(),3);QCOMPARE(page.value("nextTick").toInt(),480);
            auto note=page.value("notes").toList()[0].toMap();QCOMPARE(note.value("quarterDuration").toMap().value("numerator").toInt(),2);QVERIFY(note.contains("tpc2"));QVERIFY(note.contains("velocityBase"));QCOMPARE(score->undoStack()->getCurIdx(),before);QCOMPARE(score->selection().element(),static_cast<Element*>(n));
            observer.publishHarmonyAnalysis({{"snapshotId",snapshot},{"regions",QVariantList{QVariantMap{{"start",0},{"end",1920},{"chord","C"}}}}});QCOMPARE(observer.arrangementState().value("harmonyAnalysis").toMap().value("freshness").toString(),QString("current"));
            const QVariantMap option{{"title","力度与指法"},{"summary","左手低音更轻，标出小指。"},{"scope",all.value("scope")},{"changes",QVariantList{QVariantMap{{"ref",note.value("ref")},{"velocity",53},{"finger",5}}}}};
            auto submit=observer.arrangementRequest("submitArrangementOptions",{{"snapshotId",snapshot},{"options",QVariantList{option}},{"operationId","submit-ui-1"}});QVERIFY2(!submit.contains("error"),qUtf8Printable(submit.value("error").toString()));QCOMPARE(score->undoStack()->getCurIdx(),before);QCOMPARE(submit.value("options").toList().size(),1);
            const auto id=submit.value("options").toList()[0].toMap().value("id");
            const auto rendered=observer.arrangementRequest("arrangementOption",{{"optionId",id},{"action","performance"}});QVERIFY(!rendered.value("original").toList().isEmpty());bool heardBass=false;for(auto v:rendered.value("proposed").toList()){auto e=v.toMap();if(e.value("pitch").toInt()==36){QCOMPARE(e.value("velocity").toInt(),53);heardBass=true;}}QVERIFY(heardBass);

            auto repeated=observer.arrangementRequest("submitArrangementOptions",{{"snapshotId",snapshot},{"options",QVariantList{option}},{"operationId","submit-ui-1"}});QCOMPARE(repeated,submit);
            auto panel=main->findChild<QWidget*>("arrangementSuggestionPanel");QVERIFY(panel);QVERIFY(panel->isVisible());
            auto choices=panel->findChild<QTreeWidget*>("arrangementChoices");QVERIFY(choices);QCOMPARE(choices->topLevelItemCount(),1);
            auto preview=panel->findChild<QPushButton*>("arrangement-preview");QVERIFY(preview);QTRY_VERIFY(main->currentScoreView()->findChild<QWidget*>("regionalDifferenceLayer")->isVisible());QTest::mouseClick(preview,Qt::LeftButton);QVERIFY(!main->currentScoreView()->findChild<QWidget*>("regionalDifferenceLayer")->isVisible());QTest::mouseClick(preview,Qt::LeftButton);QVERIFY(main->currentScoreView()->findChild<QWidget*>("regionalDifferenceLayer")->isVisible());
            auto image=main->grab();image.save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("arrangement-options-060.png"));
            auto apply=panel->findChild<QPushButton*>("arrangement-apply");QVERIFY(apply);QTest::mouseClick(apply,Qt::LeftButton);
            auto bass=toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(4))->notes()[0];QCOMPARE(bass->veloOffset(),53);QCOMPARE(bass->veloType(),Note::ValueType::USER_VAL);QCOMPARE(score->undoStack()->getCurIdx(),before+1);QVERIFY(!bass->el().empty());QVERIFY(!observer.workflowData("arrangement").value("adoptions").toList().isEmpty());
            const auto path=_settings.filePath("ai-applied.mscz");QFileInfo info(path);QVERIFY(score->saveCompressedFile(info,false,false));std::unique_ptr<MasterScore> restored(main->readScore(path));QVERIFY(restored);QCOMPARE(restored->metaTag("kumo.arrangement"),score->metaTag("kumo.arrangement"));
            const auto artifact=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS");if(!artifact.isEmpty()){QFile out(QDir(artifact).filePath("ai-applied-060.mscz"));if(out.open(QIODevice::WriteOnly)){QFile input(path);QVERIFY(input.open(QIODevice::ReadOnly));out.write(input.readAll());}}
            EditData edit;score->undoRedo(true,&edit);QCOMPARE(bass->veloType(),Note::ValueType::OFFSET_VAL);QCOMPARE(bass->veloOffset(),0);QVERIFY(bass->el().empty());QVERIFY(observer.workflowData("arrangement").isEmpty());
            QVERIFY(observer.arrangementRange({{"snapshotId",snapshot},{"scope",all.value("scope")}}).contains("error"));
            auto status=observer.arrangementRequest("arrangementOperationStatus",{{"operationId","submit-ui-1"}});QCOMPARE(status,submit);
            showSuggestionPanel(false);main->currentScoreView()->cmd("escape");
      }
      void arrangementVoiceReplacement(){
            auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
            score->startCmd();score->insertMeasure(ElementType::VBOX,score->first());score->endCmd();
            score->startCmd();auto linked=new Score(score);auto excerpt=new Excerpt(score);excerpt->setPartScore(linked);excerpt->setParts({score->parts().front()});excerpt->setTitle("Piano part");Excerpt::createExcerpt(excerpt);score->excerpts().append(excerpt);score->endCmd();
            const QVariantMap scope{{"start",0},{"end",1920},{"firstTrack",4},{"endTrack",5}};
            const auto state=observer.arrangementState({{"scope",scope}});auto snapshot=state.value("snapshotId");
            auto pitch=[](int p,int tpc){return QVariantMap{{"pitch",p},{"tpc1",tpc},{"tpc2",tpc}};};
            const QVariantList events{QVariantMap{{"quarterDuration",QVariantMap{{"numerator",1},{"denominator",1}}},{"pitches",QVariantList{pitch(36,14),pitch(55,15)}}},QVariantMap{{"quarterDuration",QVariantMap{{"numerator",1},{"denominator",1}}},{"pitches",QVariantList{pitch(52,18),pitch(55,15)}}},QVariantMap{{"quarterDuration",QVariantMap{{"numerator",2},{"denominator",1}}},{"pitches",QVariantList{}}}};
            const QVariantMap option{{"title","左手拆成两组"},{"summary","先低音和五度，再接内声。"},{"scope",scope},{"voices",QVariantList{QVariantMap{{"track",4},{"events",events}}}}};
            auto submit=observer.arrangementRequest("submitArrangementOptions",{{"snapshotId",snapshot},{"options",QVariantList{option}},{"operationId","voice-submit"}});QVERIFY2(!submit.contains("error"),qUtf8Printable(submit.value("error").toString()));
            auto id=submit.value("options").toList()[0].toMap().value("id");int before=score->undoStack()->getCurIdx();
            auto used=observer.arrangementRequest("arrangementOption",{{"optionId",id},{"action","apply"},{"operationId","voice-apply"}});QVERIFY2(!used.contains("error"),qUtf8Printable(used.value("error").toString()));QCOMPARE(score->undoStack()->getCurIdx(),before+1);
            auto first=toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(4));QCOMPARE(first->actualTicks().ticks(),480);QCOMPARE(first->notes().size(),size_t(2));
            auto next=toChord(score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest)->element(4));QCOMPARE(next->notes()[0]->pitch(),52);auto partChord=toChord(linked->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest)->element(4));QCOMPARE(partChord->notes()[0]->pitch(),52);QCOMPARE(partChord->actualTicks().ticks(),480);QVERIFY(next->notes()[0]->links());
            auto same=observer.arrangementRequest("arrangementOption",{{"optionId",id},{"action","apply"},{"operationId","voice-apply"}});QCOMPARE(same,used);QCOMPARE(score->undoStack()->getCurIdx(),before+1);
            EditData edit;score->undoRedo(true,&edit);first=toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(4));QCOMPARE(first->actualTicks().ticks(),960);QCOMPARE(first->notes().size(),size_t(3));QCOMPARE(toChord(linked->firstMeasure()->first(SegmentType::ChordRest)->element(4))->actualTicks().ticks(),960);
            showSuggestionPanel(false);
      }


      void arrangementTripletsAndVariant(){
            auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
            const QVariantMap scope{{"start",0},{"end",1920},{"firstTrack",4},{"endTrack",5}};
            const auto snapshot=observer.analysisContentRevision();QVariantList events;
            for(auto p:QList<QPair<int,int>>{{36,14},{52,18},{55,15}})events.append(QVariantMap{{"quarterDuration",QVariantMap{{"numerator",1},{"denominator",3}}},{"pitches",QVariantList{QVariantMap{{"pitch",p.first},{"tpc1",p.second},{"tpc2",p.second}}}}});
            events.append(QVariantMap{{"quarterDuration",QVariantMap{{"numerator",3},{"denominator",1}}},{"pitches",QVariantList{}}});
            const QVariantMap tuplet{{"quarterTime",QVariantMap{{"numerator",0},{"denominator",1}}},{"quarterDuration",QVariantMap{{"numerator",1},{"denominator",1}}},{"actual",3},{"normal",2}};
            const QVariantMap option{{"title","三连音分解"},{"summary","把第一拍分成三个音。"},{"scope",scope},{"voices",QVariantList{QVariantMap{{"track",4},{"events",events},{"tuplets",QVariantList{tuplet}}}}}};
            auto submitted=observer.arrangementRequest("submitArrangementOptions",{{"snapshotId",snapshot},{"options",QVariantList{option}},{"operationId","triplet-submit"}});QVERIFY2(!submitted.contains("error"),qUtf8Printable(submitted.value("error").toString()));auto id=submitted.value("options").toList()[0].toMap().value("id");
            int before=score->undoStack()->getCurIdx();auto stored=observer.arrangementRequest("arrangementOption",{{"optionId",id},{"action","variant"},{"operationId","triplet-variant"}});QVERIFY2(!stored.contains("error"),qUtf8Printable(stored.value("error").toString()));QCOMPARE(score->undoStack()->getCurIdx(),before+1);QCOMPARE(RegionalVariants::state(score).value("groups").toList().size(),1);QCOMPARE(toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(4))->notes().size(),size_t(3));
            auto applied=observer.arrangementRequest("arrangementOption",{{"optionId",id},{"action","apply"},{"operationId","triplet-apply"}});QVERIFY2(!applied.contains("error"),qUtf8Printable(applied.value("error").toString()));QCOMPARE(score->undoStack()->getCurIdx(),before+2);
            for(int at:{0,160,320}){auto e=score->tick2segment(Fraction::fromTicks(at),false,SegmentType::ChordRest)->element(4);QVERIFY(e&&e->isChord());auto chord=toChord(e);QVERIFY(chord->tuplet());QCOMPARE(chord->actualTicks().ticks(),160);}
            EditData edit;score->undoRedo(true,&edit);QCOMPARE(toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(4))->notes().size(),size_t(3));score->undoRedo(true,&edit);QVERIFY(RegionalVariants::state(score).value("groups").toList().isEmpty());showSuggestionPanel(false);
      }
      void arrangementKeepsBoundaryConnections(){
            auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/p1native/regionalconnection.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
            auto chordAt=[&](int tick){return toChord(score->tick2segment(Fraction::fromTicks(tick),false,SegmentType::ChordRest)->element(0));};auto a=chordAt(0)->notes()[0],b=chordAt(1920)->notes()[0];
            score->startCmd();auto tie=new Tie(score);tie->setStartNote(a);tie->setEndNote(b);tie->setTrack(0);tie->setTick(a->tick());tie->setTick2(b->tick());tie->setParent(a);score->undoAddElement(tie);auto slur=new Slur(score);slur->setStartElement(chordAt(0));slur->setEndElement(chordAt(3840));slur->setTick(Fraction(0,1));slur->setTick2(Fraction(2,1));slur->setTrack(0);slur->setTrack2(0);score->undoAddElement(slur);score->endCmd();
            PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);const QVariantMap scope{{"start",0},{"end",1920},{"firstTrack",0},{"endTrack",1}};auto snapshot=observer.analysisContentRevision();
            auto page=observer.arrangementRange({{"scope",scope},{"snapshotId",snapshot}});QVERIFY(!page.value("boundaryRelations").toList().isEmpty());
            const QVariantMap newNote{{"pitch",60},{"tpc1",14},{"tpc2",14},{"velocity",53}};
            const QVariantMap event{{"quarterDuration",QVariantMap{{"numerator",4},{"denominator",1}}},{"pitches",QVariantList{newNote}}};
            const QVariantMap voice{{"track",0},{"events",QVariantList{event}}};
            const QVariantMap option{{"title","保持延音连接"},{"scope",scope},{"voices",QVariantList{voice}}};
            auto submitted=observer.arrangementRequest("submitArrangementOptions",{{"snapshotId",snapshot},{"options",QVariantList{option}},{"taskBrief",QVariantMap{{"allowMelodyChanges",true}}},{"operationId","links-submit"}});QVERIFY2(!submitted.contains("error"),qUtf8Printable(submitted.value("error").toString()));auto id=submitted.value("options").toList()[0].toMap().value("id");int before=score->undoStack()->getCurIdx();auto applied=observer.arrangementRequest("arrangementOption",{{"optionId",id},{"action","apply"},{"operationId","links-apply"}});QVERIFY2(!applied.contains("error"),qUtf8Printable(applied.value("error").toString()));QCOMPARE(score->undoStack()->getCurIdx(),before+1);auto changed=chordAt(0)->notes()[0];QVERIFY(changed->tieFor());QCOMPARE(changed->tieFor()->endNote(),b);QCOMPARE(b->tieBack()->startNote(),changed);bool linked=false;for(auto pair:score->spannerMap().map())if(pair.second->isSlur())linked|=pair.second->startElement()==chordAt(0)&&pair.second->endElement()==chordAt(3840);QVERIFY(linked);
            EditData edit;score->undoRedo(true,&edit);QCOMPARE(chordAt(0)->notes()[0],a);QCOMPARE(a->tieFor(),tie);QCOMPARE(b->tieBack(),tie);showSuggestionPanel(false);
      }


      void arrangementInformationAndPreflight(){
            auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
            int before=score->undoStack()->getCurIdx();
            auto save=[&](const QVariantMap& data,const QString& id){return observer.arrangementRequest("storeArrangementInfo",{{"snapshotId",observer.analysisContentRevision()},{"information",data},{"operationId",id}});};
            const QVariantMap statement{{"id","role-1"},{"kind","roleAssignment"},{"target",QVariantMap{{"start",0},{"end",1920},{"track",0}}},{"value","旋律"},{"origin",QVariantMap{{"kind","user"}}},{"status","accepted"}};
            auto result=save({{"assertions",QVariantList{statement}}},"info-1");QVERIFY2(result.value("ok").toBool(),qUtf8Printable(result.value("error").toString()));
            const QVariantMap material{{"id","reference-1"},{"type","arrangementNote"},{"text","左手保留低音"},{"origin",QVariantMap{{"kind","user"}}}};
            result=save({{"materials",QVariantList{material}}},"info-2");QVERIFY(result.value("ok").toBool());QCOMPARE(observer.workflowData("arrangement").value("assertions").toList().size(),1);QCOMPARE(observer.workflowData("arrangement").value("materials").toList().size(),1);QCOMPARE(score->undoStack()->getCurIdx(),before+2);
            EditData edit;score->undoRedo(true,&edit);QCOMPARE(observer.workflowData("arrangement").value("assertions").toList().size(),1);QVERIFY(observer.workflowData("arrangement").value("materials").toList().isEmpty());score->undoRedo(true,&edit);QVERIFY(observer.workflowData("arrangement").isEmpty());
            const auto state=observer.arrangementState({{"whole",true}});const auto snapshot=state.value("snapshotId");const QVariantMap noteRef{{"snapshotId",snapshot},{"id","480:0:0"}};
            const QVariantMap forbidden{{"title","受保护旋律"},{"scope",state.value("scope")},{"changes",QVariantList{QVariantMap{{"ref",noteRef},{"pitch",71},{"tpc1",19},{"tpc2",19}}}}};
            auto rejected=observer.arrangementRequest("submitArrangementOptions",{{"snapshotId",snapshot},{"options",QVariantList{forbidden}},{"operationId","protect-melody"}});QVERIFY(rejected.contains("error"));QCOMPARE(score->undoStack()->getCurIdx(),before);
            const QVariantMap valid{{"title","力度"},{"scope",state.value("scope")},{"changes",QVariantList{QVariantMap{{"ref",noteRef},{"velocity",57}}}}};
            const QVariantMap invalid{{"title","错误音高"},{"scope",state.value("scope")},{"changes",QVariantList{QVariantMap{{"ref",noteRef},{"pitch",140},{"tpc1",19},{"tpc2",19}}}}};
            rejected=observer.arrangementRequest("submitArrangementOptions",{{"snapshotId",snapshot},{"options",QVariantList{valid,invalid}},{"operationId","atomic-options"}});QVERIFY(rejected.contains("error"));QCOMPARE(score->undoStack()->getCurIdx(),before);QVERIFY(arrangementSuggestions(score).value("options").toList().isEmpty());showSuggestionPanel(false);
      }

      void nativeWorkflowDocumentAndTypography(){
            auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
            PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
            const QVariantMap data{{"schema",2},{"overrides",QVariantList{QVariantMap{{"start",0},{"end",1920},{"root",0},{"part",0}}}},{"positions",QVariantList{QVariantMap{{"start",0},{"part",0},{"x",1.},{"y",-1.}}}}};
            const int before=score->undoStack()->getCurIdx();auto saved=observer.setWorkflowData("harmony",data);QVERIFY2(saved.value("ok").toBool(),qUtf8Printable(saved.value("error").toString()));QCOMPARE(score->undoStack()->getCurIdx(),before+1);QCOMPARE(observer.workflowData("harmony"),data);
            saved=observer.setWorkflowData("harmony",data);QVERIFY(saved.value("ok").toBool());QVERIFY(!saved.value("changed").toBool());QCOMPARE(score->undoStack()->getCurIdx(),before+1);
            EditData edit;score->undoRedo(true,&edit);QVERIFY(observer.workflowData("harmony").isEmpty());score->undoRedo(false,&edit);QCOMPARE(observer.workflowData("harmony"),data);
            const auto path=_settings.filePath("workflow-data.mscz");QFileInfo info(path);QVERIFY(score->saveCompressedFile(info,false,false));std::unique_ptr<MasterScore> restored(main->readScore(path));QVERIFY(restored);PluginAPI::Score read(restored.get());PluginAPI::ScoreObserver readObserver;readObserver.setScore(&read);QCOMPARE(readObserver.workflowData("harmony"),data);
            QVERIFY(observer.setWorkflowData("unlisted",data).contains("error"));
            const auto normal=observer.harmonyLabel("C/E",QString::fromUtf8("I⁶"),{{"pixels",24},{"renderProbe",1}}).value("source");
            {QScopedValueRollback<qreal> ratio(MScore::pixelRatio,.75);const auto other=observer.harmonyLabel("C/E",QString::fromUtf8("I⁶"),{{"pixels",24},{"renderProbe",2}}).value("source");QCOMPARE(other,normal);}
            for(const auto chord:{"C/E","D/F#","C7/E","F#m7b5/C"}){auto label=observer.harmonyLabel(chord,QString::fromUtf8("I⁶"),{{"pixels",24}});QVERIFY(label.value("baseline").toDouble()>0);QVERIFY(label.value("height").toInt()>10);const auto source=label.value("source").toString();QImage image;QVERIFY(image.loadFromData(QByteArray::fromBase64(source.mid(source.indexOf(',')+1).toLatin1())));image.save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath(QString(chord).replace('/','_').replace('#','s')+"-baseline.png"));}
            }
      void menuMetadataRelayoutAndDocking()
            {
            auto main=Ms::mscore;
            const QString plugin=QString(TESTROOT) + "/share/plugins/HarmonyAssistant/HarmonyAssistant_MS3.qml";
            auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");
            QVERIFY(score);
            main->setCurrentScoreView(main->appendScore(score));
            main->resize(1100,800); main->show();
            QTest::qWait(50);
            // The legacy QML ScoreView performs doLayout while other native viewers exist.
            // This precisely exercises the destructor callback seen in the user's minidump.
            PluginAPI::Score wrapped(score);
            PluginAPI::ScoreView thumbnail;
            thumbnail.setWidth(200); thumbnail.setHeight(160);
            thumbnail.setScore(&wrapped);
            score->doLayout();
            auto menu=main->findChild<QMenu*>("Plugins");
            QVERIFY(menu);
            score->select(toChord(score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest)->element(0))->notes().front());
            for(int i=0;i<4;++i) {menu->popup(main->mapToGlobal(QPoint(200,30)));QTest::qWait(25);menu->hide();}
            PluginDescription description; description.path=plugin;
            QVERIFY(collectPluginMetaInformation(&description));
            main->registerPlugin(&description);
            main->pluginTriggered(plugin);
            QTest::qWait(100);
            QDockWidget* dock=nullptr;
            QmlPlugin* panel=nullptr;
            QQuickView* panelView=nullptr;
            // createWindowContainer reparents the QWindow to a native window, not the dock QObject.
            for(auto window:QGuiApplication::allWindows()) {
                  auto view=qobject_cast<QQuickView*>(window);
                  auto root=view ? qobject_cast<QmlPlugin*>(view->rootObject()) : nullptr;
                  if(root && root->menuPath()=="Plugins.Harmony Assistant") {panel=root;panelView=view;break;}
                  }
            for(auto candidate:main->findChildren<QDockWidget*>())
                  if(candidate->windowTitle()==QString::fromUtf8("和弦助手")) {dock=candidate;break;}
            QVERIFY(dock); QVERIFY(panel);
            QVERIFY(QMetaObject::invokeMethod(panel,"refreshSelection"));
            auto observer=panel->findChild<PluginAPI::ScoreObserver*>();
            QVERIFY(observer);
            observer->setScore(&wrapped);
            // Freeze the plugin's background analysis while testing the observer layer directly.
            panel->setProperty("surfaceActive",false);observer->clearAllPreviews();
            auto descriptors=observer->snapshot(480,0,8).value("notes").toList();
            for(auto& descriptor:descriptors){auto value=descriptor.toMap();value["color"]="#005d5d";value["label"]="3";value["active"]=true;descriptor=value;}
            const auto original=main->currentScoreView()->grab().toImage();
            observer->setScorePreview(descriptors);
            QTest::qWait(50);
            const auto preview=main->currentScoreView()->grab().toImage();
            QVERIFY(original!=preview);
            // Native fixed label hit testing must emit to its owning observer.
            QVariantMap anchor; for(const auto& descriptor:descriptors) {const auto value=descriptor.toMap();if(value.value("tick").toInt()==480 && value.value("track").toInt()==0) {anchor=value;break;}} QVERIFY(!anchor.isEmpty());anchor["chord"]="Cmaj13";anchor["degree"]="Imaj13";
            anchor["chordTick"]=480;anchor["chordUntil"]=960;anchor["chordOrder"]=2;
            observer->setScorePreview({anchor});observer->setActiveScorePreview(600);
            QSignalSpy activation(observer,&PluginAPI::ScoreObserver::previewActivated);
            // Locate the fixed row through a separate generic layer with known hit geometry.
            NotePreviewLayers interaction;QObject interactionOwner;
            NotePreviewEntry hit;hit.chord="C";hit.chordBox=QRectF(10,10,40,20);hit.chordTick=480;hit.activationTarget=observer;
            interaction.replace(&interactionOwner,{{nullptr,hit}});
            QVERIFY(interaction.activate(QPointF(20,20)));QCOMPARE(activation.size(),1);
            QVERIFY(!interaction.activate(QPointF(90,90)));
            auto firstMarker=observer->snapshot(0,4,8).value("notes").toList().front().toMap();
            firstMarker["chord"]="C";firstMarker["degree"]="I";firstMarker["chordTick"]=0;firstMarker["chordUntil"]=480;firstMarker["chordOrder"]=2;
            observer->setScorePreview({firstMarker,anchor});
            const auto note=toChord(score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest)->element(0))->notes().front();
            auto scoreView=main->currentScoreView();QPointF hitPoint;bool found=false;
            for(int dy=-qRound(note->spatium()*24);dy<0 && !found;dy+=2) for(int dx=2;dx<note->spatium()*6;dx+=2) {
                  const auto candidate=note->canvasPos()+QPointF(dx,dy);
                  if(scoreView->activateNotePreview(candidate)){hitPoint=candidate;found=true;break;}
                  }
            QVERIFY(found);
            observer->setScorePreview({firstMarker,anchor});
            const int priorClicks=activation.size();
            const auto physical=scoreView->toPhysical(hitPoint+QPointF(8,8)).toPoint();
            QVERIFY(scoreView->rect().contains(physical));
            QTest::mouseDClick(scoreView,Qt::LeftButton,Qt::NoModifier,physical);
            QTRY_VERIFY(activation.size()>priorClicks);
            const auto bass=toChord(score->firstSegment(SegmentType::ChordRest)->element(4))->notes().front();
            observer->setScorePreview({firstMarker,anchor});
            QPointF bassHit;bool bassFound=false;
            for(int dy=-qRound(bass->spatium()*32);dy<0 && !bassFound;dy+=2) for(int dx=2;dx<40;dx+=2) {
                  const auto candidate=bass->canvasPos()+QPointF(dx,dy);
                  if(scoreView->activateNotePreview(candidate)){bassHit=candidate;bassFound=true;break;}
                  }
            QVERIFY(bassFound);QVERIFY(qAbs(bassHit.y()-hitPoint.y())<=2.0);
            QCOMPARE(panel->property("currentTick").toInt(),0);
            observer->setScorePreview({firstMarker,anchor});observer->setActiveScorePreview(600);QTest::qWait(30);
            scoreView->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("score-fixed-label.png"));
            // The panel jump keeps dock placement; top docks open their full detail tool window.
            QVERIFY(QMetaObject::invokeMethod(panel,"openAnnotation",Q_ARG(QVariant,480),Q_ARG(QVariant,0)));
            auto editor=panel->findChild<QObject*>("harmonyConfiguration");QVERIFY(editor);
            QVERIFY(QMetaObject::invokeMethod(editor,"openColor",Q_ARG(QVariant,QString()),Q_ARG(QVariant,QString("chordColor")),Q_ARG(QVariant,QString("#ffee99"))));
            QTest::qWait(50);
            auto picker=editor->findChild<QObject*>("harmonyColorPicker");QVERIFY(picker);
            picker->setProperty("color",QColor("#224466"));
            QVERIFY(QMetaObject::invokeMethod(picker,"accept"));QTest::qWait(30);
            QCOMPARE(panel->property("configuration").value<QJSValue>().property("chordColor").toString(),QString("#224466"));
            observer->clearAllPreviews();QTest::qWait(30);
            observer->setScorePreview(descriptors);
            score->doLayout(); // includes deletion of System / StaffLines, not just Notes
            observer->setScorePreview(descriptors);
            // A user drop docks a floating window before changing its area.
            // Earlier cases intentionally preserve floating window preferences.
            dock->setFloating(false);
            main->addDockWidget(Qt::TopDockWidgetArea,dock);
            main->resizeDocks({dock},{70},Qt::Vertical);
            QTest::qWait(80);
            QCOMPARE(panel->panelPlacement(),QString("top"));
            main->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("top.png"));
            QVERIFY(panel->property("ribbon").toBool());
            QVERIFY(panel->height()>=32);QVERIFY(panel->height()<90);
            auto compactChord=panel->findChild<QQuickItem*>("harmonyRibbonChord");QVERIFY(compactChord);
            const auto chordRect=compactChord->mapRectToItem(panel,QRectF(0,0,compactChord->width(),compactChord->height()));
            QVERIFY(chordRect.top()>=0);QVERIFY(chordRect.bottom()<=panel->height());
            auto align=panel->findChild<QQuickItem*>("harmonyRibbonAlignment");QVERIFY(align);
            QTest::mouseClick(panelView,Qt::LeftButton,Qt::NoModifier,align->mapToScene(QPointF(align->width()/2,align->height()/2)).toPoint());
            auto popup=panel->findChild<QQuickWindow*>("harmonyRibbonMenuWindow");QVERIFY(popup);QTRY_VERIFY(popup->isVisible());
            std::function<QQuickItem*(QQuickItem*)> findLeft=[&](QQuickItem* item)->QQuickItem*{if(item->objectName()=="harmonyRibbonAlign0")return item;for(auto child:item->childItems())if(auto found=findLeft(child))return found;return nullptr;};
            auto left=findLeft(popup->contentItem());QVERIFY(left);
            QTest::mouseClick(popup,Qt::LeftButton,Qt::NoModifier,left->mapToScene(QPointF(left->width()/2,left->height()/2)).toPoint());
            QTRY_COMPARE(panel->property("configuration").value<QJSValue>().property("ribbonAlign").toInt(),0);QTRY_VERIFY(!popup->isVisible());
            QTRY_VERIFY(panel->detailPanelVisible());
            auto details=main->findChild<QDockWidget*>("kumoPanel/HarmonyAssistant_MS3/details");QVERIFY(details);
            QCOMPARE(main->dockWidgetArea(details),Qt::RightDockWidgetArea);
            QVERIFY(panel->detailPanelHost());
            QCOMPARE(panel->findChildren<PluginAPI::ScoreObserver*>().size(),1);
            QCOMPARE(panel->findChildren<QObject*>("harmonyConfiguration").size(),1);
            auto summary=panel->findChild<QQuickItem*>("harmonySummary");QVERIFY(summary);
            QCOMPARE(summary->window(),panel->detailPanelHost()->window());
            auto detailImage=panel->detailPanelHost()->window()->grabWindow();
            QVERIFY(!detailImage.isNull());
            detailImage.save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("dual-details.png"));
            details->setFloating(true);details->resize(800,680);QTest::qWait(50);
            QVERIFY(details->isFloating());QTRY_VERIFY(panel->property("expanded").toBool());
            details->setFloating(false);main->addDockWidget(Qt::RightDockWidgetArea,details);
            main->resizeDocks({details},{360},Qt::Horizontal);QTest::qWait(30);
            details->close();QTest::qWait(30);QVERIFY(!panel->detailPanelVisible());
            QVERIFY(QMetaObject::invokeMethod(panel,"toggleDetailPanel"));
            QTRY_VERIFY(panel->detailPanelVisible());
            auto ribbonImage=panelView->grabWindow();
            QVERIFY(!ribbonImage.isNull());
            ribbonImage.save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("ribbon.png"));
            panel->setPanelFloating(true);QTest::qWait(50);
            QVERIFY(dock->isFloating());
            dock->resize(800,680);QTest::qWait(50);
            QVERIFY(!panel->property("ribbon").toBool());
            QVERIFY(panel->property("expanded").toBool());
            QTRY_VERIFY(!panel->detailPanelVisible());
            summary=panel->findChild<QQuickItem*>("harmonySummary");QVERIFY(summary);
            QCOMPARE(summary->window(),panelView);
            auto floatingImage=panelView->grabWindow();
            QVERIFY(!floatingImage.isNull());
            floatingImage.save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("floating-panel.png"));
            dock->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("floating.png"));
            panel->setPanelFloating(false);
            main->addDockWidget(Qt::RightDockWidgetArea,dock);QTest::qWait(50);
            QCOMPARE(panel->panelPlacement(),QString("right"));
            dock->close();QTest::qWait(50);
            QTRY_VERIFY(!panel->detailPanelVisible());
            // Repeated open/close must not retain previews or dereference a destroyed dock.
            main->pluginTriggered(plugin);QTest::qWait(50);
            // This fixture deliberately skips main()'s workspace/audio initialization.
            // Exercise plugin destruction; do not invoke unrelated workspace-save code.
            for(auto candidate:main->findChildren<QDockWidget*>())
                  if(candidate->windowTitle()==QString::fromUtf8("和弦助手")) candidate->close();
            QTest::qWait(50);
            releaseHostPanels();main->hide();
            }
      void changeTickWithoutNewOnset()
            {
            auto main=Ms::mscore;
            auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");
            QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
            main->resize(1100,800);main->show();QTest::qWait(30);
            PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
            auto first=observer.snapshot(0,4,8).value("notes").toList().front().toMap();
            first["chord"]="C";first["chordTick"]=0;first["chordUntil"]=240;
            auto second=first;second["chord"]="Am";second["chordTick"]=240;second["chordUntil"]=480;
            observer.setScorePreview({first,second});
            const auto segment=score->firstSegment(SegmentType::ChordRest);
            const auto next=score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest);
            const auto bass=toChord(segment->element(4))->notes().front();
            const qreal start=segment->canvasPos().x(),half=(start+next->canvasPos().x())/2;
            auto view=main->currentScoreView();QSignalSpy activation(&observer,&PluginAPI::ScoreObserver::previewActivated);
            QMap<int,qreal> hits;
            for(int dy=-qRound(bass->spatium()*40);dy<0 && hits.size()<2;dy+=2)
                  for(qreal x=start-2;x<half+40 && hits.size()<2;x+=1) {
                        const auto point=QPointF(x,bass->canvasPos().y()+dy);
                        if(!view->activateNotePreview(point))continue;
                        const int tick=activation.last().at(0).toInt();if(!hits.contains(tick))hits.insert(tick,x);
                        }
            QVERIFY(hits.contains(0));QVERIFY(hits.contains(240));
            QVERIFY(qAbs(hits[0]-start)<=2);QVERIFY(qAbs(hits[240]-half)<=2);
            observer.setActiveScorePreview(300);view->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("no-onset-change.png"));
            observer.clearAllPreviews();main->hide();
            }
      void annotationPrioritiesWhileMoving()
            {
            auto main=Ms::mscore;
            auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");
            QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
            main->resize(1100,800);main->show();QTest::qWait(50);
            auto view=main->currentScoreView();
            PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;
            observer.setScore(&wrapped);
            auto segment=score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest);
            auto harmony=new Harmony(score);harmony->setTrack(0);segment->add(harmony);
            harmony->setHarmony("F7");score->doLayout();QTest::qWait(20);
            QVariantMap anchor;
            for (const auto& descriptor:observer.snapshot(480,0,8).value("notes").toList()) {
                  const auto value=descriptor.toMap();
                  if(value.value("tick").toInt()==480 && value.value("track").toInt()==0){anchor=value;break;}
                  }
            QVERIFY(!anchor.isEmpty());anchor["chord"]="Cmaj13";anchor["degree"]="Imaj13";
            anchor["chordTick"]=480;anchor["chordUntil"]=960;anchor["preferExistingHarmony"]=true;
            const auto baseline=view->grab().toImage();
            auto onlyChord=anchor;onlyChord["degree"]="";
            observer.setScorePreview({onlyChord});
            QCOMPARE(view->grab().toImage(),baseline); // native F7 remains, duplicate analysis stays in panel
            observer.setScorePreview({anchor});const auto preferred=view->grab().toImage();
            auto degreeOnly=anchor;degreeOnly["chord"]="";
            observer.setScorePreview({degreeOnly});QCOMPARE(view->grab().toImage(),preferred);
            QVERIFY(preferred!=baseline);
            auto simultaneous=anchor;simultaneous["preferExistingHarmony"]=false;
            observer.setScorePreview({simultaneous});QVERIFY(view->grab().toImage()!=preferred);
            // Roman notation retains its own degree while optionally supplementing chord analysis.
            observer.clearAllPreviews();harmony->setHarmonyType(HarmonyType::ROMAN);
            harmony->setHarmony("V7");score->doLayout();
            observer.setScorePreview({anchor});const auto romanPreferred=view->grab().toImage();
            onlyChord["preferExistingHarmony"]=false;observer.setScorePreview({onlyChord});
            QCOMPARE(view->grab().toImage(),romanPreferred);
            observer.clearAllPreviews();harmony->setHarmonyType(HarmonyType::STANDARD);
            harmony->setHarmony("F7");score->doLayout();
            observer.setScorePreview({simultaneous});
            const auto note=toChord(segment->element(0))->notes().front();
            QPointF hit;bool found=false;
            for(int dy=-qRound(note->spatium()*24);dy<0 && !found;dy+=2)
                  for(int dx=2;dx<note->spatium()*12;dx+=2) {
                        const auto point=note->canvasPos()+QPointF(dx,dy);
                        if(view->activateNotePreview(point)){hit=point+QPointF(8,8);found=true;break;}
                        }
            QVERIFY(found);
            // Moving selection / changing current function text must not repaint fixed chord text.
            const auto fixedArea=view->toPhysical(QRectF(hit-QPointF(8,8),QSizeF(note->spatium()*9,note->spatium()*2)));
            observer.setActiveScorePreview(600); // white inactive mask over white paper is visually identical
            const auto masked=view->grab(fixedArea).toImage();
            auto transparent=simultaneous;transparent["chordMask"]=false;
            observer.setScorePreview({transparent});
            QVERIFY(view->grab(fixedArea).toImage()!=masked);
            observer.setScorePreview({simultaneous});
            const auto fixedPixels=view->grab(fixedArea).toImage();
            for(int tick:{0,480,0,480}) {
                  auto current=observer.snapshot(tick,0,8).value("notes").toList();
                  for(auto& descriptor:current){auto d=descriptor.toMap();d["label"]=tick==0 ? "5" : "thirteen";descriptor=d;}
                  observer.setNotePreviewColors(current);
                  QCOMPARE(view->grab(fixedArea).toImage(),fixedPixels);
                  }
            observer.clearNotePreviewColors();
            // Move a native editor over an already cached preview: editor and its caret take priority.
            harmony->setOffset(harmony->offset()+hit-harmony->canvasBoundingRect().center());
            view->startEditMode(harmony);QVERIFY(view->textEditMode());
            QTest::keyClick(view,Qt::Key_Right);
            const auto editedWithPreview=view->grab().toImage();
            QVERIFY(!view->activateNotePreview(hit));
            observer.clearAllPreviews();
            QCOMPARE(view->grab().toImage(),editedWithPreview);
            view->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("native-editor-priority.png"));
            view->changeState(ViewState::NORMAL);main->hide();
            }

      void nativeStateColorsAndRangeFrame()
            {
            auto main=Ms::mscore;
            auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");
            QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
            main->resize(1100,800);main->show();QTest::qWait(30);
            auto view=main->currentScoreView();PluginAPI::Score wrapped(score);
            PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
            auto descriptors=observer.snapshot(480,0,4).value("notes").toList();
            auto segment=score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest);
            auto note=toChord(segment->element(0))->notes().front();
            auto d=descriptors.front().toMap();d["color"]="#9f1853";
            for(int state=0;state<3;++state) {
                  note->setSelected(state==0);note->setMark(state==1);note->setDropTarget(state==2);
                  observer.clearAllPreviews();view->update();QTest::qWait(10);
                  const auto baseline=view->grab().toImage();
                  observer.setNotePreviewColors({d});view->update();QTest::qWait(10);
                  QCOMPARE(view->grab().toImage(),baseline);
                  }
            note->setSelected(false);note->setMark(false);note->setDropTarget(false);
            observer.clearAllPreviews();view->update();QTest::qWait(10);
            const auto original=view->grab().toImage();observer.setNotePreviewColors({d});
            QVERIFY(view->grab().toImage()!=original);
            observer.clearAllPreviews();view->update();QTest::qWait(10);
            QCOMPARE(view->grab().toImage(),original);
            // Native P keyboard state and palette are independent of the score preview.
            HPiano keyboard;keyboard.resize(600,180);keyboard.show();QTest::qWait(10);
            score->select(note);keyboard.changeSelection(score->selection());
            for(int mode=0;mode<3;++mode) {
                  keyboard.setPlaybackActive(mode==1);
                  if(mode==1)keyboard.pressPlaybackPitch(note->pitch());
                  if(mode==2)keyboard.pressPitch(note->pitch());
                  observer.clearAllPreviews();const auto keys=keyboard.grab().toImage();
                  observer.setNotePreviewColors({d});QCOMPARE(keyboard.grab().toImage(),keys);
                  observer.clearAllPreviews();QCOMPARE(keyboard.grab().toImage(),keys);
                  keyboard.releasePlaybackPitch(note->pitch());keyboard.releasePitch(note->pitch());
                  }
            keyboard.hide();
            score->selection().setRange(segment,score->lastSegment(),0,1);score->selection().update();
            observer.clearAllPreviews();view->update();QTest::qWait(10);
            const auto selected=view->grab().toImage();observer.setNotePreviewColors({d});
            QCOMPARE(view->grab().toImage(),selected);
            view->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("native-range-color.png"));
            main->hide();
            }
      void currentPersonalBranding()
            {
            QFile version(QString(TESTROOT)+"/personal/VERSION");QVERIFY(version.open(QIODevice::ReadOnly));
            QCOMPARE(QString::fromUtf8(version.readAll()).trimmed(),QString(KUMO_PERSONAL_VERSION));
            AboutBoxDialog about;about.show();QTest::qWait(25);
            const auto label=about.findChild<QLabel*>("versionLabel");QVERIFY(label);
            QVERIFY(label->text().contains(personalBuildLabel()));
            const auto credits=about.findChild<QLabel*>("copyrightLabel");QVERIFY(credits);
            QVERIFY(credits->text().contains(personalReleaseUrl()));
            const QDir artifacts(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));
            about.grab().save(artifacts.filePath("kumo-about-022.png"));about.hide();
            MsSplashScreen splash;splash.show();QTest::qWait(30);
            splash.grab().save(artifacts.filePath("kumo-splash-022.png"));splash.hide();
            }
      void highTrebleAndTempoKeepMarkers()
            {
            auto main=Ms::mscore;
            auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/high-treble.mscx");
            QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
            main->resize(1100,800);main->show();QTest::qWait(30);
            auto view=main->currentScoreView();PluginAPI::Score wrapped(score);
            PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
            auto first=observer.snapshot(0,4,8).value("notes").toList().front().toMap();
            first["chord"]="G";first["degree"]="I";first["chordTick"]=0;first["chordUntil"]=480;
            auto second=first;second["chord"]="D/F#";second["degree"]="V";
            second["chordTick"]=480;second["chordUntil"]=960;
            observer.setScorePreview({first,second});
            QCOMPARE(observer.previewStatus().value("hidden").toInt(),0);
            const auto segment=score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest);
            QVERIFY(segment);const auto page=segment->measure()->system()->page();
            const qreal x=segment->pagePos().x()+page->pos().x()+score->spatium()*.3;
            QSignalSpy activated(&observer,&PluginAPI::ScoreObserver::previewActivated);
            bool found=false;
            for(qreal y=page->pos().y();y<segment->pagePos().y()+page->pos().y();y+=1)
                  if(view->activateNotePreview(QPointF(x,y)) && activated.last().front().toInt()==480)found=true;
            QVERIFY(found);observer.setActiveScorePreview(480);
            view->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("high-treble-tempo.png"));
            main->hide();
            }
      void denseMarkersKeepTickAndNativeFont()
            {
            auto main=Ms::mscore;
            auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");
            QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
            main->resize(1100,800);main->show();QTest::qWait(30);
            auto view=main->currentScoreView();PluginAPI::Score wrapped(score);
            PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
            auto first=observer.snapshot(0,4,8).value("notes").toList().front().toMap();
            first["chord"]="Cmaj7(#11)/E";first["chordTick"]=0;first["chordUntil"]=60;
            auto second=first;second["chord"]="G7(b9)/B";second["chordTick"]=60;second["chordUntil"]=480;
            observer.setScorePreview({first,second});
            QCOMPARE(observer.previewStatus().value("hidden").toInt(),0);
            QSignalSpy activated(&observer,&PluginAPI::ScoreObserver::previewActivated);
            const auto segment=score->firstSegment(SegmentType::ChordRest);
            const auto next=score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest);
            const auto note=toChord(segment->element(4))->notes().front();
            const qreal x0=segment->pagePos().x()+segment->measure()->system()->page()->pos().x();
            const qreal x1=x0+(next->pagePos().x()-segment->pagePos().x())/8;
            QMap<int,qreal> hitYs;
            for(int dy=-qRound(note->spatium()*30);dy<0;dy+=1) {
                  for(const auto anchor:QVector<QPair<int,qreal>>{{0,x0},{60,x1}}) {
                        QPointF point(anchor.second+note->spatium()*.3,note->canvasPos().y()+dy);
                        if(view->activateNotePreview(point) && activated.last().front().toInt()==anchor.first)hitYs.insert(anchor.first,point.y());
                        }
                  }
            QCOMPARE(hitYs.size(),2);QVERIFY(qAbs(hitYs[0]-hitYs[60])>note->spatium());
            observer.setActiveScorePreview(60);
            view->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("dense-native-font.png"));
            const auto normal=view->grab().toImage();first["chordFont"]="Arial";second["chordFont"]="Arial";
            observer.setScorePreview({first,second});QVERIFY(view->grab().toImage()!=normal);
            first["chordScale"]=2.0;first["chord"]=QString(64,QChar('W'));first["degree"]="I";
            observer.setScorePreview({first});
            // Current product keeps markers visible even when their text is wider than a measure.
            QCOMPARE(observer.previewStatus().value("hidden").toInt(),0);
            QVERIFY(observer.previewStatus().value("unplaced").toList().isEmpty());
            first["onTop"]=true;first["avoidScoreContent"]=false;
            observer.setScorePreview({first});QCOMPARE(observer.previewStatus().value("hidden").toInt(),0);
            QVERIFY(observer.previewStatus().value("unplaced").toList().isEmpty());
            const auto topImage=view->grab().toImage();observer.setActiveScorePreview(0);QVERIFY(topImage!=view->grab().toImage());
            score->setPrinting(true);const auto printing=view->grab().toImage();observer.clearAllPreviews();QCOMPARE(view->grab().toImage(),printing);score->setPrinting(false);
            first["chord"]="Cmaj7(#11)/E";first["chordScale"]=1.;first["degree"]="I";
            second["onTop"]=true;second["avoidScoreContent"]=false;second["degree"]="V";
            observer.setScorePreview({first,second});QCOMPARE(observer.previewStatus().value("hidden").toInt(),0);observer.setActiveScorePreview(60);
            view->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("chord-on-top.png"));
            main->hide();
            }
      };
#include "usability_072_tests.inc"
#include "score_editing_075_tests.inc"
#include "harmony_display_075_tests.inc"
#include "variant_metadata_076_tests.inc"
#include "harmony_panel_077_tests.inc"
#include "spectrum_078_tests.inc"
QTEST_MAIN(TestPluginHost)
#include "tst_pluginhost.moc"
