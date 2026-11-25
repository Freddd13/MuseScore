// GPL-2.0-or-later. Win32 dispatch in the test process's private desktop.
#ifdef Q_OS_WIN
#include <qt_windows.h>
namespace {
class InputSpy171 : public QObject {
    bool eventFilter(QObject* object,QEvent* event) override {
        if(event->type()==QEvent::Wheel){auto wheel=static_cast<QWheelEvent*>(event);qInfo()<<"wheel receiver"<<object<<wheel->modifiers()<<wheel->position();}
        return false;
    }
public:
    InputSpy171(){qApp->installEventFilter(this);}
};
class NativeModifiers171 {
    BYTE saved[256],keys[256];
public:
    explicit NativeModifiers171(bool control=false) {
        GetKeyboardState(saved);memcpy(keys,saved,sizeof(keys));
        for(int key:{VK_CONTROL,VK_LCONTROL,VK_RCONTROL,VK_SHIFT,VK_LSHIFT,VK_RSHIFT,VK_MENU,VK_LMENU,VK_RMENU,VK_LWIN,VK_RWIN})keys[key]=0;
        if(control)keys[VK_CONTROL]=keys[VK_LCONTROL]=0x80;
        apply();
    }
    void apply(){SetKeyboardState(keys);}
    ~NativeModifiers171(){SetKeyboardState(saved);}
};
QWidget* implicitOwner171()
{
    auto address=GetProcAddress(GetModuleHandleW(L"Qt5Widgets.dll"),"?qt_button_down@@3PEAVQWidget@@EA");
    return address?*reinterpret_cast<QWidget**>(address):nullptr;
}
HWND inputWindow171(QWidget* widget, QPoint local, POINT* client)
{
    const auto global = widget->mapToGlobal(local);
    auto window = reinterpret_cast<HWND>(widget->window()->winId());
    for (;;) {
        POINT point{global.x(), global.y()}; ScreenToClient(window,&point);
        const auto child = ChildWindowFromPointEx(window,point,CWP_SKIPINVISIBLE|CWP_SKIPTRANSPARENT);
        if (!child || child == window) { *client=point; return window; }
        window=child;
    }
}
void nativeClick171(QWidget* widget, QPoint local = QPoint(), bool control = false)
{
    if (local.isNull()) local=widget->rect().center();
    POINT client; auto window=inputWindow171(widget,local,&client);
    DWORD process=0;GetWindowThreadProcessId(window,&process);
    if(process!=GetCurrentProcessId())qFatal("Input check must stay inside its own process");
    NativeModifiers171 keyboard(control);
    auto point=MAKELPARAM(client.x,client.y);
    const WPARAM modifiers=control?MK_CONTROL:0;
    SendMessageW(window,WM_MOUSEMOVE,modifiers,point);
    SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON|modifiers,point);
    QTest::qWait(3);
    SendMessageW(window,WM_LBUTTONUP,modifiers,point);
    QTest::qWait(3);
    if(control) {
        NativeModifiers171 released;
        // Releasing Ctrl is a real part of the user's multiselection gesture.
        SendMessageW(window,WM_KEYUP,VK_CONTROL,LPARAM(0xC01D0001));
        QTest::qWait(3);
    }
}
bool nativeDrag171(QWidget* widget, QPoint from, QPoint to)
{
    NativeModifiers171 keyboard;
    POINT client;auto window=inputWindow171(widget,from,&client);
    const auto start=MAKELPARAM(client.x,client.y);
    SendMessageW(window,WM_MOUSEMOVE,0,start);
    SendMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,start);
    QCoreApplication::processEvents();
    const bool captured=QWidget::mouseGrabber()==widget;
    qInfo()<<"drag began"<<widget<<"explicit owner"<<QWidget::mouseGrabber()<<"implicit owner"<<implicitOwner171();
    const auto global=widget->mapToGlobal(to);POINT end{global.x(),global.y()};ScreenToClient(window,&end);
    keyboard.apply();
    SendMessageW(window,WM_MOUSEMOVE,MK_LBUTTON,MAKELPARAM(end.x,end.y));
    QCoreApplication::processEvents();
    keyboard.apply();
    SendMessageW(window,WM_LBUTTONUP,0,MAKELPARAM(end.x,end.y));
    QCoreApplication::processEvents();
    return captured;
}
}
#endif
static MasterScore* performanceScore123(const QString&,int);
void TestPluginHost::wholeWindowInput171()
{
#ifndef Q_OS_WIN
    QSKIP("Windows native input check");
#else
    auto main=Ms::mscore;
    guiScaling=1.; MuseScore::updateUiStyleAndTheme();
    if(!WorkspacesManager::currentWorkspace()) WorkspacesManager::initCurrentWorkspace();
    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());
    synth->setSampleRate(44100); synth->init();
    QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
    QScopedValueRollback<MasterSynthesizer*> currentSynth(Ms::synti,synth.get());
    QScopedValueRollback<bool> sequencing(noSeq,false);
    QVERIFY(!Ms::seq);
    auto sequence=new Seq; Ms::seq=sequence;
    std::unique_ptr<Seq,std::function<void(Seq*)>> guard(sequence,[](Seq* value){
        value->stopWait();value->synchronizeStoppedGui();value->waitForStoppedRendering();
        value->setScoreView(nullptr);delete value;Ms::seq=nullptr;
    });
    auto driver=new PalettePlaybackDriver093(sequence);
    sequence->setDriver(driver);sequence->setMasterSynthesizer(synth.get());
    QVERIFY(sequence->init());
    auto score=performanceScore123(_settings.filePath("two-note-input171.mscx"),8);
    QFile::copy(_settings.filePath("two-note-input171.mscx"),qEnvironmentVariable("HARMONY_TEST_ARTIFACTS")+"/two-note-input171.mscx");
    QVERIFY(score); main->setCurrentScoreView(main->appendScore(score));
    main->resize(1360,920);main->show();
    main->findChild<QAction*>("kumo-workspace-arrange")->trigger();
    main->showPerformanceEditor(true);showReferencePanel(true);
    main->showPerformanceEditor(true);
    auto editor=main->performanceEditor();
    editor->findChild<QToolButton*>("performanceWheelVelocity")->setChecked(true);
    auto audition=editor->findChild<QToolButton*>("performanceAudition");
    if(!audition) for(auto button:editor->findChildren<QToolButton*>())
        if(button->text()==QString::fromUtf8("试听")) audition=button;
    QVERIFY(audition);
    auto playback=editor->findChild<QToolButton*>("performancePlay");
    QVERIFY(playback);QTest::qWait(200);
    auto view=main->currentScoreView();
    InputSpy171 inputSpy;
    auto noteAt=[score](int tick){return toChord(score->tick2segment(Fraction::fromTicks(tick),false,SegmentType::ChordRest)->element(0))->upNote();};
    auto point=[view](Note* note){return view->matrix().mapRect(note->canvasBoundingRect()).center().toPoint();};
    for(int pass=0;pass<4;++pass){
        auto first=noteAt(0),second=noteAt(480),third=noteAt(960);
        view->adjustCanvasPosition(first,false);QTest::qWait(30);
        nativeClick171(view,point(first));nativeClick171(view,point(second),true);
        QCOMPARE(score->selection().noteList().size(),2);
        qInfo()<<"before wheel"<<QGuiApplication::keyboardModifiers()<<first->veloOffset()<<first->veloType();
        POINT client;const auto receiver=inputWindow171(view,point(first),&client);
        const auto global=view->mapToGlobal(point(first));
        NativeModifiers171 keyboard;
        SendMessageW(receiver,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),MAKELPARAM(global.x(),global.y()));
        QTest::qWait(12);
        qInfo()<<"after wheel"<<QGuiApplication::keyboardModifiers()<<first->veloOffset()<<first->veloType()<<editor->hasPending();
        QVERIFY2(editor->hasPending()||first->veloOffset()!=0,"Native wheel actually changes the selected notes");
        nativeClick171(view,point(third));
        QTRY_COMPARE(score->selection().noteList().size(),1);
        QCOMPARE(score->selection().noteList().front(),third);
        qInfo()<<"two selected notes adjusted, then native score click"<<pass<<view->mscoreState();
    }
    QCheckBox* handles=nullptr;
    for(auto check:editor->findChildren<QCheckBox*>())if(check->text()==QString::fromUtf8("音旁手柄"))handles=check;
    QVERIFY(handles);handles->setChecked(true);
    // The first handle overlaps the second head in this ascending pattern.
    // Native Ctrl-click must still select both actual notes.
    nativeClick171(view,point(noteAt(0)));nativeClick171(view,point(noteAt(480)),true);
    QCOMPARE(score->selection().noteList().size(),2);
    nativeClick171(view,point(noteAt(960)));
    QCOMPARE(score->selection().noteList().size(),1);QCOMPARE(score->selection().noteList().front(),noteAt(960));
    for(int pass=0;pass<3;++pass){
        auto first=noteAt(1440),second=noteAt(0),third=noteAt(960);
        handles->setChecked(false);
        nativeClick171(view,point(first));nativeClick171(view,point(second),true);
        QCOMPARE(score->selection().noteList().size(),2);QTest::qWait(30);
        handles->setChecked(true);
        const auto handle=(view->matrix().mapRect(first->canvasBoundingRect()).topRight()+QPointF(12,-12)).toPoint();
        const int beforeFirst=first->veloOffset(),beforeSecond=second->veloOffset();
        QVERIFY(nativeDrag171(view,handle,handle-QPoint(0,14)));
        QVERIFY(first->veloOffset()!=beforeFirst);QVERIFY(second->veloOffset()!=beforeSecond);
        qInfo()<<"two-note handle release"<<pass<<implicitOwner171()<<QWidget::mouseGrabber()<<view->mscoreState();
        QVERIFY2(!implicitOwner171(),"Native widget press owner was cleared after handle release");
        QVERIFY(!QWidget::mouseGrabber());
        nativeClick171(view,point(third));
        QTRY_COMPARE(score->selection().noteList().size(),1);QCOMPARE(score->selection().noteList().front(),third);
    }
    // A comparison drag passes through QWindow before reaching the parameter widget.
    editor->findChild<QComboBox*>("performanceScope")->setCurrentIndex(0);
    editor->fit();QTest::qWait(40);
    auto parameter=editor->findChild<QWidget*>("performanceParameterCanvas");QVERIFY(parameter);
    auto first=noteAt(0);
    workflowContext()->setExpression(score,{{"revision","input171"},{"candidate",QVariantMap{{"velocity",QVariantList{QVariantMap{{"tick",0},{"track",0},{"index",0},{"id","first"},{"target",96}}}}}}});
    QTest::qWait(40);
    const QPoint preview(PerformanceViewport::timelineStart+4,qRound(parameter->height()-PerformanceViewport::parameterPadding-(96.-1)/126*(parameter->height()-2*PerformanceViewport::parameterPadding)));
    QVERIFY(parameter->rect().contains(preview));
    QVERIFY(nativeDrag171(parameter,preview,preview-QPoint(0,10)));
    qInfo()<<"comparison release"<<implicitOwner171()<<QWidget::mouseGrabber();
    QVERIFY2(!implicitOwner171(),"Native widget press owner was cleared after comparison release");
    nativeClick171(view,point(noteAt(960)));QCOMPARE(score->selection().noteList().front(),noteAt(960));
    workflowContext()->setExpression(nullptr,{});
    for(int pass=0;pass<12;++pass) {
        nativeClick171(playback);QTRY_VERIFY(sequence->isPlaying());
        for(int n=0;n<4;++n){driver->pulse();QTest::qWait(5);}
        auto chord=toChord(score->tick2segment(Fraction::fromTicks(480),false,SegmentType::ChordRest)->element(0));
        score->select(chord->notes().front());score->select(chord->notes().back(),SelectType::ADD);main->endCmd();
        editor->findChild<QToolButton*>("performanceWheelVelocity")->setChecked(true);
        auto view=main->currentScoreView();
        const auto local=view->matrix().mapRect(chord->upNote()->canvasBoundingRect()).center().toPoint();
        POINT client;const auto native=inputWindow171(view,local,&client);
        const auto global=view->mapToGlobal(local);
        NativeModifiers171 keyboard;
        SendMessageW(native,WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA),MAKELPARAM(global.x(),global.y()));
        if(pass==3){
            auto save=getAction("file-save");QVERIFY(save->isEnabled());save->trigger();
            QTest::qWait(5);QVERIFY(sequence->isPlaying());
            QFile written(_settings.filePath("two-note-input171.mscx"));QVERIFY(written.open(QIODevice::ReadOnly));
            QVERIFY(written.readAll().contains("kumo.toolState"));
        }
        nativeClick171(playback);QTRY_VERIFY(!sequence->isPlaying());
        GUITHREADINFO state{};state.cbSize=sizeof(state);GetGUIThreadInfo(GetCurrentThreadId(),&state);
        qInfo()<<"native stop"<<pass<<"capture"<<quintptr(state.hwndCapture)<<"moving"<<quintptr(state.hwndMoveSize)
               <<"flags"<<state.flags<<"Qt grab"<<QWidget::mouseGrabber()<<"buttons"<<QGuiApplication::mouseButtons();
        const bool before=audition->isChecked();nativeClick171(audition);
        QTRY_COMPARE(audition->isChecked(),!before);
        QVERIFY(!(state.flags&GUI_INMOVESIZE));
        QVERIFY(main->isEnabled());
    }
#endif
}
