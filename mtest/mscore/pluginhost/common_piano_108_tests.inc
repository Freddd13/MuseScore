// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "mscore/workflow/sharedpiano.h"
#include "libmscore/part.h"
#include "libmscore/instrument.h"
#include "mscore/mixer/mixertrackitem.h"
#include <QCheckBox>
#include <QtEndian>
namespace {
QVariantMap commonFake108(const QString& key,int id) {
    auto value=sessionConfiguration107(sessionFake107("common"),"common")["slots"].toList().front().toMap();
    auto source=value["source"].toMap();source["state"]=QString::fromLatin1(QByteArray("0.5").toBase64());
    value["source"]=source;value["id"]=id;value["commonPiano"]=key;return value;
}
}
void TestPluginHost::commonPianoCfxOrder108() {
#ifdef KUMO_VST_HOST
    auto configuration=[](const QByteArray& xml,const QString& vendor=QString("Garritan")) {
        const auto packed=qCompress(xml);QByteArray chunk("CEGP");quint32 length=qToLittleEndian(quint32(xml.size()));
        chunk.append(reinterpret_cast<const char*>(&length),4);chunk.append(packed.mid(4));
        const auto state=QJsonDocument::fromVariant(QVariantMap{{"chunk",QString::fromLatin1(chunk.toBase64())}}).toJson(QJsonDocument::Compact);
        const QVariantMap unit{{"plugin",QVariantMap{{"format","VST2"},{"vendor",vendor},{"uid","cfx"}}},{"state",QString::fromLatin1(state.toBase64())}};
        return QVariantMap{{"slots",QVariantList{QVariantMap{{"id",1},{"source",unit}}}}};
    };
    const QByteArray room("<Target name=\"room\"><CustomData size=\"0\" adata=\"\"/></Target>");
    const QByteArray impulse("<Target name=\"impulse\"><CustomData size=\"2\" adata=\"IR\"/></Target>");
    const QByteArray begin("<AriaSave productID=\"1016\"><Settings sc=\"1\"/><EffectSlot id=\"0\" sc=\"2\" name=\"Convolution\">");
    const QByteArray end("</EffectSlot></AriaSave>");const auto a=begin+room+impulse+end;
    auto b=begin+impulse+room+end;b.replace("sc=\"2\"","sc=\"90\"");
    QVERIFY(VstHost::sameConfiguration(configuration(a),configuration(b)));
    auto changed=b;changed.replace("adata=\"IR\"","adata=\"CHANGED\"");QVERIFY(!VstHost::sameConfiguration(configuration(a),configuration(changed)));
    auto renamed=b;renamed.replace("name=\"room\"","name=\"another\"");QVERIFY(!VstHost::sameConfiguration(configuration(a),configuration(renamed)));
    auto duplicateA=a;duplicateA.replace("name=\"room\"","name=\"impulse\"");auto duplicateB=b;duplicateB.replace("name=\"room\"","name=\"impulse\"");
    QVERIFY(!VstHost::sameConfiguration(configuration(duplicateA),configuration(duplicateB)));
    QVERIFY(!VstHost::sameConfiguration(configuration(a,"Other vendor"),configuration(b,"Other vendor")));
    const QByteArray one("<AriaSave productID=\"1016\"><EffectSlot id=\"0\" name=\"reverb\"/><EffectSlot id=\"1\" name=\"eq\"/></AriaSave>");
    const QByteArray two("<AriaSave productID=\"1016\"><EffectSlot id=\"1\" name=\"eq\"/><EffectSlot id=\"0\" name=\"reverb\"/></AriaSave>");
    QVERIFY(!VstHost::sameConfiguration(configuration(one),configuration(two)));
#endif
}
void TestPluginHost::commonPianoPool108() {
#ifdef KUMO_VST_HOST
    auto counts=std::make_shared<SessionCounts107>();
    VstHost::Rack rack(false,[counts](const VstHost::Descriptor&,QString*) {
        return std::make_unique<SessionInstrument107>(counts,QString::number(counts->created+1));
    });
    const auto p1=commonFake108("piano-1",16383),p2=commonFake108("piano-2",16382);
    auto a=sessionConfiguration107(sessionFake107("local-a"),"A");auto rows=a["slots"].toList();rows.append(p1);rows.append(p2);a["slots"]=rows;
    rack.selectSession("a","A",true);QVERIFY(rack.load(a,44100));auto first=rack.plugin(16383);auto second=rack.plugin(16382);auto local=rack.plugin(1);
    QVERIFY(first&&second&&local);QVERIFY(first!=second);QCOMPARE(counts->created,3);
    rack.bindChannels({16383,16382,1});rack.midi(0,0x90,60,100);rack.midi(1,0x90,60,100);
    float out[256]{};rack.mix(128,out,{});QCOMPARE(out[0],1.f);
    rack.midi(0,0x80,60,0);std::fill_n(out,256,0);rack.mix(128,out,{});QCOMPARE(out[0],.5f);
    auto b=sessionConfiguration107(sessionFake107("local-b"),"B");rows=b["slots"].toList();rows.append(p1);b["slots"]=rows;
    rack.selectSession("b","B",true);QVERIFY(rack.load(b,44100));QCOMPARE(rack.plugin(16383),first);QCOMPARE(counts->created,4);
    rack.bindChannels({16383,1});const int idle=counts->calls["3"];
    for(int n=0;n<8;++n) { std::fill_n(out,256,0);rack.mix(128,out,{}); }
    QCOMPARE(counts->calls["3"],idle);
    rack.selectSession("a","A",true);QVERIFY(rack.load(a,44100));QCOMPARE(rack.plugin(1),local);QCOMPARE(rack.plugin(16383),first);QCOMPARE(rack.plugin(16382),second);
    for(const auto& row:rack.state()["slots"].toList()) if(row.toMap()["id"].toInt()==1) QVERIFY(!row.toMap().contains("commonPiano"));
    rack.bindChannels({16383,16382,1});rack.midi(0,0x90,60,100);rack.mix(128,out,{});
    // Same score switches to its own source, then back: pooled keys must stop.
    auto own=a;rows=own["slots"].toList();rows.removeLast();rows.removeLast();own["slots"]=rows;
    QVERIFY(rack.load(own,44100));QVERIFY(rack.load(a,44100));QCOMPARE(rack.plugin(16383),first);
    rack.bindChannels({16383,16382,1});std::fill_n(out,256,0);rack.mix(128,out,{});QCOMPARE(out[0],0.f);
    // A saved local slot can occupy a previous common bank number.
    auto moved=p1;moved["id"]=16000;auto changed=own;rows=changed["slots"].toList();rows.append(moved);changed["slots"]=rows;
    QVERIFY(rack.load(changed,44100));QCOMPARE(rack.plugin(16000),first);
    rack.releaseInactiveSessions();QCOMPARE(rack.plugin(16000),first);
    for(const auto& row:rack.retainedSessions()) QVERIFY(!row.toMap()["commonPiano"].toBool());
    rack.releaseCommonPianos();QVERIFY(rack.load({},44100));QVERIFY(rack.retainedSessions().isEmpty());
#endif
}
void TestPluginHost::commonPianoPanel108() {
#ifdef KUMO_VST_HOST
    auto main=Ms::mscore;QVERIFY(main);QVERIFY(!Ms::seq);
    const auto saved=QSettings().value("kumo/vst/commonPiano");
    struct Restore {QVariant value;~Restore(){QSettings().setValue("kumo/vst/commonPiano",value);}} restore{saved};
    QSettings().remove("kumo/vst/commonPiano");
    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());synth->setSampleRate(44100);
    QScopedValueRollback<MasterSynthesizer*> current(Ms::synti,synth.get());
    QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
    auto a=vstScore088(_settings.filePath("common-a-108.mscx"));QVERIFY(a);restoreVstForScore(a);
    auto channel=a->midiMapping().front().articulation();QVERIFY(isPianoChannel(a,channel));
    const QString original=channel->synti();const int bank=channel->bank(),program=channel->program();
    showVstPanel();auto panel=main->findChild<QDockWidget*>("kumoVstPanel");QVERIFY(panel);VstPanelCleanup088 cleanup{panel};
    panel->setFloating(true);panel->resize(430,850);main->show();panel->show();QTest::qWait(80);
    auto common=panel->findChild<QCheckBox*>("kumoVstCommonPianoEnabled");auto own=panel->findChild<QCheckBox*>("kumoVstOwnPiano");QVERIFY(common&&own);
    QVERIFY(!common->isChecked());QTest::mouseClick(common,Qt::LeftButton,Qt::NoModifier,QPoint(7,common->height()/2));QVERIFY(common->isChecked());
    auto engine=static_cast<VstSynth*>(synth->synthesizer("KumoVST"));QVERIFY(engine);const int id=engine->pianoSlot(channel->channel());QVERIFY(id>0);
    auto source=engine->rack().plugin(id);QVERIFY(source);QCOMPARE(channel->synti(),original);QCOMPARE(channel->bank(),bank);QCOMPARE(channel->program(),program);
    const auto descriptors=engine->rack().instruments();QCOMPARE(descriptors.size(),2);QCOMPARE(engine->configuration()["slots"].toList().size(),0);
    // The original Fluid program messages must leave fixed piano banks intact.
    for (const auto& mapping:a->midiMapping()) {
        const auto c=mapping.articulation();
        synth->play(NPlayEvent(ME_CONTROLLER,c->channel(),CTRL_HBANK,0),synth->index(c->synti()));
        synth->play(NPlayEvent(ME_CONTROLLER,c->channel(),CTRL_LBANK,0),synth->index(c->synti()));
    }
    synth->play(NPlayEvent(ME_NOTEON,channel->channel(),60,100),synth->index(original));
    double peak=0;for(int block=0;block<40;++block) { float audio[512]{};synth->process(256,audio);for(float sample:audio) peak=qMax(peak,qAbs(double(sample))); }
    QVERIFY(peak>.00001);const int undoBeforeSave=a->undoStack()->getCurIdx();QVERIFY(main->saveFile());QCOMPARE(a->undoStack()->getCurIdx(),undoBeforeSave);
    {
        std::unique_ptr<MasterScore> stored(main->readScore(a->fileInfo()->absoluteFilePath()));QVERIFY(stored);
        const auto group=stored->synthesizerState().group("KumoVST");QVERIFY(!group.empty());
        const auto data=QJsonDocument::fromJson(group.front().data.toUtf8()).toVariant().toMap();
        QVERIFY(!data["commonPianoSnapshot"].toMap()["instance"].toMap().isEmpty());QVERIFY(data["slots"].toList().isEmpty());
        QCOMPARE(stored->midiMapping().front().articulation()->synti(),original);
    }
    auto b=vstScore088(_settings.filePath("common-b-108.mscx"));QVERIFY(b);
    b->parts().back()->instrument()->setInstrumentId("strings.violin");
    auto violin=const_cast<Channel*>(b->midiMapping().back().articulation());violin->setProgram(40);
    restoreVstForScore(b);showVstPanel();QVERIFY(!isPianoChannel(b,violin));QCOMPARE(engine->pianoSlot(violin->channel()),0);
    QCOMPARE(engine->rack().plugin(engine->pianoSlot(b->midiMapping().front().articulation()->channel())),source);
    main->setCurrentView(0,main->scores().indexOf(a));restoreVstForScore(a);showVstPanel();QCOMPARE(engine->rack().plugin(id),source);
    QTest::mouseClick(own,Qt::LeftButton,Qt::NoModifier,QPoint(7,own->height()/2));QVERIFY(own->isChecked());QCOMPARE(engine->pianoSlot(channel->channel()),0);
    QVERIFY(main->saveFile());std::unique_ptr<MasterScore> loaded(main->readScore(a->fileInfo()->absoluteFilePath()));QVERIFY(loaded);QVERIFY(scoreUsesOwnPiano(loaded.get()));
    EditData edit; a->undoRedo(true,&edit);QVERIFY(!scoreUsesOwnPiano(a));QCOMPARE(engine->rack().plugin(engine->pianoSlot(channel->channel())),source);
    a->undoRedo(false,&edit);QVERIFY(scoreUsesOwnPiano(a));QCOMPARE(engine->pianoSlot(channel->channel()),0);
    a->undoRedo(true,&edit);QVERIFY(!scoreUsesOwnPiano(a));
    showVstPanel();QTest::qWait(100);const auto image=qEnvironmentVariable("P2_COMMON_PIANO_IMAGE");if(!image.isEmpty()) QVERIFY(panel->grab().save(image));
    QTest::mouseClick(common,Qt::LeftButton,Qt::NoModifier,QPoint(7,common->height()/2));QVERIFY(!common->isChecked());QCOMPARE(engine->pianoSlot(channel->channel()),0);
    QVERIFY(engine->rack().retainedSessions().isEmpty());QVERIFY(main->saveFile());
    main->setCurrentView(0,main->scores().indexOf(b));restoreVstForScore(b);QVERIFY(main->saveFile());
    main->closeScore(b);main->closeScore(a);
#endif
}
void TestPluginHost::commonPianoReal108_data() {
    QTest::addColumn<QString>("kind");QTest::addColumn<bool>("isolated");
    QTest::newRow("Pianoteq")<<QString("Pianoteq")<<false;
    QTest::newRow("CFX")<<QString("CFX")<<false;
    QTest::newRow("Pianoteq-separate")<<QString("Pianoteq")<<true;
    QTest::newRow("CFX-separate")<<QString("CFX")<<true;
}
void TestPluginHost::commonPianoFailure108() {
#ifdef KUMO_VST_HOST
    auto main=Ms::mscore;const auto old=QSettings().value("kumo/vst/commonPiano");
    struct Restore {QVariant value;~Restore(){QSettings().setValue("kumo/vst/commonPiano",value);}} restore{old};
    QSettings().remove("kumo/vst/commonPiano");
    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());synth->setSampleRate(44100);
    QScopedValueRollback<MasterSynthesizer*> current(Ms::synti,synth.get());
    QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
    auto score=vstScore088(_settings.filePath("common-missing-108.mscx"));QVERIFY(score);restoreVstForScore(score);
    auto c=score->midiMapping().front().articulation();auto piano=commonPianoFromCurrent(score,c);auto source=piano["source"].toMap();auto plugin=source["plugin"].toMap();
    plugin["path"]=_settings.filePath("missing-soundfont.sf3");source["plugin"]=plugin;piano["source"]=source;
    const auto answer=applyCommonPianoSettings(score,{{"enabled",true},{"instance",piano}});QVERIFY(answer["message"].toString().contains(QString::fromUtf8("本谱音源")));
    auto engine=static_cast<VstSynth*>(synth->synthesizer("KumoVST"));QVERIFY(engine);QCOMPARE(engine->pianoSlot(c->channel()),0);
    QVERIFY(engine->rack().retainedSessions().isEmpty());
    synth->play(NPlayEvent(ME_NOTEON,c->channel(),60,100),synth->index("Fluid"));double peak=0;
    for(int i=0;i<40;++i) {float audio[512]{};synth->process(256,audio);for(float value:audio) peak=qMax(peak,qAbs(double(value)));}QVERIFY(peak>.00001);
    synth->allSoundsOff(-1);QVERIFY(main->saveFile());
    {
        std::unique_ptr<MasterScore> loaded(main->readScore(score->fileInfo()->absoluteFilePath()));QVERIFY(loaded);
        const auto group=loaded->synthesizerState().group("KumoVST");
        if(!group.empty()) QVERIFY(!QJsonDocument::fromJson(group.front().data.toUtf8()).toVariant().toMap().contains("commonPianoSnapshot"));
    }
    showVstPanel();auto panel=main->findChild<QDockWidget*>("kumoVstPanel");QVERIFY(panel);VstPanelCleanup088 cleanup{panel};panel->setFloating(true);panel->resize(430,850);panel->show();QTest::qWait(40);
    auto sf2=panel->findChild<QPushButton*>("kumoVstRemoveSource");auto restoreSource=panel->findChild<QPushButton*>("kumoVstRestoreSource");QVERIFY(sf2&&restoreSource);QVERIFY(sf2->isEnabled());
    QTest::mouseClick(sf2,Qt::LeftButton);QVERIFY(engine->pianoSlot(c->channel())>0);QVERIFY(restoreSource->isEnabled());
    QTest::mouseClick(restoreSource,Qt::LeftButton);QCOMPARE(engine->pianoSlot(c->channel()),0);
    EditData edit;score->undoRedo(true,&edit);QVERIFY(engine->pianoSlot(c->channel())>0);
    auto settings=commonPianoSettings();settings["enabled"]=false;applyCommonPianoSettings(score,settings);QVERIFY(main->saveFile());main->closeScore(score);
#endif
}
void TestPluginHost::commonPianoExtensionOff108() {
#ifdef KUMO_VST_HOST
    if(personalFeaturesEnabled()) QSKIP("Run with P2_FEATURE_GATE_OFF=1");
    auto main=Ms::mscore;const auto old=QSettings().value("kumo/vst/commonPiano");
    struct Restore {QVariant value;~Restore(){QSettings().setValue("kumo/vst/commonPiano",value);}} restore{old};
    QSettings().setValue("kumo/vst/commonPiano",QByteArray("{\"enabled\":true}"));
    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());synth->setSampleRate(44100);
    QScopedValueRollback<MasterSynthesizer*> current(Ms::synti,synth.get());
    QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
    auto score=vstScore088(_settings.filePath("common-off-108.mscx"));QVERIFY(score);restoreVstForScore(score);
    auto engine=static_cast<VstSynth*>(synth->synthesizer("KumoVST"));QVERIFY(engine);QVERIFY(engine->rack().instruments().isEmpty());
    const auto c=score->midiMapping().front().articulation();QCOMPARE(engine->pianoSlot(c->channel()),0);
    QVERIFY(applyCommonPianoSettings(score,{{"enabled",false}}).contains("error"));QVERIFY(commonPianoSettings()["enabled"].toBool());
    showVstPanel();QVERIFY(!main->findChild<QDockWidget*>("kumoVstPanel"));
    synth->play(NPlayEvent(ME_NOTEON,c->channel(),60,100),synth->index("Fluid"));double peak=0;
    for(int i=0;i<40;++i) {float audio[512]{};synth->process(256,audio);for(float value:audio) peak=qMax(peak,qAbs(double(value)));}QVERIFY(peak>.00001);
    synth->allSoundsOff(-1);QVERIFY(main->saveFile());main->closeScore(score);
#endif
}
void TestPluginHost::commonPianoMixer108() {
#ifdef KUMO_VST_HOST
    auto main=Ms::mscore;const auto old=QSettings().value("kumo/vst/commonPiano");
    struct Restore {QVariant value;~Restore(){QSettings().setValue("kumo/vst/commonPiano",value);}} restore{old};
    QSettings().remove("kumo/vst/commonPiano");
    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());synth->setSampleRate(44100);
    QScopedValueRollback<MasterSynthesizer*> current(Ms::synti,synth.get());
    QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
    auto score=vstScore088(_settings.filePath("common-mixer-108.mscx"));QVERIFY(score);restoreVstForScore(score);
    auto part=score->parts().front();auto channel=const_cast<Channel*>(score->midiMapping().front().articulation());
    auto track=std::make_shared<MixerTrackItem>(MixerTrackItem::TrackType::PART,part,part->instrument(),part->instrument()->channel(0));
    {
        MixerDetails details(main);details.setWindowFlags(Qt::Window);details.setTrack(track);main->show();details.show();details.resize(330,560);details.raise();details.activateWindow();QVERIFY(QTest::qWaitForWindowActive(&details));
        auto combo=details.findChild<QComboBox*>("patchCombo");QVERIFY(combo);const int width=details.minimumSizeHint().width();
        auto piano=commonPianoFromCurrent(score,channel);auto source=piano["source"].toMap();auto descriptor=source["plugin"].toMap();
        descriptor["name"]="Grand Piano with a long name for checking the existing mixer layout";source["plugin"]=descriptor;piano["source"]=source;
        QVERIFY(!applyCommonPianoSettings(score,{{"enabled",true},{"instance",piano}}).contains("error"));
        QVERIFY(combo->currentText().startsWith(QString::fromUtf8("共用：")));QVERIFY(details.minimumSizeHint().width()<=width+4);
        int chosen=-1;
        for(int i=0;i<combo->count();++i) {
            const auto patch=static_cast<MidiPatch*>(combo->itemData(i).value<void*>());
            if(patch && patch->synti=="Fluid" && patch->bank==0 && patch->prog==4) {chosen=i;break;}
        }
        QVERIFY(chosen>=0);const int before=score->undoStack()->getCurIdx();
        QSignalSpy activated(combo,SIGNAL(activated(int)));
        QTest::mouseMove(combo,combo->rect().center());QTest::qWait(25);
        QTest::mouseClick(combo,Qt::LeftButton);QTest::qWait(30);auto view=combo->view();view->scrollTo(combo->model()->index(chosen,0));QTest::qWait(25);
        const auto itemRect=view->visualRect(combo->model()->index(chosen,0));
        const auto target=QPoint(12,itemRect.center().y());
        QTest::mouseMove(view->viewport(),target);QTest::qWait(30);
        const auto popupImage=qEnvironmentVariable("P2_COMMON_MIXER_IMAGE");if(!popupImage.isEmpty()) view->window()->grab().save(popupImage+"-popup.png");
        QTest::mouseClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,target);
        QCOMPARE(activated.count(),1);
        QTRY_VERIFY(scoreUsesOwnPiano(score));QCOMPARE(channel->program(),4);QCOMPARE(score->undoStack()->getCurIdx(),before+1);
        EditData edit;score->undoRedo(true,&edit);QVERIFY(!scoreUsesOwnPiano(score));QVERIFY(combo->currentText().startsWith(QString::fromUtf8("共用：")));
        details.hide();setScoreOwnPiano(score,true);details.show();QTest::qWait(25);QVERIFY(!combo->currentText().startsWith(QString::fromUtf8("共用：")));
        score->undoRedo(true,&edit);QVERIFY(combo->currentText().startsWith(QString::fromUtf8("共用：")));
        const auto image=qEnvironmentVariable("P2_COMMON_MIXER_IMAGE");if(!image.isEmpty()) {QTest::qWait(30);QVERIFY(details.grab().save(image));}
        details.setTrack({});
    }
    auto settings=commonPianoSettings();settings["enabled"]=false;applyCommonPianoSettings(score,settings);QVERIFY(main->saveFile());main->closeScore(score);
#endif
}
void TestPluginHost::commonPianoEffectsExport108() {
#ifdef KUMO_VST_HOST
    auto main=Ms::mscore;const auto old=QSettings().value("kumo/vst/commonPiano");
    struct Restore {QVariant value;~Restore(){QSettings().setValue("kumo/vst/commonPiano",value);}} restore{old};
    QSettings().remove("kumo/vst/commonPiano");QSettings().remove("kumo/vst/catalog");
    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());synth->setSampleRate(44100);synth->setEffect(0,0);synth->setEffect(1,0);
    QScopedValueRollback<MasterSynthesizer*> current(Ms::synti,synth.get());
    QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
    auto score=vstScore088(_settings.filePath("common-effects-108.mscx"));QVERIFY(score);restoreVstForScore(score);
    const auto channel=score->midiMapping().front().articulation();
    QVERIFY(!applyCommonPianoSettings(score,{{"enabled",true},{"instance",commonPianoFromCurrent(score,channel)}}).contains("error"));
    const auto midiBefore=_settings.filePath("common-before-108.mid");QVERIFY(main->saveMidi(score,midiBefore));
    showVstPanel(channel);auto panel=main->findChild<QDockWidget*>("kumoVstPanel");QVERIFY(panel);VstPanelCleanup088 cleanup{panel};
    panel->setFloating(true);panel->resize(430,900);main->show();panel->show();QTest::qWait(30);
    QVERIFY(selectFile088(effectFile088(),[&] {QTest::mouseClick(panel->findChild<QPushButton*>("kumoVstScan"),Qt::LeftButton);}));
    auto catalog=panel->findChild<QComboBox*>("kumoVstCatalog");QTRY_VERIFY_WITH_TIMEOUT(catalog->isEnabled(),10000);
    auto add=panel->findChild<QPushButton*>("kumoVstAddEffect");QVERIFY(add->isEnabled());QTest::mouseClick(add,Qt::LeftButton);
    auto chain=panel->findChild<QListWidget*>("kumoVstEffectChain");QVERIFY(chain);QTRY_COMPARE(chain->count(),1);chain->setCurrentRow(0);
    auto engine=static_cast<VstSynth*>(synth->synthesizer("KumoVST"));const int id=engine->pianoSlot(channel->channel());
    auto first=engine->rack().plugin(id);QVERIFY(first);QCOMPARE(engine->configuration()["slots"].toList().size(),0);
    for(const auto& row:engine->rack().instruments()) QCOMPARE(row.toMap()["effects"].toList().size(),1);
    const auto preset=_settings.filePath("common-gain-108.json");
    QVERIFY(selectFile088(preset,[&] {QTest::mouseClick(panel->findChild<QPushButton*>("kumoVstEffectSavePreset"),Qt::LeftButton);}));
    QTest::mouseClick(panel->findChild<QPushButton*>("kumoVstOpenEffect"),Qt::LeftButton);
    auto dialog=panel->findChild<QDialog*>("kumoVstParameters");QVERIFY(dialog&&dialog->isVisible());
    auto amount=dialog->findChild<QDoubleSpinBox*>("kumoVstParameterValue");QVERIFY(amount);auto input=amount->findChild<QLineEdit*>();QVERIFY(input);
    QTest::keyClick(input,Qt::Key_A,Qt::ControlModifier);QTest::keyClicks(input,"0");QTest::keyClick(input,Qt::Key_Return);
    QTRY_VERIFY(qAbs(engine->rack().plugin(id,0)->parameters().front().toMap()["value"].toDouble())<.0001);
    const double muted=peak088(score);QVERIFY(muted>=0&&muted<.000001);QCOMPARE(engine->rack().plugin(id),first);
    QVERIFY(selectFile088(preset,[&] {QTest::mouseClick(panel->findChild<QPushButton*>("kumoVstEffectLoadPreset"),Qt::LeftButton);}));
    QVERIFY(qAbs(engine->rack().plugin(id,0)->parameters().front().toMap()["value"].toDouble()-.5)<.0001);
    const double sounding=peak088(score);QVERIFY(sounding>.001);QCOMPARE(engine->rack().plugin(id),first);
    QTest::mouseClick(add,Qt::LeftButton);QTRY_COMPARE(chain->count(),2);chain->setCurrentRow(1);
    const auto secondKey=engine->rack().effects(id)[1].toMap()["key"].toString();
    QTest::mouseClick(panel->findChild<QPushButton*>("kumoVstEffectUp"),Qt::LeftButton);
    QCOMPARE(engine->rack().effects(id)[0].toMap()["key"].toString(),secondKey);
    QTest::mouseClick(panel->findChild<QPushButton*>("kumoVstRemoveEffect"),Qt::LeftButton);QTRY_COMPARE(chain->count(),1);
    QTest::mouseClick(panel->findChild<QPushButton*>("kumoVstRemoveEffect"),Qt::LeftButton);QTRY_COMPARE(chain->count(),0);
    const auto midiAfter=_settings.filePath("common-after-108.mid");QVERIFY(main->saveMidi(score,midiAfter));QFile a(midiBefore),b(midiAfter);
    QVERIFY(a.open(QIODevice::ReadOnly)&&b.open(QIODevice::ReadOnly));QCOMPARE(a.readAll(),b.readAll());
    QVERIFY(main->saveFile());
    const auto artifacts=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS");
    if(!artifacts.isEmpty()) {QFileInfo file(QDir(artifacts).filePath("common-piano-108.mscz"));QVERIFY(score->saveCompressedFile(file,false,false));}
    const auto file=score->fileInfo()->absoluteFilePath();main->closeScore(score);
    auto reopened=main->readScore(file);QVERIFY(reopened);main->setCurrentScoreView(main->appendScore(reopened));
    restoreVstForScore(reopened);QVERIFY(engine->pianoSlot(reopened->midiMapping().front().articulation()->channel())>0);QVERIFY(peak088(reopened)>.001);
    auto settings=commonPianoSettings();settings["enabled"]=false;applyCommonPianoSettings(reopened,settings);QVERIFY(main->saveFile());main->closeScore(reopened);
    qInfo()<<"Common actual export muted/audible"<<muted<<sounding;
#endif
}
void TestPluginHost::commonPianoReal108() {
#ifdef KUMO_VST_HOST
    if(!qEnvironmentVariableIsSet("P2_COMMON_PIANO_AUDIO")) QSKIP("Enable local piano integration");
    QFETCH(QString,kind);QFETCH(bool,isolated);auto main=Ms::mscore;QVERIFY(main);
    const auto saved=QSettings().value("kumo/vst/commonPiano");
    struct Restore {QVariant value;~Restore(){QSettings().setValue("kumo/vst/commonPiano",value);}} restore{saved};
    QSettings().remove("kumo/vst/commonPiano");
    std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());synth->setSampleRate(44100);synth->setEffect(0,0);synth->setEffect(1,0);
    QScopedValueRollback<MasterSynthesizer*> current(Ms::synti,synth.get());
    QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
    auto a=vstScore088(_settings.filePath("common-real-a-108.mscx"));QVERIFY(a);restoreVstForScore(a);
    auto channel=const_cast<Channel*>(a->midiMapping().front().articulation());
    QString error;const auto list=kind=="CFX"?VstHost::scanVst2("E:/music_fonts/CFX Concert Grand/VST/CFX Concert Grand VST_x64.dll",&error)
        :VstHost::scanVst3("C:/Program Files/Common Files/VST3/Pianoteq 9/Pianoteq 9.vst3",&error);
    QVERIFY2(!list.isEmpty(),qPrintable(error));QVERIFY(!assignVstInstrument(a,channel,list.front().toMap(),isolated).contains("error"));
    auto engine=static_cast<VstSynth*>(synth->synthesizer("KumoVST"));QVERIFY(engine);const int ownBank=channel->bank();auto own=engine->rack().plugin(ownBank);QVERIFY(own);
    if(kind=="CFX") QVERIFY2(own->importPreset("E:/music_fonts/CFX Concert Grand/Presets/com.Plogue.Aria/02- Compact/Player/00_Default.ariax",&error),qPrintable(error));
    saveVstEdits(a);const auto original=engine->configuration();const auto ownPreset=own->presetName();
    auto settings=commonPianoSettings();settings["enabled"]=true;settings["instance"]=commonPianoFromCurrent(a,channel);settings["name"]="Daily piano";
    QElapsedTimer timer;timer.start();QVERIFY(!applyCommonPianoSettings(a,settings).contains("error"));const auto prepareMs=timer.elapsed();
    const int firstId=engine->pianoSlot(channel->channel());const int otherChannel=a->midiMapping().back().articulation()->channel();const int secondId=engine->pianoSlot(otherChannel);
    auto first=engine->rack().plugin(firstId);auto second=engine->rack().plugin(secondId);QVERIFY(first&&second&&first!=second);QVERIFY(!engine->rack().plugin(ownBank));
    QVERIFY(VstHost::sameConfiguration({{"slots",original["slots"]}},{{"slots",engine->configuration()["slots"]}}));
    auto render=[&](int blocks) {double sum=0;for(int n=0;n<blocks;++n) {
        float audio[512]{};synth->process(256,audio);if(isolated) QTest::qWait(3);
        for(float value:audio) {if(!std::isfinite(value)) return -1.;sum+=value*value;}
    }return std::sqrt(sum/(blocks*512));};
    for(int c:{channel->channel(),otherChannel}) {
        synth->play(NPlayEvent(ME_CONTROLLER,c,64,127),synth->index("KumoVST"));synth->play(NPlayEvent(ME_NOTEON,c,60,100),synth->index("KumoVST"));
    }
    const double both=render(120);QVERIFY(both>.00001);synth->allSoundsOff(channel->channel());const double remaining=render(120);QVERIFY(remaining>.00001);
    synth->allSoundsOff(otherChannel);QVERIFY(render(120)>=0);
    showVstPanel(channel);auto panel=main->findChild<QDockWidget*>("kumoVstPanel");QVERIFY(panel);VstPanelCleanup088 cleanup{panel};
    if(!isolated) {
        const auto preset=_settings.filePath(kind+"-common-108.json");
        QVERIFY(selectFile088(preset,[&] {QTest::mouseClick(panel->findChild<QPushButton*>("kumoVstSourceSavePreset"),Qt::LeftButton);}));
        QVERIFY(selectFile088(preset,[&] {QTest::mouseClick(panel->findChild<QPushButton*>("kumoVstSourceLoadPreset"),Qt::LeftButton);}));
        QCOMPARE(engine->rack().plugin(firstId),first);QCOMPARE(engine->rack().plugin(secondId),second);
        QTest::mouseClick(panel->findChild<QPushButton*>("kumoVstRemoveSource"),Qt::LeftButton);
        QCOMPARE(commonPianoSettings()["instance"].toMap()["source"].toMap()["plugin"].toMap()["format"].toString(),QString("SF2"));
        auto restoreSource=panel->findChild<QPushButton*>("kumoVstRestoreSource");QVERIFY(restoreSource->isEnabled());QTest::mouseClick(restoreSource,Qt::LeftButton);
        first=engine->rack().plugin(firstId);second=engine->rack().plugin(secondId);QVERIFY(first&&second&&first!=second);
        QCOMPARE(commonPianoSettings()["instance"].toMap()["source"].toMap()["plugin"].toMap()["format"].toString(),list.front().toMap()["format"].toString());
    }
    auto trace=[&](const QString& stage) {
        qInfo()<<kind<<stage<<"first retained"<<(engine->rack().plugin(firstId)==first)<<"second retained"<<(engine->rack().plugin(secondId)==second);
        const auto folder=qEnvironmentVariable("P2_COMMON_PIANO_DEBUG");if(folder.isEmpty()) return;
        QFile output(QDir(folder).filePath(kind+"-"+QString::number(isolated)+"-"+stage+".json"));
        if(output.open(QIODevice::WriteOnly)) output.write(QJsonDocument::fromVariant(QVariantMap{{"runtime",engine->runtimeConfiguration()},{"settings",commonPianoSettings()}}).toJson());
    };
    QPointer<QWidget> window;
    if(kind=="CFX" && !isolated) {QVERIFY(first->openEditor(panel,&error));window=panel->findChild<QWidget*>("kumoVstPluginEditor");QVERIFY(window&&window->isVisible());
        QTest::qWait(2600);QCOMPARE(engine->rack().plugin(firstId),first);QVERIFY(window&&window->isVisible());
        bool changed=false;
        for(const auto& value:first->parameters()) {const auto parameter=value.toMap();if(parameter["readOnly"].toBool()) continue;
            first->setParameter(parameter["id"].toUInt(),parameter["value"].toDouble()>.5?.25:.75);changed=true;break;}
        QVERIFY(changed);const int before=a->undoStack()->getCurIdx();captureCommonPianoEdits(a,firstId);
        QCOMPARE(a->undoStack()->getCurIdx(),before+1);QCOMPARE(engine->rack().plugin(firstId),first);QCOMPARE(engine->rack().plugin(secondId),second);
        QVERIFY(window&&window->isVisible());EditData edit;a->undoRedo(true,&edit);QCOMPARE(engine->rack().plugin(firstId),first);QVERIFY(window&&window->isVisible());
        a->undoRedo(false,&edit);QCOMPARE(engine->rack().plugin(firstId),first);QVERIFY(window&&window->isVisible());first->closeEditor();
    }
    trace("before-save");QVERIFY(main->saveFile());trace("after-save");
    // Export preparation uses a fresh graph with the same common preset and two independent pianos.
    {
        std::unique_ptr<MasterSynthesizer> offline(synthesizerFactory(true));offline->setSampleRate(44100);
        prepareVstForScore(offline.get(),a);QVERIFY(offline->setState(a->synthesizerState()));routeVstChannels(offline.get(),a);
        auto exportEngine=static_cast<VstSynth*>(offline->synthesizer("KumoVST"));QVERIFY(exportEngine);QCOMPARE(exportEngine->pianoSlot(channel->channel()),firstId);
        QVERIFY(exportEngine->rack().plugin(firstId));QVERIFY(exportEngine->rack().plugin(firstId)!=first);QVERIFY(!exportEngine->rack().plugin(ownBank));
        QCOMPARE(exportEngine->rack().plugin(firstId)->presetName(),first->presetName());
    }
    trace("after-export");auto b=vstScore088(_settings.filePath("common-real-b-108.mscx"));QVERIFY(b);trace("after-open-b");timer.restart();restoreVstForScore(b);const auto returnMs=timer.elapsed();trace("after-restore-b");
    QCOMPARE(engine->rack().plugin(firstId),first);
    QCOMPARE(engine->rack().plugin(secondId),second);
    main->setCurrentView(0,main->scores().indexOf(a));restoreVstForScore(a);QCOMPARE(engine->rack().plugin(firstId),first);
    QVERIFY(!setScoreOwnPiano(a,true).contains("error"));QCOMPARE(engine->pianoSlot(channel->channel()),0);QVERIFY(engine->rack().plugin(ownBank));
    QCOMPARE(engine->rack().plugin(ownBank)->presetName(),ownPreset);
    QVERIFY(VstHost::sameConfiguration({{"slots",original["slots"]}},{{"slots",engine->configuration()["slots"]}}));
    QVERIFY(!setScoreOwnPiano(a,false).contains("error"));QCOMPARE(engine->rack().plugin(firstId),first);QCOMPARE(engine->rack().plugin(secondId),second);
    settings=commonPianoSettings();settings["enabled"]=false;QVERIFY(!applyCommonPianoSettings(a,settings).contains("error"));QCOMPARE(engine->pianoSlot(channel->channel()),0);
    qInfo()<<kind<<"isolated"<<isolated<<"prepare ms"<<prepareMs<<"switch ms"<<returnMs<<"two pianos rms"<<both<<"one piano rms"<<remaining;
    QVERIFY(main->saveFile());main->closeScore(b);main->closeScore(a);
#endif
}
