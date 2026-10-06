// GPL-2.0-or-later. Real async GUI loading, with isolated in-memory preferences.
#include <QtTest/QtTest>
#include <QListWidget>
#include <QProgressDialog>
#include "mtest/testutils.h"
#include "mscore/icons.h"
#include "mscore/preferences.h"
#include "audio/midi/fluid/fluidgui.h"
using namespace Ms;
class TestSfLoaderGui : public QObject, public MTest {
      Q_OBJECT
private slots:
      void initTestCase() {initMTest();genIcons();}
      void realAsyncLoadAndCancel()
            {
            const QString path=qEnvironmentVariable("SFLOADER_REAL_FONT");
            if(path.isEmpty()) QSKIP("Set SFLOADER_REAL_FONT for optional real-file GUI acceptance");
            QVERIFY(QFileInfo(path).isFile());
            preferences.setPreference(PREF_APP_PATHS_MYSOUNDFONTS,QFileInfo(path).absolutePath());
            FluidS::Fluid synth;synth.init(48000);
            QVERIFY(synth.addSoundFont(QString(TESTROOT)+"/share/sound/FluidR3Mono_GM.sf3"));
            FluidGui gui(&synth);gui.show();
            auto displayed=gui.findChild<QListWidget*>("soundFonts");QVERIFY(displayed);
            const int originalDisplayed=displayed->count();
            auto choose = [&]() {
                  for(auto widget:QApplication::topLevelWidgets()) if(auto dialog=qobject_cast<SfListDialog*>(widget)) {
                        auto list=dialog->findChild<QListWidget*>();QVERIFY(list);
                        auto items=list->findItems(QFileInfo(path).fileName(),Qt::MatchExactly);QVERIFY(!items.isEmpty());
                        list->clearSelection();items.front()->setSelected(true);
                        QVERIFY(QMetaObject::invokeMethod(dialog,"okClicked",Qt::DirectConnection));return;
                        }
                  QFAIL("SoundFont selection dialog did not open");
                  };
            const QStringList original=synth.soundFonts();
            bool canceled=false;
            QTimer cancelTimer;
            QTimer::singleShot(0,&gui,choose);
            connect(&cancelTimer,&QTimer::timeout,[&]() {
                  for(auto widget:QApplication::topLevelWidgets()) if(auto progress=qobject_cast<QProgressDialog*>(widget)) {
                        if (!progress->isVisible()) continue;
                        progress->cancel();QMetaObject::invokeMethod(&gui,"cancelLoadClicked",Qt::DirectConnection);canceled=true;cancelTimer.stop();
                        }
                  });
            cancelTimer.start(20);
            QVERIFY(QMetaObject::invokeMethod(&gui,"soundFontAddClicked",Qt::DirectConnection));
            // cancel() ends the modal loop before the worker finishes. Keep
            // processing events until the loading result has been cleaned up.
            QVERIFY(canceled);QTRY_VERIFY(!synth.loadWasCanceled());QCOMPARE(synth.soundFonts(),original);
            QVERIFY(synth.error().contains("Canceled"));
            QVERIFY(!synth.loadWasCanceled());
            int heartbeats=0;QTimer timer;connect(&timer,&QTimer::timeout,[&](){++heartbeats;});timer.start(20);
            QTimer::singleShot(0,&gui,choose);
            QVERIFY(QMetaObject::invokeMethod(&gui,"soundFontAddClicked",Qt::DirectConnection));
            QTRY_COMPARE(displayed->count(),originalDisplayed+1);
            timer.stop();QVERIFY(heartbeats>1);QVERIFY(synth.soundFonts().contains(QFileInfo(path).fileName()));
            QCOMPARE(synth.loadProgress(),100);QVERIFY(synth.removeSoundFont(QFileInfo(path).fileName()));
            QCOMPARE(synth.soundFonts(),original);
            }
};
QTEST_MAIN(TestSfLoaderGui)
#include "tst_sfloadergui.moc"
