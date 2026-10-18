// GPL-2.0-or-later. External-font rendering checks with isolated settings.
#include "build/pch/pch.h"
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QSettings>
#include "mtest/testutils.h"
#include "libmscore/score.h"
#include "libmscore/staff.h"
#include "libmscore/glissando.h"
#include "libmscore/sym.h"
#include "mscore/musescore.h"
#include "mscore/preferences.h"

using namespace Ms;

class TestFontEngraving : public QObject {
      Q_OBJECT
      QTemporaryDir settings;
   private slots:
      void initTestCase()
            {
            QVERIFY(settings.isValid());
            QCoreApplication::setOrganizationName("MuseScoreRegression");
            QCoreApplication::setApplicationName("FontEngraving");
            QSettings::setDefaultFormat(QSettings::IniFormat);
            QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
            dataPath = settings.path();
            MScore::noGui = true;
            MScore::testMode = true;
            noSeq = true;
            converterMode = true;
            initMuseScoreResources();
            QStringList arguments;
            MuseScore::init(arguments);
            QVERIFY(Ms::mscore);
            }
      void privateFontWavyGlissando_data();
      void privateFontWavyGlissando();
      void cleanupTestCase()
            {
            delete Ms::mscore;
            Ms::mscore = nullptr;
            }
      };

#include "../../libmscore/spanners/private_font_glissando_tests.inc"

QTEST_MAIN(TestFontEngraving)
#include "tst_fontengraving.moc"
