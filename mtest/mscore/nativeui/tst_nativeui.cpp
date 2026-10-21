// GPL-2.0-or-later. Normal main-window initialization with isolated files and settings.
#include <QtTest/QtTest>
#include <QDockWidget>
#include <QMenuBar>
#include <QQuickItem>
#include <QQuickWindow>
#include <QPainter>
#include <QScreen>
#include "mscore/plugin/api/scoreobserver.h"
#include "mscore/plugin/api/score.h"
#include <QSettings>
#include <QTabBar>
#include <QTemporaryDir>
#include "mtest/testutils.h"
#include "mscore/musescore.h"
#include "mscore/preferences.h"
#include "mscore/scoreview.h"
#include "mscore/shortcut.h"
#include "mscore/plugin/qmlplugin.h"
#include "mscore/performanceeditor/performanceeditor.h"
#include "mscore/workflow/builtinpanels.h"
#include "libmscore/chord.h"
#include "libmscore/measure.h"
#include "libmscore/note.h"
#include "libmscore/segment.h"
#include "libmscore/undo.h"

static void initNativeUiFonts() { Q_INIT_RESOURCE(musescorefonts_Petaluma); }

using namespace Ms;

class TestNativeUi : public QObject {
    Q_OBJECT
    QTemporaryDir _files{qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", QDir::tempPath()) + "/nativeui-XXXXXX"};
    QString _scorePath;
    MasterScore* _score = nullptr;
    Note* firstNote() const {
        for (auto segment = _score->firstSegment(SegmentType::ChordRest); segment; segment = segment->next1(SegmentType::ChordRest))
            for (auto element : segment->elist())
                if (element && element->isChord()) return toChord(element)->upNote();
        return nullptr;
    }
    QmlPlugin* expression() const {
        auto pane = Ms::mscore->performanceEditor()->findChild<QWidget*>("embeddedExpressionPage");
        return pane ? qobject_cast<QmlPlugin*>(pane->property("root").value<QObject*>()) : nullptr;
    }
    void changeVelocity(int value) {
        _score->startCmd();
        firstNote()->undoChangeProperty(Pid::VELO_TYPE, int(Note::ValueType::USER_VAL));
        firstNote()->undoChangeProperty(Pid::VELO_OFFSET, value);
        _score->endCmd();
    }
    bool capture(QWidget* host, QmlPlugin* root, const QString& name) const {
        auto picture = host->grab().toImage();
        auto content = root->window()->grabWindow();
        if (picture.isNull() || content.isNull()) return false;
        const auto origin = host->mapFromGlobal(root->window()->mapToGlobal(QPoint()));
        QPainter painter(&picture);
        painter.drawImage(QRect(origin, root->window()->size()), content);
        painter.end();
        return picture.save(_files.filePath(name));
    }
    QByteArray savedXml() const {
        QFile file(_scorePath);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    }
private slots:
    void initTestCase() {
        QVERIFY(_files.isValid());
        _files.setAutoRemove(false); // Keep the exact settings, saved score and screenshots for review.
        QCoreApplication::setOrganizationName("MuseScoreRegression");
        QCoreApplication::setApplicationName("NativeUi");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QVERIFY(MuseScoreApplication::setCustomConfigFolder(_files.path()));
        QSettings settings;
        settings.setValue(PREF_APP_STARTUP_FIRSTSTART, false);
        settings.setValue(PREF_APP_STARTUP_SESSIONSTART, int(SessionStart::EMPTY));
        settings.setValue(PREF_UI_APP_STARTUP_SHOWSPLASHSCREEN, false);
        settings.setValue(PREF_UI_APP_STARTUP_SHOWSTARTCENTER, false);
        settings.setValue(PREF_UI_APP_STARTUP_SHOWTOURS, false);
        settings.sync();
        _scorePath = _files.filePath("keyboard-save.mscx");
        QVERIFY(QFile::copy(QString(TESTROOT) + "/mtest/mscore/scoreobserver/piano.mscx", _scorePath));
        initMuseScoreResources();
        initNativeUiFonts();
        MScore::testMode = false;
        MScore::noGui = false;
        noSeq = true; // These checks use the real menus, workspaces and input routing without opening a sound device.
        converterMode = false;
        QStringList args{_scorePath};
        MuseScore::init(args);
        QVERIFY(Ms::mscore);
        QTRY_VERIFY(Ms::mscore->currentScoreView());
        _score = Ms::mscore->currentScoreView()->score()->masterScore();
        QVERIFY(_score && firstNote());
        QVERIFY(!MScore::noGui);
        QVERIFY(Ms::mscore->menuBar()->actions().size() >= 5);
        Ms::mscore->resize(1280, 900);
        Ms::mscore->show();
        Ms::mscore->activateWindow();
        QTest::qWait(150);
        qInfo().noquote() << "Isolated main-window files:" << _files.path();
    }
    void saveFromEmbeddedExpression() {
        auto main = Ms::mscore;
        showExpressionPanel();
        QTRY_VERIFY(expression());
        auto root = expression();
        QTRY_VERIFY(root->window() && root->isVisible());
        changeVelocity(91);
        auto save = getAction("file-save");
        QVERIFY(save && save->isEnabled());
        QVERIFY(save->shortcuts().contains(QKeySequence::Save));
        QSignalSpy saves(save, &QAction::triggered);
        root->forceActiveFocus();
        root->window()->requestActivate();
        QTest::qWait(50);
        QTest::keyClick(root->window(), Qt::Key_S, Qt::ControlModifier);
        QTRY_COMPARE(saves.count(), 1);
        QTRY_VERIFY(savedXml().contains("<velocity>91</velocity>"));
        QVERIFY(capture(main, root, "embedded-save.png"));
    }
    void saveFromFloatingExpression() {
        auto main = Ms::mscore;
        auto editor = main->performanceEditor();
        auto root = expression();
        QVERIFY(root && root->window());
        auto detach = editor->findChild<QAction*>("editorArea/float/embeddedExpressionPage");
        QVERIFY(detach);
        detach->trigger();
        auto dock = main->findChild<QDockWidget*>("kumoEditorArea/embeddedExpressionPage");
        QTRY_VERIFY(dock && dock->isVisible() && dock->isFloating());
        changeVelocity(102);
        QVERIFY(root && root->window());
        auto save = getAction("file-save");
        QSignalSpy saves(save, &QAction::triggered);
        dock->activateWindow();
        root->forceActiveFocus();
        root->window()->requestActivate();
        QTest::qWait(80);
        QTest::keyClick(root->window(), Qt::Key_S, Qt::ControlModifier);
        QTRY_COMPARE(saves.count(), 1);
        QTRY_VERIFY(savedXml().contains("<velocity>102</velocity>"));
        QVERIFY(capture(dock, root, "floating-save.png"));
        auto restore = editor->findChild<QAction*>("editorArea/restore/embeddedExpressionPage");
        QVERIFY(restore); restore->trigger();
    }
    void referenceEndpointsAndEightTrackPlan() {
        PluginAPI::Score wrapped(_score);
        PluginAPI::ScoreObserver observer;
        observer.setScore(&wrapped);
        observer.setEnabled(true);
        QTRY_VERIFY(!observer.analysisRevision().isEmpty());
        QVariantList tracks;
        for (int i=0; i<8; ++i) {
            QVariantList associations{
                QVariantMap{{"start", 480}, {"end", 960}, {"from", 1000.}, {"until", 1500.}, {"active", true}},
                QVariantMap{{"start", 960}, {"end", 1440}, {"from", 2000.}, {"until", 2500.}, {"active", true}}};
            tracks.append(QVariantMap{{"id", QString("track-%1").arg(i)}, {"name", QString("Audio %1").arg(i+1)},
                {"path", _files.filePath("reference.wav")}, {"participate", true}, {"speed", 1.}, {"associations", associations}});
        }
        for (int mode=0; mode<3; ++mode) {
            QVariantMap request{{"revision", observer.analysisRevision()}, {"start", 480}, {"end", 1440},
                {"mode", mode}, {"tracks", tracks}, {"leader", QVariantMap{{"start",480},{"end",1440},{"from",1000.},{"until",2000.}}}};
            const auto plan=observer.referencePlan(request);
            QVERIFY2(!plan.contains("error"), qPrintable(plan.value("error").toString()));
            QCOMPARE(plan.value("startUtick").toInt(), 480);
            QCOMPARE(plan.value("untilUtick").toInt(), 1440);
            const auto clips=plan.value("clips").toList();
            QCOMPARE(clips.size(), 8);
            for (auto value:clips) {
                const auto pieces=value.toMap().value("pieces").toList();
                QCOMPARE(pieces.size(), 2);
                const auto first=pieces.front().toMap(), second=pieces.back().toMap();
                QCOMPARE(first.value("from").toDouble(), 1000.);
                QCOMPARE(first.value("until").toDouble(), 1500.);
                QCOMPARE(second.value("from").toDouble(), 2000.);
                QCOMPARE(second.value("until").toDouble(), 2500.);
                QVERIFY(qAbs(first.value("at").toDouble()) < .001);
                QVERIFY(qAbs(first.value("duration").toDouble()-500.) < .001);
                QVERIFY(qAbs(second.value("at").toDouble()-500.) < .001);
                QVERIFY(qAbs(second.value("duration").toDouble()-500.) < .001);
            }
        }
    }
};

int main(int argc, char** argv) {
    QTextCodec::setCodecForLocale(QTextCodec::codecForName("UTF-8"));
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    MuseScoreApplication app("kumo-nativeui-" + QString::number(QCoreApplication::applicationPid()), argc, argv);
    TestNativeUi test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_nativeui.moc"
