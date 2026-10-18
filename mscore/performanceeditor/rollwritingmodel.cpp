// SPDX-License-Identifier: GPL-2.0-or-later
#include "libmscore/chord.h"
#include "libmscore/input.h"
#include "libmscore/note.h"
#include "libmscore/pitchspelling.h"
#include "libmscore/staff.h"
#include "libmscore/undo.h"
#include "mscore/musescore.h"
#include "mscore/plugin/api/score.h"
#include "mscore/scoreview.h"
#include "mscore/seq.h"
#include "mscore/workflow/arrangement.h"
#include "mscore/workflow/playbackpreview.h"
#include "performanceeditor.h"
#include "rollnotation.h"
#include "rollwriting.h"
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QEventLoop>
#include <QGridLayout>
#include <QHideEvent>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabBar>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidgetAction>
#include <QtConcurrent>
#include <algorithm>

namespace Ms {
namespace {
QString freshId() { return QUuid::createUuid().toString(QUuid::WithoutBraces); }
} // namespace
void RollWritingBar::readScope(const QVariantMap &requested) {
  if (requested.isEmpty())
    _pendingLocations.clear();
  stopAudition();
  cancelGesture();
  _pages.stop();
  if (_cancel)
    *_cancel = true;
  ++_generation;
  _ready = false;
  _hasChanges = false;
  _built.clear();
  _rows.clear();
  _selected.clear();
  _data.clear();
  _measures.clear();
  _tuplets.clear();
  _score = _editor->score();
  if (!_score) {
    setMessage(tr("打开乐谱后选择要写音的小节。"));
    updateActions();
    return;
  }
  PluginAPI::Score wrapped(_score);
  _reader.setScore(&wrapped);
  auto selected = _reader.arrangementState(
      requested.isEmpty() ? QVariantMap() : QVariantMap{{"scope", requested}});
  if (selected.contains("error")) {
    setMessage(selected["error"].toString());
    updateActions();
    return;
  }
  const auto range = selected["scope"].toMap();
  _state = _reader.arrangementState({{"firstMeasure", range["firstMeasure"]},
                                     {"lastMeasure", range["lastMeasure"]}});
  if (_state.contains("error")) {
    setMessage(_state["error"].toString());
    updateActions();
    return;
  }
  auto scope = _state["scope"].toMap();
  scope["firstTrack"] = (range["firstTrack"].toInt() / 4) * 4;
  scope["endTrack"] = ((range["endTrack"].toInt() + 3) / 4) * 4;
  _state["scope"] = scope;
  _revision = _state["snapshotId"].toString();
  _cursor = scope["start"].toInt();
  _cursorTick = _cursor;
  _range->setText(tr("写音：第 %1–%2 小节 · 谱表 %3–%4 · %5")
                      .arg(scope["firstMeasure"].toInt())
                      .arg(scope["lastMeasure"].toInt())
                      .arg(scope["firstTrack"].toInt() / 4 + 1)
                      .arg((scope["endTrack"].toInt() + 3) / 4)
                      .arg(_mode->currentText()));
  _range->setToolTip(_range->text());
  QSignalBlocker block(_track);
  const int previous = _track->currentData().toInt();
  _track->clear();
  for (int t = scope["firstTrack"].toInt(); t < scope["endTrack"].toInt(); ++t)
    _track->addItem(tr("谱表 %1 · 声部 %2").arg(t / 4 + 1).arg(t % 4 + 1), t);
  _track->setCurrentIndex(
      qMax(0, _track->findData(previous >= scope["firstTrack"].toInt()
                                   ? previous
                                   : range["firstTrack"].toInt())));
  setMessage(tr("正在读取第 %1–%2 小节…")
                 .arg(scope["firstMeasure"].toInt())
                 .arg(scope["lastMeasure"].toInt()));
  _pages.start();
  updateActions();
}
void RollWritingBar::readPage() {
  if (!_enabled || !isVisible()) {
    _pages.stop();
    return;
  }
  if (seq && seq->isPlaying()) {
    _pages.stop();
    setMessage(tr("暂停播放后继续读取这段音符。"));
    return;
  }
  if (seq && !seq->backgroundRenderingIdle()) {
    _pages.setInterval(30);
    return;
  }
  _pages.setInterval(0);
  auto page = _reader.arrangementRange({{"snapshotId", _state["snapshotId"]},
                                        {"scope", _state["scope"]},
                                        {"fromTick", _cursor},
                                        {"limit", 64}});
  if (page.contains("error")) {
    _pages.stop();
    setMessage(page["error"].toString());
    updateActions();
    return;
  }
  for (auto name : {"notes", "meters"}) {
    auto values = _data[name].toList();
    values.append(page[name].toList());
    _data[name] = values;
  }
  if (_data["notes"].toList().size() > 16000) {
    _pages.stop();
    setMessage(tr("这段音符较多，选择其中几个小节后继续写音。"));
    return;
  }
  if (page["done"].toBool()) {
    _pages.stop();
    finishRead();
  } else
    _cursor = page["nextTick"].toInt();
}
void RollWritingBar::finishRead() {
  QSet<int> seenMeasures;
  QSet<QString> seenTuplets;
  const auto scope = _state["scope"].toMap();
  for (auto value : _data["meters"].toList()) {
    const auto m = value.toMap();
    const int tick = m["tick"].toInt();
    if (!seenMeasures.contains(tick)) {
      seenMeasures.insert(tick);
      _measures.append(QVariantMap{{"start", tick}, {"end", m["end"]}});
    }
  }
  QMap<QString, int> playedLengths;
  for (auto value : _data["notes"].toList()) {
    const auto note = value.toMap();
    if (note["grace"].toBool() || note["contextOnly"].toBool())
      continue;
    const auto events = note["playEvents"].toList();
    playedLengths[note["logicalId"].toString()] +=
        events.size() == 1 ? note["duration"].toInt() *
                                 events.front().toMap()["length"].toInt() / 1000
                           : note["duration"].toInt();
  }
  for (auto value : _data["notes"].toList()) {
    auto n = value.toMap();
    if (n["contextOnly"].toBool() || n["grace"].toBool())
      continue;
    auto tuplet = n["tuplet"].toMap();
    if (!tuplet.isEmpty()) {
      tuplet["track"] = n["track"];
      const auto key =
          QString("%1:%2").arg(n["track"].toInt()).arg(tuplet["start"].toInt());
      if (!seenTuplets.contains(key)) {
        seenTuplets.insert(key);
        _tuplets.append(tuplet);
      }
    }
    if (n["tieBack"].toBool())
      continue;
    const int start = n["tick"].toInt(),
              end = n.value("logicalEnd", n["end"]).toInt();
    if (start < scope["start"].toInt() || end > scope["end"].toInt()) {
      setMessage(
          tr("有延音连到范围外，请把延音两端一起选中，再点“改用当前选区”。"));
      updateActions();
      return;
    }
    int rawStart = start, rawEnd = end;
    const auto events = n["playEvents"].toList();
    if (n["playEventType"].toInt() == int(PlayEventType::User) &&
        events.size() == 1) {
      rawStart += n["duration"].toInt() *
                  events.front().toMap()["onTime"].toInt() / 1000;
      rawEnd = rawStart +
               playedLengths.value(n["logicalId"].toString(), end - start);
    }
    _rows.append(QVariantMap{
        {"id", n["id"]},
        {"sourceId", n["id"]},
        {"tripletGrid", tuplet.value("ratio").toString() == "3/2"},
        {"rasterQuantum", tuplet.value("duration", 480).toInt() / 3},
        {"track", n["track"]},
        {"pitch", n.value("writtenPitch", n["pitch"])},
        {"tpc1", n["tpc1"]},
        {"tpc2", n["tpc2"]},
        {"start", start},
        {"end", end},
        {"rawStart", rawStart},
        {"rawEnd", rawEnd},
        {"customEventCount",
         n["playEventType"].toInt() == int(PlayEventType::User) ? events.size()
                                                                : 1},
        {"velocity", n["velocity"]}});
  }
  _identity = rollNotationIdentity(_score, scope);
  const auto saved = stored()["drafts"].toMap().value(scopeId()).toMap();
  if (!saved["draft"].toBool() && saved["identity"].toString() == _identity) {
    QMap<QString, QVariantMap> raw;
    auto key = [](const QVariantMap &row) {
      return QString("%1:%2:%3:%4")
          .arg(row["track"].toInt())
          .arg(row["start"].toInt())
          .arg(row["end"].toInt())
          .arg(row["pitch"].toInt());
    };
    for (auto value : saved["notes"].toList())
      raw[key(value.toMap())] = value.toMap();
    for (auto &value : _rows) {
      auto row = value.toMap();
      const auto source = raw.value(key(row));
      if (!source.isEmpty()) {
        row["rawStart"] = source.value("rawStart", row["rawStart"]);
        row["rawEnd"] = source.value("rawEnd", row["rawEnd"]);
        value = row;
      }
    }
  }
  _originalRows = _rows;
  if (_mode->currentIndex() == 1 && saved["draft"].toBool()) {
    if (saved["identity"].toString() == _identity) {
      _hasChanges = true;
      _rows = saved["notes"].toList();
      _tuplets = saved["tuplets"].toList();
      const auto options = saved["settings"].toMap();
      QSignalBlocker autoVoice(_autoVoice), precision(_precision),
          follow(_followDynamics), velocity(_fixedVelocity);
      _grid->setCurrentIndex(
          qMax(0, _grid->findData(options.value("quantum", 120))));
      _autoVoice->setChecked(options.value("suggestVoices", true).toBool());
      _precision->setChecked(options.value("preserveTiming", false).toBool());
      _followDynamics->setChecked(
          options.value("followDynamics", true).toBool());
      _fixedVelocity->setValue(options.value("newVelocity", 80).toInt());
      _fixedVelocity->setEnabled(!_followDynamics->isChecked());
      _locked.clear();
      for (auto track : options["manualLocks"].toList())
        _locked.insert(track.toInt());
      setMessage(tr("已恢复这段草稿。拖动调整，或点“草稿效果”试听。"));
    } else {
      setMessage(tr("这段乐谱已有修改，原草稿仍保留在“草稿…”中；可从现在的谱子"
                    "重新开始。"));
      updateActions();
      return;
    }
  } else
    setMessage(_mode->currentIndex() == 0
                   ? tr("拖空白处画音，拖音符移动，拖右端改长度；Ctrl+"
                        "拖动复制，Delete "
                        "删除选音。松开后写入乐谱。")
                   : tr("拖空白处画音；先听“谱中效果／草稿效果”，满意后点“使用"
                        "草稿”。"));
  for (auto value : _rows) {
    const auto row = value.toMap();
    for (auto location : _pendingLocations) {
      const auto at = location.toMap();
      if (row["start"] == at["start"] && row["track"] == at["track"] &&
          row["pitch"] == at["pitch"])
        _selected.insert(row["id"].toString());
    }
  }
  _pendingLocations.clear();
  selectSourceNotes();
  _indexDirty = true;
  _ready = true;
  _revision = _reader.analysisContentRevision();
  _savedText = _score->masterScore()->metaTag("kumo.rollWriting");
  updateActions();
  _editor->updateSurfaces();
  if (_mode->currentIndex() == 1 && _hasChanges)
    QTimer::singleShot(0, this, [this] {
      if (_enabled && _ready && !_busy)
        calculate(true);
    });
}
void RollWritingBar::contextChanged() {
  if (_closing || !_enabled || !isVisible() || _internal ||
      _editor->property("kumoEditorClosing").toBool())
    return;
  if (_editor->score() != _score) {
    readScope();
    return;
  }
  if (!_score || !_ready || _gesture || _busy)
    return;
  const auto text = _score->masterScore()->metaTag("kumo.rollWriting");
  if (text != _savedText) {
    _savedText = text;
    readScope(_state["scope"].toMap());
    return;
  }
  const auto revision = _reader.analysisContentRevision();
  if (revision == _revision)
    return;
  _revision = revision;
  if (rollNotationIdentity(_score, _state["scope"].toMap()) != _identity) {
    if (_mode->currentIndex() == 0)
      QTimer::singleShot(0, this, [this] {
        if (_enabled && !_gesture && !_busy)
          readScope(_state["scope"].toMap());
      });
    else {
      _ready = false;
      setMessage(tr("乐谱已修改，草稿已保留。点“改用当前选区”重新读取，或从谱子"
                    "重新开始。"));
      updateActions();
    }
  } else
    _state["snapshotId"] = _revision;
}
void RollWritingBar::calculate(bool refreshOnly) {
  if (!_ready || _busy)
    return;
  stopAudition();
  _busy = true;
  _refreshOnly = refreshOnly;
  _beforeHasChanges = _hasChanges;
  _previousBuilt = _built;
  _built.clear();
  _jobGeneration = _generation;
  _cancel = std::make_shared<std::atomic_bool>(false);
  const auto cancel = _cancel;
  const auto rows = _rows, previous = _originalRows, measures = _measures,
             tuplets = _tuplets;
  const auto scope = _state["scope"].toMap(), options = settings();
  const bool local = _mode->currentIndex() == 0;
  setMessage(tr("正在整理这次输入的时值和声部…"));
  updateActions();
  _calculation.setFuture(QtConcurrent::run(
      [rows, previous, scope, measures, tuplets, options, local, cancel] {
        return buildRollEdit(rows, previous, scope, measures, tuplets, options,
                             local, cancel.get());
      }));
}
void RollWritingBar::calculated() {
  _busy = false;
  if (_jobGeneration != _generation || !_enabled) {
    updateActions();
    return;
  }
  _built = _calculation.result();
  if (_built.value("cancelled").toBool()) {
    updateActions();
    return;
  }
  if (_built.contains("error")) {
    const auto message = _built["error"].toString();
    if (_refreshOnly) {
      setMessage(message);
      updateActions();
      return;
    }
    _built = _previousBuilt;
    _hasChanges = _beforeHasChanges;
    _rows = _before;
    _selected = _beforeSelected;
    _indexDirty = true;
    setMessage(message);
    _editor->updateSurfaces();
    updateActions();
    return;
  }
  _hasChanges = !_built["changedTracks"].toList().isEmpty();
  if (_built["unchanged"].toBool()) {
    setMessage(tr("这些音已经是当前写法。"));
    updateActions();
    return;
  }
  if (_reader.analysisContentRevision() != _state["snapshotId"].toString()) {
    if (rollNotationIdentity(_score, _state["scope"].toMap()) != _identity) {
      readScope(_state["scope"].toMap());
      setMessage(tr("乐谱刚有修改，已重新读取这一段，请重试刚才的拖动。"));
      return;
    }
    _state["snapshotId"] = _reader.analysisContentRevision();
  }
  QMap<QString, QVariantMap> updated;
  for (auto value : _built["notes"].toList()) {
    const auto row = value.toMap();
    if (!row["contextOnly"].toBool())
      updated[row["id"].toString()] = row;
  }
  for (auto &value : _rows) {
    const auto id = value.toMap()["id"].toString();
    if (updated.contains(id))
      value = updated[id];
  }
  _indexDirty = true;
  if (_refreshOnly) {
    setMessage(tr("草稿已准备好，可以比较试听或使用。"));
    updateActions();
    _editor->updateSurfaces();
    return;
  }
  _internal = true;
  if (_mode->currentIndex() == 0) {
    _pendingLocations.clear();
    for (auto value : _rows) {
      const auto row = value.toMap();
      if (_selected.contains(row["id"].toString()))
        _pendingLocations.append(QVariantMap{{"start", row["start"]},
                                             {"track", row["track"]},
                                             {"pitch", row["pitch"]}});
    }
    auto request = _built;
    request["snapshotId"] = _state["snapshotId"];
    request["lockedTracks"] = settings()["lockedTracks"];
    request["source"] =
        QVariantMap{{"kind", "rollDirect"}, {"allowVoiceReassignment", true}};
    request["workflowRecord"] = record(false);
    const auto result = applyRollNotation(&_reader, _score, request);
    _internal = false;
    if (result.contains("error")) {
      _built = _previousBuilt;
      _hasChanges = _beforeHasChanges;
      _rows = _before;
      _selected = _beforeSelected;
      _indexDirty = true;
      setMessage(result["error"].toString());
      _editor->updateSurfaces();
      updateActions();
      return;
    }
    readScope(_state["scope"].toMap());
    setMessage(tr("已写入乐谱 · Ctrl+Z 撤销这次操作 · Ctrl+S 保存。"));
  } else {
    saveDraft();
    _internal = false;
    setMessage(tr("草稿已更新 · Ctrl+Z 撤销 · “使用草稿”写入乐谱。"));
    _editor->updateSurfaces();
    updateActions();
  }
}
void RollWritingBar::saveDraft() {
  if (!_score)
    return;
  const auto bytes =
      QJsonDocument::fromVariant(record(true)).toJson(QJsonDocument::Compact);
  if (bytes.size() > 4 * 1024 * 1024) {
    setMessage(tr("草稿较大，请选较短的谱段保存。"));
    return;
  }
  const auto text = QString::fromUtf8(bytes);
  if (text == _score->masterScore()->metaTag("kumo.rollWriting"))
    return;
  _score->startCmd();
  _score->undo(
      new ChangeMetaText(_score->masterScore(), "kumo.rollWriting", text));
  _score->endCmd();
  if (mscore)
    mscore->endCmd();
  _savedText = text;
  _revision = _reader.analysisContentRevision();
  _state["snapshotId"] = _revision;
}
void RollWritingBar::restoreFromScore() {
  if (!_score)
    return;
  stopAudition();
  auto root = stored(), drafts = root["drafts"].toMap();
  if (drafts.contains(scopeId())) {
    auto current = drafts[scopeId()].toMap();
    current["draft"] = false;
    drafts[scopeId()] = current;
    root["drafts"] = drafts;
    _internal = true;
    _score->startCmd();
    _score->undo(new ChangeMetaText(
        _score->masterScore(), "kumo.rollWriting",
        QString::fromUtf8(
            QJsonDocument::fromVariant(root).toJson(QJsonDocument::Compact))));
    _score->endCmd();
    if (mscore)
      mscore->endCmd();
    _internal = false;
  }
  readScope(_state["scope"].toMap());
}
void RollWritingBar::stopAudition() {
  if (_score && !_previewKey.isEmpty() &&
      playbackPreviewPrepared(_score, _previewKey))
    stopPlaybackPreview(true);
  _preview.reset();
  _previewKey.clear();
  updateActions();
}
bool RollWritingBar::preparePreview() {
  if (!_ready || _busy || !_score)
    return false;
  if (_preview && playbackPreviewPrepared(_score, _previewKey))
    return true;
  stopPlaybackPreview(true);
  if (seq && seq->isPlaying())
    seq->stopWait();
  if (seq && !seq->backgroundRenderingIdle()) {
    setMessage(tr("正在准备声音，稍后可试听。"));
    return false;
  }
  QScopedValueRollback<bool> preparing(_busy, true);
  setMessage(tr("正在准备这段试听…"));
  updateActions();
  QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  const auto values =
      !_built.isEmpty() && _built.value("baseSettings").toMap() == settings()
          ? _built
          : buildRollEdit(_rows, _originalRows, _state["scope"].toMap(),
                          _measures, _tuplets, settings(), false);
  if (values.contains("error")) {
    setMessage(values["error"].toString());
    return false;
  }
  _preview.reset(_score->masterScore()->clone());
  if (!_preview) {
    setMessage(tr("试听材料创建失败，请重试。"));
    return false;
  }
  PluginAPI::Score wrapper(_preview.get());
  PluginAPI::ScoreObserver reader;
  reader.setScore(&wrapper);
  auto request = values;
  request["snapshotId"] = reader.analysisContentRevision();
  request["source"] =
      QVariantMap{{"kind", "rollDraft"}, {"allowVoiceReassignment", true}};
  request["lockedTracks"] = settings()["lockedTracks"];
  auto result = applyRollNotation(&reader, _preview.get(), request);
  if (result.contains("error")) {
    setMessage(result["error"].toString());
    _preview.reset();
    return false;
  }
  _preview->doLayout();
  _previewKey = "roll-" + freshId();
  preparePlaybackPreview(_score, _previewKey,
                         renderPlaybackPreview(_score, _score),
                         renderPlaybackPreview(_score, _preview.get()));
  return true;
}
void RollWritingBar::audition(bool proposed) {
  const bool prepared = preparePreview();
  updateActions();
  if (!prepared)
    return;
  const auto result =
      startPlaybackPreview(_score, proposed, _state["scope"].toMap());
  setMessage(result.contains("error") ? result["error"].toString()
             : proposed               ? tr("正在听：草稿效果")
                                      : tr("正在听：谱中效果"));
  updateActions();
}
void RollWritingBar::applyDraft() {
  if (!_ready || _busy)
    return;
  stopAudition();
  auto values =
      !_built.isEmpty() && _built.value("baseSettings").toMap() == settings()
          ? _built
          : buildRollEdit(_rows, _originalRows, _state["scope"].toMap(),
                          _measures, _tuplets, settings(), false);
  if (values.contains("error")) {
    setMessage(values["error"].toString());
    return;
  }
  values["snapshotId"] = _state["snapshotId"];
  values["source"] =
      QVariantMap{{"kind", "rollDraft"}, {"allowVoiceReassignment", true}};
  values["lockedTracks"] = settings()["lockedTracks"];
  values["workflowRecord"] = record(false);
  _internal = true;
  const auto result = applyRollNotation(&_reader, _score, values);
  _internal = false;
  if (result.contains("error")) {
    setMessage(result["error"].toString());
    return;
  }
  readScope(_state["scope"].toMap());
  setMessage(tr("已使用草稿 · Ctrl+Z 恢复采用前的乐谱和草稿。"));
}
} // namespace Ms
