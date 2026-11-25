// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "audiocalibration.h"
#include "scoremixstate.h"
#include "documentsettings.h"
#include "featuregate.h"
#include "controlflowlayout.h"
#include "vstpanel.h"
#include "mscore/musescore.h"
#include "mscore/preferences.h"
#include "mscore/seq.h"
#include "mscore/scoreview.h"
#include "mscore/shortcut.h"
#include "audio/midi/msynthesizer.h"
#include "libmscore/instrument.h"
#include "libmscore/part.h"
#include <QtWidgets>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
#include <cmath>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
namespace Ms {
namespace {
class PeakSink final : public QIODevice {
  public:
    double peak=0;bool finite=true;qint64 samples=0;
    qint64 readData(char*,qint64)override{return -1;}
    qint64 writeData(const char* data,qint64 bytes)override {
        const auto values=reinterpret_cast<const float*>(data);
        for(qint64 i=0;i<bytes/qint64(sizeof(float));++i) {
            if(std::isfinite(values[i]))peak=qMax(peak,double(std::abs(values[i])));else finite=false;
        }
        samples+=bytes/sizeof(float);return bytes;
    }
};
QByteArray soundIdentity(Score* score) {
    QVariantList groups,channels;
    for(const auto& group:replaceScoreMix(score->masterScore()->synthesizerState(),synti->mixState())) {
        QVariantList values;for(const auto& value:group)values.append(QVariantList{value.id,value.data});
        groups.append(QVariantMap{{"name",group.name()},{"values",values}});
    }
    for(const auto& mapping:score->masterScore()->midiMapping()) {
        auto channel=mapping.articulation();
        channels.append(QVariantList{channel->volume(),channel->pan(),channel->reverb(),channel->chorus(),channel->mute(),channel->soloMute()});
    }
    return QCryptographicHash::hash(QJsonDocument::fromVariant(QVariantList{groups,channels}).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256);
}
}
bool writeAudioPeak(Score* score,const QString& path)
{
    if(!personalFeaturesEnabled())return false;
    PeakSink sink;QElapsedTimer timer;timer.start();
    const bool ok=mscore->saveAudio(score,&sink,[&](float fraction) {
        if(timer.elapsed()>250){fprintf(stderr,"KUMO_GAIN_PROGRESS=%.4f\n",qBound(0.,double(fraction),1.));fflush(stderr);timer.restart();}
        return true;
    },false);
    const QVariantMap result=!ok?QVariantMap{{"error",QObject::tr("无法测量当前音源，请检查音源及音色是否已准备好。")}}
      :!sink.finite?QVariantMap{{"error",QObject::tr("音源输出含有异常数值，请检查效果器或重新加载音源。")}}
      :sink.peak<=1e-8?QVariantMap{{"error",QObject::tr("没有测到声音。请检查静音、独听及音源输出。")}}
      :QVariantMap{{"peak",sink.peak},{"peakDb",20.*std::log10(sink.peak)},
                   {"seconds",double(sink.samples)/2/preferences.getInt(PREF_EXPORT_AUDIO_SAMPLERATE)}};
    QSaveFile file(path);if(!file.open(QIODevice::WriteOnly))return false;
    file.write(QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact));return file.commit()&&!result.contains("error");
}
AudioCalibrationPanel::AudioCalibrationPanel(QWidget* parent,const QString& program):QWidget(parent),renderer(program.isEmpty()?QCoreApplication::applicationFilePath():program)
{
    setObjectName("audioCalibrationPanel");
    auto layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);layout->setSpacing(6);
    layout->setAlignment(Qt::AlignTop);
    toggle=new QToolButton(this);toggle->setObjectName("audioCalibrationToggle");toggle->setText(tr("自动校准总音量"));
    toggle->setCheckable(true);toggle->setArrowType(Qt::RightArrow);toggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    layout->addWidget(toggle,0,Qt::AlignLeft);
    contents=new QWidget(this);auto body=new QVBoxLayout(contents);body->setContentsMargins(8,0,0,0);body->setSpacing(6);
    body->setAlignment(Qt::AlignTop);
    auto explanation=new QLabel(tr("按当前音源和混音测量全曲，为最响处留出余量。"),contents);explanation->setWordWrap(true);body->addWidget(explanation);
    auto row=new QHBoxLayout;row->addWidget(new QLabel(tr("目标峰值"),contents));
    target=new QDoubleSpinBox(contents);target->setObjectName("audioCalibrationTarget");target->setRange(-24,0);target->setDecimals(1);target->setSuffix(" dBFS");target->setValue(-6);
    target->setToolTip(tr("建议 −6 dBFS，便于保留重音和后续调整的余量。"));row->addWidget(target);row->addStretch();body->addLayout(row);
    auto buttons=new ControlFlowLayout;
    const auto button=[&](const char* name,const QString& text){auto value=new QToolButton(contents);value->setObjectName(name);value->setText(text);buttons->addWidget(value);return value;};
    measure=button("audioCalibrationMeasure",tr("测量全曲"));cancel=button("audioCalibrationCancel",tr("取消"));
    preview=button("audioCalibrationPreview",tr("试听建议"));preview->setCheckable(true);
    stop=button("audioCalibrationStop",tr("停止播放"));stop->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    stop->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    apply=button("audioCalibrationApply",tr("采用建议"));restore=button("audioCalibrationRestore",tr("恢复校准前"));
    stop->setToolTip(tr("停止乐谱试听，回到已采用的总音量。"));
    apply->setToolTip(tr("使用建议的总增益；Ctrl+S 随谱保存，停止播放后可 Ctrl+Z 撤销。"));
    restore->setToolTip(tr("恢复这份谱第一次采用校准前的总音量。"));
    body->addLayout(buttons);
    progress=new QProgressBar(contents);progress->setObjectName("audioCalibrationProgress");progress->setRange(0,100);progress->setTextVisible(false);progress->setFixedHeight(4);body->addWidget(progress);
    message=new QLabel(contents);message->setObjectName("audioCalibrationStatus");message->setWordWrap(true);body->addWidget(message);
    layout->addWidget(contents);contents->hide();cancel->hide();progress->hide();
    connect(toggle,&QToolButton::toggled,this,[this](bool shown){contents->setVisible(shown);toggle->setArrowType(shown?Qt::DownArrow:Qt::RightArrow);DocumentSettings(owner).setValue("audioCalibration/expanded",shown);});
    connect(measure,&QToolButton::clicked,this,&AudioCalibrationPanel::start);
    connect(cancel,&QToolButton::clicked,this,[this]{cancelled=true;process.kill();});
    connect(target,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this]{preview->setChecked(false);updateResult();saveResult();});
    connect(preview,&QToolButton::toggled,this,&AudioCalibrationPanel::previewChanged);
    connect(stop,&QToolButton::clicked,this,[this]{
        if(seq&&(seq->isPlaying()||preview->isChecked())&&mscore->currentScoreView())mscore->currentScoreView()->cmd("play");
        preview->setChecked(false);updateResult();
    });
    connect(apply,&QToolButton::clicked,this,[this]{
        preview->setChecked(false);
        if(!owner||result.isEmpty()||owner->state()!=revision||soundIdentity(owner)!=fingerprint){message->setText(tr("乐谱或声音设置已改变，请重新测量。"));return;}
        if(!result.contains("originalGain"))result["originalGain"]=synti->gain();
        applyScoreMasterGain(owner,proposedGain());revision=owner->state();fingerprint=soundIdentity(owner);saveResult();updateResult();
    });
    connect(restore,&QToolButton::clicked,this,[this]{
        preview->setChecked(false);if(!owner||!result.contains("originalGain"))return;
        applyScoreMasterGain(owner,result.value("originalGain").toFloat());revision=owner->state();fingerprint=soundIdentity(owner);updateResult();
    });
    connect(documentUiState(),&DocumentUiState::scoreChanged,this,[this](Score* score){selectScore(score);});
    connect(&process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus){complete(code);});
    connect(&process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){if(error==QProcess::FailedToStart)complete(-1);});
    connect(&process,&QProcess::readyReadStandardError,this,[this]{
        output+=process.readAllStandardError();int end;
        while((end=output.indexOf('\n'))>=0){const auto line=output.left(end);output.remove(0,end+1);
            if(line.startsWith("KUMO_GAIN_PROGRESS="))progress->setValue(qBound(0,int(line.mid(19).toDouble()*100),99));}
    });
    if(seq){
        connect(seq,&Seq::stopped,this,[this]{preview->setChecked(false);updateResult();});
        connect(seq,&Seq::started,this,[this]{updateResult();});
    }
    if(synti)connect(synti,&MasterSynthesizer::gainChanged,this,[this](float){preview->setChecked(false);if(process.state()==QProcess::NotRunning)updateResult();});
    selectScore(mscore?mscore->currentScore():nullptr);
}
AudioCalibrationPanel::~AudioCalibrationPanel()
{
    preview->setChecked(false);disconnect(&process,nullptr,this,nullptr);
    if(process.state()!=QProcess::NotRunning){process.kill();process.waitForFinished(1000);}
}
void AudioCalibrationPanel::selectScore(Score* score)
{
    preview->setChecked(false);
    if(process.state()!=QProcess::NotRunning){cancelled=true;process.kill();}
    owner=score?score->masterScore():nullptr;result=owner?DocumentSettings(owner).value("audioCalibration/result").toMap():QVariantMap();
    revision=owner?owner->state():ScoreContentState();
    fingerprint=owner&&synti?soundIdentity(owner):QByteArray();
    const auto expected=owner;
    QTimer::singleShot(0,this,[this,expected]{if(owner==expected&&owner&&synti)fingerprint=soundIdentity(owner);});
    {QSignalBlocker blocked(target);target->setValue(result.value("targetDb",-6.).toDouble());}
    const bool expanded=owner&&DocumentSettings(owner).value("audioCalibration/expanded",false).toBool();
    {QSignalBlocker blocked(toggle);toggle->setChecked(expanded);}
    contents->setVisible(expanded);toggle->setArrowType(expanded?Qt::DownArrow:Qt::RightArrow);
    updateResult();
}
void AudioCalibrationPanel::saveResult()
{
    if(owner){result["targetDb"]=target->value();DocumentSettings(owner).setValue("audioCalibration/result",result);}
}
float AudioCalibrationPanel::proposedGain()const
{
    const double peak=result.value("peak").toDouble(),gain=result.value("measuredGain").toDouble();
    return peak>0?float(qBound(0.,gain*std::pow(10.,target->value()/20)/peak,10.)):0;
}
void AudioCalibrationPanel::updateResult()
{
    const bool running=process.state()!=QProcess::NotRunning;
    stop->setEnabled(preview->isChecked()||(seq&&seq->isPlaying()));
    measure->setEnabled(owner&&synti&&!running);cancel->setVisible(running);progress->setVisible(running);
    const bool ready=owner&&synti&&result.contains("peak")&&!running;
    const bool changed=ready&&qAbs(proposedGain()-synti->gain())>1e-6;
    apply->setEnabled(changed);preview->setEnabled(changed||preview->isChecked());restore->setEnabled(owner&&synti&&result.contains("originalGain")&&!running&&qAbs(result.value("originalGain").toFloat()-synti->gain())>1e-6);
    if(running)return;
    if(!owner)message->setText(tr("打开乐谱后，可测量当前音源的全曲输出。"));
    else if(ready){
        const double adjustment=20*std::log10(qMax(1e-8,double(proposedGain()))/qMax(1e-8,result.value("measuredGain").toDouble()));
        const double changeNow=20*std::log10(qMax(1e-8,double(proposedGain()))/qMax(1e-8,double(synti->gain())));
        message->setText(tr("测得峰值 %1 dBFS；建议调节 %2%3 dB\n预计峰值 %4 dBFS · %5").arg(result.value("peakDb").toDouble(),0,'f',1).arg(changeNow>=0?"+":"").arg(changeNow,0,'f',1).arg(result.value("peakDb").toDouble()+adjustment,0,'f',1).arg(preview->isChecked()?tr("正在试听建议音量"):changed?tr("采用后可 Ctrl+Z 撤销"):tr("当前已采用建议增益")));
    } else message->setText(tr("选择目标后点“测量全曲”。测量期间可以继续浏览乐谱。"));
}
void AudioCalibrationPanel::previewChanged(bool on)
{
    if(!synti)return;
    if(on) {
        if(!owner||result.isEmpty()||owner->state()!=revision||soundIdentity(owner)!=fingerprint){QSignalBlocker blocked(preview);preview->setChecked(false);message->setText(tr("乐谱或声音设置已改变，请重新测量。"));return;}
        synti->setPreviewGain(proposedGain());
        if(seq&&!seq->isPlaying()&&mscore->currentScoreView())mscore->currentScoreView()->cmd("play");
        preview->setText(tr("切回原音量"));
    } else {
        synti->clearPreviewGain();preview->setText(tr("试听建议"));
    }
    updateResult();
}
void AudioCalibrationPanel::start()
{
    if(!owner||!synti||process.state()!=QProcess::NotRunning)return;
    preview->setChecked(false);
    if(hasPendingCfxEdits(owner)){message->setText(tr("请先关闭 CFX 音色窗口，待读取当前音色后再测量。"));return;}
    captureScoreMix(owner);saveVstEdits(owner);
    const QString cache=qEnvironmentVariable("KUMO_CACHE_DIR",QDir("E:/").exists()?QString("E:/MuseScoreCache"):QStandardPaths::writableLocation(QStandardPaths::CacheLocation));
    QDir().mkpath(cache+"/calibration");files.reset(new QTemporaryDir(cache+"/calibration/measure-XXXXXX"));
    if(!files->isValid()){message->setText(tr("无法建立测量目录：%1").arg(cache));return;}
    const auto snapshot=files->filePath("score.mscz");
    std::unique_ptr<MasterScore> copy(owner->masterScore()->clone());
    QFileInfo snapshotInfo(snapshot);
    if(!copy->saveCompressedFile(snapshotInfo,false)){message->setText(tr("无法准备测量用的乐谱副本。"));return;}
    const auto profile=files->filePath("settings");QDir().mkpath(profile);
    const auto settingsPath=profile+"/"+QCoreApplication::organizationName()+"/"+QCoreApplication::applicationName()+".ini";
    QDir().mkpath(QFileInfo(settingsPath).absolutePath());
    QSettings settings(settingsPath,QSettings::IniFormat);QSettings current;
    for(const auto& key:current.allKeys())settings.setValue(key,current.value(key));settings.sync();
    jobOwner=owner;revision=owner->state();measuredGain=synti->gain();fingerprint=soundIdentity(owner);cancelled=false;output.clear();
    result.remove("peak");result.remove("peakDb");
    auto environment=QProcessEnvironment::systemEnvironment();environment.insert("QT_QPA_PLATFORM","offscreen");environment.insert("KUMO_MCP_AUTOSTART","0");environment.insert("TEMP",files->path());environment.insert("TMP",files->path());process.setProcessEnvironment(environment);
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args){args->flags|=CREATE_NO_WINDOW|BELOW_NORMAL_PRIORITY_CLASS;});
#endif
    process.setProgram(renderer);
    process.setArguments({"-s","-m","-c",profile,"-o",files->filePath("result.gain-peak.json"),snapshot});
    message->setText(tr("正在后台测量全曲，包括反复、踏板和效果尾音…"));progress->setValue(0);process.start();updateResult();
}
void AudioCalibrationPanel::complete(int code)
{
    updateResult();
    if(jobOwner!=owner)return;
    if(cancelled){message->setText(tr("测量已取消。"));return;}
    QFile file(files?files->filePath("result.gain-peak.json"):QString());QVariantMap measured;
    if(file.open(QIODevice::ReadOnly))measured=QJsonDocument::fromJson(file.readAll()).toVariant().toMap();
    if(code||!measured.contains("peak")){message->setText(measured.value("error",tr("测量未完成，请检查音源后重试。退出码：%1").arg(code)).toString());return;}
    if(!owner||owner->state()!=revision||soundIdentity(owner)!=fingerprint){message->setText(tr("测量期间乐谱或声音设置已改变，请重新测量。"));return;}
    const auto original=result.value("originalGain");result=measured;result["measuredGain"]=measuredGain;
    if(original.isValid())result["originalGain"]=original;saveResult();updateResult();
}
}
