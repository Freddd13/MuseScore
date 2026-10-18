// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include <QtTest>
#include <QPluginLoader>
#include <QTemporaryDir>
#include <QImage>
#include <QDataStream>
#include <QElapsedTimer>
#include "referenceaudio/spectrumfactory.h"
#include "referenceaudio/auditionfilter.h"
#include <cmath>

class TestSpectrum : public QObject {
    Q_OBJECT
    QTemporaryDir _dir{
        QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", QDir::tempPath())).filePath("spectrum-XXXXXX")};
    QPluginLoader _loader;
    QObject *_session = nullptr;
    QString tone(const QString &name, int rate = 44100, int channels = 1, bool silent = false,
                 double frequency = 440, bool harmonics = false, double startAt = 0, bool pulse = false) {
        const auto path = _dir.filePath(name + ".wav");
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly))
            return {};
        const int frames = rate * (pulse?8:2);
        QDataStream s(&file);
        s.setByteOrder(QDataStream::LittleEndian);
        s.writeRawData("RIFF", 4);
        s << quint32(36 + frames * channels * 2);
        s.writeRawData("WAVEfmt ", 8);
        s << quint32(16) << quint16(1) << quint16(channels) << quint32(rate) << quint32(rate * channels * 2)
          << quint16(channels * 2) << quint16(16);
        s.writeRawData("data", 4);
        s << quint32(frames * channels * 2);
        for (int f = 0; f < frames; ++f) {
            const double phase = 2 * 3.14159265358979323846 * frequency * f / rate;
            const double v =
                silent || f < startAt * rate ? 0
                : harmonics ? .30 * std::sin(phase) + .15 * std::sin(2 * phase) + .10 * std::sin(3 * phase)
                            : .45 * std::sin(phase);
            for (int c = 0; c < channels; ++c)
                s << qint16(v * (pulse && std::fmod(double(f)/rate,.5)>.08 ? 0 : 1) * (c ? -1 : 1) * 32767);
        }
        return path;
    }
    QVariantMap options(const QString &path) const {
        return {{"path", path},  {"from", 200.}, {"until", 1200.},
                {"columns", 96}, {"rows", 352},  {"fftSize", 16384}};
    }
    void request(const QVariantMap &value) {
        QVERIFY(QMetaObject::invokeMethod(_session, "request", Q_ARG(QVariantMap, value)));
    }
    QImage image() const { return qvariant_cast<QImage>(_session->property("image")); }
  private slots:
    void initTestCase() {
        QVERIFY(_dir.isValid());
        _loader.setFileName(qEnvironmentVariable(
            "KUMO_SPECTRUM_MODULE", QCoreApplication::applicationDirPath() + "/kumo_referenceaudio"));
        auto factory = qobject_cast<KumoSpectrumFactory *>(_loader.instance());
        QVERIFY2(factory, qPrintable(_loader.errorString()));
        _session = factory->createSpectrum(this);
        QVERIFY(_session);
        QVERIFY(_session->property("available").toBool());
        QVERIFY(QMetaObject::invokeMethod(_session, "setCacheOptions", Q_ARG(QString, _dir.path()),
                                          Q_ARG(int, 8), Q_ARG(int, 8)));
    }
    void monoPitchAndDiskRestore() {
        auto value = options(tone("a440"));
        QElapsedTimer timer;
        timer.start();
        request(value);
        QTRY_VERIFY_WITH_TIMEOUT(!_session->property("busy").toBool(), 10000);
        auto result = image();
        QVERIFY2(!result.isNull(), qPrintable(_session->property("status").toString()));
        int maximum = -1, row = -1;
        for (int y = 0; y < result.height(); ++y) {
            const int v = result.constScanLine(y)[48];
            if (v > maximum) {
                maximum = v;
                row = y;
            }
        }
        const double pitch = 108.5 - (row + .5) / 352 * 88.;
        QVERIFY(qAbs(pitch - 69) < .6);
        QVERIFY(maximum > 200);
        qInfo() << "STFT mono milliseconds" << timer.elapsed();
        const auto files = QDir(_dir.filePath("kumo-spectrum-v1")).entryList({"*.kspec"}, QDir::Files);
        QCOMPARE(files.size(), 1);
        auto factory = qobject_cast<KumoSpectrumFactory *>(_loader.instance());
        QScopedPointer<QObject> restored(factory->createSpectrum(this));
        QMetaObject::invokeMethod(restored.data(), "setCacheOptions", Q_ARG(QString, _dir.path()),
                                  Q_ARG(int, 8), Q_ARG(int, 8));
        QMetaObject::invokeMethod(restored.data(), "request", Q_ARG(QVariantMap, value));
        QTRY_VERIFY_WITH_TIMEOUT(!restored->property("busy").toBool(), 10000);
        QCOMPARE(qvariant_cast<QImage>(restored->property("image")), result);
    }
    void fixedBlocksReuseAcrossPanAndZoom() {
        auto value=options(tone("fixed-block"));request(value);
        QTRY_VERIFY_WITH_TIMEOUT(!_session->property("busy").toBool(),10000);
        QCOMPARE(_session->property("info").toMap()["computedBlocks"].toInt(),1);
        value["from"]=450.;value["until"]=1800.;value["columns"]=384;request(value);
        QTRY_VERIFY_WITH_TIMEOUT(!_session->property("busy").toBool(),10000);
        auto info=_session->property("info").toMap();QCOMPARE(info["computedBlocks"].toInt(),0);QCOMPARE(info["cachedBlocks"].toInt(),1);QCOMPARE(image().width(),384);
        value["from"]=3900.;value["until"]=4100.;request(value);
        QTRY_VERIFY_WITH_TIMEOUT(!_session->property("busy").toBool(),10000);
        info=_session->property("info").toMap();QCOMPARE(info["computedBlocks"].toInt(),1);QCOMPARE(info["cachedBlocks"].toInt(),1);
        value["refresh"]=true;request(value);QTRY_VERIFY_WITH_TIMEOUT(!_session->property("busy").toBool(),10000);
        info=_session->property("info").toMap();QCOMPARE(info["computedBlocks"].toInt(),2);QCOMPARE(info["cachedBlocks"].toInt(),0);
        qInfo()<<"Fixed block memory KiB"<<_session->property("cacheOptions").toMap()["usedMemoryKiB"]<<"worker ms"<<info["workerMilliseconds"];
    }
    void oppositeStereoAndResampled() {
        auto value = options(tone("stereo", 48000, 2));
        request(value);
        QTRY_VERIFY_WITH_TIMEOUT(!_session->property("busy").toBool(), 10000);
        auto result = image();
        QVERIFY(!result.isNull());
        const int row = qRound((108.5 - 69) / 88. * 352);
        QVERIFY(result.constScanLine(row)[48] > 190);
        QCOMPARE(_session->property("info").toMap().value("sampleRate").toInt(), 48000);
    }

    void decodedFormats_data() {
        QTest::addColumn<QString>("name");
        QTest::newRow("flac") << "a440.flac";
        QTest::newRow("vbr-mp3") << "a440-vbr.mp3";
    }

    void decodedFormats() {
        QFETCH(QString, name);
        const auto folder = qEnvironmentVariable("SPECTRUM_FORMAT_FIXTURES");
        if (folder.isEmpty())
            QSKIP("Set SPECTRUM_FORMAT_FIXTURES to the generated audio fixtures.");
        const auto path = QDir(folder).filePath(name);
        QVERIFY(QFileInfo::exists(path));
        request(options(path));
        QTRY_VERIFY_WITH_TIMEOUT(!_session->property("busy").toBool(), 10000);
        const auto result = image();
        QVERIFY2(!result.isNull(), qPrintable(_session->property("status").toString()));
        const int row = qRound((108.5 - 69) / 88. * 352);
        QVERIFY(result.constScanLine(row)[48] > 170);
    }
    void lowPianoPitch() {
        auto value = options(tone("low-a0", 44100, 1, false, 27.5));
        value["fftSize"] = 32768;
        request(value);
        QTRY_VERIFY_WITH_TIMEOUT(!_session->property("busy").toBool(), 10000);
        const auto result = image();
        QVERIFY(!result.isNull());
        int maximum = -1, row = -1;
        for (int y = 0; y < result.height(); ++y)
            if (result.constScanLine(y)[48] > maximum) {
                maximum = result.constScanLine(y)[48];
                row = y;
            }
        const double pitch = 108.5 - (row + .5) / 352. * 88.;
        QVERIFY(qAbs(pitch - 21.) < .7);
        QVERIFY(maximum > 190);
    }

    void harmonicLocations() {
        request(options(tone("harmonics", 44100, 1, false, 110., true)));
        QTRY_VERIFY_WITH_TIMEOUT(!_session->property("busy").toBool(), 10000);
        const auto result = image();
        QVERIFY(!result.isNull());
        auto brightness = [&](double pitch) {
            return result.constScanLine(qRound((108.5 - pitch) / 88. * 352))[48];
        };
        QVERIFY(brightness(45) > 190);
        QVERIFY(brightness(57) > 170);
        QVERIFY(brightness(64.02) > 150);
        QVERIFY(brightness(53) < 80);
    }

    void fundamentalDisplayUsesExistingAnalysis(){
        auto value=options(tone("fundamental-mix",44100,1,false,440,true));request(value);QTRY_VERIFY_WITH_TIMEOUT(!_session->property("busy").toBool(),10000);const auto original=image();
        value["fundamentalMix"]=1.;request(value);QTRY_VERIFY_WITH_TIMEOUT(!_session->property("busy").toBool(),10000);const auto shaped=image();QCOMPARE(_session->property("info").toMap()["computedBlocks"].toInt(),0);
        auto peak=[](const QImage& image,int pitch){int row=qRound((108.5-pitch)/88.*image.height()),v=0;for(int y=qMax(0,row-1);y<=qMin(image.height()-1,row+1);++y)v=qMax(v,int(image.constScanLine(y)[48]));return v;};
        QVERIFY(peak(shaped,69)>=peak(original,69)-5);QVERIFY(peak(shaped,81)<peak(original,81)-5);
        value["fundamentalMix"]=0.;request(value);QTRY_VERIFY_WITH_TIMEOUT(!_session->property("busy").toBool(),10000);QCOMPARE(image(),original);
    }
    void lowPresetKeepsOnsetTime() {
        auto value = options(tone("step", 44100, 1, false, 440., false, 1.));
        value["from"] = 0.;
        value["until"] = 2000.;
        value["columns"] = 80;
        value["fftSize"] = 32768;
        request(value);
        QTRY_VERIFY_WITH_TIMEOUT(!_session->property("busy").toBool(), 10000);
        const auto result = image();
        QVERIFY(!result.isNull());
        const auto row = result.constScanLine(qRound((108.5 - 69) / 88. * 352));
        int maximum = 0;
        for (int x = 0; x < 80; ++x)
            maximum = qMax(maximum, int(row[x]));
        int crossing = 0;
        while (crossing < 80 && row[crossing] < maximum - 18)
            ++crossing;
        const double milliseconds = (crossing + .5) / 80. * 2000.;
        QVERIFY2(qAbs(milliseconds - 1000.) < 50., qPrintable(QString::number(milliseconds)));
    }

    void silenceAndInvalidSource() {
        auto value = options(tone("silence", 22050, 1, true));
        request(value);
        QTRY_VERIFY_WITH_TIMEOUT(!_session->property("busy").toBool(), 10000);
        auto result = image();
        QVERIFY(!result.isNull());
        for (int y = 0; y < result.height(); ++y)
            for (int x = 0; x < result.width(); ++x)
                QCOMPARE(result.constScanLine(y)[x], uchar(0));
        request(options(_dir.filePath("missing.wav")));
        QTRY_VERIFY_WITH_TIMEOUT(!_session->property("busy").toBool(), 10000);
        QVERIFY(image().isNull());
        QVERIFY(!_session->property("status").toString().contains("已更新"));
    }
    void rapidRequestsPublishLatest() {
        auto value = options(tone("rapid"));
        value["columns"] = 1024;
        value["fftSize"] = 32768;
        request(value);
        value["columns"] = 64;
        for (int i = 0; i < 30; ++i) {
            value["from"] = i * 10.;
            value["until"] = 1000 + i * 10.;
            request(value);
        }
        QTRY_COMPARE_WITH_TIMEOUT(_session->property("info").toMap().value("from").toDouble(), 290., 10000);
        QTRY_VERIFY_WITH_TIMEOUT(!_session->property("busy").toBool(), 10000);
        QVERIFY(!image().isNull());
        QVERIFY(_session->property("cacheOptions").toMap().value("usedMemoryKiB").toInt() <= 8192);
    }
    void cancellationAndBounds() {
        auto value = options(tone("cancel"));
        value["columns"] = 1024;
        value["fftSize"] = 32768;
        request(value);
        QVERIFY(QMetaObject::invokeMethod(_session, "cancel"));
        QTRY_VERIFY_WITH_TIMEOUT(!_session->property("busy").toBool(), 10000);
        value["until"] = 200000.;
        request(value);
        QTRY_VERIFY_WITH_TIMEOUT(!_session->property("busy").toBool(), 10000);
        QVERIFY(image().isNull());
    }
    void pulseTempoAndCancellation(){
        const auto path=tone("pulse-120",44100,1,false,440,false,0,true);QSignalSpy ready(_session,SIGNAL(tempoReady(QVariantMap)));
        QVariantMap request{{"path",path},{"from",0.},{"until",8000.}};
        QVERIFY(QMetaObject::invokeMethod(_session,"estimateTempo",Q_ARG(QVariantMap,request)));QTRY_COMPARE_WITH_TIMEOUT(ready.count(),1,10000);auto result=ready.front()[0].toMap();QVERIFY2(!result.contains("error"),qUtf8Printable(result["error"].toString()));auto candidates=result["candidates"].toList();QVERIFY(!candidates.isEmpty());qInfo()<<"Tempo candidates"<<candidates;QVERIFY(qAbs(candidates.front().toMap()["bpm"].toDouble()-120)<1.);const double phase=std::fmod(candidates.front().toMap()["firstBeat"].toDouble(),500.);QVERIFY(qMin(phase,500-phase)<35.);
        ready.clear();QVERIFY(QMetaObject::invokeMethod(_session,"estimateTempo",Q_ARG(QVariantMap,request)));QVERIFY(QMetaObject::invokeMethod(_session,"cancel"));QTest::qWait(100);QVERIFY(!_session->property("tempoBusy").toBool());QCOMPARE(ready.count(),0);
    }
    void listeningFilterResponse(){
        auto render=[](double frequency,const QVariantMap& options){KumoAudio::AuditionFilter filter(48000,options);QVector<float> samples(96000);for(int i=0;i<48000;++i){const float value=float(.1*std::sin(2*3.14159265358979323846*frequency*i/48000));samples[i*2]=value;samples[i*2+1]=-value;}filter.process(samples.data(),48000);double power=0;for(int i=24000;i<48000;++i){power+=double(samples[i*2])*samples[i*2];if(qAbs(samples[i*2]+samples[i*2+1])>1e-5)return -1.;}return std::sqrt(power/24000);};
        const double original=render(900,{}),boost=render(900,{{"enabled",true},{"middle",6.}});QVERIFY(qAbs(boost/original-std::pow(10.,6./20))<.02);
        const double center=render(440,{{"selectedFrequency",true},{"frequency",440.},{"width",.5}}),outside=render(1760,{{"selectedFrequency",true},{"frequency",440.},{"width",.5}});QVERIFY(qAbs(center/original-1)<.02);QVERIFY(outside<center*.15);
        QVector<float> untouched{.1f,-.1f,.2f,-.2f},copy=untouched;KumoAudio::AuditionFilter bypass(48000,{{"enabled",false},{"middle",12.}});bypass.process(copy.data(),2);QCOMPARE(copy,untouched);
    }
    void cleanupTestCase() {
        delete _session;
        _session = nullptr;
    }
};
QTEST_GUILESS_MAIN(TestSpectrum)
#include "tst_spectrum.moc"
