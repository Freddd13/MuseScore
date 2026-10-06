// GPL-2.0-or-later. Actual Qt Widgets / QML host regressions, isolated from user settings.
#include <QtTest/QtTest>
#include <QDockWidget>
#include <QMenuBar>
#include <QQuickView>
#include <QJSValue>
#include <QTemporaryDir>
#include <QSettings>
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
#include "libmscore/note.h"
#include "libmscore/segment.h"

static void initPluginHostResources() { Q_INIT_RESOURCE(musescorefonts_Petaluma); }

using namespace Ms;
class TestPluginHost : public QObject {
      Q_OBJECT
      QTemporaryDir _settings;
   private slots:
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
                  if(root && root->version()=="1.3.0") {panel=root;panelView=view;break;}
                  }
            for(auto candidate:main->findChildren<QDockWidget*>())
                  if(candidate->windowTitle()=="Harmony Assistant") {dock=candidate;break;}
            QVERIFY(dock); QVERIFY(panel);
            QVERIFY(QMetaObject::invokeMethod(panel,"refreshSelection"));
            auto observer=panel->findChild<PluginAPI::ScoreObserver*>();
            QVERIFY(observer);
            observer->setScore(&wrapped);
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
            main->addDockWidget(Qt::TopDockWidgetArea,dock);
            main->resizeDocks({dock},{140},Qt::Vertical);
            QTest::qWait(80);
            QCOMPARE(panel->panelPlacement(),QString("top"));
            main->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("top.png"));
            QVERIFY(panel->property("ribbon").toBool());
            QTRY_VERIFY(panel->detailPanelVisible());
            auto details=main->findChild<QDockWidget*>("pluginDetailDock");QVERIFY(details);
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
            QCOMPARE(summary->window(),panelView);
            auto floatingImage=panelView->grabWindow();
            QVERIFY(!floatingImage.isNull());
            floatingImage.save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("floating-panel.png"));
            dock->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("floating.png"));
            panel->setPanelFloating(false);
            main->addDockWidget(Qt::RightDockWidgetArea,dock);QTest::qWait(50);
            QCOMPARE(panel->panelPlacement(),QString("right"));
            dock->close();QTest::qWait(50);
            QTRY_VERIFY(main->findChildren<QDockWidget*>("pluginDetailDock").isEmpty());
            // Repeated open/close must not retain previews or dereference a destroyed dock.
            main->pluginTriggered(plugin);QTest::qWait(50);
            // This fixture deliberately skips main()'s workspace/audio initialization.
            // Exercise plugin destruction; do not invoke unrelated workspace-save code.
            for(auto candidate:main->findChildren<QDockWidget*>())
                  if(candidate->windowTitle()=="Harmony Assistant") candidate->close();
            QTest::qWait(50);
            main->hide();
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
      };
QTEST_MAIN(TestPluginHost)
#include "tst_pluginhost.moc"
