// GPL-2.0-or-later. Real loader/renderer regressions; no external SF2 is committed.
#include <QtTest/QtTest>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <algorithm>
#include <memory>
#include <limits>
#include <cstdio>
#include "audio/midi/fluid/fluid.h"
#include "audio/midi/fluid/sfont.h"
#include "audio/midi/event.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include <winioctl.h>
#include <io.h>
#include <psapi.h>
#endif

using namespace FluidS;
using namespace Ms;
namespace {
void word(QByteArray& b, quint16 n) { b.append(char(n)); b.append(char(n >> 8)); }
void dword(QByteArray& b, quint32 n) { word(b, quint16(n)); word(b, quint16(n >> 16)); }
void text(QByteArray& b, const char* s) { QByteArray n(s); n=n.left(20); n.append(QByteArray(20-n.size(),char(0))); b += n; }
QByteArray chunk(const char* id, const QByteArray& data)
      {
      QByteArray b(id, 4); dword(b, quint32(data.size())); b += data;
      if (data.size() & 1) b.append(char(0));
      return b;
      }
QByteArray metadata(quint32 frames)
      {
      QByteArray phdr; text(phdr, "Piano"); word(phdr,0); word(phdr,0); word(phdr,0);
      dword(phdr,0); dword(phdr,0); dword(phdr,0);
      text(phdr,"EOP"); word(phdr,0); word(phdr,0); word(phdr,1);
      dword(phdr,0); dword(phdr,0); dword(phdr,0);
      QByteArray bag; word(bag,0); word(bag,0); word(bag,1); word(bag,0);
      QByteArray pgen; word(pgen,41); word(pgen,0); word(pgen,0); word(pgen,0);
      QByteArray inst; text(inst,"Piano"); word(inst,0); text(inst,"EOI"); word(inst,1);
      QByteArray igen; word(igen,53); word(igen,0); word(igen,0); word(igen,0);
      QByteArray shdr; text(shdr,"sample"); dword(shdr,0); dword(shdr,frames);
      dword(shdr,8); dword(shdr,frames > 16 ? frames-8 : frames); dword(shdr,44100);
      shdr.append(char(60)); shdr.append(char(0)); word(shdr,0); word(shdr,1);
      QByteArray terminal(46,char(0)); shdr += terminal;
      QByteArray pdta("pdta",4);
      pdta += chunk("phdr",phdr) + chunk("pbag",bag) + chunk("pmod",QByteArray(10,char(0)))
            + chunk("pgen",pgen) + chunk("inst",inst) + chunk("ibag",bag)
            + chunk("imod",QByteArray(10,char(0))) + chunk("igen",igen) + chunk("shdr",shdr);
      return chunk("LIST",pdta);
      }
bool fixture(const QString& path, quint32 sampleBytes = 220, quint32 frames = 64)
      {
      QFile f(path);
      if (!f.open(QIODevice::ReadWrite | QIODevice::Truncate)) return false;
#ifdef Q_OS_WIN
      if (sampleBytes >= (quint32(1) << 31)) {
            DWORD returned;
            const HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(f.handle()));
            if (!DeviceIoControl(h, FSCTL_SET_SPARSE, nullptr, 0, nullptr, 0, &returned, nullptr)) return false;
            }
#endif
      QByteArray version; word(version,2); word(version,1);
      QByteArray info = chunk("LIST", QByteArray("INFO",4) + chunk("ifil",version));
      QByteArray pdta = metadata(frames);
      const quint64 total = 12 + quint64(info.size()) + 12 + 8 + sampleBytes + pdta.size();
      if (total - 8 > std::numeric_limits<quint32>::max()) return false;
      QByteArray head("RIFF",4); dword(head,quint32(total-8)); head += "sfbk"; head += info;
      head += "LIST"; dword(head,sampleBytes+12); head += "sdta"; head += "smpl"; dword(head,sampleBytes);
      if (f.write(head) != head.size()) return false;
      QByteArray pcm(128,char(0));
      for (int i=0;i<64;++i) { const short v=short(std::sin(i*0.3)*12000); pcm[2*i]=char(v); pcm[2*i+1]=char(v>>8); }
      if (f.write(pcm) != pcm.size()) return false;
      return f.seek(head.size()+qint64(sampleBytes)) && f.write(pdta)==pdta.size();
      }
Sample* firstSample(SFont& sf)
      {
      auto p = sf.get_preset(0,0);
      return p && !p->zones.isEmpty() && p->zones.front()->instrument
            ? p->zones.front()->instrument->zones.front()->sample : nullptr;
      }
quint64 privateBytes()
      {
#ifdef Q_OS_WIN
      PROCESS_MEMORY_COUNTERS_EX m {}; m.cb=sizeof(m);
      if (GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&m),sizeof(m))) return m.PrivateUsage;
#endif
      return 0;
      }
void render(Fluid& synth, unsigned frames, float* out, float* fx1, float* fx2)
      {
      std::fill(out,out+frames*2,0.0f); std::fill(fx1,fx1+frames*2,0.0f); std::fill(fx2,fx2+frames*2,0.0f);
      synth.process(frames,out,fx1,fx2);
      }
}

class TestSfLoader : public QObject {
      Q_OBJECT
      QTemporaryDir _dir { QDir::currentPath()+"/sfloader-XXXXXX" };
private slots:
      void smallSf2()
            {
            QVERIFY(_dir.isValid()); const QString path=_dir.filePath("small.sf2"); QVERIFY(fixture(path));
            Fluid synth; synth.init(44100); SFont sf(&synth); QVERIFY2(sf.read(path),qPrintable(sf.error()));
            Sample* s=firstSample(sf); QVERIFY(s); QVERIFY(!s->data); QVERIFY(sf.preloadSamples());
            short* data=s->data; QVERIFY(data); QVERIFY(sf.preloadSamples()); QCOMPARE(s->data,data);
            QVERIFY(synth.addSoundFont(path)); synth.play(NPlayEvent(ME_CONTROLLER,0,CTRL_PROGRAM,0));
            synth.play(NPlayEvent(ME_NOTEON,0,60,90));
            float out[1024],a[1024],b[1024]; render(synth,512,out,a,b);
            QVERIFY(std::any_of(out,out+1024,[](float v){return std::abs(v)>0.00001f;}));
            }
      void sparseAbove2GiB()
            {
            const QString path=_dir.filePath("large.sf2"); QVERIFY(fixture(path,0x80000020));
            Fluid synth; synth.init(44100); QVERIFY2(synth.addSoundFont(path),qPrintable(synth.error()));
            auto p=synth.find_preset(0,0); QVERIFY(p); auto s=p->zones.front()->instrument->zones.front()->sample;
            QVERIFY(s->data); QCOMPARE(synth.loadProgress(),100); QVERIFY(synth.removeSoundFont(QFileInfo(path).fileName()));
            }
      void corruptAndFailedAddPreservesExisting()
            {
            const QString good=_dir.filePath("good.sf2"),bad=_dir.filePath("bad.sf2");
            QVERIFY(fixture(good)); QVERIFY(fixture(bad,220,100000));
            Fluid synth; synth.init(44100); QVERIFY(synth.addSoundFont(good));
            const QStringList before=synth.soundFonts(); QVERIFY(!synth.addSoundFont(bad));
            QVERIFY(synth.error().contains("range")); QCOMPARE(synth.soundFonts(),before);
            QFile f(bad); QVERIFY(f.open(QIODevice::ReadWrite)); QVERIFY(f.resize(16)); f.close();
            QVERIFY(!synth.addSoundFont(bad)); QVERIFY(synth.error().contains("bounds") || synth.error().contains("mismatch"));
            QCOMPARE(synth.soundFonts(),before);
            }
      void malformedListLength()
            {
            const QString path=_dir.filePath("length.sf2"); QVERIFY(fixture(path));
            QFile f(path); QVERIFY(f.open(QIODevice::ReadWrite)); QVERIFY(f.seek(16));
            QByteArray n; dword(n,0xffffffff); QCOMPARE(f.write(n),qint64(4)); f.close();
            Fluid synth; synth.init(44100); QVERIFY(!synth.addSoundFont(path)); QVERIFY(!synth.error().isEmpty());
            }
      void cancelAndRetry()
            {
            const QString path=_dir.filePath("cancel.sf2"); QVERIFY(fixture(path));
            Fluid synth; synth.init(44100); SFont sf(&synth); QVERIFY(sf.read(path));
            synth.setLoadWasCanceled(true); QVERIFY(!sf.preloadSamples()); QCOMPARE(sf.error(),QString("Canceled"));
            QVERIFY(!firstSample(sf)->data); synth.setLoadWasCanceled(false); QVERIFY(sf.preloadSamples());
            }
      void shortSampleReadIsRetryable()
            {
            const QString path=_dir.filePath("short.sf2"); QVERIFY(fixture(path));
            Fluid synth; synth.init(44100); SFont sf(&synth); QVERIFY(sf.read(path));
            QFile f(path); QVERIFY(f.open(QIODevice::ReadWrite)); QVERIFY(f.resize(sf.samplePos()+8)); f.close();
            QVERIFY(!sf.preloadSamples()); QVERIFY(!firstSample(sf)->data);
            QVERIFY(fixture(path)); QVERIFY(sf.preloadSamples());
            }
      void existingSf3()
            {
            Fluid synth; synth.init(44100);
            QVERIFY2(synth.addSoundFont(QString(TESTROOT)+"/share/sound/FluidR3Mono_GM.sf3"),qPrintable(synth.error()));
            synth.play(NPlayEvent(ME_CONTROLLER,0,CTRL_PROGRAM,0)); synth.play(NPlayEvent(ME_NOTEON,0,60,90));
            float out[1024],a[1024],b[1024]; render(synth,512,out,a,b);
            QVERIFY(std::any_of(out,out+1024,[](float v){return std::abs(v)>0.00001f;}));
            }
      void allocationFailure()
            {
#ifdef Q_OS_WIN
            const QString path=_dir.filePath("allocation.sf2"); QVERIFY(fixture(path,0x80000020,128*1024*1024));
            Fluid synth; synth.init(44100);
            const QString good=_dir.filePath("before-allocation.sf2"); QVERIFY(fixture(good));
            QVERIFY(synth.addSoundFont(good)); const QStringList previous=synth.soundFonts();
            HANDLE job=CreateJobObjectW(nullptr,nullptr); QVERIFY(job);
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit {};
            limit.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_PROCESS_MEMORY;
            limit.ProcessMemoryLimit=SIZE_T(privateBytes()+64*1024*1024);
            QVERIFY(SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limit,sizeof(limit)));
            QVERIFY(AssignProcessToJobObject(job,GetCurrentProcess()));
            const bool loaded=synth.addSoundFont(path); const QString error=synth.error();
            limit.BasicLimitInformation.LimitFlags=0;
            const bool restored=SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limit,sizeof(limit));
            CloseHandle(job); QVERIFY(restored); QVERIFY(!loaded); QVERIFY2(error.contains("memory"),qPrintable(error));
            QCOMPARE(synth.soundFonts(),previous);
#else
            QSKIP("Windows process memory-limit regression");
#endif
            }
};

// Optional real-file benchmark, kept in the same binary as loader regressions.
// The original SoundFont is read only; results/WAV live in the ignored build tree.
int benchmark(const QStringList& args)
      {
      const QString file=args.value(2); const int seconds=args.value(3,"600").toInt();
      Fluid synth; synth.init(48000); const quint64 before=privateBytes(); QElapsedTimer timer; timer.start();
      if (!synth.addSoundFont(file)) { qCritical()<<synth.error(); return 1; }
      qInfo()<<"PRELOAD_MS"<<timer.elapsed()<<"PRIVATE_DELTA_BYTES"<<qint64(privateBytes()-before);
      synth.play(NPlayEvent(ME_CONTROLLER,0,CTRL_PROGRAM,0));
      float out[1024],a[1024],b[1024];
      QFile wav("sf2-preview.wav"); if (!wav.open(QIODevice::WriteOnly)) return 2;
      QByteArray header("RIFF",4); dword(header,36+48000*4*6); header+="WAVEfmt "; dword(header,16);
      word(header,1);word(header,2);dword(header,48000);dword(header,48000*4);word(header,4);word(header,16);
      header+="data";dword(header,48000*4*6);wav.write(header);
      for (int section=0;section<3;++section) {
            synth.allSoundsOff(-1); synth.play(NPlayEvent(ME_NOTEON,0,section==0?36:section==1?60:84,section==0?40:section==1?85:120));
            for (int pos=0;pos<48000*2;pos+=512) {
                  const int frames=qMin(512,48000*2-pos); render(synth,frames,out,a,b); QByteArray pcm; pcm.reserve(frames*4);
                  for(int i=0;i<frames*2;++i) { if(!std::isfinite(out[i])) return 3; word(pcm,quint16(short(qBound(-32768.0f,out[i]*32767,32767.0f)))); }
                  wav.write(pcm);
                  }
            }
      wav.close();
      for (int keys : {16,32,64}) {
            synth.allSoundsOff(-1); synth.play(NPlayEvent(ME_CONTROLLER,0,64,127));
            for(int k=0;k<keys;++k) synth.play(NPlayEvent(ME_NOTEON,0,24+k,90));
            QVector<qint64> times; times.reserve(2000); int maxVoices=0;
            for(int block=0;block<2000;++block) { QElapsedTimer t; t.start(); render(synth,512,out,a,b); times.append(t.nsecsElapsed()); maxVoices=qMax(maxVoices,synth.activeVoiceCount()); }
            std::sort(times.begin(),times.end()); qInfo()<<"PRESSURE_KEYS"<<keys<<"VOICES"<<maxVoices<<"P99_US"<<times[int(times.size()*0.99)]/1000<<"MAX_US"<<times.back()/1000;
            }
      synth.allSoundsOff(-1); synth.play(NPlayEvent(ME_CONTROLLER,0,64,0));
#ifdef Q_OS_WIN
      IO_COUNTERS ioBefore {},ioAfter {}; GetProcessIoCounters(GetCurrentProcess(),&ioBefore);
#endif
      timer.restart(); qint64 deadline=0,maxNs=0; int late=0,cycle=-1,blocks=0;
      while(timer.elapsed()<seconds*1000) {
            const int next=int(timer.elapsed()/2000);
            if(next!=cycle) { synth.allNotesOff(0); synth.play(NPlayEvent(ME_CONTROLLER,0,64,cycle%2?0:127)); cycle=next;
                  for(int k=0;k<16;++k) synth.play(NPlayEvent(ME_NOTEON,0,36+(cycle*7+k)%48,40+(cycle*13+k*3)%87)); }
            QElapsedTimer t;t.start();render(synth,512,out,a,b);const qint64 elapsed=t.nsecsElapsed();maxNs=qMax(maxNs,elapsed);
            if(elapsed>10666666) ++late;
            for(float v:out) if(!std::isfinite(v)) return 3;
            ++blocks;deadline+=10666666;
            const qint64 remaining=deadline-timer.nsecsElapsed();if(remaining>1000000) QThread::usleep(ulong(remaining/1000));
            if(blocks%2800==0) qInfo()<<"RUN_SECONDS"<<timer.elapsed()/1000<<"MAX_CALLBACK_US"<<maxNs/1000<<"DEADLINE_MISSES"<<late;
            }
#ifdef Q_OS_WIN
      GetProcessIoCounters(GetCurrentProcess(),&ioAfter);
      qInfo()<<"HOT_READ_OPERATIONS"<<ioAfter.ReadOperationCount-ioBefore.ReadOperationCount<<"HOT_READ_BYTES"<<ioAfter.ReadTransferCount-ioBefore.ReadTransferCount;
#endif
      qInfo()<<"SOFTWARE_CALLBACK_DEADLINE_MISSES"<<late<<"MAX_US"<<maxNs/1000<<"SECONDS"<<timer.elapsed()/1000;
      synth.allSoundsOff(-1); const quint64 resident=privateBytes();
      if(!synth.removeSoundFont(QFileInfo(file).fileName())) return 4;
      qInfo()<<"RELEASED_PRIVATE_BYTES"<<qint64(resident-privateBytes());
      for (int repeat = 0; repeat < 2; ++repeat) {
            timer.restart();
            if (!synth.addSoundFont(file)) return 1;
            qInfo()<<"RELOAD_MS"<<timer.elapsed()<<"PRIVATE_BYTES"<<privateBytes();
            synth.allSoundsOff(-1);
            if (!synth.removeSoundFont(QFileInfo(file).fileName())) return 4;
            qInfo()<<"UNLOADED_PRIVATE_BYTES"<<privateBytes();
            }
      return late ? 5 : 0;
      }
int main(int argc,char** argv)
      {
      QCoreApplication app(argc,argv);
      if(app.arguments().value(1)=="--benchmark") {
            // Windows mtests use the GUI subsystem: Qt's default messages go
            // to the debugger rather than redirected stdout/stderr.
            static FILE* report = std::fopen("vsl-benchmark.txt","w");
            if (!report) return 2;
            qInstallMessageHandler([](QtMsgType,const QMessageLogContext&,const QString& message) {
                  const QByteArray text=message.toUtf8();
                  std::fprintf(report,"%s\n",text.constData()); std::fflush(report);
                  });
            const int result=benchmark(app.arguments());
            qInstallMessageHandler(nullptr); std::fclose(report);
            return result;
            }
      TestSfLoader test;return QTest::qExec(&test,argc,argv);
      }
#include "tst_sfloader.moc"
