#include "mscore/workflow/toolicons.h"
#include <QQuickImageProvider>
#include <QGroupBox>

void TestPluginHost::componentToolIconsAndSourceGroups()
{
    const QList<QPair<QString,QString>> actions{
        {"open","打开"},{"save","保存"},{"add","添加"},{"delete","删除"},
        {"record","录制"},{"stop","停止"},{"play","播放"},{"pause","暂停"},
        {"loop","循环"},{"midi","MIDI"},{"mute","静音"},{"solo","独听"},
        {"link","关联"},{"fit","匹配视图"},{"settings","设置"},{"apply","采用"},
        {"keyboard","音源"},{"panel","打开界面"},{"undo","恢复"},{"refresh","重新读取"},
        {"select","选择"},{"hand","手形平移"},{"close","关闭"},{"back","返回"}
    };
    const QDir output(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path()));
    QImage gallery(680,222,QImage::Format_ARGB32_Premultiplied);
    gallery.fill(qApp->palette().color(QPalette::Window));
    QPainter painter(&gallery); painter.setPen(qApp->palette().color(QPalette::WindowText));
    QmlPluginEngine engine;
    auto provider=static_cast<QQuickImageProvider*>(engine.imageProvider("kumo-tools"));
    QVERIFY(provider);
    for(int i=0;i<actions.size();++i){
        const auto icon=componentToolIcon(actions[i].first); QVERIFY2(!icon.isNull(),qPrintable(actions[i].first));
        const QPoint cell((i%10)*68,(i/10)*72);
        painter.drawPixmap(cell+QPoint(22,8),icon.pixmap(24,24));
        painter.drawText(QRect(cell+QPoint(0,38),QSize(68,22)),Qt::AlignCenter,actions[i].second);
        QSize actual;
        const auto tinted=provider->requestPixmap(actions[i].first+"/f1f2f3",&actual,QSize(16,16)).toImage();
        QVERIFY(!tinted.isNull());
        bool drawn=false;
        for(int y=0;y<tinted.height();++y)for(int x=0;x<tinted.width();++x){
            const auto pixel=tinted.pixelColor(x,y);
            if(pixel.alpha()>0) drawn=true;
            if(pixel.alpha()>253){QVERIFY(qAbs(pixel.red()-241)<=1);QVERIFY(qAbs(pixel.green()-242)<=1);QVERIFY(qAbs(pixel.blue()-243)<=1);}
        }
        QVERIFY(drawn);
    }
    painter.end(); QVERIFY(gallery.save(output.filePath("component-icons-092.png")));
    QPushButton native("设置");setComponentToolIcon(&native,"settings");
    const auto before=native.icon().pixmap(16,16).toImage();
    auto palette=native.palette();palette.setColor(QPalette::ButtonText,Qt::white);native.setPalette(palette);
    QVERIFY(native.icon().pixmap(16,16).toImage()!=before);
    auto main=Ms::mscore;
    auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));main->resize(1280,900);main->show();
    main->showPerformanceEditor(true);
    auto editor=main->performanceEditor();QVERIFY(editor);
    auto play=editor->findChild<QToolButton*>("performancePlay");QVERIFY(play&&!play->icon().isNull());
    QCOMPARE(play->property("componentToolIcon").toString(),QString("play"));
    QVERIFY(!play->text().contains(QChar(0x25b6)));
    QTest::qWait(80);
    QVERIFY(editor->grab().save(output.filePath("component-editor-092.png")));
#ifdef KUMO_VST_HOST
    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());synth->setSampleRate(44100);
    QScopedValueRollback<MasterSynthesizer*> current(Ms::synti,synth.get());
    showVstPanel();auto dock=main->findChild<QDockWidget*>("kumoVstPanel");QVERIFY(dock);
    dock->setFloating(true);dock->resize(460,740);dock->show();
    auto source=dock->findChild<QGroupBox*>("kumoVstCurrentSourceGroup");
    auto library=dock->findChild<QGroupBox*>("kumoVstLibraryGroup");
    auto effects=dock->findChild<QGroupBox*>("kumoVstEffectsGroup");
    QVERIFY(source&&library&&effects);QTest::qWait(80);
    QVERIFY(source->mapTo(dock,QPoint()).y()<library->mapTo(dock,QPoint()).y());
    QVERIFY(library->mapTo(dock,QPoint()).y()<effects->mapTo(dock,QPoint()).y());
    auto switchBack=dock->findChild<QPushButton*>("kumoVstRemoveSource");QVERIFY(switchBack);
    QVERIFY(source->isAncestorOf(switchBack));QVERIFY(!switchBack->icon().isNull());
    QVERIFY(dock->grab().save(output.filePath("source-panel-groups-092.png")));
    dock->close();
#endif
}
