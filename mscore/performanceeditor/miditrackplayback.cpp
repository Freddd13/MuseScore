// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "miditrackpane.h"
#include "performanceeditor.h"
#include "mscore/seq.h"
#include "mscore/workflow/midicapture.h"
#include "mscore/workflow/miditransport.h"
#include "mscore/workflow/midisources.h"
#include "mscore/workflow/playbackpreview.h"
#include "mscore/workflow/toolicons.h"
#include "mscore/workflow/workflowcontext.h"
#include "mscore/pianotools.h"
#include "mscore/musescore.h"
#include "audio/midi/msynthesizer.h"
#include "libmscore/instrument.h"
#include "libmscore/part.h"
#include "libmscore/staff.h"
#include "libmscore/playbacktiming.h"
#include "libmscore/repeatlist.h"
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QSignalBlocker>
#include <QScrollBar>
#include <QToolButton>
#include <cmath>
#include <algorithm>
namespace Ms
{
namespace
{
int occurrenceTick(MasterScore* score, int tick, int occurrence, bool endpoint = false)
{
    int seen = 0;
    for (const auto segment : score->repeatList())
        if (tick >= segment->tick && (tick < segment->tick + segment->len() ||
                                      (endpoint && tick == segment->tick + segment->len())))
            if (++seen == occurrence)
                return segment->utick + tick - segment->tick;
    int end=0;
    for(const auto segment:score->repeatList())end=qMax(end,segment->tick+segment->len());
    // A reference MIDI can continue beyond the score. Its trailing events keep
    // their time; the joint score session still finishes at its own endpoint.
    if(tick>end||(tick==end&&!endpoint)){
        const int atEnd=occurrenceTick(score,end,occurrence,true);
        if(atEnd>=0)return atEnd+tick-end;
    }
    if(tick<0&&occurrence==1)return 0;
    return -1;
}
int occurrenceAt(MasterScore* score, int utick, int tick)
{
    int seen = 0;
    for (const auto segment : score->repeatList())
        if (tick >= segment->tick && tick < segment->tick + segment->len()) {
            ++seen;
            if (utick >= segment->utick && utick < segment->utick + segment->len())
                return seen;
        }
    return 1;
}
} // namespace
void MidiTrackPane::stopTransport(bool releaseSounds)
{
    if (_playSession)
        workflowContext()->finishPlaybackSession();
    if (_playbackId && midiTrackPlaybackState().value("id").toULongLong() == _playbackId)
        stopMidiTrackPlayback();
    _playbackId = 0;
    if (_monitorId && midiTrackPlaybackState().value("id").toULongLong() == _monitorId)
        stopMidiTrackPlayback();
    _monitorId = 0;
    if (_scorePlayback && _document.score() &&
        playbackPreviewPrepared(_document.score(), _scorePreviewKey))
        stopPlaybackPreview(true);
    _scorePlayback = false;
    _scorePlaybackStarted = false;
    if (releaseSounds) {
        _soundTail.stop();
        if (seq)
            clearMidiTrackSounds(seq->synti(), this);
    } else if (!_closing)
        _soundTail.start(2000);
    if (!_closing)
        updateActions();
}
void MidiTrackPane::registerPlaybackSession(double restartAt, bool followsScore)
{
    auto session = new QObject(this);
    // Ordinary score transport replaces a standalone MIDI session. A joint
    // preview owns its score transport and is stopped by this session instead.
    session->setProperty("kumoMidiTrackSession", !followsScore);
    _playSession = session;
    workflowContext()->setPlaybackSession(session);
    connect(session, &QObject::destroyed, this, [this] {
        if (_closing)
            return;
        if (_playbackId)
            stopMidiTrackPlayback();
        _playbackId = 0;
        if (_scorePlayback && _document.score() &&
            playbackPreviewPrepared(_document.score(), _scorePreviewKey))
            stopPlaybackPreview(true);
        _scorePlayback = false;
        _scorePlaybackStarted = false;
        _soundTail.start(2000);
        updateActions();
    });
    workflowContext()->setPlaybackRewind(session, [this, restartAt] {
        stopTransport();
        _cursorSeconds = restartAt;
        togglePlayback();
    });
}
void MidiTrackPane::releasePreview()
{
    if (_previewPitch >= 0) {
        sendMidiTrackMonitor(_monitorId, NPlayEvent(ME_NOTEON, 0, _previewPitch, 0));
        if (mscore)
            if (auto piano = mscore->findChild<PianoTools*>())
                piano->releasePitch(_previewPitch);
        _soundTail.start(10000);
    }
    _previewPitch = _previewChannel = -1;
    _previewScore.clear();
}
bool MidiTrackPane::prepareMonitor()
{
    if (!_document.score() || !seq || !seq->isRunning() || !seq->synti())
        return false;
    if (_monitorId && midiTrackPlaybackState().value("id").toULongLong() == _monitorId)
        return true;
    auto track = _document.track(_activeId);
    if (track.isEmpty())
        return false;
    track["mute"] = false;
    track["solo"] = true;
    const auto sounds = prepareMidiTrackSounds(seq->synti(), _document.score(), {track}, this);
    if (!sounds.error.isEmpty() || sounds.sounds.isEmpty()) {
        showMessage(sounds.error);
        return false;
    }
    _monitorSound = sounds.sounds.first();
    _monitorChannel = int(_document.score()->midiMapping().size());
    _monitorRate = qRound(seq->synti()->sampleRate());
    _monitorId = startMidiTrackMonitor(_monitorSound, _monitorChannel, _monitorRate);
    _soundTail.stop();
    return _monitorId != 0;
}
void MidiTrackPane::togglePlayback()
{
    if(_capture&&_capture->state().value("recording").toBool()){toggleRecording();return;}
    finishGesture(true);
    releasePreview();
    if (_scorePlayback || (_playbackId && midiTrackPlaybackState().value("requested").toBool())) {
        stopTransport();
        showMessage(tr("播放已停止。点时间尺选择下次起点。"));
        return;
    }
    workflowContext()->finishPlaybackSession();
    _playbackId = 0;
    contextChanged();
    if (!_document.score() || !seq || !seq->isRunning() || !seq->synti()) {
        showMessage(tr("声音设备尚未就绪，请在偏好设置中选择输出。"));
        return;
    }
    if (!seq->backgroundRenderingIdle()) {
        showMessage(tr("正在准备声音，完成后可播放。"));
        return;
    }
    stopPlaybackPreview(true);
    if (seq->isPlaying())
        seq->stopWait();
    auto master = _document.score();
    const bool follow = _withScore->isChecked();
    auto tracks = _document.tracks();
    const auto options = _document.state().value("scoreOptions").toMap();
    bool midiSolo = false;
    for (const auto& value : tracks)
        midiSolo |= value.toMap().value("solo").toBool();
    if (options.value("solo").toBool())
        for (auto& value : tracks) {
            auto track = value.toMap();
            track.insert("mute", true);
            value = track;
        }
    _soundTail.stop();
    showMessage(tr("正在准备MIDI音源…"));
    const auto prepared = prepareMidiTrackSounds(seq->synti(), master, tracks, this);
    if (!prepared.error.isEmpty()) {
        showMessage(prepared.error);
        return;
    }
    const auto sounds = prepared.sounds;
    // Rendering prepares the actual repeat and tempo map before converting
    // reference seconds. The same material is then used by score playback.
    PlaybackPreviewMaterial material;
    if (follow)
        material = renderPlaybackPreview(master, master);
    QString mappingError;
    for (auto& value : tracks) {
        auto track = value.toMap();
        if (track.value("mute").toBool() || (midiSolo && !track.value("solo").toBool()))
            continue;
        if (_timeView->currentIndex() == 1) {
            const auto association = _associations.value(track.value("audioId").toString() + ":" +
                                                         track.value("associationId").toString());
            const int occurrence = association.value("occurrence", 1).toInt();
            const bool clip=follow&&!association.isEmpty();
            const int first=association.value("start").toInt(),last=association.value("end").toInt();
            if(clip&&last<=first){mappingError=tr("原曲关联的起止需要调整，请在“对齐…”中重新选择谱段。");continue;}
            auto mapped = [&](double seconds, bool end = false) {
                const int raw=tickForSeconds(track,seconds);
                const int tick=clip?qBound(first,raw,last):raw;
                if (!follow)
                    return master->tempomap()->tick2time(tick);
                const int utick = occurrenceTick(master, tick, occurrence, end);
                if (utick < 0) {
                    mappingError = tr("“%1”的第%2遍对应位置超出了乐谱，请调整关联范围或遍次。")
                                       .arg(track.value("name").toString())
                                       .arg(occurrence);
                    return 0.;
                }
                return PlaybackTiming::time(master, utick);
            };
            QVariantList notes;
            for (const auto& item : track.value("notes").toList()) {
                auto n = item.toMap();
                if(clip&&(tickForSeconds(track,n.value("end").toDouble())<=first||tickForSeconds(track,n.value("start").toDouble())>=last))continue;
                n["start"] = mapped(n.value("start").toDouble());
                n["end"] = mapped(n.value("end").toDouble(), true);
                notes.append(n);
            }
            track["notes"] = notes;
            auto sourceEvents=track.value("events").toList();
            std::stable_sort(sourceEvents.begin(),sourceEvents.end(),[](const QVariant& a,const QVariant& b){return a.toMap().value("seconds").toDouble()<b.toMap().value("seconds").toDouble();});
            QVariantList events;
            for (const auto& item : sourceEvents) {
                auto e = item.toMap();
                if(clip&&tickForSeconds(track,e.value("seconds").toDouble())>last)continue;
                e["seconds"] = mapped(e.value("seconds").toDouble());
                events.append(e);
            }
            track["events"] = events;
            track["offsetMs"] = 0.;
            track["anchors"] = QVariantList();
            value = track;
        }
    }
    if (!mappingError.isEmpty()) {
        showMessage(mappingError);
        return;
    }
    const auto activeTrack = _document.track(_activeId);
    const auto activeAssociation=_associations.value(activeTrack.value("audioId").toString()+":"+activeTrack.value("associationId").toString());
    int startTick = tickForSeconds(activeTrack, _cursorSeconds), occurrence = 1;
    double origin = midiTrackTime(activeTrack, _cursorSeconds);
    if (_timeView->currentIndex() == 1) {
        if (follow) {
            const auto association =
                _associations.value(activeTrack.value("audioId").toString() + ":" +
                                    activeTrack.value("associationId").toString());
            occurrence = association.value("occurrence", 1).toInt();
            if(!association.isEmpty()&&(startTick<association.value("start").toInt()||startTick>=association.value("end").toInt())){
                startTick=association.value("start").toInt();_cursorSeconds=secondsForTick(activeTrack,startTick);
            }
            const int utick = occurrenceTick(master, startTick, occurrence);
            if (utick < 0) {
                showMessage(tr("当前起点不在选定遍次内，请调整播放起点。"));
                return;
            }
            origin = PlaybackTiming::time(master, utick);
        } else
            origin = master->tempomap()->tick2time(startTick);
    } else if (follow) {
        const int utick = master->repeatList().utime2utick(qMax(0., origin));
        startTick = master->repeatList().utick2tick(utick);
        occurrence = occurrenceAt(master, utick, startTick);
    }
    auto plan = buildMidiTrackPlayback(tracks, sounds, int(master->midiMapping().size()),
                                       qRound(seq->synti()->sampleRate()), origin, follow);
    const bool scoreAudible =
        follow && !options.value("mute").toBool() && (!midiSolo || options.value("solo").toBool());
    if (!plan.error.isEmpty() && !scoreAudible) {
        showMessage(plan.error);
        return;
    }
    const bool clipped=follow&&_timeView->currentIndex()==1&&!activeAssociation.isEmpty();
    const int endTick=clipped?activeAssociation.value("end").toInt():_editor->_endTick;
    const QVariantMap playbackRange{{"start", qMin(startTick, qMax(0, endTick - 1))},
                                   {"end", endTick},
                                   {"untilUtick", clipped?occurrenceTick(master,endTick,occurrence,true):master->repeatList().ticks()},
                                    {"occurrence", occurrence},
                                    {"exactStart", true}};
    if (follow) {
        const QString key = "midi-tracks-" + _document.revision();
        _scorePreviewKey = key;
        if (!scoreAudible)
            material.events.clear();
        preparePlaybackPreview(master, key, material, material);
        const auto started = startPlaybackPreview(master, false, playbackRange);
        if (started.contains("error")) {
            showMessage(started.value("error").toString());
            return;
        }
        _scorePlayback = true;
        _scorePlaybackStarted = false;
        _startWait.start();
    }
    if (follow && plan.error.isEmpty()) {
        const auto repeated = plan;
        const double restartAt = _cursorSeconds;
        setPlaybackPreviewRepeat(this, [this, repeated, restartAt, playbackRange] {
            if (!_closing && _document.score() &&
                playbackPreviewPrepared(_document.score(), _scorePreviewKey)) {
                auto range = playbackRange;
                range["loop"] = true;
                const auto started = startPlaybackPreview(_document.score(), false, range);
                if (started.contains("error")) {
                    showMessage(started.value("error").toString());
                    return;
                }
                _scorePlayback = true;
                _playbackId = startMidiTrackPlayback(repeated);
                _scorePlaybackStarted = false;
                _startWait.restart();
                registerPlaybackSession(restartAt, true);
            }
        });
    }
    if (plan.error.isEmpty())
        _playbackId = startMidiTrackPlayback(std::move(plan));
    registerPlaybackSession(follow ? _cursorSeconds : 0, follow);
    _editor->_following = true;
    _editor->_followButton->setChecked(true);
    _editor->_followButton->setText(tr("跟随播放"));
    _poll.start();
    updateActions();
    showMessage(follow ? tr("正在听：乐谱与已开启声音的MIDI轨。再次点击播放或按空格停止。")
                       : tr("正在听：已开启声音的MIDI轨。再次点击播放或按空格停止。"));
}
void MidiTrackPane::toggleRecording()
{
    if (_capture && _capture->state().value("recording").toBool()) {
        _capture->stop();
        updateActions();
        return;
    }
    finishGesture(true);
    if (!_document.score() || _activeId.isEmpty())
        return;
    stopTransport();
    if (!_capture) {
        _capture = new MidiCaptureSession(this);
        connect(_capture, &MidiCaptureSession::changed, this, [this] {
            const auto capture = _capture->state();
            if (!capture.value("recording").toBool() && !capture.value("stopping").toBool() &&
                _monitorId) {
                if (midiTrackPlaybackState().value("id").toULongLong() == _monitorId)
                    stopMidiTrackPlayback();
                _monitorId = 0;
                _soundTail.start(10000);
            }
            updateActions();
        });
        connect(_capture, &MidiCaptureSession::failed, this, [this](const QString& error) {
            showMessage(error);
            updateActions();
        });
        connect(_capture, &MidiCaptureSession::finished, this,
                [this](const QString& path, const QString& reason) {
                    _recordImport = true;
                    showMessage(tr("录制已结束：%1。正在加入音轨…").arg(reason));
                    loadPath(path, false);
                });
    }
    _capture->setInputEnabled(true);
    _recordScore = _document.score();
    _recordId = _activeId;
    _recordAt = _cursorSeconds;
    const auto result = _capture->start(
        {{"endMode", "manual"},
         {"monitor", true},
         {"firstTrack", _source->currentData().toMap().value("staff").toInt() * 4}});
    if (result.contains("error"))
        showMessage(result.value("error").toString());
    else {
        if (!prepareMonitor()) {
            _capture->cancel();
            showMessage(tr("当前MIDI音源尚未准备好，请选择音源后重试。"));
            return;
        }
        _capture->setMonitorSink([this](const NPlayEvent& event) {
            if (_closing || !_enabled || !seq || !_recordScore || !_editor->score() ||
                _editor->score()->masterScore() != _recordScore)
                return;
            if (!sendMidiTrackMonitor(_monitorId, event)) {
                _monitorId = startMidiTrackMonitor(_monitorSound, _monitorChannel, _monitorRate);
                sendMidiTrackMonitor(_monitorId, event);
            }
        });
        showMessage(tr("正在录到“%1”。弹奏后点停止录制，结果会直接显示在卷帘里。")
                        .arg(_document.track(_activeId).value("name").toString()));
        _poll.start();
    }
    updateActions();
}
void MidiTrackPane::poll()
{
    if (_closing || !_editor)
        return;
    collectMidiTrackPlayback();
    if (!_enabled && !_loading.isRunning() &&
        !(_capture && _capture->state().value("recording").toBool())) {
        _poll.stop();
        return;
    }
    contextChanged();
    syncInset();
    const auto score = _editor->_score;
    if (score && score->state() != _musicState) {
        _musicState = score->state();
        finishGesture(false);
        rebuildRows();
    }
    const auto playback = midiTrackPlaybackState();
    if (_playbackId && playback.value("id").toULongLong() == _playbackId) {
        if (playback.value("playing").toBool()) {
            const auto activeTrack = _document.track(_activeId);
            const int previous = tickForSeconds(activeTrack, _cursorSeconds);
            double seconds = playback.value("seconds").toDouble();
            if (_timeView->currentIndex() == 1) {
                const int tick =
                    !score ? 0
                    : _withScore->isChecked()
                        ? score->repeatList().utick2tick(score->repeatList().utime2utick(seconds))
                        : score->tempomap()->time2tick(seconds);
                _cursorSeconds = secondsForTick(_document.track(_activeId), tick);
            } else
                _cursorSeconds = midiTrackTime(_document.track(_activeId), seconds, true);
            const int tick = tickForSeconds(activeTrack, _cursorSeconds),
                      span = _editor->_scroll->pageStep();
            if (_editor->_following && !_gesture &&
                (tick < _editor->_scroll->value() ||
                 tick > _editor->_scroll->value() + span * .85)) {
                _editor->_automaticScroll = true;
                _editor->_scroll->setValue(qMax(0, tick - int(span * .12)));
                _editor->_automaticScroll = false;
            }
            for (QWidget* surface : {static_cast<QWidget*>(_editor->_noteCanvas), _editor->_canvas,
                                     static_cast<QWidget*>(_editor->_ruler)})
                for (int at : {previous, tick})
                    surface->update(
                        QRect(qRound(_editor->xForTick(at, false)) - 3, 0, 7, surface->height()));
        } else if (!playback.value("requested").toBool()) {
            _playbackId = 0;
            if (!_scorePlayback) {
                if (_playSession)
                    workflowContext()->finishPlaybackSession();
                _cursorSeconds = 0;
                _soundTail.start(10000);
                showMessage(tr("MIDI播放结束，可再次播放；点时间尺可另选起点。"));
            }
        }
    }
    if (_playbackId && playback.value("id").toULongLong() != _playbackId) {
        _playbackId = 0;
        if (_playSession)
            workflowContext()->finishPlaybackSession();
    }
    if (_scorePlayback && seq) {
        if (!playbackPreviewPrepared(_document.score(), _scorePreviewKey)) {
            _scorePlayback = false;
            _playbackId = 0;
        } else if (seq->isPlaying())
            _scorePlaybackStarted = true;
        else if (_scorePlaybackStarted && playbackPreviewLoop()) {
            _scorePlaybackStarted = false;
            _startWait.restart();
        } else if ((_scorePlaybackStarted || _startWait.elapsed() > 5000) &&
                   seq->backgroundRenderingIdle()) {
            const bool started = _scorePlaybackStarted;
            stopTransport();
            _soundTail.start(10000);
            if (!started)
                showMessage(tr("乐谱播放尚未开始，请检查当前输出设备后重试。"));
        }
    }
    if (_capture && _capture->state().value("recording").toBool()) {
        const auto capture = _capture->state();
        showMessage(tr("录制中 · %1秒 · %2个音 · %3")
                        .arg(capture.value("seconds").toDouble(), 0, 'f', 1)
                        .arg(capture.value("count").toInt())
                        .arg(capture.value("device").toString()));
    }
    updateActions();
}
} // namespace Ms
