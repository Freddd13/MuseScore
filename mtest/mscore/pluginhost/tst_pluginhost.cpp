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
                  if(root && root->version()=="1.4.0") {panel=root;panelView=view;break;}
                  }
            for(auto candidate:main->findChildren<QDockWidget*>())
                  if(candidate->windowTitle()=="Harmony Assistant") {dock=candidate;break;}
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
            main->hide();
            }
      };
QTEST_MAIN(TestPluginHost)
#include "tst_pluginhost.moc"
