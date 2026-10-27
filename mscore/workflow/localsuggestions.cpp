// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "localsuggestions.h"
#include "arrangement.h"
#include "mscore/musescore.h"
#include "mscore/plugin/api/score.h"
#include <QRegularExpression>
#include <QUrl>
#include <limits>

namespace Ms {
namespace {
QString module(const QString &name, QString text) {
    text.remove(QRegularExpression("^\\.(pragma|import).*?$", QRegularExpression::MultilineOption));
    auto matches =
        QRegularExpression("^(?:function|var)\\s+([A-Za-z_$][\\w$]*)", QRegularExpression::MultilineOption)
            .globalMatch(text);
    QStringList exports;
    while (matches.hasNext()) {
        auto n = matches.next().captured(1);
        exports.append(n + ":" + n);
    }
    return "var " + name + "=(function(){\n" + text + "\nreturn {" + exports.join(',') + "};})();\n";
}
} // namespace
LocalSuggestionTask::LocalSuggestionTask(QObject *parent) : QObject(parent), _observer(this) {
    _pages.setInterval(0);
    connect(&_pages, &QTimer::timeout, this, &LocalSuggestionTask::page);
    connect(&_observer, &PluginAPI::ScoreObserver::readOnlyJobFinished, this,
            [this](int token, const QVariantMap &result) {
                if (!_running || token != _token)
                    return;
                if (_observer.analysisContentRevision() != _state["snapshotId"].toString()) {
                    finish(tr("乐谱已更新，请再点一次分析选段。"));
                    return;
                }
                if (result.contains("error")) {
                    finish(result["error"].toString());
                    return;
                }
                auto choices = result.value("options").toList();
                if (choices.isEmpty()) {
                    finish(result.value("messages").toStringList().join("；"));
                    return;
                }
                QVariantMap brief{{"scope", _state["scope"]},
                                  {"preserveTracks", result.value("preserveTracks", _settings.value("preserveTracks", QVariantList{0}))},
                                  {"localSettings", _settings}, {"localKind", _kind},
                                  {"pianoSettings", _state.value("pianoSettings").toMap()},
                                  {"preserveHarmony", true}};
                auto submitted = _observer.arrangementRequest(
                    "submitArrangementOptions",
                    {{"snapshotId", _state["snapshotId"]}, {"options", choices}, {"taskBrief", brief}});
                _running = false;
                if (submitted.contains("error")) {
                    emit progress(submitted["error"].toString());
                    return;
                }
                emit progress(tr("已准备%1种写法 · 选择后查看差异、试听或采用").arg(choices.size()));
                emit completed(result);
            });
}
void LocalSuggestionTask::cancel() {
    _pages.stop();
    _observer.cancelReadOnlyJob();
    _running = false;
    _token = 0;
    _readingPlayback = false;
}
void LocalSuggestionTask::finish(const QString &message) {
    cancel();
    emit progress(message.isEmpty() ? tr("这段暂未找到需要改变的写法。") : message);
}
void LocalSuggestionTask::start(Score *score, const QString &kind, const QVariantMap &settings) {
    cancel();
    if (!score) {
        emit progress(tr("先在乐谱中选中要分析的段落。"));
        return;
    }
    PluginAPI::Score wrapped(score);
    _observer.setScore(&wrapped);
    _state = _observer.arrangementState();
    if (_state.contains("error")) {
        emit progress(_state["error"].toString());
        return;
    }
    _kind = kind;
    _settings = settings;
    _data = {{"scope", _state["scope"]}, {"snapshotId", _state["snapshotId"]}};
    auto harmony = _state.value("harmonyAnalysis").toMap();
    if (harmony["snapshotId"] == _state["snapshotId"])
        _data["harmonyRegions"] = harmony["regions"];
    _config = _observer.loadConfiguration("ArrangementAssistant");
    // Voice roles and corrections belong to the current score, not the last file.
    _config.remove("roles");
    _config.remove("handOverrides");
    _config.remove("ignored");
    _config.remove("chords");
    const auto piano = _state.value("pianoSettings").toMap();
    for (auto it = piano.cbegin(); it != piano.cend(); ++it)
        _config[it.key()] = it.value();
    if (settings.value("hand").toString() == "left" || settings.value("hand").toString() == "right") {
        auto scope = _state["scope"].toMap();
        _config["handOverrides"] = QVariantList{QVariantMap{{"start", scope["start"]},
                                                            {"end", scope["end"]},
                                                            {"firstTrack", scope["firstTrack"]},
                                                            {"endTrack", scope["endTrack"]},
                                                            {"hand", settings["hand"]}}};
    }
    if (settings.contains("left"))
        _config["left"] = settings["left"];
    if (settings.contains("right"))
        _config["right"] = settings["right"];
    if (settings.contains("style"))
        _config["style"] = settings["style"];
    _cursor = _state["scope"].toMap()["start"].toInt();
    _limit = 32;
    _running = true;
    _pages.start();
    emit progress(tr("正在读取选段…"));
}
void LocalSuggestionTask::page() {
    if (_readingPlayback) {
        playbackPage();
        return;
    }
    const auto scope = _state["scope"].toMap();
    auto result = _observer.arrangementRange(
        {{"snapshotId", _state["snapshotId"]}, {"scope", scope}, {"fromTick", _cursor}, {"limit", _limit}});
    if (result.contains("error")) {
        finish(result["error"].toString());
        return;
    }
    for (const auto &name :
         QStringList{"notes", "rests", "meters", "harmonies", "hands", "pedals", "parts", "tempos"}) {
        auto rows = _data[name].toList();
        rows.append(result[name].toList());
        _data[name] = rows;
    }
    if (_data["notes"].toList().size() > 1024) {
        finish(tr("这段超过1024个音，请先选几小节比较，操作会更方便。"));
        return;
    }
    const double ms = result.value("milliseconds").toDouble();
    if (ms > 2)
        _limit = qMax(1, int(_limit * 2. / ms));
    emit progress(tr("已读取%1个音").arg(_data["notes"].toList().size()));
    if (!result.value("done").toBool()) {
        _cursor = result["nextTick"].toInt();
        return;
    }
    _readingPlayback = true;
    _data["playback"] = QVariantMap{{"scope", scope}, {"notes", _data["notes"]},
                                     {"meters", _data["meters"]},
                                     {"revision", _observer.analysisPlaybackRevision()},
                                     {"events", QVariantList{}}, {"prepared", false}};
    _cursor = std::numeric_limits<int>::min();
    _limit = 32;
    emit progress(tr("正在准备这段的实际起音、松键和踏板…"));
}
void LocalSuggestionTask::playbackPage() {
    if (_observer.analysisContentRevision() != _state["snapshotId"].toString()) {
        finish(tr("乐谱已更新，请再点一次分析选段。"));
        return;
    }
    auto playback = _data["playback"].toMap();
    const auto scope = _state["scope"].toMap();
    const auto revision = playback["revision"].toString();
    if (!playback["prepared"].toBool()) {
        auto result = _observer.analysisPlaybackPrepare(scope["start"].toInt(), scope["end"].toInt(), revision);
        if (result.contains("error")) {
            finish(tr("实际演奏资料读取失败：%1。请重新分析这段。").arg(result["error"].toString()));
            return;
        }
        emit progress(tr("准备实际演奏 · %1%").arg(result["progress"].toInt()));
        if (!_running)
            return;
        if (!result["ready"].toBool())
            return;
        for (const auto& field : {"ranges", "endTick", "endSeconds"})
            playback[field] = result[field];
        playback["prepared"] = true;
        _data["playback"] = playback;
        return;
    }
    auto result = _observer.analysisPlaybackPage(_cursor, scope["firstTrack"].toInt(),
                                                 scope["endTrack"].toInt(), _limit, revision);
    if (result.contains("error")) {
        finish(tr("实际演奏资料读取失败：%1。请重新分析这段。").arg(result["error"].toString()));
        return;
    }
    auto events = playback["events"].toList();
    events.append(result["events"].toList());
    playback["events"] = events;
    _data["playback"] = playback;
    if (result["milliseconds"].toDouble() > 2)
        _limit = qMax(1, int(_limit * 2. / result["milliseconds"].toDouble()));
    emit progress(tr("已读取实际演奏 · %1条音符与踏板事件").arg(events.size()));
    if (!_running)
        return;
    if (!result["done"].toBool()) {
        _cursor = result["nextTick"].toInt();
        return;
    }
    playback.remove("prepared");
    _data["playback"] = playback;
    beginCalculation();
}
void LocalSuggestionTask::beginCalculation() {
    if (!_running)
        return;
    _pages.stop();
    QString source = "(function(){\n";
    for (const auto &pair :
         QList<QPair<QString, QString>>{{"Harmony", "HarmonyAssistant/Harmony.js"},
                                        {"Timeline", "HarmonyAssistant/Timeline.js"},
                                        {"Playback", "ArrangementAssistant/Playback.js"},
                                        {"Rules", "ArrangementAssistant/Rules.js"},
                                        {"Suggestions", "ArrangementAssistant/Suggestions.js"}}) {
        auto text = _observer.readTextFile(
            QUrl::fromLocalFile(mscoreGlobalShare + "plugins/" + pair.second).toString());
        if (text.isEmpty()) {
            finish(tr("找不到编配规则文件：%1").arg(pair.second));
            return;
        }
        source += module(pair.first, text);
    }
    source += "return Suggestions.generate(input);})()";
    _token = _observer.startReadOnlyJob(
        source, {{"kind", _kind}, {"data", _data}, {"config", _config}, {"settings", _settings}});
    emit progress(tr("正在比较前后音、手位和声部…"));
}
} // namespace Ms
