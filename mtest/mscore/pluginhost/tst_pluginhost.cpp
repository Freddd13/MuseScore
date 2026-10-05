// GPL-2.0-or-later. Actual Qt Widgets / QML host regressions, isolated from user settings.
#include <QtTest/QtTest>
#include <QDockWidget>
#include <QMenuBar>
#include <QQuickView>
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
                  if(root && root->version()=="1.1.0") {panel=root;panelView=view;break;}
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
            auto ribbonImage=panelView->grabWindow();
            QVERIFY(!ribbonImage.isNull());
            ribbonImage.save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("ribbon.png"));
            panel->setPanelFloating(true);QTest::qWait(50);
            QVERIFY(dock->isFloating());
            dock->resize(800,680);QTest::qWait(50);
            QVERIFY(!panel->property("ribbon").toBool());
            QVERIFY(panel->property("expanded").toBool());
            auto floatingImage=panelView->grabWindow();
            QVERIFY(!floatingImage.isNull());
            floatingImage.save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("floating-panel.png"));
            dock->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("floating.png"));
            panel->setPanelFloating(false);
            main->addDockWidget(Qt::RightDockWidgetArea,dock);QTest::qWait(50);
            QCOMPARE(panel->panelPlacement(),QString("right"));
            dock->close();QTest::qWait(50);
            // Repeated open/close must not retain previews or dereference a destroyed dock.
            main->pluginTriggered(plugin);QTest::qWait(50);
            // This fixture deliberately skips main()'s workspace/audio initialization.
            // Exercise plugin destruction; do not invoke unrelated workspace-save code.
            for(auto candidate:main->findChildren<QDockWidget*>())
                  if(candidate->windowTitle()=="Harmony Assistant") candidate->close();
            QTest::qWait(50);
            main->hide();
            }
      };
QTEST_MAIN(TestPluginHost)
#include "tst_pluginhost.moc"
