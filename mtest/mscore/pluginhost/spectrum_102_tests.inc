// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "mscore/performanceeditor/spectrumpane.h"
void TestPluginHost::spectrumListeningKeepsLoopPosition()
{
    auto score=spectrumScore078();QVERIFY(score);
    const auto path=spectrumAudio078(_settings.filePath("spectrum-listen-102.wav"));QVERIFY(!path.isEmpty());
    QVERIFY(spectrumTracks078(score,path));auto media=referenceProject();
    auto tracks=media->property("tracks").toList();auto track=tracks.front().toMap();track["volume"]=0.;tracks[0]=track;QVariantMap response;
    QVERIFY(QMetaObject::invokeMethod(media,"setTracks",Q_RETURN_ARG(QVariantMap,response),Q_ARG(QVariantList,tracks)));QVERIFY(!response.contains("error"));
    auto main=Ms::mscore;main->showPerformanceEditor(true);auto editor=main->performanceEditor();
    auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs");QVERIFY(tabs);tabs->setCurrentIndex(2);
    auto pane=editor->findChild<QWidget*>("transcriptionSpectrumPane");QVERIFY(pane);
    auto choice=pane->findChild<QComboBox*>("transcriptionClips");QVERIFY(choice);QTRY_COMPARE(choice->count(),2);choice->setCurrentIndex(0);
    auto display=pane->findChild<QComboBox*>("transcriptionDisplay");QVERIFY(display);display->setCurrentIndex(0);
    editor->findChild<QAction*>("editorArea/float/transcriptionSpectrumPane")->trigger();
    auto dock=main->findChild<QDockWidget*>("kumoEditorArea/transcriptionSpectrumPane");QVERIFY(dock);dock->resize(920,700);
    struct Restore {
        QObject* media;PerformanceEditor* editor;QTabBar* tabs;
        QToolButton* loop=nullptr;bool loopBefore=false;
        QSlider* middle=nullptr;int middleBefore=0;
        QCheckBox* equalizer=nullptr;bool equalizerBefore=false;
        QCheckBox* selected=nullptr;bool selectedBefore=false;
        QDoubleSpinBox* speed=nullptr;double speedBefore=100;
        ~Restore(){
            QMetaObject::invokeMethod(media,"stop");
            if(loop)loop->setChecked(loopBefore);
            if(middle)middle->setValue(middleBefore);
            if(equalizer&&equalizer->isChecked()!=equalizerBefore)equalizer->click();
            if(selected&&selected->isChecked()!=selectedBefore)selected->click();
            if(speed)speed->setValue(speedBefore);
            editor->findChild<QAction*>("editorArea/restore/transcriptionSpectrumPane")->trigger();tabs->setCurrentIndex(0);
        }
    } restore{media,editor,tabs};
    auto canvas=pane->findChild<QWidget*>("transcriptionSpectrumCanvas");QVERIFY(canvas);
    auto session=pane->findChild<QObject*>("transcriptionSpectrumSession");QVERIFY(session);
    QTRY_COMPARE_WITH_TIMEOUT(session->property("info").toMap()["sourcePath"].toString(),path,10000);QTRY_VERIFY_WITH_TIMEOUT(!session->property("busy").toBool(),10000);
    auto tool=pane->findChild<QComboBox*>("transcriptionTool");QVERIFY(tool);tool->setCurrentIndex(1);
    const auto info=session->property("info").toMap();const double from=info["from"].toDouble(),until=info["until"].toDouble();QVERIFY(from<100&&until>6000);
    const int width=canvas->width()-PerformanceViewport::gutter-PerformanceViewport::rightMargin;
    const auto point=[&](double ms){return QPoint(PerformanceViewport::gutter+qRound((ms-from)/(until-from)*width),canvas->height()/2);};
    const auto a=point(1000.),b=point(5000.);QTest::mousePress(canvas,Qt::LeftButton,Qt::NoModifier,a);QTest::mouseMove(canvas,b,30);QTest::mouseRelease(canvas,Qt::LeftButton,Qt::NoModifier,b);
    QToolButton* loop=nullptr;for(auto button:pane->findChildren<QToolButton*>())if(button->text()==QString::fromUtf8("循环"))loop=button;QVERIFY(loop);restore.loop=loop;restore.loopBefore=loop->isChecked();if(!loop->isChecked())QTest::mouseClick(loop,Qt::LeftButton);
    auto settings=pane->findChild<QToolButton*>("spectrumDisplaySettings");auto scroll=pane->findChild<QScrollArea*>("spectrumOptionsScroll");QVERIFY(settings&&scroll);if(!scroll->isVisible())QTest::mouseClick(settings,Qt::LeftButton);
    auto options=pane->findChild<QTabWidget*>("spectrumOptionsTabs");QVERIFY(options);options->setCurrentIndex(1);
    auto middle=pane->findChild<QSlider*>("spectrumListen_middle");QVERIFY(middle);restore.middle=middle;restore.middleBefore=middle->value();middle->setValue(0);scroll->ensureWidgetVisible(middle);
    QCheckBox* equalizer=nullptr;for(auto check:pane->findChildren<QCheckBox*>())if(check->text()==QString::fromUtf8("试听均衡"))equalizer=check;QVERIFY(equalizer);restore.equalizer=equalizer;restore.equalizerBefore=equalizer->isChecked();scroll->ensureWidgetVisible(equalizer);if(!equalizer->isChecked())QTest::mouseClick(equalizer,Qt::LeftButton,Qt::NoModifier,QPoint(10,equalizer->height()/2));QVERIFY(equalizer->isChecked());scroll->ensureWidgetVisible(middle);
    canvas->setFocus();QTest::keyClick(canvas,Qt::Key_Space);QTRY_VERIFY_WITH_TIMEOUT(media->property("playing").toBool(),10000);
    auto source=[&]{double value=-1;QMetaObject::invokeMethod(media,"sourcePosition",Q_RETURN_ARG(double,value),Q_ARG(QString,path),Q_ARG(QString,track["id"].toString()));return value;};
    QTRY_VERIFY_WITH_TIMEOUT(source()>1600.,10000);const auto previous=source();QVERIFY(previous<4000.);
    middle->setFocus();QTest::keyClick(middle,Qt::Key_Right);
    auto plan=[&]{return media->property("playbackPlan").toMap();};
    QTRY_COMPARE_WITH_TIMEOUT(plan()["clips"].toList().front().toMap()["listen"].toMap()["middle"].toInt(),1,5000);
    const auto after=source();qInfo()<<"Loop parameter change source ms"<<previous<<after<<"offset"<<plan()["offset"];
    QVERIFY2(after>=previous-120.,"循环听辨中调节参数应继续当前位置");
    QVERIFY(plan()["offset"].toDouble()>300.);
    QVERIFY(plan()["clips"].toList().front().toMap()["listen"].toMap()["enabled"].toBool());
    auto selected=pane->findChild<QCheckBox*>("spectrumListenSelected");QVERIFY(selected);restore.selected=selected;restore.selectedBefore=selected->isChecked();scroll->ensureWidgetVisible(selected);QTest::mouseClick(selected,Qt::LeftButton,Qt::NoModifier,QPoint(10,selected->height()/2));QTRY_VERIFY(plan()["clips"].toList().front().toMap()["listen"].toMap()["selectedFrequency"].toBool());QVERIFY(source()>=after-120.);
    QDoubleSpinBox* speed=nullptr;for(auto number:pane->findChildren<QDoubleSpinBox*>())if(number->minimum()==50&&number->maximum()==150)speed=number;QVERIFY(speed);restore.speed=speed;restore.speedBefore=speed->value();const auto beforeSpeed=source();speed->setValue(75.);QTRY_VERIFY(plan()["clips"].toList().front().toMap()["pieces"].toList().front().toMap()["duration"].toDouble()>5100.);QVERIFY(source()>=beforeSpeed-120.);
    QTRY_VERIFY_WITH_TIMEOUT(source()>4500.,8000);QTRY_VERIFY_WITH_TIMEOUT(source()<1400.,4000);const auto pauseAt=source();
    canvas->setFocus();QTest::keyClick(canvas,Qt::Key_Space);QTRY_VERIFY(!media->property("playing").toBool());
    QTest::keyClick(canvas,Qt::Key_Space);QTRY_VERIFY(media->property("playing").toBool());QVERIFY(source()>=pauseAt-120.);
    QTest::keyClick(canvas,Qt::Key_Space);QTRY_VERIFY(!media->property("playing").toBool());
    QTest::mouseClick(loop,Qt::LeftButton);QVERIFY(!loop->isChecked());choice->setCurrentIndex(1);tool->setCurrentIndex(0);
    QTRY_COMPARE_WITH_TIMEOUT(session->property("info").toMap()["from"].toDouble(),8000.,10000);QTRY_VERIFY(!session->property("busy").toBool());
    const int clipWidth=canvas->width()-PerformanceViewport::gutter-PerformanceViewport::rightMargin;
    QTest::mouseClick(canvas,Qt::LeftButton,Qt::NoModifier,QPoint(PerformanceViewport::gutter+qRound(.95*clipWidth),canvas->height()/2));
    canvas->setFocus();QTest::keyClick(canvas,Qt::Key_Space);QTRY_VERIFY_WITH_TIMEOUT(media->property("completed").toBool(),5000);QVERIFY(!media->property("playing").toBool());
    QTest::mouseClick(canvas,Qt::LeftButton,Qt::NoModifier,QPoint(PerformanceViewport::gutter+clipWidth/2,canvas->height()/2));
    QTest::keyClick(canvas,Qt::Key_Space);QTRY_VERIFY(media->property("playing").toBool());qInfo()<<"Seek after end source ms"<<source()<<"offset"<<plan()["offset"];
    QVERIFY2(source()>9500.&&source()<10500.,"片段播完后点选新位置，应从点选处开始");
}

void TestPluginHost::spectrumTuningTransposeAndComparison()
{
    auto score=spectrumScore078();QVERIFY(score);const auto path=spectrumAudio078(_settings.filePath("spectrum-tuning-102.wav"));QVERIFY(!path.isEmpty());QVERIFY(spectrumTracks078(score,path));
    auto main=Ms::mscore;main->showPerformanceEditor(true);auto editor=main->performanceEditor();auto tabs=editor->findChild<QTabBar*>("workflowExpressionTabs");QVERIFY(tabs);tabs->setCurrentIndex(2);
    auto pane=editor->findChild<Ms::SpectrumPane*>("transcriptionSpectrumPane");QVERIFY(pane);
    auto choice=pane->findChild<QComboBox*>("transcriptionClips");QVERIFY(choice);QTRY_COMPARE(choice->count(),2);choice->setCurrentIndex(0);
    auto display=pane->findChild<QComboBox*>("transcriptionDisplay");QVERIFY(display);const auto oldDisplay=display->currentIndex();display->setCurrentIndex(0);
    editor->findChild<QAction*>("editorArea/float/transcriptionSpectrumPane")->trigger();auto dock=main->findChild<QDockWidget*>("kumoEditorArea/transcriptionSpectrumPane");QVERIFY(dock);dock->resize(920,700);
    QDoubleSpinBox* tuning=nullptr;for(auto number:pane->findChildren<QDoubleSpinBox*>())if(number->prefix().startsWith("A4"))tuning=number;QVERIFY(tuning);
    QSpinBox* transpose=nullptr;for(auto number:pane->findChildren<QSpinBox*>())if(number->minimum()==-24&&number->maximum()==24)transpose=number;QVERIFY(transpose);
    auto notes=pane->findChild<QCheckBox*>("spectrumShowNotes");auto gain=pane->findChild<QSlider*>("spectrumDisplay_gain");QVERIFY(notes&&gain);
    struct Restore {PerformanceEditor* editor;QTabBar* tabs;QDoubleSpinBox* tuning;double oldTuning;QSpinBox* transpose;int oldTranspose;QComboBox* display;int oldDisplay;QCheckBox* notes;bool oldNotes;QSlider* gain;int oldGain;~Restore(){tuning->setValue(oldTuning);transpose->setValue(oldTranspose);display->setCurrentIndex(oldDisplay);if(notes->isChecked()!=oldNotes)notes->click();gain->setValue(oldGain);editor->findChild<QAction*>("editorArea/restore/transcriptionSpectrumPane")->trigger();tabs->setCurrentIndex(0);}} restore{editor,tabs,tuning,tuning->value(),transpose,transpose->value(),display,oldDisplay,notes,notes->isChecked(),gain,gain->value()};
    tuning->setValue(415.3);transpose->setValue(2);
    auto session=pane->findChild<QObject*>("transcriptionSpectrumSession");auto canvas=pane->findChild<QWidget*>("transcriptionSpectrumCanvas");QVERIFY(session&&canvas);
    auto peakPitch=[&]{const auto info=session->property("info").toMap();const auto image=qvariant_cast<QImage>(session->property("image"));if(image.isNull()||info["sourcePath"].toString()!=path)return -1.;const double span=info["until"].toDouble()-info["from"].toDouble();if(span<=0)return -1.;const int x=qBound(0,int((1000.-info["from"].toDouble())/span*image.width()),image.width()-1);int row=0,peak=0;for(int y=0;y<image.height();++y)if(image.constScanLine(y)[x]>peak){peak=image.constScanLine(y)[x];row=y;}return info["highPitch"].toDouble()-(row+.5)/image.height()*(info["highPitch"].toDouble()-info["lowPitch"].toDouble());};
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(peakPitch()-70.)<.35,10000);QTRY_VERIFY(!session->property("busy").toBool());
    auto settings=pane->findChild<QToolButton*>("spectrumDisplaySettings");auto scroll=pane->findChild<QScrollArea*>("spectrumOptionsScroll");auto options=pane->findChild<QTabWidget*>("spectrumOptionsTabs");QVERIFY(settings&&scroll&&options);if(!scroll->isVisible())QTest::mouseClick(settings,Qt::LeftButton);options->setCurrentIndex(0);scroll->ensureWidgetVisible(gain);QTest::qWait(150);
    QSignalSpy analysis(session,SIGNAL(changed()));gain->setFocus();QTest::keyClick(gain,Qt::Key_Right);QTest::qWait(150);QCOMPARE(analysis.count(),0);
    QTest::mouseClick(settings,Qt::LeftButton);QTest::qWait(80);auto tool=pane->findChild<QComboBox*>("transcriptionTool");QVERIFY(tool);tool->setCurrentIndex(0);
    const auto info=session->property("info").toMap();const int width=canvas->width()-PerformanceViewport::gutter-PerformanceViewport::rightMargin;const int x=PerformanceViewport::gutter+qRound((1000.-info["from"].toDouble())/(info["until"].toDouble()-info["from"].toDouble())*width);
    QTest::mouseClick(canvas,Qt::LeftButton,Qt::NoModifier,QPoint(x,qRound((108.5-70.)/88.*canvas->height())));
    auto mark=pane->findChild<QToolButton*>("transcriptionMark");QVERIFY(mark&&mark->isEnabled());QTest::mouseClick(mark,Qt::LeftButton);
    auto data=QJsonDocument::fromJson(score->metaTag("kumo.transcription").toUtf8()).toVariant().toMap();QCOMPARE(data["pitches"].toList().size(),1);const auto saved=data["pitches"].toList().front().toMap();QCOMPARE(saved["pitch"].toInt(),72);QCOMPARE(saved["transpose"].toInt(),2);QCOMPARE(saved["tuning"].toDouble(),415.3);QVERIFY(qAbs(saved["originalFrequency"].toDouble()-440.)<12.);
    display->setCurrentIndex(1);QTest::mouseClick(settings,Qt::LeftButton);options->setCurrentIndex(0);QTRY_VERIFY(notes->isVisible());scroll->ensureWidgetVisible(notes);QTRY_VERIFY(scroll->viewport()->rect().contains(notes->mapTo(scroll->viewport(),QPoint(10,notes->height()/2))));if(notes->isChecked())QTest::mouseClick(notes,Qt::LeftButton,Qt::NoModifier,QPoint(10,notes->height()/2));QTRY_VERIFY(!notes->isChecked());qInfo()<<"Overlay note visibility"<<pane->isVisible()<<display->currentIndex()<<notes->isChecked();QVERIFY(!pane->showRollNotes());
    const auto out=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QTest::qWait(150);QVERIFY(editor->grab().save(out.filePath("spectrum-overlay-hidden-102.png")));QTest::mouseClick(notes,Qt::LeftButton,Qt::NoModifier,QPoint(10,notes->height()/2));QVERIFY(pane->showRollNotes());display->setCurrentIndex(0);QTest::mouseClick(settings,Qt::LeftButton);QTRY_VERIFY(canvas->isVisible()&&!canvas->visibleRegion().isEmpty());QTest::qWait(150);
    transpose->setValue(-1);auto rows=pane->findChild<QListWidget*>("transcriptionMarks");QVERIFY(rows);QTRY_COMPARE(rows->count(),1);rows->item(0)->setSelected(true);
    auto rest=score->findCR(Fraction(),0);QVERIFY(rest&&rest->isRest());score->select(rest);main->currentScoreView()->cmd("note-input");score->inputState().setDuration(TDuration(TDuration::DurationType::V_QUARTER));auto input=pane->findChild<QToolButton*>("transcriptionInput");QVERIFY(input);QTest::mouseClick(input,Qt::LeftButton);QTRY_VERIFY(score->findCR(Fraction(),0)->isChord());QCOMPARE(toChord(score->findCR(Fraction(),0))->notes().front()->pitch(),69);
    QFileInfo file(_settings.filePath("spectrum-tuned-102.mscz"));QVERIFY(score->saveCompressedFile(file,false,false));std::unique_ptr<MasterScore> reopened(main->readScore(file.absoluteFilePath()));QVERIFY(reopened);QCOMPARE(reopened->metaTag("kumo.transcription"),score->metaTag("kumo.transcription"));QCOMPARE(toChord(reopened->findCR(Fraction(),0))->notes().front()->pitch(),69);
    QVERIFY(dock->grab().save(out.filePath("spectrum-tuning-102.png")));getAction("undo")->trigger();QTRY_VERIFY(score->findCR(Fraction(),0)->isRest());getAction("undo")->trigger();QTRY_VERIFY(QJsonDocument::fromJson(score->metaTag("kumo.transcription").toUtf8()).toVariant().toMap()["pitches"].toList().isEmpty());getAction("redo")->trigger();getAction("redo")->trigger();QTRY_VERIFY(score->findCR(Fraction(),0)->isChord());main->currentScoreView()->cmd("escape");
}
