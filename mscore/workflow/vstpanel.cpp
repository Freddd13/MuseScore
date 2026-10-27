// SPDX-License-Identifier: GPL-2.0-or-later
#include "vstpanel.h"
#include "vstparameters.h"
#include "vstpresets.h"
#include "sharedpiano.h"
#ifdef KUMO_VST_HOST
#include "audio/midi/midipatch.h"
#include "audio/midi/msynthesizer.h"
#include "audio/vst/vstsynth.h"
#include "featuregate.h"
#include "libmscore/instrument.h"
#include "libmscore/part.h"
#include "libmscore/score.h"
#include "libmscore/undo.h"
#include "mscore/musescore.h"
#include "mscore/seq.h"
#include "mscore/shortcut.h"
#include "mscore/workspace.h"
#include "mscore/mixer/mixerdetails.h"
#include "workspaceviews.h"
#include "toolicons.h"
#include "controlflowlayout.h"
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>
#include <QUuid>
#include <QtWidgets>

namespace Ms {
namespace {
void updateVstHistory(Score *score) {
  if (!mscore || !mscore->currentScore() ||
      mscore->currentScore()->masterScore() != score->masterScore())
    return;
  getAction("undo")->setEnabled(score->undoStack()->canUndo());
  getAction("redo")->setEnabled(score->undoStack()->canRedo());
}
VstSynth *backend() {
  return synti ? static_cast<VstSynth *>(synti->synthesizer("KumoVST"))
               : nullptr;
}
bool retainOpenSources() {
  return personalFeaturesEnabled() && QSettings().value("kumo/vst/retainOpenScores",false).toBool();
}
void selectSourceSession(Score* score) {
  auto engine=backend(); if (!engine) return;
  if (!score) { engine->rack().selectSession({}, {}, false); return; }
  score=score->masterScore();
  auto id=score->property("kumoVstSessionId").toString();
  if (id.isEmpty()) {
    id=QUuid::createUuid().toString(QUuid::WithoutBraces); score->QObject::setProperty("kumoVstSessionId",id);
    QObject::connect(score,&QObject::destroyed,qApp,[id] {
      if (auto engine=backend()) engine->rack().releaseSession(id);
    });
  }
  engine->rack().selectSession(id,score->name(),retainOpenSources());
}
SynthesizerState withConfiguration(SynthesizerState state,
                                   const QVariantMap &value) {
  for (auto it = state.begin(); it != state.end();)
    if (it->name() == "KumoVST")
      it = state.erase(it);
    else
      ++it;
  if (!value.isEmpty()) {
    SynthesizerGroup group;
    group.setName("KumoVST");
    group.push_back(
        IdValue(0, QString::fromUtf8(QJsonDocument::fromVariant(value).toJson(
                       QJsonDocument::Compact))));
    state.push_back(group);
  }
  state.setIsDefault(false);
  return state;
}
class ChangeVstState final : public UndoCommand {
  Score *score;
  SynthesizerState value;
  bool captureLive = false;
  void flip(EditData *) override {
    const auto old = score->synthesizerState();
    score->setSynthesizerState(value);
    value = old;
    if (captureLive) {
      captureLive = false;
      return;
    }
    if (mscore && mscore->currentScore() &&
        mscore->currentScore()->masterScore() == score) {
      if (seq) {
        seq->stopWait();
        seq->waitForStoppedRendering();
      }
      if (auto engine = backend()) {
        prepareCommonPianos(synti,score,false);
        engine->setState(score->synthesizerState().group("KumoVST"));
        if (!score->synthesizerState().group("KumoVST").empty() &&
            engine->problems().isEmpty())
          score->setSynthesizerState(withConfiguration(
              score->synthesizerState(), engine->configuration()));
      }
      routeVstChannels(synti, score);
      if (seq) {
        seq->initInstruments();
        seq->invalidatePlaybackEvents();
      }
    }
    refreshVstParameterWindows();
    score->setPlaylistDirty();
  }

public:
  ChangeVstState(Score *target, const SynthesizerState &next, bool capture = false)
      : score(target), value(next), captureLive(capture) {}
  UNDO_NAME("ChangeVstState")
};
QString identity(const QVariantMap &d) {
  return d["path"].toString() + "|" + d["uid"].toString();
}
class Panel final : public QDockWidget {
  QComboBox *parts, *catalog, *effectTarget, *sourceMode;
  QWidget *sourceModeBox;
  QListWidget *chain;
  QStackedWidget *chainArea;
  QLabel *state, *current, *chainHint;
  QPushButton *scan, *forget, *choose, *removeSource, *restoreSource, *openSource, *addEffect,
      *removeEffect, *up, *down, *openEffect, *loadPreset, *savePreset, *effectLoadPreset,
      *effectSavePreset;
  QCheckBox *isolate, *retainSources, *commonEnabled, *ownPiano;
  QLineEdit *commonName;
  QPushButton *adoptCommon;
  QLabel *commonState;
  QPushButton *releaseInactive;
  QLabel *retainedState;
  QProcess scanner;
  QTimer watch;
  QVariantList known;
  QMap<Channel *, QVariantMap> removed;
  QPointer<Score> score;
  QList<Channel *> channels;
  bool updating = false, savePending = false, closing = false;
  quint64 lastRevision = 0;
  quint64 lastCommonRevision = 0;
  QVariantMap removedCommon;
  QString lastProblems;
  bool syncChannels() {
    QList<Channel *> next;
    QStringList labels;
    if (score)
      for (const auto &mapping : score->masterScore()->midiMapping()) {
        auto channel = const_cast<Channel *>(mapping.articulation());
        if (!channel || next.contains(channel))
          continue;
        next.append(channel);
        labels.append(mapping.part()->partName() + " · " + channel->name());
      }
    QStringList oldLabels;
    for (int i = 0; i < parts->count(); ++i)
      oldLabels.append(parts->itemText(i));
    if (next == channels && labels == oldLabels)
      return false;
    auto previous = parts->currentIndex() >= 0 && parts->currentIndex() < channels.size()
                        ? channels[parts->currentIndex()]
                        : nullptr;
    QSignalBlocker block(parts);
    channels = next;
    parts->clear();
    parts->addItems(labels);
    if (channels.contains(previous))
      parts->setCurrentIndex(channels.indexOf(previous));
    for (auto it = removed.begin(); it != removed.end();)
      if (!channels.contains(it.key()))
        it = removed.erase(it);
      else
        ++it;
    return true;
  }
  Channel *selected() const {
    if (!score || !mscore || !mscore->currentScore() ||
        mscore->currentScore()->masterScore() != score)
      return nullptr;
    const int i = parts->currentIndex();
    auto candidate = i >= 0 && i < channels.size() ? channels[i] : nullptr;
    for (const auto &mapping : score->masterScore()->midiMapping())
      if (mapping.articulation() == candidate)
        return candidate;
    return nullptr;
  }
  int selectedSlot() const {
    auto c = selected();
    if (c && backend() && backend()->pianoSlot(c->channel())) return backend()->pianoSlot(c->channel());
    return c && c->synti() == "KumoVST" ? c->bank() : 0;
  }
  bool commonSlot(int id) const {
    auto c=selected();return id && c && backend() && backend()->pianoSlot(c->channel())==id;
  }
  bool commonSelection() const {
    return selected() && isPianoChannel(score,selected()) && !scoreUsesOwnPiano(score)
        && commonPianoSettings()["enabled"].toBool();
  }
  int targetSlot() const {
    return effectTarget->currentIndex() == 1 ? 0 : selectedSlot();
  }
  QVariantMap configuration() {
    return backend() ? (commonSlot(targetSlot())?backend()->runtimeConfiguration():backend()->configuration()) : QVariantMap();
  }
  QVariantMap applyPanelConfiguration(const QVariantMap& value,int id) {
    if (commonSlot(id)) {
      auto settings=commonPianoSettings();
      for (const auto& row:value["slots"].toList()) if (row.toMap()["id"].toInt()==id) settings["instance"]=row;
      return applyCommonPianoSettings(score,settings);
    }
    return applyVstConfiguration(score,value);
  }
  QVariantMap unit(int slot, int effect) {
    auto c = backend() ? backend()->runtimeConfiguration(false) : QVariantMap();
    if (!slot) {
      auto list = c["masterEffects"].toList();
      return effect >= 0 && effect < list.size() ? list[effect].toMap()
                                                 : QVariantMap();
    }
    for (const auto &v : c["slots"].toList()) {
      auto s = v.toMap();
      if (s["id"].toInt() != slot)
        continue;
      if (effect < 0)
        return s["source"].toMap();
      auto list = s["effects"].toList();
      return effect < list.size() ? list[effect].toMap() : QVariantMap();
    }
    return {};
  }
  void editUnit(int slot, int effect,
                const std::function<void(QVariantMap &)> &edit) {
    if (!score)
      return;
    auto value = commonSlot(slot)?backend()->runtimeConfiguration():backend()->configuration();
    if (!slot) {
      auto list = value["masterEffects"].toList();
      if (effect < 0 || effect >= list.size())
        return;
      auto u = list[effect].toMap();
      edit(u);
      list[effect] = u;
      value["masterEffects"] = list;
    } else {
      auto instances = value["slots"].toList();
      for (int i = 0; i < instances.size(); ++i) {
        auto s = instances[i].toMap();
        if (s["id"].toInt() != slot)
          continue;
        if (effect < 0) {
          auto u = s["source"].toMap();
          edit(u);
          s["source"] = u;
        } else {
          auto list = s["effects"].toList();
          if (effect >= list.size())
            return;
          auto u = list[effect].toMap();
          edit(u);
          list[effect] = u;
          s["effects"] = list;
        }
        instances[i] = s;
      }
      value["slots"] = instances;
    }
    report(applyPanelConfiguration(value,slot));
    refresh();
  }
  void report(const QVariantMap &result) {
    state->setText(
        result.contains("error")
            ? result["error"].toString()
            : result.value("message", tr("已更新；随乐谱保存，Ctrl+Z 撤销。"))
                  .toString());
  }
  void saveCatalog() {
    QSettings().setValue(
        "kumo/vst/catalog",
        QJsonDocument::fromVariant(known).toJson(QJsonDocument::Compact));
    populateCatalog();
  }
  void populateCatalog() {
    const auto key = catalog->currentData().toString();
    QSignalBlocker b(catalog);
    catalog->clear();
    catalog->setEnabled(!known.isEmpty());
    forget->setEnabled(!known.isEmpty());
    if (known.isEmpty()) {
      catalog->addItem(tr("先点“添加插件…”选择 VST 音源或效果器"), QString());
      catalog->setCurrentIndex(0);
      return;
    }
    for (const auto &v : known) {
      auto d = v.toMap();
      catalog->addItem(componentToolIcon(d["instrument"].toBool() ? "keyboard" : "settings"), d["name"].toString() + " · " +
                       (d["instrument"].toBool() ? tr("音源") : tr("效果")) +
                       " · " + d["format"].toString(),
                       identity(d));
      catalog->setItemData(catalog->count() - 1, d, Qt::UserRole + 1);
    }
    const int i = catalog->findData(key);
    if (i >= 0)
      catalog->setCurrentIndex(i);
  }
  QVariantMap picked() const {
    return catalog->currentData(Qt::UserRole + 1).toMap();
  }
  void scanFile() {
    const auto path = QFileDialog::getOpenFileName(
        this, tr("添加 VST 音源或效果器"),
        QSettings()
            .value("kumo/vst/lastFolder",
                   QStringLiteral("C:/Program Files/Common Files/VST3"))
            .toString(),
        tr("VST 插件 (*.vst3 *.dll)"));
    if (path.isEmpty())
      return;
    QSettings().setValue("kumo/vst/lastFolder", QFileInfo(path).absolutePath());
    scan->setEnabled(false);
    state->setText(tr("正在扫描 %1…").arg(QFileInfo(path).fileName()));
    scanner.setProperty("path", path);
    scanner.start(QCoreApplication::applicationDirPath() +
                      "/MuseScoreVstHost.exe",
                  {"--scan", path});
    QTimer::singleShot(60000, this, [this] {
      if (scanner.state() != QProcess::NotRunning) {
        scanner.kill();
        state->setText(
            tr("扫描耗时超过一分钟，已停止。可从插件提供的管理程序检查安装。"));
      }
    });
  }
  VstHost::Plugin *parameterTarget(int slot, int effect, const QString &key,
                                   const QString &pluginId) {
    if (closing || !backend())
      return nullptr;
    if (effect < 0)
      return identity(unit(slot, -1)["plugin"].toMap()) == pluginId
                 ? backend()->rack().plugin(slot, -1)
                 : nullptr;
    auto config = backend()->runtimeConfiguration(false);
    QVariantList list;
    if (!slot)
      list = config["masterEffects"].toList();
    else
      for (const auto &v : config["slots"].toList())
        if (v.toMap()["id"].toInt() == slot)
          list = v.toMap()["effects"].toList();
    for (int i = 0; i < list.size(); ++i) {
      auto value = list[i].toMap();
      if ((key.isEmpty() ? i == effect : value["key"].toString() == key) &&
          identity(value["plugin"].toMap()) == pluginId)
        return backend()->rack().plugin(slot, i);
    }
    return nullptr;
  }
  void commitEdits() {
    if (closing || !score || !backend() || (seq && seq->isPlaying()))
      return;
    captureCommonPianoEdits(score,selectedSlot());
    saveVstEdits(score);
    savePending = false;
    refreshVstParameterWindows();
  }
  void showParameters(int slot, int effect) {
    const auto value = unit(slot, effect), descriptor = value["plugin"].toMap();
    const auto key = value["key"].toString(), pluginId = identity(descriptor);
    const QPointer<Score> owner = score;
    const auto binding = QString("%1:%2:%3")
                             .arg(quintptr(score.data()), 0, 16)
                             .arg(slot)
                             .arg(effect < 0      ? "source"
                                  : key.isEmpty() ? QString::number(effect)
                                                  : key);
    const auto scope = slot ? parts->currentText() : tr("主输出");
    showVstParameters(
        this, binding, scope + " · " + descriptor["name"].toString(),
        [this, owner, slot, effect, key, pluginId]() -> VstHost::Plugin * {
          if (closing || !owner || score != owner || !mscore || !mscore->currentScore() ||
              mscore->currentScore()->masterScore() != owner)
            return nullptr;
          return parameterTarget(slot, effect, key, pluginId);
        },
        [this](bool commit) {
          savePending = true;
          if (commit)
            commitEdits();
        },
        getAction("undo"), getAction("redo"));
  }
  void preset(bool save, int slot, int effect) {
    if (!score || !backend())
      return;
    const QPointer<Score> owner = score;
    const auto before = unit(slot, effect), descriptor = before["plugin"].toMap();
    const auto label = descriptor["name"].toString();
    const QString folder = QSettings().value("kumo/vst/presetFolder").toString();
    const auto path =
        save ? QFileDialog::getSaveFileName(this, tr("保存%1预设").arg(label), folder,
                                            tr("插件预设 (*.json)"))
             : QFileDialog::getOpenFileName(this, tr("载入%1预设").arg(label), folder,
                                            tr("插件预设 (*.json *.ariax *.fxp *.fxb)"));
    if (path.isEmpty())
      return;
    if (score != owner || identity(unit(slot, effect)["plugin"].toMap()) != identity(descriptor) ||
        unit(slot, effect)["key"] != before["key"]) {
      state->setText(tr("当前音源或效果已经变化，选中它后可以继续载入预设。"));
      return;
    }
    QSettings().setValue("kumo/vst/presetFolder", QFileInfo(path).absolutePath());
    if (seq) {
      seq->stopWait();
      seq->waitForStoppedRendering();
    }
    commitEdits();
    auto engine = backend();
    auto plugin = engine->rack().plugin(slot, effect);
    if (!plugin)
      return;
    QString error;
    bool wrapped = false;
    QByteArray incoming;
    if (!save) {
      incoming = readVstPreset(path, descriptor, &wrapped, &error);
      if (!error.isEmpty()) {
        state->setText(error);
        return;
      }
    }
    state->setText(save ? tr("正在保存%1预设…").arg(label) : tr("正在载入%1预设…").arg(label));
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    engine->rack().suspend();
    const auto original = plugin->state(&error);
    bool ok = error.isEmpty();
    if (ok && !save) {
      ok = wrapped ? plugin->restore(incoming, &error) : plugin->importPreset(path, &error);
      if (!ok) {
        QString restoreError;
        plugin->restore(original, &restoreError);
      }
    }
    engine->rack().resume();
    if (!ok) {
      state->setText(error);
      return;
    }
    if (save) {
      const auto bytes = encodeVstPreset(original, descriptor);
      QSaveFile file(path);
      if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        state->setText(file.errorString());
        return;
      }
    } else {
      savePending = true;
      commitEdits();
    }
    state->setText(save ? tr("%1预设已保存：%2").arg(label, QFileInfo(path).fileName())
                        : commonSlot(slot)?tr("%1已采用，参加共用的钢琴一起更新；Ctrl+Z撤销。").arg(label)
                                          :tr("%1已载入预设 · Ctrl+Z撤销，保存乐谱后保留。").arg(label));
  }
  void editor(int slot, int effect) {
    if (!backend())
      return;
    auto plugin = backend()->rack().plugin(slot, effect);
    if (!plugin) {
      state->setText(tr("请先选择并载入音源。"));
      return;
    }
    QString error;
    if (plugin->openEditor(this, &error))
      state->setText(commonSlot(slot)?tr("已打开共用钢琴；修改的声音用于参加共用的乐谱。")
                                    :tr("插件窗口已打开；修改的预设随乐谱保存。"));
    else {
      if (plugin->parameters().isEmpty()) {
        state->setText(error);
        return;
      }
      showParameters(slot, effect);
      state->setText(tr("已打开插件参数。"));
    }
  }
  void changeSourceMode(bool isolated) {
    const int id=selectedSlot();
    if (updating || !score || !backend() || !id)
      return;
    auto plugin=backend()->rack().plugin(id);
    const auto currentUnit=unit(id,-1);
    if (!plugin || currentUnit["plugin"].toMap()["format"]=="SF2" ||
        currentUnit["isolated"].toBool()==isolated)
      return;
    const bool reopen=plugin->editorVisible();
    const QPointer<Score> owner=score;
    if (seq) {
      seq->stopWait();
      seq->synchronizeStoppedGui();
      seq->waitForStoppedRendering();
    }
    commitEdits();
    if (score!=owner || selectedSlot()!=id) {
      refresh();
      return;
    }
    state->setText(tr("正在切换音源运行方式…"));
    state->repaint();
    sourceMode->setEnabled(false);
    current->setText(tr("正在切换到%1…\n音色：%2")
        .arg(isolated?tr("独立运行"):tr("直接运行"),currentUnit["plugin"].toMap()["name"].toString()));
    current->repaint();
    editUnit(id,-1,[isolated](QVariantMap& value) { value["isolated"]=isolated; });
    auto next=backend()->rack().plugin(selectedSlot());
    if (!next || unit(selectedSlot(),-1)["isolated"].toBool()!=isolated)
      return;
    if (reopen) {
      QString error;
      if (!next->openEditor(this,&error)) {
        state->setText(error);
        return;
      }
    }
    state->setText(commonSlot(selectedSlot())
        ?tr("共用钢琴已改为%1，音色和参数保留；Ctrl+Z撤销。")
             .arg(isolated?tr("独立运行"):tr("直接运行"))
        :tr("当前音源已改为%1，音色和参数保留；Ctrl+Z撤销。")
             .arg(isolated?tr("独立运行"):tr("直接运行")));
  }
  void changeChain(const std::function<void(QVariantList &)> &edit) {
    const int id = targetSlot();
    if (!id && effectTarget->currentIndex() != 1)
      return;
    auto c = configuration();
    if (!id) {
      auto list = c["masterEffects"].toList();
      edit(list);
      c["masterEffects"] = list;
    } else {
      auto list = c["slots"].toList();
      for (int i = 0; i < list.size(); ++i) {
        auto s = list[i].toMap();
        if (s["id"].toInt() == id) {
          auto effects = s["effects"].toList();
          edit(effects);
          if (!commonSlot(id) && effects.isEmpty() && s["source"].toMap()["plugin"].toMap()["format"] == "SF2") {
            removeSource->click();
            state->setText(tr("效果已移除，当前乐器继续使用原SF2音色。Ctrl+Z恢复效果。"));
            return;
          }
          s["effects"] = effects;
          list[i] = s;
        }
      }
      c["slots"] = list;
    }
    report(applyPanelConfiguration(c,id));
    refresh();
  }
  void refresh() {
    if (updating)
      return;
    updating = true;
    auto c = selected();
    const int id = selectedSlot();
    auto engine = backend();
    refreshRetained();
    const bool shared=commonSlot(id);
    const bool sharedDesired=commonSelection();
    const auto common=commonPianoSettings();
    {
      QSignalBlocker a(commonEnabled),b(ownPiano),c(commonName);
      commonEnabled->setChecked(common["enabled"].toBool());
      ownPiano->setChecked(scoreUsesOwnPiano(score));
      commonName->setText(common.value("name",tr("共用钢琴")).toString());
    }
    commonEnabled->setEnabled(common["enabled"].toBool() || !common["instance"].toMap().isEmpty() || isPianoChannel(score,c));
    ownPiano->setEnabled(score && common["enabled"].toBool());
    ownPiano->setVisible(common["enabled"].toBool() || scoreUsesOwnPiano(score));
    adoptCommon->setEnabled(isPianoChannel(score,c));
    const auto commonError=synti?synti->property("kumoCommonPianoError").toString():QString();
    commonState->setText(!commonError.isEmpty()?tr("共用音源未载入 · 当前使用本谱音源")
        :shared?tr("当前乐器使用共用钢琴")
        :common["enabled"].toBool()?tr("当前乐器使用本谱音源"):tr("共用关闭 · 各谱使用自己的音源"));
    commonState->setToolTip(commonError);
    commonState->setVisible(common["enabled"].toBool() || !commonError.isEmpty());
    choose->setText(commonSelection()?tr("用于共用钢琴"):tr("用作此乐器音源"));
    removeSource->setText(sharedDesired?tr("使用内置SF2"):tr("使用原内置音源"));
    restoreSource->setText(sharedDesired?tr("恢复上个共用音源"):tr("恢复刚才音源"));
    const auto patch = c && synti ? synti->getPatchInfo(c->synti(), c->bank(), c->program()) : nullptr;
    const bool externalSource = c && (shared || c->synti() == "KumoVST") &&
        unit(id, -1)["plugin"].toMap()["format"].toString() != "SF2";
    current->setText(c ? tr("当前音源：%1\n音色：%2")
                          .arg((shared?tr("共用钢琴 · "):QString())+(externalSource
                                   ? tr("VST · %1").arg(unit(id,-1)["isolated"].toBool()?tr("独立运行"):tr("直接运行"))
                                   : tr("内置 SF2／SF3")),
                               shared?unit(id,-1)["plugin"].toMap()["name"].toString()
                                     :patch ? patch->name : externalSource ? tr("音源文件待重新定位") : tr("乐谱中的内置音色"))
                        : tr("打开乐谱后选择乐器。"));
    choose->setEnabled(c && !picked().isEmpty() &&
                       picked()["instrument"].toBool());
    const bool loaded = id && engine && engine->rack().plugin(id);
    sourceModeBox->setVisible(externalSource);
    sourceMode->setEnabled(loaded && externalSource);
    {
      QSignalBlocker blocked(sourceMode);
      sourceMode->setCurrentIndex(unit(id,-1)["isolated"].toBool()?1:0);
    }

    const bool external =
        unit(id, -1)["plugin"].toMap()["format"].toString() != "SF2";
    openSource->setEnabled(loaded && external);
    loadPreset->setEnabled(loaded && external);
    savePreset->setEnabled(loaded && external);
    removeSource->setEnabled(id || sharedDesired);
    restoreSource->setEnabled(sharedDesired?!removedCommon.isEmpty():c && !id && removed.contains(c));
    const int row = chain->currentRow();
    QSignalBlocker block(chain);
    chain->clear();
    const auto list = engine && (targetSlot() || effectTarget->currentIndex() == 1)
                          ? engine->rack().effects(targetSlot())
                          : QVariantList();
    for (const auto &v : list) {
      auto e = v.toMap();
      auto item =
          new QListWidgetItem(e["plugin"].toMap()["name"].toString(), chain);
      item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
      item->setCheckState(e["bypass"].toBool() ? Qt::Unchecked : Qt::Checked);
      item->setToolTip(
          tr("勾选启用，取消勾选旁通；效果按从上到下的顺序处理。"));
    }
    chainArea->setCurrentWidget(chain->count()
                                    ? static_cast<QWidget *>(chain)
                                    : static_cast<QWidget *>(chainHint));
    if (row >= 0 && row < chain->count())
      chain->setCurrentRow(row);
    addEffect->setEnabled(score && !picked().isEmpty() &&
                          !picked()["instrument"].toBool() &&
                          (id || effectTarget->currentIndex() == 1 ||
                           (c && c->synti() == "Fluid")));
    effectLoadPreset->setEnabled(chain->currentRow() >= 0);
    effectSavePreset->setEnabled(chain->currentRow() >= 0);
    removeEffect->setEnabled(chain->currentRow() >= 0);
    openEffect->setEnabled(chain->currentRow() >= 0);
    up->setEnabled(chain->currentRow() > 0);
    down->setEnabled(chain->currentRow() >= 0 &&
                     chain->currentRow() < chain->count() - 1);
    if (engine && !engine->problems().isEmpty())
      state->setText(engine->problems().join('\n'));
    updating = false;
    refreshVstParameterWindows();
  }
  void refreshRetained() {
    QSignalBlocker blocked(retainSources); retainSources->setChecked(retainOpenSources());
    const auto rows=backend()?backend()->rack().retainedSessions():QVariantList{};
    int instances=0,scores=0,pianos=0; QStringList names;
    for (const auto& row:rows) { const auto value=row.toMap(); instances+=value["instances"].toInt(); names.append(value["label"].toString());
      if(value["commonPiano"].toBool()) ++pianos; else ++scores; }
    releaseInactive->setEnabled(!rows.isEmpty());
    retainedState->setText(rows.isEmpty()?tr("当前没有闲置音源")
        :pianos?tr("保留%1份谱、%2架闲置钢琴").arg(scores).arg(pianos)
               :tr("保留%1份谱的%2个音源／效果").arg(scores).arg(instances));
    retainedState->setToolTip(names.join('\n'));
  }
  void persistEdits() {
    if (!score || !backend())
      return;
    if (backend()->rack().takeChanges())
      savePending = true;
    if (!savePending || vstParameterGestureActive() || (seq && seq->isPlaying()) ||
        backend()->rack().hasOpenEditors())
      return;
    commitEdits();
  }

public:
  explicit Panel(QWidget *parent) : QDockWidget(tr("音源与效果"), parent) {
    setObjectName("kumoVstPanel");
    setAllowedAreas(Qt::AllDockWidgetAreas);
    addEditorDockActions(this);
    auto body = new QWidget(this);
    auto scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(body);
    setWidget(scroll);
    auto layout = new QVBoxLayout(body);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);
    auto row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("乐器"), body));
    parts = new QComboBox(body);
    parts->setObjectName("kumoVstParts");
    parts->setSizeAdjustPolicy(
        QComboBox::AdjustToMinimumContentsLengthWithIcon);
    row->addWidget(parts, 1);
    layout->addLayout(row);
    auto sourceGroup = new QGroupBox(tr("正在使用的音源"), body);
    sourceGroup->setObjectName("kumoVstCurrentSourceGroup");
    auto sourceLayout = new QVBoxLayout(sourceGroup);
    sourceLayout->setSpacing(6);
    current = new QLabel(body);
    current->setObjectName("kumoVstCurrentSource");
    current->setWordWrap(true);
    current->setFixedHeight(fontMetrics().lineSpacing() * 2);
    sourceLayout->addWidget(current);
    commonEnabled=new QCheckBox(tr("所有谱子的钢琴自动共用"),body);
    commonEnabled->setObjectName("kumoVstCommonPianoEnabled");
    commonEnabled->setToolTip(tr("各谱钢琴自动采用同一套音源和预设；多架钢琴分别处理按键与踏板。"));
    sourceLayout->addWidget(commonEnabled);
    ownPiano=new QCheckBox(tr("本谱使用自己的钢琴音源"),body);
    ownPiano->setObjectName("kumoVstOwnPiano");sourceLayout->addWidget(ownPiano);
    commonState=new QLabel(body);commonState->setObjectName("kumoVstCommonPianoState");
    commonState->setFixedHeight(fontMetrics().lineSpacing());sourceLayout->addWidget(commonState);
    auto commonDetailsButton=new QToolButton(body);commonDetailsButton->setText(tr("共用设置"));
    commonDetailsButton->setObjectName("kumoVstCommonPianoDetailsToggle");
    commonDetailsButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    commonDetailsButton->setArrowType(Qt::RightArrow);commonDetailsButton->setCheckable(true);
    sourceLayout->addWidget(commonDetailsButton,0,Qt::AlignLeft);
    auto commonDetails=new QWidget(body);auto commonLayout=new QVBoxLayout(commonDetails);
    commonLayout->setContentsMargins(0,0,0,0);commonLayout->setSpacing(6);
    auto nameRow=new QHBoxLayout;nameRow->addWidget(new QLabel(tr("名称"),body));
    commonName=new QLineEdit(body);commonName->setObjectName("kumoVstCommonPianoName");nameRow->addWidget(commonName);
    commonLayout->addLayout(nameRow);
    adoptCommon=new QPushButton(componentToolIcon("keyboard"),tr("采用当前钢琴声音"),body);
    adoptCommon->setObjectName("kumoVstCommonPianoAdopt");
    auto commonActions=new ControlFlowLayout;commonActions->addWidget(adoptCommon);commonLayout->addLayout(commonActions);
    sourceLayout->addWidget(commonDetails);commonDetails->setVisible(false);
    const bool detailsShown=QSettings().value("kumo/vst/commonPianoDetails",false).toBool();
    connect(commonDetailsButton,&QToolButton::toggled,this,[commonDetails,commonDetailsButton](bool shown) {
      commonDetails->setVisible(shown);commonDetailsButton->setArrowType(shown?Qt::DownArrow:Qt::RightArrow);
      QSettings().setValue("kumo/vst/commonPianoDetails",shown);
    });
    commonDetailsButton->setChecked(detailsShown);
    connect(commonEnabled,&QCheckBox::toggled,this,[this](bool enabled) {
      if(updating) return;
      commitEdits();auto settings=commonPianoSettings();settings["enabled"]=enabled;
      if(enabled && settings["instance"].toMap().isEmpty()) settings["instance"]=commonPianoFromCurrent(score,selected());
      if(!settings.contains("name")) settings["name"]=tr("共用钢琴");
      state->setText(enabled?tr("正在准备共用钢琴…"):tr("正在恢复各谱音源…"));
      QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
      report(applyCommonPianoSettings(score,settings));refresh();
    });
    connect(ownPiano,&QCheckBox::toggled,this,[this](bool own) {
      if(updating) return;commitEdits();report(setScoreOwnPiano(score,own));refresh();
    });
    connect(adoptCommon,&QPushButton::clicked,this,[this] {
      commitEdits();auto settings=commonPianoSettings();settings["instance"]=commonPianoFromCurrent(score,selected());
      settings["enabled"]=true;report(applyCommonPianoSettings(score,settings));refresh();
    });
    connect(commonName,&QLineEdit::editingFinished,this,[this] {
      auto settings=commonPianoSettings();auto name=commonName->text().trimmed();
      if(name.isEmpty()) {name=tr("共用钢琴");commonName->setText(name);}
      if(name==settings.value("name",tr("共用钢琴")).toString()) return;
      settings["name"]=name;report(applyCommonPianoSettings(score,settings));refresh();
    });
    retainSources=new QCheckBox(tr("换谱时保留已加载音源"),body);
    retainSources->setObjectName("kumoVstRetainSources");
    retainSources->setToolTip(tr("返回已打开的乐谱时复用音源与效果。闲置时停止声音处理，关闭乐谱后释放。"));
    sourceLayout->addWidget(retainSources);
    auto retentionRow=new ControlFlowLayout;
    releaseInactive=new QPushButton(tr("释放闲置音源"),body);
    releaseInactive->setObjectName("kumoVstReleaseInactive"); setComponentToolIcon(releaseInactive,"close");
    retainedState=new QLabel(body); retainedState->setObjectName("kumoVstRetainedState");
    retentionRow->addWidget(releaseInactive); retentionRow->addWidget(retainedState); sourceLayout->addLayout(retentionRow);
    connect(retainSources,&QCheckBox::toggled,this,[this](bool enabled) {
      QSettings().setValue("kumo/vst/retainOpenScores",enabled); selectSourceSession(score);
      if (!enabled && backend()) backend()->rack().releaseInactiveSessions();
      state->setText(enabled?tr("已开启：换谱保留音源，返回时继续使用；关谱后释放。")
          :tr("已关闭：闲置音源已释放，换谱沿用原有加载方式。")); refreshRetained();
    });
    connect(releaseInactive,&QPushButton::clicked,this,[this] {
      if (backend()) backend()->rack().releaseInactiveSessions();
      state->setText(tr("闲置音源已释放，当前乐谱的声音继续保留。")); refreshRetained();
    });
    layout->addWidget(sourceGroup);
    auto libraryGroup = new QGroupBox(tr("添加或更换插件"), body);
    libraryGroup->setObjectName("kumoVstLibraryGroup");
    auto libraryLayout = new QVBoxLayout(libraryGroup);
    libraryLayout->setSpacing(6);
    catalog = new QComboBox(body);
    catalog->setObjectName("kumoVstCatalog");
    catalog->setSizeAdjustPolicy(
        QComboBox::AdjustToMinimumContentsLengthWithIcon);
    catalog->setToolTip(tr("选择已添加的音源或效果器，再使用下方对应操作。"));
    libraryLayout->addWidget(catalog);
    row = new QHBoxLayout;
    scan = new QPushButton(tr("添加插件…"), body);
    scan->setObjectName("kumoVstScan");
    row->addWidget(scan);
    forget = new QPushButton(tr("移出列表"), body);
    forget->setObjectName("kumoVstForget");
    row->addWidget(forget);
    row->addStretch();
    libraryLayout->addLayout(row);
    isolate = new QCheckBox(tr("新添加的插件使用独立进程"), body);
    isolate->setChecked(QSettings().value("kumo/vst/isolated", false).toBool());
    connect(isolate, &QCheckBox::toggled, this,
            [](bool value) { QSettings().setValue("kumo/vst/isolated", value); });
    isolate->setToolTip(tr("将这个插件放在单独进程，额外增加约两个 256 "
                           "样本块的播放延迟。直接运行适合低延迟弹奏。"));
    libraryLayout->addWidget(isolate);
    auto sourceActions = new ControlFlowLayout;
    choose = new QPushButton(tr("用作此乐器音源"), body);
    choose->setObjectName("kumoVstChoose");
    openSource = new QPushButton(tr("打开音源"), body);
    openSource->setObjectName("kumoVstOpenSource");
    sourceActions->addWidget(choose);
    libraryLayout->addLayout(sourceActions);
    auto restoreActions = new ControlFlowLayout;
    removeSource = new QPushButton(tr("使用原内置音源"), body);
    removeSource->setObjectName("kumoVstRemoveSource");
    removeSource->setToolTip(tr("恢复选用 VST 之前的音源与音色，例如 SF2 钢琴。"));
    restoreSource = new QPushButton(tr("恢复刚才音源"), body);
    restoreSource->setObjectName("kumoVstRestoreSource");
    restoreActions->addWidget(openSource);
    restoreActions->addWidget(removeSource);
    restoreActions->addWidget(restoreSource);
    sourceLayout->addLayout(restoreActions);
    auto presets = new ControlFlowLayout;
    auto import = new QPushButton(tr("载入预设…"), body);
    auto save = new QPushButton(tr("保存预设…"), body);
    loadPreset = import;
    savePreset = save;
    import->setObjectName("kumoVstSourceLoadPreset");
    save->setObjectName("kumoVstSourceSavePreset");
    presets->addWidget(import);
    presets->addWidget(save);
    sourceModeBox=new QWidget(body);
    sourceModeBox->setObjectName("kumoVstSourceModeBox");
    auto modeRow=new QHBoxLayout(sourceModeBox);
    modeRow->setContentsMargins(0,0,0,0);modeRow->setSpacing(6);
    auto modeLabel=new QLabel(tr("运行方式"),sourceModeBox);
    sourceMode=new QComboBox(sourceModeBox);
    sourceMode->setObjectName("kumoVstSourceMode");
    sourceMode->setAccessibleName(tr("当前音源运行方式"));
    sourceMode->addItem(tr("直接运行"),false);
    sourceMode->addItem(tr("独立运行"),true);
    sourceMode->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    sourceMode->setToolTip(tr("直接运行适合低延迟弹奏。独立运行将音源窗口放在单独进程，"
                              "可减少它对谱面操作的影响；44.1 kHz下额外延迟约11.6毫秒。"
                              "切换保留音色和参数，Ctrl+Z撤销。"));
    modeLabel->setBuddy(sourceMode);
    modeRow->addWidget(modeLabel);modeRow->addWidget(sourceMode);
    presets->addWidget(sourceModeBox);
    connect(sourceMode,QOverload<int>::of(&QComboBox::activated),this,
            [this](int index) { changeSourceMode(sourceMode->itemData(index).toBool()); });
    sourceLayout->addLayout(presets);
    layout->addWidget(libraryGroup);
    auto effectsGroup = new QGroupBox(tr("效果链"), body);
    effectsGroup->setObjectName("kumoVstEffectsGroup");
    auto effectsLayout = new QVBoxLayout(effectsGroup);
    effectsLayout->setSpacing(6);
    effectTarget = new QComboBox(body);
    effectTarget->setObjectName("kumoVstEffectTarget");
    effectTarget->addItems({tr("此乐器效果"), tr("主输出效果")});
    effectsLayout->addWidget(effectTarget);
    chainArea = new QStackedWidget(body);
    chainArea->setMinimumHeight(100);
    chain = new QListWidget(chainArea);
    chain->setObjectName("kumoVstEffectChain");
    chainHint = new QLabel(tr("从上方列表选择效果器\n再点“加效果”"), chainArea);
    chainHint->setWordWrap(true);
    chainHint->setAlignment(Qt::AlignCenter);
    auto hintPalette = chainHint->palette();
    auto hintInk = hintPalette.color(QPalette::WindowText);
    hintInk.setAlpha(170);
    hintPalette.setColor(QPalette::WindowText, hintInk);
    chainHint->setPalette(hintPalette);
    chainArea->addWidget(chain);
    chainArea->addWidget(chainHint);
    effectsLayout->addWidget(chainArea, 1);
    auto effectActions = new ControlFlowLayout;
    addEffect = new QPushButton(tr("加效果"), body);
    addEffect->setObjectName("kumoVstAddEffect");
    openEffect = new QPushButton(tr("打开"), body);
    openEffect->setObjectName("kumoVstOpenEffect");
    removeEffect = new QPushButton(tr("删除"), body);
    removeEffect->setObjectName("kumoVstRemoveEffect");
    up = new QPushButton(tr("上移"), body);
    down = new QPushButton(tr("下移"), body);
    up->setObjectName("kumoVstEffectUp");
    down->setObjectName("kumoVstEffectDown");
    for (auto b : {addEffect, openEffect, removeEffect})
      effectActions->addWidget(b);
    effectsLayout->addLayout(effectActions);
    auto effectOrder = new ControlFlowLayout;
    effectOrder->addWidget(up);
    effectOrder->addWidget(down);
    effectsLayout->addLayout(effectOrder);
    auto effectPresets = new ControlFlowLayout;
    effectLoadPreset = new QPushButton(tr("载入效果预设…"), body);
    effectLoadPreset->setObjectName("kumoVstEffectLoadPreset");
    effectSavePreset = new QPushButton(tr("保存效果预设…"), body);
    effectSavePreset->setObjectName("kumoVstEffectSavePreset");
    effectPresets->addWidget(effectLoadPreset);
    effectPresets->addWidget(effectSavePreset);
    effectsLayout->addLayout(effectPresets);
    layout->addWidget(effectsGroup, 1);
    for (const auto& item : QList<QPair<QAbstractButton*, QString>>{
             {scan,"add"},{forget,"delete"},{choose,"keyboard"},{openSource,"panel"},
             {removeSource,"keyboard"},{restoreSource,"undo"},{import,"open"},{save,"save"},
             {addEffect,"add"},{openEffect,"panel"},{removeEffect,"delete"},{up,"up"},{down,"down"},
             {effectLoadPreset,"open"},{effectSavePreset,"save"}})
      setComponentToolIcon(item.first, item.second);
    connect(effectLoadPreset, &QPushButton::clicked, this,
            [this] { preset(false, targetSlot(), chain->currentRow()); });
    connect(effectSavePreset, &QPushButton::clicked, this,
            [this] { preset(true, targetSlot(), chain->currentRow()); });
    state = new QLabel(tr("添加已安装的 VST3 或 x64 VST2 文件，再选择音源。"),
                       body);
    state->setWordWrap(true);
    state->setMinimumHeight(fontMetrics().lineSpacing() * 3);
    state->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(state);
    known = QJsonDocument::fromJson(
                QSettings().value("kumo/vst/catalog").toByteArray())
                .toVariant()
                .toList();
    populateCatalog();
    connect(scan, &QPushButton::clicked, this, [this] { scanFile(); });
    connect(
        &scanner, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
        this, [this](int code, QProcess::ExitStatus exit) {
          scan->setEnabled(true);
          auto result = QJsonDocument::fromJson(scanner.readAllStandardOutput())
                            .toVariant()
                            .toMap();
          if (exit != QProcess::NormalExit || code ||
              !result["error"].toString().isEmpty()) {
            state->setText(result["error"].toString().isEmpty()
                               ? tr("插件扫描未完成，请检查插件安装和授权。")
                               : result["error"].toString());
            return;
          }
          if (result["plugins"].toList().isEmpty()) {
            state->setText(tr("这份文件没有列出可用插件，请选择 x64 VST2 或 "
                              "VST3 插件文件。"));
            return;
          }
          for (const auto &v : result["plugins"].toList()) {
            auto d = v.toMap();
            bool found = false;
            for (const auto &k : known)
              if (identity(k.toMap()) == identity(d))
                found = true;
            if (!found)
              known.append(d);
          }
          saveCatalog();
          state->setText(tr("扫描完成，选择列表中的插件即可使用。"));
          refresh();
        });
    connect(&scanner, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) {
              scan->setEnabled(true);
              state->setText(
                  tr("扫描工具无法启动：%1").arg(scanner.errorString()));
            });
    connect(forget, &QPushButton::clicked, this, [this] {
      int i = catalog->currentIndex();
      if (i >= 0 && i < known.size()) {
        known.removeAt(i);
        saveCatalog();
        refresh();
      }
    });
    connect(parts, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { refresh(); });
    connect(catalog, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { refresh(); });
    connect(effectTarget, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this] { refresh(); });
    connect(choose, &QPushButton::clicked, this, [this] {
      if (!score || !selected())
        return;
      state->setText(tr("正在载入 %1…").arg(picked()["name"].toString()));
      QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
      if (commonSelection()) {
        auto settings=commonPianoSettings();auto piano=settings["instance"].toMap();
        piano["source"]=QVariantMap{{"plugin",picked()},{"isolated",isolate->isChecked()}};
        settings["instance"]=piano;report(applyCommonPianoSettings(score,settings));
      } else report(assignVstInstrument(score, selected(), picked(),isolate->isChecked()));
      refresh();
    });
    connect(openSource, &QPushButton::clicked, this,
            [this] { editor(selectedSlot(), -1); });
    connect(removeSource, &QPushButton::clicked, this, [this] {
      auto c = selected();
      if (!c)
        return;
      if (commonSelection()) {
        auto settings=commonPianoSettings();removedCommon=settings["instance"].toMap();
        settings["instance"]=commonPianoFromCurrent(score,c,true);
        report(applyCommonPianoSettings(score,settings));refresh();return;
      }
      if (!selectedSlot()) return;
      const auto config = configuration();
      QVariantMap original;
      for (const auto &s : config["slots"].toList())
        if (s.toMap()["id"].toInt() == c->bank())
          original = s.toMap()["originalPatch"].toMap();
      for (const auto &v : config["slots"].toList())
        if (v.toMap()["id"].toInt() == c->bank())
          removed[c] = {{"instance", v}, {"bank", c->bank()}};
      MidiPatch patch;
      patch.synti = original.value("synti", "Fluid").toString();
      patch.bank = original["bank"].toInt();
      patch.prog = original["program"].toInt();
      patch.drum = false;
      patch.sfid = 0;
      if (seq)
        seq->stopWait();
      auto next = config;
      auto instances = next["slots"].toList();
      bool shared = false;
      for (const auto &mm : score->masterScore()->midiMapping())
        if (mm.articulation() != c && mm.articulation()->synti() == "KumoVST" &&
            mm.articulation()->bank() == c->bank())
          shared = true;
      if (!shared)
        for (int i = instances.size() - 1; i >= 0; --i)
          if (instances[i].toMap()["id"].toInt() == c->bank())
            instances.removeAt(i);
      next["slots"] = instances;
      if (instances.isEmpty() && next["masterEffects"].toList().isEmpty())
        next.clear();
      score->startCmd();
      score->undo(new ChangeVstState(
          score->masterScore(),
          withConfiguration(score->masterScore()->synthesizerState(), next)));
      score->undo(new ChangePatch(score, c, &patch));
      score->undo(new SetUserBankController(
          c, original["userBankController"].toBool()));
      score->setInstrumentsChanged(true);
      score->endCmd();
      updateVstHistory(score);
      if (seq)
        seq->initInstruments();
      routeVstChannels(synti, score);
      state->setText(tr("已恢复原音源，Ctrl+Z 可切回。"));
      refresh();
    });
    connect(restoreSource, &QPushButton::clicked, this, [this] {
      auto c = selected();
      if (commonSelection() && !removedCommon.isEmpty()) {
        auto settings=commonPianoSettings();settings["instance"]=removedCommon;
        report(applyCommonPianoSettings(score,settings));refresh();return;
      }
      if (!c || selectedSlot() || !removed.contains(c))
        return;
      const auto remembered = removed.value(c);
      MidiPatch p;
      p.synti = "KumoVST";
      p.bank = remembered["bank"].toInt();
      p.prog = 0;
      p.drum = false;
      p.sfid = 0;
      if (seq)
        seq->stopWait();
      auto next = configuration();
      auto instances = next["slots"].toList();
      QVariantMap saved = remembered["instance"].toMap();
      for (const auto &item : instances)
        if (item.toMap()["id"].toInt() == p.bank) {
          int id = p.bank;
          for (const auto &v : instances)
            id = qMax(id, v.toMap()["id"].toInt() + 1);
          p.bank = id;
          break;
        }
      if (saved.isEmpty())
        return;
      saved["id"] = p.bank;
      instances.append(saved);
      next["slots"] = instances;
      next["schema"] = 1;
      score->startCmd();
      score->undo(new ChangeVstState(
          score->masterScore(),
          withConfiguration(score->masterScore()->synthesizerState(), next)));
      score->undo(new ChangePatch(score, c, &p));
      score->undo(new SetUserBankController(c, true));
      score->setInstrumentsChanged(true);
      score->endCmd();
      updateVstHistory(score);
      if (seq)
        seq->initInstruments();
      routeVstChannels(synti, score);
      refresh();
    });
    connect(import, &QPushButton::clicked, this, [this] { preset(false, selectedSlot(), -1); });
    connect(save, &QPushButton::clicked, this, [this] { preset(true, selectedSlot(), -1); });
    connect(addEffect, &QPushButton::clicked, this, [this] {
      auto d = picked();
      if (d.isEmpty())
        return;
      if (d["instrument"].toBool()) {
        state->setText(tr("请选择效果器；钢琴音源放在上方的音源位置。"));
        return;
      }
      if (effectTarget->currentIndex() == 0 && !selectedSlot() && selected() &&
          selected()->synti() == "Fluid") {
        auto fluid = synti->synthesizer("Fluid");
        QStringList paths;
        for (const auto &font : fluid->soundFontsInfo())
          paths.append(font.fileName);
        if (paths.isEmpty()) {
          state->setText(tr("请先在合成器中载入 SF2 音色库。"));
          return;
        }
        const QVariantMap source{
            {"path", paths.join('\n')},
            {"format", "SF2"},
            {"uid", QString::number(selected()->bank()) + ":" +
                        QString::number(selected()->program())},
            {"name", tr("SF2 · ") + parts->currentText()},
            {"vendor", "MuseScore"},
            {"instrument", true}};
        auto result = assignVstInstrument(
            score, selected(), source, false,
            QVariantMap{{"key", QUuid::createUuid().toString(QUuid::Id128)},
                        {"plugin", d},
                        {"isolated", isolate->isChecked()}});
        report(result);
        refresh();
        if (chain->count())
          chain->setCurrentRow(chain->count() - 1);
        return;
      }
      changeChain([&](QVariantList &list) {
        list.append(
            QVariantMap{{"key", QUuid::createUuid().toString(QUuid::Id128)},
                        {"plugin", d},
                        {"isolated", isolate->isChecked()}});
      });
      if (chain->count())
        chain->setCurrentRow(chain->count() - 1);
    });
    connect(openEffect, &QPushButton::clicked, this,
            [this] { editor(targetSlot(), chain->currentRow()); });
    connect(removeEffect, &QPushButton::clicked, this, [this] {
      const int i = chain->currentRow();
      changeChain([i](QVariantList &list) {
        if (i >= 0 && i < list.size())
          list.removeAt(i);
      });
    });
    connect(up, &QPushButton::clicked, this, [this] {
      const int i = chain->currentRow();
      changeChain([i](QVariantList &list) {
        if (i > 0 && i < list.size())
          list.swapItemsAt(i, i - 1);
      });
      chain->setCurrentRow(i - 1);
    });
    connect(down, &QPushButton::clicked, this, [this] {
      const int i = chain->currentRow();
      changeChain([i](QVariantList &list) {
        if (i >= 0 && i + 1 < list.size())
          list.swapItemsAt(i, i + 1);
      });
      chain->setCurrentRow(i + 1);
    });
    connect(chain, &QListWidget::currentRowChanged, this, [this] {
      if (!updating) {
        const int i = chain->currentRow();
        effectLoadPreset->setEnabled(i >= 0);
        effectSavePreset->setEnabled(i >= 0);
        removeEffect->setEnabled(i >= 0);
        openEffect->setEnabled(i >= 0);
        up->setEnabled(i > 0);
        down->setEnabled(i >= 0 && i + 1 < chain->count());
      }
    });
    connect(chain, &QListWidget::itemChanged, this,
            [this](QListWidgetItem *item) {
              if (updating)
                return;
              const int i = chain->row(item);
              const bool bypass = item->checkState() != Qt::Checked;
              editUnit(targetSlot(), i,
                       [bypass](QVariantMap &u) { u["bypass"] = bypass; });
            });
    connect(&watch, &QTimer::timeout, this, [this] {
      auto current = mscore && mscore->currentScore()
                         ? mscore->currentScore()->masterScore()
                         : nullptr;
      if (current != score) {
        score = current;
        removed.clear();
        savePending = false;
        closeVstParameterWindows(this);
        syncChannels();
        refresh();
      } else if (syncChannels())
        refresh();
      persistEdits();
      refreshRetained();
      if (lastCommonRevision!=commonPianoRevision()) {
        lastCommonRevision=commonPianoRevision();refresh();
      }
      if (backend() && (backend()->revision() != lastRevision ||
                        backend()->problems().join("\n") != lastProblems)) {
        lastProblems = backend()->problems().join("\n");
        lastRevision = backend()->revision();
        refresh();
      }
    });
    watch.start(500);
    connect(qApp, &QCoreApplication::aboutToQuit, this, [this] {
      if (scanner.state() != QProcess::NotRunning) {
        scanner.kill();
        scanner.waitForFinished(2000);
      }
      persistEdits();
    });
    resize(420, 600);
  }
  ~Panel() override {
    closing = true;
    watch.stop();
    disconnect(&scanner, nullptr, this, nullptr);
    closeVstParameterWindows(this);
    if (scanner.state() != QProcess::NotRunning) {
      scanner.kill();
      scanner.waitForFinished(2000);
    }
  }
  void select(Channel *c) {
    auto current = mscore && mscore->currentScore()
                       ? mscore->currentScore()->masterScore()
                       : nullptr;
    if (current != score) {
      score = current;
      removed.clear();
      savePending = false;
      closeVstParameterWindows(this);
    }
    syncChannels();
    if (c) {
      const int i = channels.indexOf(c);
      if (i >= 0)
        parts->setCurrentIndex(i);
    }
    refresh();
    show();
    raise();
  }
};
QPointer<Panel> panel;
} // namespace
void prepareVstForScore(MasterSynthesizer* master,Score* score) {
  prepareCommonPianos(master,score,false);
}
void routeVstChannels(MasterSynthesizer *master, Score *score) {
  auto engine = master ? static_cast<VstSynth *>(master->synthesizer("KumoVST"))
                       : nullptr;
  if (!engine)
    return;
  prepareCommonPianos(master,score,true);
  QVector<int> banks(256, 0);
  if (score)
    for (const auto &m : score->masterScore()->midiMapping()) {
      auto c = m.articulation();
      if (c && c->synti() == "KumoVST" && c->channel() < banks.size())
        banks[c->channel()] = c->bank();
    }
  routeCommonPianos(master,score,banks);
  engine->setRouting(banks);
  if (master==synti && mscore)
    for (auto details:mscore->findChildren<MixerDetails*>()) details->refreshPlaybackSource();
}
void restoreVstForScore(Score *score) {
  if (auto engine = backend()) {
    selectSourceSession(score);
    prepareCommonPianos(synti,score,false);
    if (score)
      engine->setState(
          score->masterScore()->synthesizerState().group("KumoVST"));
    else
      engine->clearState();
    routeVstChannels(synti, score);
    refreshVstParameterWindows();
  }
}
QVariantMap applyVstConfiguration(Score *score, const QVariantMap &value) {
  if (!personalFeaturesEnabled() || !backend())
    return {{"error", QObject::tr("请开启个人扩展功能后使用音源与效果。")}};
  if (!score)
    return {{"error", QObject::tr("请先打开乐谱。")}};
  score = score->masterScore();
  if (seq) {
    seq->stopWait();
    seq->waitForStoppedRendering();
  }
  auto state = score->synthesizerState();
  if (state.isDefault())
    state = synti->state();
  auto next = withConfiguration(state, value);
  score->startCmd();
  score->undo(new ChangeVstState(score, next));
  score->setInstrumentsChanged(true);
  score->endCmd();
  updateVstHistory(score);
  auto problems = backend()->problems();
  return problems.isEmpty()
             ? QVariantMap{{"message", QObject::tr("已更新音源与效果；Ctrl+Z "
                                                   "撤销，保存乐谱后保留。")}}
             : QVariantMap{{"message", problems.join('\n')}};
}
QVariantMap assignVstInstrument(Score *score, Channel *channel,
                                const QVariantMap &descriptor, bool isolated,
                                const QVariantMap &initialEffect,
                                const QVariantMap &copiedInstance) {
  if (!score || !channel || !backend() || !personalFeaturesEnabled())
    return {{"error", QObject::tr("请先打开乐谱并选择乐器。")}};
  if (!descriptor["instrument"].toBool())
    return {
        {"error", QObject::tr("请选择乐器音源；效果器可添加在下方效果链。")}};
  if (seq) {
    seq->stopWait();
    seq->waitForStoppedRendering();
  }
  score = score->masterScore();
  auto config = backend()->configuration();
  auto instances = config["slots"].toList();
  int id = 1;
  for (const auto &v : instances)
    id = qMax(id, v.toMap()["id"].toInt() + 1);
  if (id > 16383)
    return {{"error", QObject::tr("这个乐谱中的音源实例数量已达到上限。")}};
  QVariantMap original{{"synti", channel->synti()},
                       {"bank", channel->bank()},
                       {"program", channel->program()},
                       {"userBankController", channel->userBankController()}};
  QVariantList effects;
  if (channel->synti() == "KumoVST")
    for (const auto &v : instances)
      if (v.toMap()["id"].toInt() == channel->bank()) {
        original = v.toMap()["originalPatch"].toMap();
        effects = v.toMap()["effects"].toList();
      }
  if (!copiedInstance.isEmpty())
    effects = copiedInstance["effects"].toList();
  if (!initialEffect.isEmpty())
    effects.append(initialEffect);
  if (channel->synti() == "KumoVST") {
    bool shared = false;
    for (const auto &m : score->masterScore()->midiMapping())
      if (m.articulation() != channel &&
          m.articulation()->synti() == "KumoVST" &&
          m.articulation()->bank() == channel->bank())
        shared = true;
    if (!shared)
      for (int i = instances.size() - 1; i >= 0; --i)
        if (instances[i].toMap()["id"].toInt() == channel->bank())
          instances.removeAt(i);
  }
  instances.append(QVariantMap{
      {"id", id},
      {"label", descriptor["name"]},
      {"originalPatch", original},
      {"effects", effects},
      {"source", copiedInstance.isEmpty() ? QVariantMap{{"plugin", descriptor},
                                                        {"isolated", isolated}}
                                          : copiedInstance["source"].toMap()}});
  config["slots"] = instances;
  config["schema"] = 1;
  auto state = score->synthesizerState();
  if (state.isDefault())
    state = synti->state();
  MidiPatch patch;
  patch.synti = "KumoVST";
  patch.bank = id;
  patch.prog = 0;
  patch.drum = false;
  patch.sfid = 0;
  patch.name = descriptor["name"].toString();
  score->startCmd();
  score->undo(new ChangeVstState(score, withConfiguration(state, config)));
  score->undo(new ChangePatch(score, channel, &patch));
  score->undo(new SetUserBankController(channel, true));
  score->setInstrumentsChanged(true);
  score->endCmd();
  updateVstHistory(score);
  routeVstChannels(synti, score);
  if (seq)
    seq->initInstruments();
  return {{"message",
           backend()->problems().isEmpty()
               ? QObject::tr("已采用 %1；打开音源可选择预设，保存乐谱后保留。")
                     .arg(patch.name)
               : backend()->problems().join('\n')}};
}
QVariantMap copyVstInstrument(Score *score, Channel *channel, int bank) {
  if (!backend())
    return {{"error", QObject::tr("请先打开音源与效果。")}};
  for (const auto &v : backend()->configuration()["slots"].toList()) {
    auto slot = v.toMap();
    if (slot["id"].toInt() != bank)
      continue;
    auto source = slot["source"].toMap();
    return assignVstInstrument(score, channel, source["plugin"].toMap(),
                               source["isolated"].toBool(), {}, slot);
  }
  return {{"error", QObject::tr("请重新选择这份音源。")}};
}
void saveVstEdits(Score *score,bool fileSave) {
  if (!score || !mscore || !mscore->currentScore() ||
      mscore->currentScore()->masterScore() != score->masterScore() ||
      !personalFeaturesEnabled() || !backend())
    return;
  captureCommonPianoEdits(score);
  const auto value = backend()->configuration();
  const auto group = score->masterScore()->synthesizerState().group("KumoVST");
  if (!value.isEmpty() && (group.empty() || !VstHost::sameConfiguration(value,
      QJsonDocument::fromJson(group.front().data.toUtf8()).toVariant().toMap()))) {
    auto state = score->masterScore()->synthesizerState();
    if (state.isDefault())
      state = synti->state();
    score->startCmd();
    score->undo(new ChangeVstState(score->masterScore(), withConfiguration(state, value), true));
    score->endCmd();
    updateVstHistory(score);
  }
  bool commonActive=false;
  if (fileSave) for (const auto& mapping:score->masterScore()->midiMapping())
    if (backend()->pianoSlot(mapping.articulation()->channel())) {commonActive=true;break;}
  if (fileSave && commonActive && commonPianoSettings()["enabled"].toBool() && !scoreUsesOwnPiano(score)) {
    const auto settings=commonPianoSettings();
    auto c=backend()->configuration(false);
    const QVariantMap snapshot{{"schema",1},{"name",settings.value("name",QObject::tr("共用钢琴"))},
                               {"instance",settings["instance"]}};
    c["commonPianoSnapshot"]=snapshot;c["schema"]=1;
    // The last-used sound is saved alongside the local settings. Saving this
    // auxiliary copy does not add an undo step or change the channel's patch.
    score->masterScore()->setSynthesizerState(withConfiguration(score->masterScore()->synthesizerState(),c));
    backend()->rememberPianoSnapshot(snapshot);
  }
}
void showVstPanel(Channel *channel) {
  if (!personalFeaturesEnabled() || !mscore)
    return;
  if (!panel) {
    panel = new Panel(mscore);
    const bool restored = mscore->restoreDockWidget(panel);
    if (!restored)
      mscore->addDockWidget(Qt::RightDockWidgetArea, panel);
    for (auto dock : mscore->findChildren<QDockWidget *>())
      if (!restored && dock != panel && dock->isVisible() &&
          !dock->isFloating() &&
          mscore->dockWidgetArea(dock) == Qt::RightDockWidgetArea) {
        mscore->tabifyDockWidget(dock, panel);
        break;
      }
    auto action = mscore->findChild<QAction *>("kumo-vst");
    if (action)
      QObject::connect(panel, &QDockWidget::visibilityChanged, action,
                       [action](bool shown) {
                         QSignalBlocker b(action);
                         action->setChecked(shown);
                       });
  }
  panel->select(channel);
}
void addVstPanelAction(QMenu *menu) {
  if (!menu)
    return;
  // Menus are built inside the main-window constructor, before the global
  // mscore pointer has been assigned. Use the menu's own window at startup.
  MuseScore *window = nullptr;
  for (auto parent = menu->parentWidget(); parent && !window;
       parent = parent->parentWidget())
    window = qobject_cast<MuseScore *>(parent);
  if (!window)
    window = mscore;
  if (!window)
    return;
  auto action = window->findChild<QAction *>("kumo-vst");
  if (!action) {
    action = new QAction(QObject::tr("音源与效果（VST）"), window);
    action->setObjectName("kumo-vst");
    action->setCheckable(true);
    action->setIcon(personalToolIcon("kumo-vst"));
    markPersonalAction(action);
    Workspace::addActionAndString(action, "kumo-vst");
    QObject::connect(action, &QAction::triggered, window, [action](bool shown) {
      if (shown)
        showVstPanel();
      else if (panel)
        panel->hide();
      QSignalBlocker b(action);
      action->setChecked(panel && panel->isVisible());
    });
  }
  if (!menu->actions().contains(action))
    menu->addAction(action);
}
} // namespace Ms
#else
namespace Ms {
void addVstPanelAction(QMenu *) {}
void showVstPanel(Channel *) {}
void restoreVstForScore(Score *) {}
void saveVstEdits(Score *,bool) {}
QVariantMap copyVstInstrument(Score *, Channel *, int) { return {}; }
void routeVstChannels(MasterSynthesizer *, Score *) {}
void prepareVstForScore(MasterSynthesizer *, Score *) {}
QVariantMap applyVstConfiguration(Score *, const QVariantMap &) {
  return {{"error", QStringLiteral("此平台尚未提供 VST。")}};
}
QVariantMap assignVstInstrument(Score *, Channel *, const QVariantMap &, bool,
                                const QVariantMap &, const QVariantMap &) {
  return {{"error", QStringLiteral("此平台尚未提供 VST。")}};
}
} // namespace Ms
#endif
