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
#include "mscore/mssplashscreen.h"
#include "mscore/musescoredialogs.h"
#include "personalbranding.h"
#include "mscore/workflow/builtinpanels.h"
#include "mscore/workflow/referencefactory.h"
#include "mscore/workflow/workflowcontext.h"
#include <QDataStream>
#include <QToolButton>
#include <QListWidget>
#include <QPushButton>
#include <QTreeWidget>
#include "mscore/workflow/arrangement.h"
#include <QComboBox>
#include <cmath>
#include "mscore/performanceeditor/performanceeditor.h"
#include <QTabBar>
#include <QFileDialog>
#include <QLineEdit>
#include <QQuickItem>
#include <QQmlComponent>
#include <QJsonDocument>

#include "mscore/plugin/qmlpluginengine.h"
#include "libmscore/notevelocity.h"
#include "libmscore/regionalvariants.h"
#include "libmscore/excerpt.h"
#include "libmscore/tie.h"
#include "libmscore/slur.h"
#include "libmscore/tuplet.h"
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
static void releaseHostPanels(){
 QList<QPointer<QmlPlugin>> panels;for(auto window:QGuiApplication::allWindows())if(auto view=qobject_cast<QQuickView*>(window))if(auto root=qobject_cast<QmlPlugin*>(view->rootObject()))panels.append(root);
 for(auto panel:panels)if(panel)panel->disposePanelForReload();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
}

class TestPluginHost : public QObject {
      Q_OBJECT
      QTemporaryDir _settings;
   private slots:
      void cleanup(){releaseHostPanels();if(Ms::mscore)Ms::mscore->showPerformanceEditor(false);}
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
            QTRY_VERIFY2_WITH_TIMEOUT(!candidate().value("velocity").toList().isEmpty(),qPrintable(root->property("status").toString()),10000);
            QTest::qWait(80);QVERIFY(root->window()->grabWindow().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("builtin-expression-ready-058.png")));
            auto chord=score->findCR(Fraction::fromTicks(480),0);QVERIFY(chord&&chord->isChord());auto first=toChord(chord)->notes().front();
            const auto previousType=first->veloType();const int previousRaw=first->veloOffset();const int beforeUndo=score->undoStack()->getCurIdx();
            QVERIFY(click("expressionApplyVelocity"));QTRY_COMPARE(score->undoStack()->getCurIdx(),beforeUndo+1);
            QCOMPARE(first->veloType(),Note::ValueType::USER_VAL);QCOMPARE(NoteVelocity::effective(NoteVelocity::referenceBase(first),first->veloType(),first->veloOffset()),100);
            EditData undo;score->undoRedo(true,&undo);QCOMPARE(first->veloType(),previousType);QCOMPARE(first->veloOffset(),previousRaw);
            QVERIFY(editor->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("builtin-expression-058.png")));
            QVERIFY(main->currentScoreView()->height()>80);
            QVERIFY(root->window());QTest::qWait(80);QVERIFY(root->window()->grabWindow().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("builtin-expression-controls-058.png")));
            tabs->setCurrentIndex(0);main->showPerformanceEditor(false);
            }
      void transcriptionManualFlow()
            {
            auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->resize(1280,960);main->show();
            const auto path=_settings.filePath("spectral-a440.wav");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));QDataStream data(&file);data.setByteOrder(QDataStream::LittleEndian);data.writeRawData("RIFF",4);data<<quint32(36+44100*4);data.writeRawData("WAVEfmt ",8);data<<quint32(16)<<quint16(1)<<quint16(1)<<quint32(44100)<<quint32(88200)<<quint16(2)<<quint16(16);data.writeRawData("data",4);data<<quint32(44100*4);for(int i=0;i<88200;++i)data<<qint16(15000*std::sin(2*3.14159265358979323846*440*i/44100));file.close();
            auto media=referenceProject();QVariantMap response;const QVariantList tracks{QVariantMap{{"id","spectral-test"},{"name","440 Hz"},{"path",path},{"associations",QVariantList{QVariantMap{{"start",0},{"end",1920},{"from",0.},{"until",2000.},{"name","第1小节"},{"active",true}}}}}};
            workflowContext()->setReferenceScore(score);QVERIFY(QMetaObject::invokeMethod(media,"setTracks",Q_RETURN_ARG(QVariantMap,response),Q_ARG(QVariantList,tracks)));QVERIFY(response.value("ok").toBool());
            main->showPerformanceEditor(true);auto editor=main->performanceEditor();auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs");QVERIFY(tabs);QCOMPARE(tabs->count(),3);tabs->setCurrentIndex(2);QTest::qWait(150);
            auto pane=editor->findChild<QWidget*>("transcriptionSpectrumPane");QVERIFY(pane&&pane->isVisible());auto session=pane->findChild<QObject*>("transcriptionSpectrumSession");QVERIFY(session);QVERIFY(session->property("available").toBool());
            QTRY_VERIFY_WITH_TIMEOUT(!qvariant_cast<QImage>(session->property("image")).isNull(),10000);
            auto playback=pane->findChild<QToolButton*>("transcriptionPlay");QVERIFY(playback);QTRY_VERIFY(playback->isEnabled());const auto oldVolume=media->property("volume");media->setProperty("volume",0.);QSignalSpy playbackChanged(media,SIGNAL(changed()));QTest::mouseClick(playback,Qt::LeftButton);QVERIFY(playbackChanged.count()>0);QTRY_VERIFY_WITH_TIMEOUT(!media->property("playing").toBool()||media->property("diagnostics").toMap().value("outputFrames").toLongLong()>0,4000);if(media->property("playing").toBool())QTest::mouseClick(playback,Qt::LeftButton);QTRY_VERIFY(!media->property("playing").toBool());media->setProperty("volume",oldVolume);
            auto canvas=pane->findChild<QWidget*>("transcriptionSpectrumCanvas");QVERIFY(canvas);auto mark=pane->findChild<QToolButton*>("transcriptionMark");auto input=pane->findChild<QToolButton*>("transcriptionInput");QVERIFY(mark&&input);
            auto rest=score->findCR(Fraction(),0);QVERIFY(rest&&rest->isRest());score->select(rest);main->currentScoreView()->cmd("note-input");score->inputState().setDuration(TDuration(TDuration::DurationType::V_QUARTER));
            QTest::mouseClick(canvas,Qt::LeftButton,Qt::NoModifier,QPoint(gutterForTest()+100,qRound((108.5-69)/88.*canvas->height())));
            QTest::mouseMove(canvas,QPoint(canvas->width()-20,canvas->height()-2));const int before=score->undoStack()->getCurIdx();QTest::mouseClick(mark,Qt::LeftButton);QTRY_COMPARE(score->undoStack()->getCurIdx(),before+1);
            const auto saved=QJsonDocument::fromJson(score->masterScore()->metaTag("kumo.transcription").toUtf8()).toVariant().toMap();QCOMPARE(saved.value("pitches").toList().size(),1);QCOMPARE(saved.value("pitches").toList().front().toMap().value("pitch").toInt(),69);
            auto marks=pane->findChild<QListWidget*>("transcriptionMarks");QVERIFY(marks&&marks->count()==1);marks->item(0)->setSelected(true);QTest::mouseClick(input,Qt::LeftButton);QTRY_COMPARE(score->undoStack()->getCurIdx(),before+2);auto chord=score->findCR(Fraction(),0);QVERIFY(chord&&chord->isChord());QCOMPARE(toChord(chord)->notes().front()->pitch(),69);
            Shortcut::load();for(const char* key:{"undo","redo"}){auto shortcut=Shortcut::getShortcut(key);shortcut->setStandardKey(shortcut->standardKey());}QVERIFY(!getAction("undo")->shortcut().isEmpty());main->activateWindow();canvas->setFocus();QTest::keySequence(canvas,getAction("undo")->shortcut());QTRY_VERIFY(score->findCR(Fraction(),0)->isRest());QTest::keySequence(canvas,getAction("undo")->shortcut());QTRY_VERIFY(score->masterScore()->metaTag("kumo.transcription").isEmpty());QTRY_COMPARE(marks->count(),0);QTest::keySequence(canvas,getAction("redo")->shortcut());QTest::keySequence(canvas,getAction("redo")->shortcut());QTRY_VERIFY(score->findCR(Fraction(),0)->isChord());
            QFileInfo info(_settings.filePath("spectral-mark.mscz"));QVERIFY(score->saveCompressedFile(info,false,false));std::unique_ptr<MasterScore> restored(main->readScore(info.absoluteFilePath()));QVERIFY(restored);QCOMPARE(restored->metaTag("kumo.transcription"),score->masterScore()->metaTag("kumo.transcription"));
            const auto out=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));
            QFile savedInput(info.absoluteFilePath());QVERIFY(savedInput.open(QIODevice::ReadOnly));QFile savedOutput(out.filePath("transcription-compat-059.mscz"));QVERIFY(savedOutput.open(QIODevice::WriteOnly));QCOMPARE(savedOutput.write(savedInput.readAll()),savedInput.size());savedOutput.close();
            QVERIFY(pane->grab().save(out.filePath("transcription-separate-059.png")));
            auto display=pane->findChild<QComboBox*>("transcriptionDisplay");QVERIFY(display);display->setCurrentIndex(1);QTest::qWait(100);QVERIFY(editor->grab().save(out.filePath("transcription-overlay-059.png")));display->setCurrentIndex(0);
            auto clips=pane->findChild<QComboBox*>("transcriptionClips");QVERIFY(clips);
            auto updated=tracks.front().toMap();auto firstClip=updated.value("associations").toList().front().toMap();firstClip["id"]="clip-one";auto secondClip=firstClip;secondClip["id"]="clip-two";secondClip["name"]="第二个片段";secondClip["from"]=500.;secondClip["until"]=1500.;updated["associations"]=QVariantList{firstClip,secondClip};QVariantList changed{updated};
            QMetaObject::invokeMethod(media,"setTracks",Q_RETURN_ARG(QVariantMap,response),Q_ARG(QVariantList,changed));clips->setCurrentIndex(2);updated["volume"]=.5;changed=QVariantList{updated};QMetaObject::invokeMethod(media,"setTracks",Q_RETURN_ARG(QVariantMap,response),Q_ARG(QVariantList,changed));QCOMPARE(clips->currentData().toMap().value("id").toString(),QString("clip-two"));
            clips->setCurrentIndex(0);updated["volume"]=.6;changed=QVariantList{updated};QMetaObject::invokeMethod(media,"setTracks",Q_RETURN_ARG(QVariantMap,response),Q_ARG(QVariantList,changed));QCOMPARE(clips->currentIndex(),0);
            tabs->setCurrentIndex(0);QVERIFY(!pane->isVisible());main->currentScoreView()->cmd("escape");
            }
      static int gutterForTest(){return 76;}


      void arrangementMcpTransport(){
            const auto bridge=qEnvironmentVariable("P2_CONTEXT_BRIDGE");const auto done=qEnvironmentVariable("P2_CONTEXT_DONE");
            if(bridge.isEmpty()||done.isEmpty())QSKIP("Run P2/test_context_host.py for the real MCP connection");
            auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
            QmlPluginEngine engine;QQmlComponent component(&engine,QUrl::fromLocalFile(bridge));QVERIFY2(!component.isError(),qPrintable(component.errorString()));
            std::unique_ptr<QmlPlugin> root(qobject_cast<QmlPlugin*>(component.create()));QVERIFY2(root,qPrintable(component.errorString()));root->runPlugin();
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

      void arrangementAiWorkflow(){
            auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->resize(1280,960);main->show();
            PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
            auto n=toChord(score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest)->element(0))->notes()[0];
            score->select(n,SelectType::SINGLE);const int before=score->undoStack()->getCurIdx();
            const auto state=observer.arrangementState();QVERIFY(!state.contains("error"));const auto selected=state.value("scope").toMap();QCOMPARE(selected.value("start").toInt(),480);QCOMPARE(selected.value("end").toInt(),960);
            auto all=observer.arrangementState({{"whole",true}});const auto snapshot=all.value("snapshotId").toString();QVERIFY(!snapshot.isEmpty());
            auto page=observer.arrangementRange({{"snapshotId",snapshot},{"scope",all.value("scope")},{"fromTick",0},{"limit",1}});QVERIFY2(!page.contains("error"),qPrintable(page.value("error").toString()));QCOMPARE(page.value("notes").toList().size(),3);QCOMPARE(page.value("nextTick").toInt(),480);
            auto note=page.value("notes").toList()[0].toMap();QCOMPARE(note.value("quarterDuration").toMap().value("numerator").toInt(),2);QVERIFY(note.contains("tpc2"));QVERIFY(note.contains("velocityBase"));QCOMPARE(score->undoStack()->getCurIdx(),before);QCOMPARE(score->selection().element(),static_cast<Element*>(n));
            observer.publishHarmonyAnalysis({{"snapshotId",snapshot},{"regions",QVariantList{QVariantMap{{"start",0},{"end",1920},{"chord","C"}}}}});QCOMPARE(observer.arrangementState().value("harmonyAnalysis").toMap().value("freshness").toString(),QString("current"));
            const QVariantMap option{{"title","力度与指法"},{"summary","左手低音更轻，标出小指。"},{"scope",all.value("scope")},{"changes",QVariantList{QVariantMap{{"ref",note.value("ref")},{"velocity",53},{"finger",5}}}}};
            auto submit=observer.arrangementRequest("submitArrangementOptions",{{"snapshotId",snapshot},{"options",QVariantList{option}},{"operationId","submit-ui-1"}});QVERIFY2(!submit.contains("error"),qPrintable(submit.value("error").toString()));QCOMPARE(score->undoStack()->getCurIdx(),before);QCOMPARE(submit.value("options").toList().size(),1);
            const auto id=submit.value("options").toList()[0].toMap().value("id");
            const auto rendered=observer.arrangementRequest("arrangementOption",{{"optionId",id},{"action","performance"}});QVERIFY(!rendered.value("original").toList().isEmpty());bool heardBass=false;for(auto v:rendered.value("proposed").toList()){auto e=v.toMap();if(e.value("pitch").toInt()==36){QCOMPARE(e.value("velocity").toInt(),53);heardBass=true;}}QVERIFY(heardBass);

            auto repeated=observer.arrangementRequest("submitArrangementOptions",{{"snapshotId",snapshot},{"options",QVariantList{option}},{"operationId","submit-ui-1"}});QCOMPARE(repeated,submit);
            auto panel=main->findChild<QWidget*>("arrangementSuggestionPanel");QVERIFY(panel);QVERIFY(panel->isVisible());
            auto choices=panel->findChild<QTreeWidget*>("arrangementChoices");QVERIFY(choices);QCOMPARE(choices->topLevelItemCount(),1);
            auto preview=panel->findChild<QPushButton*>("arrangement-preview");QVERIFY(preview);QTest::mouseClick(preview,Qt::LeftButton);QVERIFY(main->currentScoreView()->findChild<QWidget*>("regionalDifferenceLayer")->isVisible());
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
            auto submit=observer.arrangementRequest("submitArrangementOptions",{{"snapshotId",snapshot},{"options",QVariantList{option}},{"operationId","voice-submit"}});QVERIFY2(!submit.contains("error"),qPrintable(submit.value("error").toString()));
            auto id=submit.value("options").toList()[0].toMap().value("id");int before=score->undoStack()->getCurIdx();
            auto used=observer.arrangementRequest("arrangementOption",{{"optionId",id},{"action","apply"},{"operationId","voice-apply"}});QVERIFY2(!used.contains("error"),qPrintable(used.value("error").toString()));QCOMPARE(score->undoStack()->getCurIdx(),before+1);
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
            auto submitted=observer.arrangementRequest("submitArrangementOptions",{{"snapshotId",snapshot},{"options",QVariantList{option}},{"operationId","triplet-submit"}});QVERIFY2(!submitted.contains("error"),qPrintable(submitted.value("error").toString()));auto id=submitted.value("options").toList()[0].toMap().value("id");
            int before=score->undoStack()->getCurIdx();auto stored=observer.arrangementRequest("arrangementOption",{{"optionId",id},{"action","variant"},{"operationId","triplet-variant"}});QVERIFY2(!stored.contains("error"),qPrintable(stored.value("error").toString()));QCOMPARE(score->undoStack()->getCurIdx(),before+1);QCOMPARE(RegionalVariants::state(score).value("groups").toList().size(),1);QCOMPARE(toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(4))->notes().size(),size_t(3));
            auto applied=observer.arrangementRequest("arrangementOption",{{"optionId",id},{"action","apply"},{"operationId","triplet-apply"}});QVERIFY2(!applied.contains("error"),qPrintable(applied.value("error").toString()));QCOMPARE(score->undoStack()->getCurIdx(),before+2);
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
            auto submitted=observer.arrangementRequest("submitArrangementOptions",{{"snapshotId",snapshot},{"options",QVariantList{option}},{"taskBrief",QVariantMap{{"allowMelodyChanges",true}}},{"operationId","links-submit"}});QVERIFY2(!submitted.contains("error"),qPrintable(submitted.value("error").toString()));auto id=submitted.value("options").toList()[0].toMap().value("id");int before=score->undoStack()->getCurIdx();auto applied=observer.arrangementRequest("arrangementOption",{{"optionId",id},{"action","apply"},{"operationId","links-apply"}});QVERIFY2(!applied.contains("error"),qPrintable(applied.value("error").toString()));QCOMPARE(score->undoStack()->getCurIdx(),before+1);auto changed=chordAt(0)->notes()[0];QVERIFY(changed->tieFor());QCOMPARE(changed->tieFor()->endNote(),b);QCOMPARE(b->tieBack()->startNote(),changed);bool linked=false;for(auto pair:score->spannerMap().map())if(pair.second->isSlur())linked|=pair.second->startElement()==chordAt(0)&&pair.second->endElement()==chordAt(3840);QVERIFY(linked);
            EditData edit;score->undoRedo(true,&edit);QCOMPARE(chordAt(0)->notes()[0],a);QCOMPARE(a->tieFor(),tie);QCOMPARE(b->tieBack(),tie);showSuggestionPanel(false);
      }


      void arrangementInformationAndPreflight(){
            auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
            int before=score->undoStack()->getCurIdx();
            auto save=[&](const QVariantMap& data,const QString& id){return observer.arrangementRequest("storeArrangementInfo",{{"snapshotId",observer.analysisContentRevision()},{"information",data},{"operationId",id}});};
            const QVariantMap statement{{"id","role-1"},{"kind","roleAssignment"},{"target",QVariantMap{{"start",0},{"end",1920},{"track",0}}},{"value","旋律"},{"origin",QVariantMap{{"kind","user"}}},{"status","accepted"}};
            auto result=save({{"assertions",QVariantList{statement}}},"info-1");QVERIFY2(result.value("ok").toBool(),qPrintable(result.value("error").toString()));
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
            const int before=score->undoStack()->getCurIdx();auto saved=observer.setWorkflowData("harmony",data);QVERIFY2(saved.value("ok").toBool(),qPrintable(saved.value("error").toString()));QCOMPARE(score->undoStack()->getCurIdx(),before+1);QCOMPARE(observer.workflowData("harmony"),data);
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
QTEST_MAIN(TestPluginHost)
#include "tst_pluginhost.moc"
