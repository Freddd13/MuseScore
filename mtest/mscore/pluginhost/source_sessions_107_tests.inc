// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "audio/vst/fluidsource.h"
#include "mscore/seq.h"
#include <QElapsedTimer>
namespace {
struct SessionCounts107 { int created=0,destroyed=0; QMap<QString,int> calls,closed; QMap<QString,bool> held; };
class SessionInstrument107 final : public VstHost::Plugin {
    std::shared_ptr<SessionCounts107> counts; QString id; double value=.5;
public:
    SessionInstrument107(std::shared_ptr<SessionCounts107> c,QString name):counts(c),id(name) { ++counts->created; }
    ~SessionInstrument107() override { ++counts->destroyed; }
    bool prepare(double,int,QString*,bool=false) override { return true; }
    bool restore(const QByteArray& bytes,QString*) override { value=bytes.toDouble(); return true; }
    QByteArray state(QString*) override { return QByteArray::number(value); }
    int latency() const override { return 0; }
    qint64 tail() const override { return 0; }
    QString presetName() const override { return QString::number(value); }
    bool openEditor(QWidget*,QString*) override { return true; }
    void closeEditor() override { ++counts->closed[id]; }
    bool process(int n,const float*,const float*,float* left,float* right,const VstHost::Midi* events,int length,const VstHost::Clock&) override {
        ++counts->calls[id];
        for(int i=0;i<length;++i) {
            const auto& event=events[i];
            if ((event.status&0xf0)==0x90) counts->held[id]=event.second>0;
            if ((event.status&0xf0)==0x80 || ((event.status&0xf0)==0xb0 && event.first==120)) counts->held[id]=false;
            if ((event.status&0xf0)==0xb0 && event.first==91) value=event.second/127.;
        }
        for(int i=0;i<n;++i) left[i]=right[i]=counts->held[id]?float(value):0;
        return true;
    }
    void setParameter(quint32,double v) override { value=v; }
    QVariantList parameters() const override { return {}; }
    bool takeChanges() override { return false; }
};
QVariantMap sessionConfiguration107(const QVariantMap& descriptor,const QString& label) {
    const QVariantMap source{{"plugin",descriptor}};
    const QVariantMap slot{{"id",1},{"label",label},{"source",source}};
    return {{"schema",1},{"slots",QVariantList{slot}}};
}
QVariantMap sessionFake107(const QString& id) { return {{"format","SF2"},{"uid",id},{"name",id},{"instrument",true}}; }
}
void TestPluginHost::commonPianoPlaybackSnapshot128() {
#ifdef KUMO_VST_HOST
    auto counts=std::make_shared<SessionCounts107>();
    VstHost::Rack rack(false,[counts](const VstHost::Descriptor& d,QString*) { return std::make_unique<SessionInstrument107>(counts,d.uid); });
    auto config=sessionConfiguration107(sessionFake107("controller-snapshot"),"Common piano");
    auto slotRows=config["slots"].toList();auto row=slotRows.front().toMap();
    row["commonPiano"]="piano-1";auto source=row["source"].toMap();
    source["state"]=QString::fromLatin1(QByteArray("0.5").toBase64());row["source"]=source;slotRows[0]=row;config["slots"]=slotRows;
    rack.selectSession("a","Score A",false);QVERIFY(rack.load(config,44100));
    auto first=rack.plugin(1);QVERIFY(first);rack.bindChannels({1});
    rack.midi(0,0xb0,91,101);float audio[128]{};rack.mix(64,audio,{});
    const auto played=rack.state();QVERIFY(!VstHost::sameConfiguration(config,played));
    rack.selectSession("b","Score B",false);QVERIFY(rack.load(config,44100));
    QCOMPARE(counts->created,1);QCOMPARE(rack.plugin(1),first);QCOMPARE(counts->destroyed,0);
    rack.bindChannels({1});rack.midi(0,0xb0,91,51);rack.mix(64,audio,{});rack.state();
    rack.selectSession("a","Score A",false);QVERIFY(rack.load(config,44100));
    QCOMPARE(counts->created,1);QCOMPARE(rack.plugin(1),first);
    // A deliberately changed preset still reaches the instrument.
    source["state"]=QString::fromLatin1(QByteArray("0.9").toBase64());row["source"]=source;slotRows[0]=row;config["slots"]=slotRows;
    QVERIFY(rack.load(config,44100));QCOMPARE(rack.plugin(1)->presetName(),QString("0.9"));
    auto edited=rack.plugin(1);rack.state();rack.selectSession("c","Score C",false);QVERIFY(rack.load(config,44100));
    QCOMPARE(rack.plugin(1),edited);QCOMPARE(rack.plugin(1)->presetName(),QString("0.9"));
#endif
}
void TestPluginHost::sourceSessionReuse107() {
#ifdef KUMO_VST_HOST
    auto counts=std::make_shared<SessionCounts107>();
    VstHost::Rack rack(false,[counts](const VstHost::Descriptor& d,QString*) { return std::make_unique<SessionInstrument107>(counts,d.uid); });
    auto a=sessionConfiguration107(sessionFake107("A"),"first"),b=sessionConfiguration107(sessionFake107("B"),"second");
    rack.selectSession("a","谱A",true); QVERIFY(rack.load(a,44100)); auto first=rack.plugin(1); QVERIFY(first);
    rack.bindChannels({1}); rack.midi(0,0x90,60,100); float audio[128]{}; rack.mix(64,audio,{}); QVERIFY(counts->held["A"]);
    first->setParameter(0,.73); a=rack.state();
    rack.selectSession("b","谱B",true); QVERIFY(rack.load(b,44100)); auto second=rack.plugin(1); QVERIFY(second!=first);
    QVERIFY(!counts->held["A"]); QCOMPARE(counts->closed["A"],1); QCOMPARE(rack.retainedSessions().size(),1);
    const int idle=counts->calls["A"]; rack.bindChannels({1});
    for(int i=0;i<20;++i) rack.mix(64,audio,{});
    QCOMPARE(counts->calls["A"],idle);
    rack.selectSession("a","谱A",true); QVERIFY(rack.load(a,44100)); QCOMPARE(rack.plugin(1),first);
    QCOMPARE(first->presetName(),QString("0.73")); QCOMPARE(counts->created,2); QCOMPARE(counts->destroyed,0);
    rack.bindChannels({1}); std::fill_n(audio,128,0); rack.mix(64,audio,{}); QCOMPARE(audio[0],0.f);
    rack.releaseInactiveSessions(); QCOMPARE(rack.retainedSessions().size(),0); QCOMPARE(counts->destroyed,1); QCOMPARE(rack.plugin(1),first);
    rack.selectSession("b","谱B",true); QVERIFY(rack.load(b,44100)); QCOMPARE(counts->created,3);
    rack.releaseSession("a"); QVERIFY(rack.retainedSessions().isEmpty()); QCOMPARE(counts->destroyed,2);
    rack.selectSession("c","谱C",false); QVERIFY(rack.load(b,44100)); QCOMPARE(rack.retainedSessions().size(),1);
    rack.selectSession("d","谱D",false); QVERIFY(rack.load(a,48000)); QVERIFY(rack.retainedSessions().isEmpty());
    // Offline exporters have their own fresh graph and never keep scores alive.
    VstHost::Rack offline(true,[counts](const VstHost::Descriptor& d,QString*) { return std::make_unique<SessionInstrument107>(counts,d.uid); });
    offline.selectSession("a","export",true); QVERIFY(offline.load(a,44100));
    offline.selectSession("b","export",true); QVERIFY(offline.load(b,44100)); QVERIFY(offline.retainedSessions().isEmpty());
    // Changed saved material replaces an inactive graph instead of reviving it.
    rack.selectSession("changed-a","A",true); QVERIFY(rack.load(a,44100));
    rack.selectSession("other","B",true); QVERIFY(rack.load(b,44100));
    auto changed=a; auto rows=changed["slots"].toList(); auto row=rows.front().toMap(); auto unit=row["source"].toMap();
    unit["state"]=QString::fromLatin1(QByteArray("0.88").toBase64()); row["source"]=unit; rows[0]=row; changed["slots"]=rows;
    const int made=counts->created,freed=counts->destroyed;
    rack.selectSession("changed-a","A",true); QVERIFY(rack.load(changed,44100));
    QCOMPARE(counts->created,made+1); QCOMPARE(counts->destroyed,freed+1); QCOMPARE(rack.plugin(1)->presetName(),QString("0.88"));
#endif
}
void TestPluginHost::sourceSessionPianos107_data() {
    QTest::addColumn<QString>("source");
    QTest::addColumn<bool>("isolated");
    QTest::newRow("SF2")<<QString("SF2")<<false;
    QTest::newRow("Pianoteq")<<QString("Pianoteq")<<false;
    QTest::newRow("CFX")<<QString("CFX")<<false;
    QTest::newRow("Pianoteq-separate")<<QString("Pianoteq")<<true;
    QTest::newRow("CFX-separate")<<QString("CFX")<<true;
}
void TestPluginHost::sourceSessionPianos107() {
#ifdef KUMO_VST_HOST
    if(!qEnvironmentVariableIsSet("P2_SOURCE_SESSIONS_AUDIO")) QSKIP("Enable local piano output checks");
    QFETCH(QString,source); QFETCH(bool,isolated); QString error; QVariantMap descriptor;
    const QVariantMap sf{{"path",qEnvironmentVariable("P0_TEST_SOUNDFONT")},{"format","SF2"},{"uid","0:0"},{"name","Piano"},{"instrument",true}};
    if(source=="SF2") descriptor=sf;
    else {
        const auto values=source=="Pianoteq"?VstHost::scanVst3("C:/Program Files/Common Files/VST3/Pianoteq 9/Pianoteq 9.vst3",&error)
            :VstHost::scanVst2("E:/music_fonts/CFX Concert Grand/VST/CFX Concert Grand VST_x64.dll",&error);
        QVERIFY2(!values.isEmpty(),qPrintable(error)); descriptor=values.front().toMap();
    }
    VstHost::Rack rack(false,VstHost::createFluidSource); auto a=sessionConfiguration107(descriptor,"A"),b=sessionConfiguration107(sf,"B");
    if(isolated) { auto rows=a["slots"].toList(); auto slot=rows.front().toMap(); auto unit=slot["source"].toMap();
        unit["isolated"]=true; slot["source"]=unit; rows[0]=slot; a["slots"]=rows; }
    QElapsedTimer timer; timer.start(); rack.selectSession("a","Piano A",true); QVERIFY(rack.load(a,44100));
    auto first=rack.plugin(1); QVERIFY2(first,qPrintable(rack.problems().join('\n')));
    if(source=="CFX") QVERIFY2(first->importPreset("E:/music_fonts/CFX Concert Grand/Presets/com.Plogue.Aria/02- Compact/Player/00_Default.ariax",&error),qPrintable(error));
    const auto loadMs=timer.elapsed(); a=rack.state(); const auto preset=first->presetName();
    rack.bindChannels({1}); rack.midi(0,0xb0,64,127); rack.midi(0,0x90,60,100);
    auto render=[&] { double sum=0; VstHost::Clock clock; clock.rate=44100;
        for(int block=0;block<120;++block) { float data[512]{}; rack.mix(256,data,clock); rack.master(256,data,clock); clock.sample+=256;
            if(isolated) QTest::qWait(3);
            for(float v:data) { if(!std::isfinite(v)) return -1.; sum+=v*v; } }
        return std::sqrt(sum/(120*512)); };
    const double before=render(); QVERIFY(before>.00001);
    rack.selectSession("b","Piano B",true); QVERIFY(rack.load(b,44100)); QVERIFY(rack.plugin(1)!=first); rack.bindChannels({1});
    rack.midi(0,0x90,67,100); QVERIFY(render()>.00001);
    timer.restart(); rack.selectSession("a","Piano A",true); QVERIFY(rack.load(a,44100)); const auto returnMs=timer.elapsed();
    QCOMPARE(rack.plugin(1),first); QCOMPARE(first->presetName(),preset); rack.bindChannels({1});
    const double returned=render(); QVERIFY(returned>=0); qInfo()<<source<<"load ms"<<loadMs<<"return ms"<<returnMs<<"held rms"<<before<<"returned rms"<<returned;
    QVERIFY2(returned<before*.1,"Returning to a score must release old keys and pedal");
    rack.midi(0,0x90,64,100); QVERIFY(render()>.00001);
#else
    QFAIL("VST host required");
#endif
}

void TestPluginHost::sourceSessionPanel107_data() {
    QTest::addColumn<bool>("cfx");
    QTest::newRow("SF2") << false;
    QTest::newRow("CFX") << true;
}
void TestPluginHost::sourceSessionPanel107() {
#ifdef KUMO_VST_HOST
    QFETCH(bool,cfx);
    if(cfx && !qEnvironmentVariableIsSet("P2_SOURCE_SESSIONS_AUDIO")) QSKIP("Enable local CFX session check");
    auto main=Ms::mscore; QVERIFY(main); QVERIFY(!Ms::seq);
    const auto setting=QSettings().value("kumo/vst/retainOpenScores",false);
    struct Restore { QVariant old; ~Restore(){QSettings().setValue("kumo/vst/retainOpenScores",old);} } restore{setting};
    QSettings().setValue("kumo/vst/retainOpenScores",false);
    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory()); synth->setSampleRate(44100);
    QScopedValueRollback<MasterSynthesizer*> current(Ms::synti,synth.get());
    auto engine=static_cast<VstSynth*>(synth->synthesizer("KumoVST")); QVERIFY(engine);
    QVariantMap descriptor{{"path",qEnvironmentVariable("P0_TEST_SOUNDFONT")},{"format","SF2"},{"uid","0:0"},{"name","Piano"},{"instrument",true}};
    if(cfx) { QString error; const auto values=VstHost::scanVst2("E:/music_fonts/CFX Concert Grand/VST/CFX Concert Grand VST_x64.dll",&error);
        QVERIFY2(!values.isEmpty(),qPrintable(error)); descriptor=values.front().toMap(); }
    auto a=vstScore088(_settings.filePath("session-a-107.mscx")); QVERIFY(a); restoreVstForScore(a);
    auto channelA=const_cast<Channel*>(a->midiMapping().front().articulation());
    QVERIFY(!assignVstInstrument(a,channelA,descriptor).contains("error")); auto first=engine->rack().plugin(channelA->bank()); QVERIFY(first);
    showVstPanel(channelA); auto panel=main->findChild<QDockWidget*>("kumoVstPanel"); QVERIFY(panel); VstPanelCleanup088 cleanup{panel};
    panel->setFloating(true); panel->resize(420,650); main->show(); panel->show(); QTest::qWait(80);
    auto retain=panel->findChild<QCheckBox*>("kumoVstRetainSources"); auto release=panel->findChild<QPushButton*>("kumoVstReleaseInactive");
    auto label=panel->findChild<QLabel*>("kumoVstRetainedState"); QVERIFY(retain&&release&&label); QVERIFY(!retain->isChecked());
    QTest::mouseClick(retain,Qt::LeftButton,Qt::NoModifier,QPoint(7,retain->height()/2)); QVERIFY(retain->isChecked());
    QVERIFY(QSettings().value("kumo/vst/retainOpenScores").toBool()); QVERIFY(main->saveFile());
    auto b=vstScore088(_settings.filePath("session-b-107.mscx")); QVERIFY(b); restoreVstForScore(b);
    auto channelB=const_cast<Channel*>(b->midiMapping().front().articulation());
    QVERIFY(!assignVstInstrument(b,channelB,descriptor).contains("error")); QVERIFY(main->saveFile());
    showVstPanel(channelB); QTRY_COMPARE(engine->rack().retainedSessions().size(),1); QTRY_VERIFY(release->isEnabled());
    QCOMPARE(engine->rack().retainedSessions().front().toMap()["id"].toString(),a->property("kumoVstSessionId").toString());
    main->setCurrentView(0,main->scores().indexOf(a)); QCOMPARE(main->currentScore(),static_cast<Score*>(a)); restoreVstForScore(a); showVstPanel(channelA);
    QCOMPARE(engine->rack().plugin(channelA->bank()),first);
    const auto image=qEnvironmentVariable("P2_SOURCE_SESSION_IMAGE"); if(!image.isEmpty()&&!cfx) { QTest::qWait(100); QVERIFY(panel->grab().save(image)); }
    // Closing the other score releases only its retained graph.
    QPointer<Score> closed=b; b->undoStack()->setClean(); main->closeScore(b); QTRY_VERIFY(closed.isNull());
    QTRY_VERIFY(engine->rack().retainedSessions().isEmpty()); QTRY_VERIFY(!release->isEnabled()); QCOMPARE(engine->rack().plugin(channelA->bank()),first);
    auto c=vstScore088(_settings.filePath("session-c-107.mscx")); QVERIFY(c); restoreVstForScore(c);
    auto channelC=const_cast<Channel*>(c->midiMapping().front().articulation());
    QVERIFY(!assignVstInstrument(c,channelC,descriptor).contains("error")); showVstPanel(channelC); QTRY_VERIFY(release->isEnabled());
    auto active=engine->rack().plugin(channelC->bank()); QTest::mouseClick(release,Qt::LeftButton);
    QVERIFY(engine->rack().retainedSessions().isEmpty()); QCOMPARE(engine->rack().plugin(channelC->bank()),active);
    QTest::mouseClick(retain,Qt::LeftButton,Qt::NoModifier,QPoint(7,retain->height()/2)); QVERIFY(!retain->isChecked());
    QVERIFY(main->saveFile()); main->setCurrentView(0,main->scores().indexOf(a)); restoreVstForScore(a); QVERIFY(main->saveFile());
    std::unique_ptr<MasterScore> loaded(main->readScore(a->fileInfo()->absoluteFilePath())); QVERIFY(loaded);
    QVERIFY(loaded->property("kumoVstSessionId").toString().isEmpty()); QVERIFY(!loaded->synthesizerState().group("KumoVST").empty());
    main->closeScore(c); main->closeScore(a);
#endif
}

void TestPluginHost::sourceSessionEffectTail107_data() {
    QTest::addColumn<bool>("isolated");
    QTest::newRow("in-process") << false;
    QTest::newRow("separate-process") << true;
}
void TestPluginHost::sourceSessionEffectTail107() {
#ifdef KUMO_VST_HOST
    QFETCH(bool,isolated);
    QString error; const auto descriptors=VstHost::scanVst2(effectFile088(),&error); QVERIFY2(!descriptors.isEmpty(),qPrintable(error));
    auto counts=std::make_shared<SessionCounts107>();
    VstHost::Rack rack(false,[counts](const VstHost::Descriptor& d,QString*) { return std::make_unique<SessionInstrument107>(counts,d.uid); });
    auto a=sessionConfiguration107(sessionFake107("A"),"first"); auto rows=a["slots"].toList(); auto slot=rows.front().toMap();
    slot["effects"]=QVariantList{QVariantMap{{"plugin",descriptors.front()},{"isolated",isolated}}}; rows[0]=slot; a["slots"]=rows;
    a["masterEffects"]=QVariantList{QVariantMap{{"plugin",descriptors.front()},{"isolated",isolated}}};
    rack.selectSession("a","A",true); QVERIFY(rack.load(a,44100)); auto effect=rack.plugin(1,0); auto master=rack.plugin(0,0); QVERIFY(effect&&master);
    rack.bindChannels({1}); rack.midi(0,0x90,60,100); float audio[512]{};
    for(int i=0;i<20;++i) { std::fill_n(audio,512,0); rack.mix(256,audio,{}); rack.master(256,audio,{}); if(isolated) QTest::qWait(2); }
    QVERIFY(audio[128]>0);
    const auto before=rack.state();
    rack.selectSession("b","B",true); QVERIFY(rack.load({},44100));
    rack.selectSession("a","A",true); QVERIFY(rack.load(before,44100)); QCOMPARE(rack.plugin(1,0),effect); QCOMPARE(rack.plugin(0,0),master);
    QVERIFY(VstHost::sameConfiguration(rack.state(),before));
    rack.bindChannels({1});
    for(int i=0;i<20;++i) { std::fill_n(audio,512,0); rack.mix(256,audio,{}); rack.master(256,audio,{});
        if(isolated) QTest::qWait(2); for(float value:audio) QCOMPARE(value,0.f); }
#endif
}
