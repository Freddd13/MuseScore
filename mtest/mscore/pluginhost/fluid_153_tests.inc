// GPL-2.0-or-later. Cold SF3 samples must be ready before the first audio event.
#include "audio/midi/fluid/sfont.h"
namespace {
QSet<FluidS::Sample*> presetSamples153(FluidS::Preset* preset)
{
    QSet<FluidS::Sample*> samples;if(!preset)return samples;
    auto add=[&](FluidS::Instrument* instrument){if(!instrument)return;
        if(instrument->global_zone&&instrument->global_zone->sample)samples.insert(instrument->global_zone->sample);
        for(auto zone:instrument->zones)if(zone->sample)samples.insert(zone->sample);};
    if(preset->global_zone())add(preset->global_zone()->instrument);
    for(auto zone:preset->zones)add(zone->instrument);return samples;
}
int residentSamples153(const QSet<FluidS::Sample*>& samples)
{
    int count=0;for(auto sample:samples)if(sample->data)++count;return count;
}
}
void TestPluginHost::fluidColdSamples153_data()
{
    QTest::addColumn<bool>("existing");QTest::newRow("new-channels")<<false;QTest::newRow("selected-strings")<<true;
}
void TestPluginHost::fluidColdSamples153()
{
    QFETCH(bool,existing);
    MasterSynthesizer synth;auto fluid=new FluidS::Fluid;synth.registerSynthesizer(fluid);synth.setSampleRate(44100);
    // An already selected second timbre must also be ready after replacing fonts.
    if(existing)synth.play(NPlayEvent(ME_CONTROLLER,1,CTRL_PROGRAM,40),0);
    const auto font=qEnvironmentVariable("P0_TEST_SOUNDFONT");QVERIFY(QFileInfo::exists(font));
    QElapsedTimer total,probe;total.start();probe.start();double guiMaximum=0;int guiTicks=0;
    QTimer timer;timer.setInterval(5);connect(&timer,&QTimer::timeout,[&]{guiMaximum=qMax(guiMaximum,probe.nsecsElapsed()/1.e6);probe.restart();++guiTicks;});timer.start();
    auto load=QtConcurrent::run([&]{return fluid->loadSoundFonts({font});});
    QTRY_VERIFY_WITH_TIMEOUT(load.isFinished(),30000);timer.stop();QVERIFY(load.result());const auto loadMs=total.elapsed();
    auto piano=presetSamples153(fluid->find_preset(0,0)),strings=presetSamples153(fluid->find_preset(0,40)),other=presetSamples153(fluid->find_preset(0,16));
    QVERIFY(!piano.isEmpty()&&!strings.isEmpty()&&!other.isEmpty());
    const int pianoBefore=residentSamples153(piano),stringsBefore=residentSamples153(strings),otherBefore=residentSamples153(other);
    QVector<double> callbacks;double peak=0;
    for(int channel=0;channel<(existing?2:1);++channel){
        QElapsedTimer cold;cold.start();synth.play(NPlayEvent(ME_CONTROLLER,channel,CTRL_PROGRAM,channel?40:0),0);
        for(int pitch:{36,48,60,72,84})synth.play(NPlayEvent(ME_NOTEON,channel,pitch,100),0);
        float samples[512]{};synth.process(256,samples);callbacks.append(cold.nsecsElapsed()/1.e6);
        for(float value:samples){QVERIFY(std::isfinite(value));peak=qMax(peak,qAbs(double(value)));}
    }
    qInfo()<<"153 SF3 cold load"<<loadMs<<"GUI ticks"<<guiTicks<<"maximum GUI interval"<<guiMaximum<<"piano resident"<<pianoBefore<<piano.size()<<"strings resident"<<stringsBefore<<strings.size()<<"unused resident"<<otherBefore<<other.size()<<"cold callbacks"<<callbacks<<"peak"<<peak;
    const QDir out(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QFile report(out.filePath(existing?"fluid-cold-153-existing.json":"fluid-cold-153-new.json"));QVERIFY(report.open(QIODevice::WriteOnly));
    QVariantList firstCallbacks;for(auto value:callbacks)firstCallbacks.append(value);
    report.write(QJsonDocument::fromVariant(QVariantMap{{"loadMilliseconds",loadMs},{"guiTicks",guiTicks},{"maximumGuiInterval",guiMaximum},{"pianoSamples",piano.size()},{"pianoPrepared",pianoBefore},{"stringsSamples",strings.size()},{"stringsPrepared",stringsBefore},{"unusedSamples",other.size()},{"unusedPrepared",otherBefore},{"firstCallbacks",firstCallbacks},{"peak",peak}}).toJson());
    QCOMPARE(pianoBefore,piano.size());QCOMPARE(stringsBefore,existing?strings.size():0);QVERIFY(otherBefore<other.size());
    QVERIFY(guiTicks>0);QVERIFY(guiMaximum<150);QVERIFY(peak>.00001);
    for(double callback:callbacks)QVERIFY2(callback<256000./44100.,"SF3 preparation entered the first audio period");
}
void TestPluginHost::fluidCancelPreparation153_data()
{
    QTest::addColumn<bool>("existing");QTest::newRow("new-channels")<<false;QTest::newRow("selected-strings")<<true;
}
void TestPluginHost::fluidCancelPreparation153()
{
    QFETCH(bool,existing);
    MasterSynthesizer synth;auto fluid=new FluidS::Fluid;synth.registerSynthesizer(fluid);synth.setSampleRate(44100);
    if(existing)synth.play(NPlayEvent(ME_CONTROLLER,1,CTRL_PROGRAM,40),0);
    const auto font=qEnvironmentVariable("P0_TEST_SOUNDFONT");
    auto load=QtConcurrent::run([&]{return fluid->loadSoundFonts({font});});
    QTRY_VERIFY_WITH_TIMEOUT(fluid->loadProgress()>5||load.isFinished(),10000);QVERIFY(!load.isFinished());
    QElapsedTimer callback;callback.start();synth.play(NPlayEvent(ME_CONTROLLER,0,CTRL_PROGRAM,0),0);
    const double milliseconds=callback.nsecsElapsed()/1.e6;QVERIFY(milliseconds<256000./44100.);
    fluid->setLoadWasCanceled(true);QTRY_VERIFY_WITH_TIMEOUT(load.isFinished(),10000);QVERIFY(!load.result());
    QVERIFY(fluid->soundFonts().isEmpty());
    fluid->setLoadWasCanceled(false);auto retry=QtConcurrent::run([&]{return fluid->loadSoundFonts({font});});
    QTRY_VERIFY_WITH_TIMEOUT(retry.isFinished(),30000);QVERIFY(retry.result());
    auto piano=presetSamples153(fluid->find_preset(0,0));QVERIFY(!piano.isEmpty());QCOMPARE(residentSamples153(piano),piano.size());
    synth.play(NPlayEvent(ME_CONTROLLER,0,CTRL_PROGRAM,0),0);synth.play(NPlayEvent(ME_NOTEON,0,60,100),0);
    float samples[512]{};synth.process(256,samples);double peak=0;for(float sample:samples)peak=qMax(peak,qAbs(double(sample)));QVERIFY(peak>.00001);
    qInfo()<<"153 font cancel and retry, incoming event"<<milliseconds<<"ms, sample peak"<<peak;
}
