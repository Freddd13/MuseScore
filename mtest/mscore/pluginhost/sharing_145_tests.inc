// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "libmscore/synthesizerstate.h"

namespace {
QByteArray shareReport145(Score* score)
{
    QVariantList notes, midi, groups;
    for (auto segment=score->firstSegment(SegmentType::ChordRest); segment;
         segment=segment->next1(SegmentType::ChordRest))
        for (int track=0; track<score->ntracks(); ++track)
            if (auto element=segment->element(track)) {
                if (!element->isChord()) continue;
                auto chord=toChord(element);
                for (auto note:chord->notes()) {
                    QVariantList events, fingerings;
                    for (const auto& event:note->playEvents())
                        events.append(QVariantMap{{"pitch",event.pitch()},
                            {"on",event.ontime()},{"length",event.len()}});
                    for (auto attached:note->el())
                        if (attached->isFingering())
                            fingerings.append(toFingering(attached)->plainText());
                    notes.append(QVariantMap{{"tick",segment->tick().ticks()},
                        {"track",track},{"duration",chord->actualTicks().ticks()},
                        {"pitch",note->pitch()},{"tpc1",note->tpc1()},{"tpc2",note->tpc2()},
                        {"velocityType",int(note->veloType())},{"velocity",note->veloOffset()},
                        {"events",events},{"fingerings",fingerings}});
                }
            }
    EventMap rendered;
    score->renderMidi(&rendered,false,false,Ms::mscore->synthesizerState());
    for (const auto& row:rendered)
        if (row.second.type()==ME_NOTEON || row.second.type()==ME_NOTEOFF ||
            (row.second.type()==ME_CONTROLLER && row.second.controller()==CTRL_SUSTAIN))
            midi.append(QVariantMap{{"tick",row.first},{"type",row.second.type()},
                {"channel",row.second.channel()},{"a",row.second.dataA()},{"b",row.second.dataB()}});
    const auto state=RegionalVariants::state(score);
    for (auto value:state.value("groups").toList()) {
        auto group=value.toMap(); QVariantList variants;
        for (auto item:group.value("variants").toList()) {
            const auto variant=item.toMap();
            variants.append(QVariantMap{{"id",variant.value("id")},{"name",variant.value("name")}});
        }
        groups.append(QVariantMap{{"id",group.value("id")},{"active",group.value("active")},
            {"main",group.value("main")},{"start",group.value("start")},
            {"end",group.value("end")},{"tracks",group.value("tracks")},{"variants",variants}});
    }
    QVariantMap documents;
    for (const auto& section:{"reference","harmony","transcription","arrangement"})
        documents[section]=score->metaTag(QString("kumo.")+section);
    const auto sound=score->synthesizerState().group("KumoVST");
    return QJsonDocument::fromVariant(QVariantMap{{"notes",notes},{"midi",midi},
        {"pedals",pedalRanges142(score)},{"groups",groups},{"documents",documents},
        {"vst",sound.empty()?QString():sound.front().data}}).toJson(QJsonDocument::Compact);
}
}

void TestPluginHost::userPluginReload145()
{
    QVERIFY(personalFeaturesEnabled());
    QTemporaryDir folder; QVERIFY(folder.isValid());
    const QString path=folder.filePath("HarmonyAssistant_MS3.qml");
    QFile qml(path); QVERIFY(qml.open(QIODevice::WriteOnly));
    qml.write("import QtQuick 2.0\nimport MuseScore 3.0\nMuseScore {\n"
        "menuPath: \"Plugins.User Harmony\"\nversion: \"1.0\"\n"
        "description: \"User copy retained during reload\"\nrequiresScore: false\n"
        "onRun: { Qt.quit(); }\n}\n");
    qml.close();
    QVERIFY2(!retiredPersonalPlugin(path),"A user-owned plugin must survive bundled-entry cleanup");
    QVERIFY(retiredPersonalPlugin(QDir(mscoreGlobalShare).filePath(
        "plugins/HarmonyAssistant/HarmonyAssistant_MS3.qml")));
    QVERIFY(!retiredPersonalPlugin(folder.filePath("plugins-old/ReferencePlayer_MS3.qml")));
    const QString previous=preferences.getString(PREF_APP_PATHS_MYPLUGINS);
    const auto restore=qScopeGuard([&]{preferences.setPreference(PREF_APP_PATHS_MYPLUGINS,previous);});
    preferences.setPreference(PREF_APP_PATHS_MYPLUGINS,folder.path());
    PluginManager manager(Ms::mscore); manager.init(); manager.resize(700,500); manager.show();
    auto description=[&]() -> PluginDescription* {
        for (int i=0;i<manager.pluginCount();++i)
            if (QFileInfo(manager.getPluginDescription(i)->path).absoluteFilePath()==path)
                return manager.getPluginDescription(i);
        return nullptr;
    };
    auto row=[&]() -> QListWidgetItem* {
        for (int i=0;i<manager.pluginListWidget->count();++i) {
            auto item=manager.pluginListWidget->item(i);
            const auto d=manager.getPluginDescription(item->data(Qt::UserRole).toInt());
            if (QFileInfo(d->path).absoluteFilePath()==path) return item;
        }
        return nullptr;
    };
    QVERIFY(description()); QVERIFY(row());
    manager.pluginListWidget->setCurrentItem(row()); manager.pluginListWidget->setFocus();
    QTest::keyClick(manager.pluginListWidget,Qt::Key_Space);
    QVERIFY(description()->load);
    description()->shortcut.setKeys({QKeySequence("Ctrl+Alt+F11")});
    for (int repeat=0; repeat<3; ++repeat) {
        QTimer dismiss; dismiss.setInterval(25);
        connect(&dismiss,&QTimer::timeout,this,[]{
            for (auto widget:QApplication::topLevelWidgets())
                if (auto box=qobject_cast<QMessageBox*>(widget)) box->accept();
        });
        dismiss.start();
        QVERIFY(QMetaObject::invokeMethod(&manager,"reloadPluginsClicked",Qt::DirectConnection));
        QVERIFY(description()); QVERIFY(description()->load); QVERIFY(row());
        QCOMPARE(manager.pluginListWidget->currentItem(),row());
        QCOMPARE(description()->shortcut.keys(),QList<QKeySequence>{QKeySequence("Ctrl+Alt+F11")});
    }
    manager.writePluginList();
    PluginManager reopened(Ms::mscore); QVERIFY(reopened.readPluginList());
    bool found=false;
    for (int i=0;i<reopened.pluginCount();++i) {
        const auto d=reopened.getPluginDescription(i);
        if (QFileInfo(d->path).absoluteFilePath()!=path) continue;
        found=true; QVERIFY(d->load);
        QCOMPARE(d->shortcut.keys(),QList<QKeySequence>{QKeySequence("Ctrl+Alt+F11")});
    }
    QVERIFY(found);
    QVERIFY(manager.grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()))
        .filePath("user-plugin-reload-145.png")));
    if (auto d=description()) Ms::mscore->unregisterPlugin(d);
}

void TestPluginHost::sharePrepare145()
{
    if (!personalFeaturesEnabled()) QSKIP("Prepare with extensions enabled");
    const QString directory=qEnvironmentVariable("P2_GATE_ROUNDTRIP");
    if (directory.isEmpty()) QSKIP("Set an isolated round-trip directory");
    QDir out(directory); QVERIFY(out.mkpath("."));
    const QString source=out.filePath("source.mscx");
    QFile fixture(source); QVERIFY(fixture.open(QIODevice::WriteOnly));
    fixture.write(systemLockFixture138()); fixture.close();
    auto main=Ms::mscore; auto score=main->readScore(source); QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    auto pedal=new Pedal(score); pedal->setTrack(4); pedal->setTrack2(4);
    pedal->setTick(Fraction()); pedal->setTick2(Fraction::fromTicks(3840));
    score->startCmd(); score->undoAddElement(pedal); score->endCmd();
    const auto a=RegionalVariants::create(score,0,3840,{4},"Left hand"); QVERIFY(a.value("ok").toBool());
    const auto group=a.value("group").toString(), first=a.value("variant").toString();
    const auto b=RegionalVariants::duplicate(score,group,"B",true); QVERIFY(b.value("ok").toBool());
    const auto second=b.value("variant").toString();
    auto note=toChord(score->findCR(Fraction(),4))->upNote();
    auto finger=new Fingering(score); finger->setParent(note); finger->setTrack(4); finger->setPlainText("2");
    NoteEventList events; events.clear(); events.append(NoteEvent(0,30,700));
    score->startCmd(); note->undoChangeProperty(Pid::PITCH,50);
    note->undoChangeProperty(Pid::TPC1,16); note->undoChangeProperty(Pid::TPC2,16);
    note->undoChangeProperty(Pid::VELO_TYPE,int(Note::ValueType::USER_VAL));
    note->undoChangeProperty(Pid::VELO_OFFSET,93);
    score->undo(new ChangeNoteEventList(note,events)); score->undoAddElement(finger); score->endCmd();
    PluginAPI::Score wrapped(score); PluginAPI::ScoreObserver observer; observer.setScore(&wrapped);
    for (const auto& section:{"reference","harmony","transcription","arrangement"}) {
        const QVariantMap document{{"schema",1},{"section",section},{"source","external recording.wav"},
            {"start",0},{"end",3840},{"items",QVariantList{QVariantMap{{"tick",480},{"value",64}}}}};
        QVERIFY(observer.setWorkflowData(section,document).value("ok").toBool());
    }
    const auto recording=out.filePath("external recording.wav");
    const QVariantMap association{{"id","sharing-association"},{"name","First two bars"},
        {"active",true},{"start",0},{"end",3840},
        {"from",500},{"until",2500},{"firstTrack",0},{"endTrack",8},{"occurrence",1},
        {"anchors",QVariantList{QVariantMap{{"tick",1920},{"ms",1500}}}}};
    const QVariantMap track{{"id","sharing-track"},{"name","External recording"},
        {"path",recording},{"originalPath",recording},{"relativePath","external recording.wav"},
        {"associations",QVariantList{association}},{"mute",false},{"solo",false},
        {"participate",true},{"volume",.8},{"speed",1.},{"a",500},{"b",2500}};
    const QVariantMap reference{{"schema",2},{"structure",observer.referenceStructureKey()},
        {"tracks",QVariantList{track}},{"selectedTrack",0},{"playbackMode",0},{"volume",.5},{"linked",false}};
    QVERIFY(observer.setWorkflowData("reference",reference).value("ok").toBool());
    auto sound=score->synthesizerState();
    sound.push_back(SynthesizerGroup("KumoVST",{{0,"{\"schema\":1,\"slots\":[],\"retained\":\"saved source settings\"}"}}));
    sound.setIsDefault(false); score->setSynthesizerState(sound);
    QVERIFY(RegionalVariants::select(score,group,first).value("ok").toBool());
    const auto aReport=shareReport145(score);
    QVERIFY(RegionalVariants::select(score,group,second).value("ok").toBool());
    const auto bReport=shareReport145(score); QVERIFY(aReport!=bReport);
    QFile expected(out.filePath("expected.json")); QVERIFY(expected.open(QIODevice::WriteOnly));
    expected.write(QJsonDocument::fromVariant(QVariantMap{{"group",group},{"A",first},{"B",second},
        {"reportA",QString::fromUtf8(aReport)},{"reportB",QString::fromUtf8(bReport)}}).toJson()); expected.close();
    QFileInfo saved(out.filePath("enabled.mscz")); QVERIFY(score->saveCompressedFile(saved,false,false));
    QCOMPARE(shareReport145(score),bReport);
}

void TestPluginHost::shareDisabledRoundTrip145()
{
    if (qEnvironmentVariable("P2_FEATURE_GATE_OFF").isEmpty()) QSKIP("Start with extensions disabled");
    QVERIFY(!personalFeaturesEnabled());
    const QDir out(qEnvironmentVariable("P2_GATE_ROUNDTRIP"));
    QFile expected(out.filePath("expected.json")); QVERIFY(expected.open(QIODevice::ReadOnly));
    const auto values=QJsonDocument::fromJson(expected.readAll()).toVariant().toMap();
    auto main=Ms::mscore; auto score=main->readScore(out.filePath("enabled.mscz")); QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score)); main->resize(1280,850); main->show();
    QCOMPARE(shareReport145(score),values.value("reportB").toString().toUtf8());
    main->showPerformanceEditor(false); QVERIFY(!main->performanceEditor());
    main->showPerformanceEditor(true); QVERIFY(!main->performanceEditor());
    showAIConnection(); QVERIFY(!main->findChild<QDialog*>("kumoAIConnection"));
    QFileInfo saved(out.filePath("disabled-saved.mscz")); QVERIFY(score->saveCompressedFile(saved,false,false));
    QCOMPARE(shareReport145(score),values.value("reportB").toString().toUtf8());
    QVERIFY(main->grab().save(out.filePath("extensions-off-145.png")));
}

void TestPluginHost::shareEnabledRestore145()
{
    if (!personalFeaturesEnabled()) QSKIP("Restore with extensions enabled");
    const QDir out(qEnvironmentVariable("P2_GATE_ROUNDTRIP"));
    QFile expected(out.filePath("expected.json")); QVERIFY(expected.open(QIODevice::ReadOnly));
    const auto values=QJsonDocument::fromJson(expected.readAll()).toVariant().toMap();
    auto main=Ms::mscore; auto score=main->readScore(out.filePath("disabled-saved.mscz")); QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));
    QCOMPARE(shareReport145(score),values.value("reportB").toString().toUtf8());
    QVERIFY(RegionalVariants::select(score,values.value("group").toString(),values.value("A").toString()).value("ok").toBool());
    QCOMPARE(shareReport145(score),values.value("reportA").toString().toUtf8());
    QVERIFY(RegionalVariants::select(score,values.value("group").toString(),values.value("B").toString()).value("ok").toBool());
    QCOMPARE(shareReport145(score),values.value("reportB").toString().toUtf8());
    QVERIFY(score->synthesizerState().group("KumoVST").size()==1);
    showReferencePanel(true);
    QPointer<QmlPlugin> reference;
    for (auto window:QGuiApplication::allWindows())
        if (auto view=qobject_cast<QQuickView*>(window))
            if (auto root=qobject_cast<QmlPlugin*>(view->rootObject()))
                if (root->menuPath()=="Plugins.Reference Player") reference=root;
    QVERIFY(reference);
    auto tracks=[&]{return reference->property("tracks").value<QJSValue>().toVariant().toList();};
    QTRY_COMPARE_WITH_TIMEOUT(tracks().size(),1,5000);
    QCOMPARE(tracks().front().toMap().value("id").toString(),QString("sharing-track"));
    const auto associations=tracks().front().toMap().value("associations").toList();
    QCOMPARE(associations.size(),1);
    QCOMPARE(associations.front().toMap().value("id").toString(),QString("sharing-association"));
    QCOMPARE(associations.front().toMap().value("anchors").toList().size(),1);
    QTRY_VERIFY_WITH_TIMEOUT(tracks().front().toMap().value("missing").toBool(),5000);
}
