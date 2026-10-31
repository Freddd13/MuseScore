// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
void TestPluginHost::emptyVstReadAndSave156()
{
#ifdef KUMO_VST_HOST
    auto score=identityScore140("空音源读取");QVERIFY(score);
    mscore->setCurrentScoreView(mscore->appendScore(score));
    auto synth=std::unique_ptr<MasterSynthesizer>(synthesizerFactory());
    synth->setSampleRate(44100);
    QScopedValueRollback<MasterSynthesizer*> current(Ms::synti,synth.get());
    auto engine=static_cast<VstSynth*>(synth->synthesizer("KumoVST"));QVERIFY(engine);
    engine->setConfiguration({});
    const int before=score->undoStack()->getCurIdx();
    const int groups=score->synthesizerState().group("KumoVST").size();
    for(int i=0;i<3;++i){
        QVERIFY(engine->configuration().isEmpty());
        QVERIFY(engine->configuration(false).isEmpty());
        saveVstEdits(score,true);
        QCOMPARE(score->undoStack()->getCurIdx(),before);
        QCOMPARE(int(score->synthesizerState().group("KumoVST").size()),groups);
    }
    // Empty effects lists from an existing file stay readable and stable.
    const QVariantMap empty{{"slots",QVariantList()},{"masterEffects",QVariantList()}};
    engine->setConfiguration(empty);
    QCOMPARE(engine->configuration(),empty);
    saveVstEdits(score,true);
    const int stored=score->undoStack()->getCurIdx();
    saveVstEdits(score,true);
    QCOMPARE(score->undoStack()->getCurIdx(),stored);
#else
    QSKIP("VST host disabled");
#endif
}
