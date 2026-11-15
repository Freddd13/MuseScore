// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "arrangement.h"
#include "toolicons.h"
#include "featuregate.h"
#include "localsuggestions.h"
#include "paneldock.h"
#include "workflowcontext.h"
#include "regionalcomparison.h"
#include "playbackpreview.h"
#include "panelwindowcontrols.h"
#include "workspaceviews.h"
#include "mscore/scoreview.h"
#include "mscore/musescore.h"
#include "mscore/plugin/api/score.h"
#include "mscore/plugin/api/scoreobserver.h"
#include "mscore/shortcut.h"
#include "mscore/plugin/qmlpluginengine.h"
#include "libmscore/measure.h"
#include "libmscore/chord.h"
#include "libmscore/note.h"
#include "libmscore/segment.h"
#include "libmscore/select.h"
#include "libmscore/repeatlist.h"
#include <QAction>
#include <QDockWidget>
#include <QLabel>
#include <QTextBrowser>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QUuid>
#include <QComboBox>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QPixmap>
#include <QTimer>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QSettings>
#include <QSet>
#include <QScrollArea>
#include <QTabBar>
#include <QToolButton>
#include <algorithm>
#include <limits>

namespace Ms {
namespace {
class SuggestionPanel : public QWidget {
    PluginAPI::ScoreObserver _observer;
    QTreeWidget *_choices;
    QTreeWidget *_changes;
    QLabel *_scope;
    QTextBrowser *_explanation;
    QLabel *_status;
    QString _selected;
    QPointer<ScoreView> _previewView;
    QPushButton *_showDifferences = nullptr;
    QPushButton *_cancelLocal = nullptr;
    QString _seenRevision;
    QVariantList _items;
    LocalSuggestionTask _local;
    QVariantMap _localSettings;
    QComboBox *_kind;
    QComboBox *_hand;
    QCheckBox *_lockFingers;
    QCheckBox *_preserveMelody;
    Score *score() const { return mscore ? mscore->currentScore() : nullptr; }
    bool attach() {
        if (!score())
            return false;
        PluginAPI::Score wrapped(score());
        _observer.setScore(&wrapped);
        return true;
    }
    QVariantMap selected() const {
        for (auto v : _items)
            if (v.toMap().value("id") == _selected)
                return v.toMap();
        return {};
    }
    QVariantMap inputSettings() const {
        auto settings=_localSettings;
        settings["hand"]=_hand->currentData();
        settings["lockFingers"]=_lockFingers->isChecked();
        settings["preserveMelody"]=_preserveMelody->isChecked();
        settings["preserveTracks"]=_preserveMelody->isChecked()?QVariantList{0}:QVariantList{};
        return settings;
    }
    bool matchesInputs(const QVariantMap& option) const {
        if(option.value("localKind").toString().isEmpty())return true;
        return option.value("localKind")==_kind->currentData()
            && option.value("localSettings").toMap()==inputSettings()
            && score() && option.value("pianoSettings").toMap()==score()->property("kumoPianoSettings").toMap();
    }
    bool sourceCurrent(const QVariantMap& option) {
        return !option.isEmpty() && attach()
            && option.value("snapshotId").toString() == _observer.analysisContentRevision();
    }
    void updateButtons() {
        const auto option=selected();const bool ready=!_local.running()&&!option.isEmpty()&&!option.value("used").toBool()&&matchesInputs(option)&&sourceCurrent(option);
        if(_cancelLocal)_cancelLocal->setEnabled(_local.running());
        for (auto button : findChildren<QPushButton *>()) {
            const auto name = button->objectName();
            if (!name.startsWith("arrangement-")) continue;
            const bool engraving = option.value("engravingOnly").toBool();
            if (name == "arrangement-compare" || name == "arrangement-copy") {
                button->setVisible(engraving);
                button->setEnabled(ready);
            } else if (name == "arrangement-original" || name == "arrangement-audition" ||
                       name == "arrangement-stop" || name == "arrangement-variant" ||
                       name == "arrangement-preview") {
                button->setVisible(!engraving);
                button->setEnabled(name == "arrangement-stop" ? !option.isEmpty() : ready);
            } else if (name == "arrangement-applySelection" || name == "arrangement-velocityView") {
                button->setVisible(!option.value("velocityDifferences").toList().isEmpty() &&
                    (name == "arrangement-velocityView" || option.value("velocityOnly").toBool()));
                button->setEnabled(ready && (name == "arrangement-velocityView" || !selectedVelocityIds().isEmpty()));
            } else {
                button->setEnabled(name == "arrangement-stop" ? !option.isEmpty() : ready);
            }
        }
    }
    void inputsChanged() {
        _local.cancel();stopPlaybackPreview(true);updateButtons();updateDifference();
        if(!selected().isEmpty()&&!matchesInputs(selected()))_status->setText(tr("设置已改变，点“分析选段”更新建议。"));
    }
    void updateDifference() {
        if (!mscore || !mscore->currentScoreView()) return;
        if (_previewView && _previewView != mscore->currentScoreView()) clearArrangementPreview(_previewView);
        const auto option = selected();
        _previewView = mscore->currentScoreView();
        if (isVisible() && !_local.running() && _showDifferences && _showDifferences->isChecked() && !option.isEmpty() &&
            !option.value("engravingOnly").toBool() && !option.value("used").toBool() && matchesInputs(option) && sourceCurrent(option)) {
            operate("preview");
        } else {
            clearArrangementPreview(_previewView);
        }
    }
    QVariantList selectedVelocityIds() const {
        QVariantList result;
        QSet<QString> seen;
        for (int i = 0; i < _changes->topLevelItemCount(); ++i) {
            const auto item = _changes->topLevelItem(i);
            const auto id = item->data(0, Qt::UserRole + 1).toString();
            if (!id.isEmpty() && item->checkState(0) == Qt::Checked && !seen.contains(id)) {
                seen.insert(id);
                result.append(id);
            }
        }
        return result;
    }
    QString engravingValue(const QString& kind, const QVariant& value) const {
        if (kind == "lineBreak")
            return value.toBool() ? tr("换行") : tr("随排版决定");
        if (kind == "symbolOffset") {
            const auto point = value.toMap();
            return tr("横 %1，纵 %2").arg(point.value("x").toDouble(), 0, 'g', 4)
                                       .arg(point.value("y").toDouble(), 0, 'g', 4);
        }
        if (kind == "measureStretch")
            return tr("%1%").arg(value.toDouble() * 100, 0, 'g', 4);
        return value.isValid() ? tr("%1 谱间").arg(value.toDouble(), 0, 'g', 4) : tr("自动");
    }
    void details() {
        auto option = selected();
        auto scope = option.value("scope").toMap();
        const auto velocity = option.value("velocityDifferences").toList();
        const auto engraving = option.value("engravingDifferences").toList();
        const bool onlyVelocity = option.value("velocityOnly").toBool() && !velocity.isEmpty();
        const auto first=scope.value("firstMeasure").toInt(),last=scope.value("lastMeasure").toInt();
        const auto measures=first==last?QString::number(first):tr("%1–%2").arg(first).arg(last);
        _scope->setText(option.isEmpty() ? tr("在乐谱中选几小节，选择要比较的内容，再点“分析选段”。Codex 发来的写法也列在这里。")
                                         : tr("第%1小节 · %2处变化")
                                               .arg(measures)
                                               .arg(!engraving.isEmpty() ? engraving.size()
                                                    : onlyVelocity ? velocity.size() : option.value("differences").toList().size()));
        _explanation->setPlainText(option.value("summary").toString());
        QSignalBlocker block(_changes);
        _changes->clear();
        _changes->setColumnHidden(1, onlyVelocity);
        _changes->headerItem()->setText(0, onlyVelocity ? tr("音符位置") : tr("位置"));
        _changes->headerItem()->setText(2, onlyVelocity ? tr("实际 MIDI 力度") : tr("变化"));
        if (!engraving.isEmpty()) {
            for (auto value : engraving) {
                const auto row = value.toMap();
                const auto kind = row.value("kind").toString();
                const auto position = tr("第 %1 小节").arg(row.value("measure").toInt());
                const auto change = tr("%1  %2 → %3").arg(row.value("label").toString(),
                    engravingValue(kind, row.value("before")), engravingValue(kind, row.value("after")));
                const auto staff = kind == "symbolOffset" || kind == "staffSpacing"
                    ? tr("谱表 %1").arg(row.value("track").toInt() / VOICES + 1) : tr("共用");
                auto item = new QTreeWidgetItem(_changes, {position, staff, change});
                item->setData(0, Qt::UserRole, row);
                item->setToolTip(2, row.value("reason").toString());
            }
        }
        for (auto value : onlyVelocity && !velocity.isEmpty() ? QVariantList{} : option.value("differences").toList()) {
            auto row = value.toMap();
            const auto old = row.value("current").toMap(), next = row.value("other").toMap();
            QStringList types;
            auto a = old.value("notes").toList(), b = next.value("notes").toList();
            if (old.value("duration") != next.value("duration") || old.value("rest") != next.value("rest"))
                types << tr("节奏");
            if (a.size() != b.size())
                types << tr("增减音");
            bool pitch = false, velocity = false, marks = false;
            for (int i = 0; i < qMin(a.size(), b.size()); ++i) {
                auto x = a[i].toMap(), y = b[i].toMap();
                pitch |= x.value("pitch") != y.value("pitch") || x.value("tpc1") != y.value("tpc1");
                velocity |= x.value("velocityType") != y.value("velocityType") ||
                            x.value("velocityRaw") != y.value("velocityRaw");
                if (x.value("fingerings") != y.value("fingerings")) {
                    auto from=x.value("fingerings").toStringList(),to=y.value("fingerings").toStringList();
                    types << tr("指法 %1 → %2").arg(from.isEmpty()?tr("未标"):from.join(" / "))
                                                .arg(to.isEmpty()?tr("清除"):to.join(" / "));
                }
                if (x.value("hands") != y.value("hands")) {
                    auto names=[&](const QVariant& value){QStringList out;for(auto hand:value.toStringList())out << (hand.startsWith("left")?tr("左手"):tr("右手"))+(hand.endsWith(":end")?tr("结束"):QString());return out.isEmpty()?tr("未标"):out.join(" / ");};
                    types << tr("分手 %1 → %2").arg(names(x.value("hands")),names(y.value("hands")));
                }
                marks |= x.value("otherMarks") != y.value("otherMarks");
            }
            if (pitch)
                types << tr("音高");
            if (velocity)
                types << tr("力度");
            if (marks)
                types << tr("标记");
            auto item = new QTreeWidgetItem(_changes, {tr("第%1小节").arg(row.value("measure").toInt()),
                                                       tr("谱表%1 · 声部%2")
                                                           .arg(row.value("track").toInt() / 4 + 1)
                                                           .arg(row.value("track").toInt() % 4 + 1),
                                                       types.join("、")});
            item->setData(0, Qt::UserRole, row);
            for(int column=0;column<3;++column)item->setToolTip(column,item->text(column));
        }
        QSet<QString> listed;
        for (auto value : velocity) {
            const auto row = value.toMap();
            const auto id = row.value("ref").toMap().value("id").toString();
            const auto role = row.value("role").toString();
            const auto roleName = role == "melody" ? tr("旋律") : role == "background" ? tr("背景")
                : role == "bass" ? tr("低音") : QString();
            const int before = row.value("before").toInt(), after = row.value("after").toInt();
            const auto change = tr("%1 → %2（%3%4）").arg(before).arg(after)
                .arg(after > before ? QStringLiteral("+") : QString()).arg(after - before);
            const auto position = tr("第%1小节 · 第%2拍").arg(row.value("displayMeasureNumber").toInt())
                .arg(row.value("beat").toDouble(), 0, 'g', 3);
            const auto track = tr("谱表%1 · 声部%2%3").arg(row.value("staff").toInt() + 1)
                .arg(row.value("voice").toInt() + 1).arg(roleName.isEmpty() ? QString() : QString(" · ") + roleName);
            const auto note = row.value("noteName").toString();
            const auto occurrence = tr("第%1遍").arg(row.value("occurrence").toInt());
            auto item = new QTreeWidgetItem(_changes, {
                onlyVelocity ? tr("%1 · %2.%3").arg(note).arg(row.value("displayMeasureNumber").toInt())
                    .arg(row.value("beat").toDouble(), 0, 'g', 3) : position,
                track, onlyVelocity ? change : note + ": " + change});
            item->setData(0, Qt::UserRole, row);
            const auto explanation = tr("%1 · %2 · %3\n%4 · %5")
                .arg(position, track, occurrence, change, row.value("reason").toString());
            item->setToolTip(0, explanation);
            item->setToolTip(2, explanation);
            auto font = item->font(2);
            font.setBold(true);
            item->setFont(2, font);
            if (onlyVelocity && !listed.contains(id)) {
                item->setData(0, Qt::UserRole + 1, id);
                item->setCheckState(0, Qt::Checked);
            }
            listed.insert(id);
        }
        updateButtons();
        _status->setText(option.isEmpty() ? tr("分析完成后，选择写法查看变化、试听或采用。") : option.value("used").toBool()
                             ? tr("已采用这版 · Ctrl+Z撤销 · Ctrl+S保存")
                             : !engraving.isEmpty() ? tr("选择一处调整，再点“排版前后图”。可以采用或另存副本。")
                             : tr("已选写法的变化显示在谱上；点列表中的位置查看相关音。"));
        if(!option.isEmpty()&&!option.value("used").toBool()&&!matchesInputs(option))_status->setText(tr("设置已改变，点“分析选段”更新建议。"));
        if(!option.isEmpty()&&!option.value("used").toBool()&&!sourceCurrent(option))
            _status->setText(tr("谱子有修改。请让Codex更新这段写法，或点“分析选段”重新计算。"));
    }
    void compareEngraving(QVariantMap request) {
        request.remove("operationId"); // Image pairs are recomputed on request and use the shared image cache.
        const auto item = _changes->currentItem();
        if (item)
            request["atTick"] = item->data(0, Qt::UserRole).toMap().value("tick");
        QDialog dialog(this);
        dialog.setWindowTitle(tr("排版前后图"));
        auto layout = new QVBoxLayout(&dialog);
        auto tools = new QHBoxLayout;
        auto whole = new QCheckBox(tr("显示整行"));
        whole->setChecked(true);
        auto fit = new QCheckBox(tr("适合窗口"));
        fit->setChecked(true);
        tools->addWidget(whole);
        tools->addWidget(fit);
        tools->addStretch();
        layout->addLayout(tools);
        auto views = new QHBoxLayout;
        layout->addLayout(views, 1);
        struct ImagePane { QScrollArea* view; QLabel* label; QPixmap image; };
        auto createPane = [&](const QString& title) {
            auto column = new QVBoxLayout;
            column->addWidget(new QLabel(title));
            auto view = new QScrollArea;
            auto label = new QLabel;
            label->setAlignment(Qt::AlignTop | Qt::AlignLeft);
            view->setWidget(label);
            view->setWidgetResizable(true);
            column->addWidget(view);
            views->addLayout(column, 1);
            return ImagePane{view, label, {}};
        };
        auto before = createPane(tr("原谱")), after = createPane(tr("建议排版"));
        auto status = new QLabel(tr("正在准备排版前后图…"));
        status->setWordWrap(true);
        layout->addWidget(status);
        auto close = new QDialogButtonBox(QDialogButtonBox::Close);
        connect(close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(close);
        auto draw = [&] {
            for (auto pane : {&before, &after}) {
                const int width = qMax(200, pane->view->viewport()->width() - 16);
                pane->label->setPixmap(fit->isChecked() && pane->image.width() > width
                    ? pane->image.scaledToWidth(width, Qt::SmoothTransformation) : pane->image);
            }
        };
        auto update = [&] {
            request["wholeSystem"] = whole->isChecked();
            const auto result = _observer.arrangementRequest("arrangementOption", request);
            status->setText(result.value("error").toString());
            for (const auto& key : {QString("before"), QString("after")}) {
                auto& pane = key == "before" ? before : after;
                pane.image = QPixmap();
                pane.image.loadFromData(QByteArray::fromBase64(
                    result.value(key).toMap().value("pngBase64").toString().toLatin1()), "PNG");
            }
            draw();
        };
        connect(whole, &QCheckBox::toggled, &dialog, update);
        connect(fit, &QCheckBox::toggled, &dialog, draw);
        dialog.resize(1100, 560);
        QTimer::singleShot(0, &dialog, update);
        dialog.exec();
    }
    void operate(const QString &action) {
        if (!attach() || _selected.isEmpty())
            return;
        if(action!="stop"&&!matchesInputs(selected())) {
            _status->setText(tr("设置已改变，点“分析选段”更新建议。"));return;
        }
        QVariantMap request{{"optionId", _selected}, {"action", action},
            {"operationId", QUuid::createUuid().toString(QUuid::WithoutBraces)}};
        if (action == "applySelection") {
            request["action"] = "apply";
            request["noteIds"] = selectedVelocityIds();
        }
        if (action == "velocityView") {
            mscore->showPerformanceEditor(true);
            if (auto tabs = mscore->findChild<QTabBar *>("workflowExpressionTabs"))
                tabs->setCurrentIndex(0);
            if (auto parameter = mscore->findChild<QComboBox *>("performanceParameter"))
                parameter->setCurrentIndex(0);
            _showDifferences->setChecked(true);
            updateDifference();
            return;
        }
        if (action == "compare") {
            compareEngraving(request);
            return;
        }
        if (action == "copy") {
            const auto original = score()->masterScore()->fileInfo();
            const auto initial = original->absoluteDir().filePath(original->completeBaseName() + tr(" · 雕版副本.mscz"));
            QFileDialog dialog(this, tr("保存排版副本"), initial, tr("MuseScore 乐谱 (*.mscz)"));
            dialog.setAcceptMode(QFileDialog::AcceptSave);
            dialog.setDefaultSuffix("mscz");
            if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
                return;
            const auto path = dialog.selectedFiles().first();
            request["destination"] = path;
            request["overwrite"] = QFileInfo::exists(path);
        }
        const auto result = _observer.arrangementRequest("arrangementOption", request);
        if (result.contains("error"))
            _status->setText(result.value("error").toString());
        else if (action == "preview") {
            if (!result.value("velocityDifferences").toList().isEmpty())
                _status->setText(tr("点列表看改动的音。用“力度对照”在演奏编辑器看原值和建议值。"));
            else
                _status->setText(tr("已在谱上显示%1处变化。点列表中的位置查看相关音。").arg(result.value("differences").toList().size()));
        }
        else if (action == "audition" || action == "original") {
            auto from=score()->tick2measure(Fraction::fromTicks(score()->masterScore()->repeatList().utick2tick(result.value("fromUtick").toInt())));
            const auto label=action=="audition"?tr("新写法"):tr("谱中写法");
            _status->setText(from ? tr("正在听：%1 · 从第%2小节开始%3").arg(label).arg(from->no()+1)
                .arg(from->tick().ticks()<selected().value("scope").toMap().value("start").toInt()?tr("（含进入这段的前文）"):QString())
                : tr("正在听：%1").arg(label));
        }
        else if (action == "variant")
            _status->setText(tr("已保存另一种写法，可在“分段试写”切换。Ctrl+S保存乐谱。"));
        else if (action == "copy")
            _status->setText(tr("已保存排版副本：%1").arg(result.value("path").toString()));
        else if (action == "apply" || action == "applySelection")
            refresh();
        else if (action == "stop")
            _status->setText(tr("已停止比较，可以继续编辑乐谱。"));
    }

    QVariantMap velocityMarks() {
        if (!attach()) return {};
        const auto saved = score()->QObject::property("kumoVelocityRoleSelection").toMap();
        return saved.value("snapshotId") == _observer.analysisContentRevision() ? saved : QVariantMap{};
    }
    void markVelocityRole(const QString &role) {
        if (!attach()) return;
        const auto notes = score()->selection().noteList();
        if (notes.empty()) {
            _status->setText(tr("先在谱上选音，再标记力度角色。"));
            return;
        }
        auto marks = velocityMarks();
        auto roles = marks.value("roles").toMap();
        auto locked = marks.value("locked").toStringList();
        int count = 0;
        for (const auto note : notes) {
            const auto id = arrangementNoteIdentity(note);
            if (id.isEmpty()) continue;
            if (role == "lock") {
                if (!locked.contains(id)) locked.append(id);
            } else if (role == "clear") {
                roles.remove(id);
                locked.removeAll(id);
            } else {
                roles[id] = role;
            }
            ++count;
        }
        score()->QObject::setProperty("kumoVelocityRoleSelection",
            QVariantMap{{"snapshotId", _observer.analysisContentRevision()}, {"roles", roles}, {"locked", locked}});
        _status->setText(tr("已标记 %1 个音。重新选取乐句后，点“力度建议…”预览。").arg(count));
    }
    void proposeVelocity() {
        if (!attach()) {
            _status->setText(tr("先打开乐谱，选取要处理的乐句。"));
            return;
        }
        const auto state = _observer.arrangementState({});
        if (state.contains("error")) {
            _status->setText(state.value("error").toString());
            return;
        }
        const auto scope = state.value("scope").toMap();
        const auto marks = velocityMarks();
        const int start = scope.value("start").toInt(), end = scope.value("end").toInt();
        QDialog dialog(this);
        dialog.setWindowTitle(tr("力度建议"));
        auto outer = new QVBoxLayout(&dialog);
        auto scroll = new QScrollArea(&dialog);
        scroll->setWidgetResizable(true);
        auto controls = new QWidget;
        auto form = new QFormLayout(controls);
        scroll->setWidget(controls);
        outer->addWidget(scroll);
        auto kind = new QComboBox;
        kind->addItem(tr("突出指定旋律"), "melody");
        kind->addItem(tr("收轻指定背景"), "background");
        kind->addItem(tr("向句峰推进后收回"), "phrase");
        form->addRow(tr("选段"), new QLabel(tr("第 %1–%2 小节").arg(scope.value("firstMeasure").toInt()).arg(scope.value("lastMeasure").toInt())));
        form->addRow(tr("效果"), kind);
        auto amount = new QSpinBox;
        amount->setRange(1, 24);
        amount->setValue(3);
        form->addRow(tr("MIDI 力度幅度"), amount);
        auto peak = new QComboBox;
        QSet<int> tracks;
        int peakDistance = std::numeric_limits<int>::max(), centerIndex = 0;
        for (auto segment = score()->tick2segment(Fraction::fromTicks(start), false, SegmentType::ChordRest);
             segment && segment->tick().ticks() < end; segment = segment->next1(SegmentType::ChordRest)) {
            const int tick = segment->tick().ticks();
            bool sounds = false;
            for (int track = scope.value("firstTrack").toInt(); track < scope.value("endTrack").toInt(); ++track) {
                const auto item = segment->element(track);
                if (item && item->isChord()) {
                    tracks.insert(track);
                    sounds = true;
                }
            }
            if (!sounds) continue;
            const auto measure = segment->measure();
            const double beat = 1. + (tick - measure->tick().ticks()) / (DIVISION * 4. / measure->timesig().denominator());
            const auto distance = qAbs(tick - (start + (end - start) / 2));
            if (distance < peakDistance) {
                peakDistance = distance;
                centerIndex = peak->count();
            }
            peak->addItem(tr("第 %1 小节 · 第 %2 拍").arg(measure->no() + 1).arg(beat, 0, 'g', 3), tick);
        }
        peak->setCurrentIndex(centerIndex);
        form->addRow(tr("句峰"), peak);
        peak->setEnabled(false);
        connect(kind, QOverload<int>::of(&QComboBox::currentIndexChanged), peak,
            [kind, peak] { peak->setEnabled(kind->currentData() == "phrase"); });
        auto manual = new QCheckBox(tr("保留手调力度及自定义发音"));
        manual->setChecked(true);
        form->addRow(manual);
        auto marked = new QLabel(tr("已标角色 %1 音，已锁定 %2 音。逐音标记优先于声部。")
            .arg(marks.value("roles").toMap().size()).arg(marks.value("locked").toStringList().size()));
        marked->setWordWrap(true);
        form->addRow(marked);
        QMap<int, QComboBox *> roleInputs;
        auto sorted = tracks.values();
        std::sort(sorted.begin(), sorted.end());
        const auto defaults = score()->QObject::property("kumoPianoSettings").toMap().value("roles").toMap();
        for (int track : sorted) {
            auto role = new QComboBox;
            role->addItem(tr("未指定"), "");
            role->addItem(tr("旋律"), "melody");
            role->addItem(tr("背景"), "background");
            role->addItem(tr("低音"), "bass");
            const auto current = defaults.value(QString::number(track)).toMap().value("role").toString();
            const auto index = role->findData(current == "accompaniment" || current == "inner" ? "background" : current);
            if (index >= 0) role->setCurrentIndex(index);
            roleInputs.insert(track, role);
            form->addRow(tr("谱表 %1 · 声部 %2").arg(track / VOICES + 1).arg(track % VOICES + 1), role);
        }
        auto buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
        auto generate = buttons->addButton(tr("生成建议"), QDialogButtonBox::AcceptRole);
        generate->setEnabled(!tracks.isEmpty());
        outer->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        dialog.resize(392, 420);
        if (dialog.exec() != QDialog::Accepted) return;
        QVariantMap roles;
        for (auto it = roleInputs.cbegin(); it != roleInputs.cend(); ++it)
            roles.insert(QString::number(it.key()), it.value()->currentData());
        QVariantList locked;
        for (const auto &id : marks.value("locked").toStringList()) locked.append(id);
        const auto result = _observer.arrangementRequest("proposeVelocityAdjustment",
            {{"snapshotId", state.value("snapshotId")}, {"scope", scope}, {"kind", kind->currentData()},
             {"amount", amount->value()}, {"peakTick", peak->currentData()}, {"preserveManual", manual->isChecked()},
             {"roles", roles}, {"noteRoles", marks.value("roles")}, {"lockedNotes", locked}});
        if (result.contains("error")) _status->setText(result.value("error").toString());
        else {
            const auto options = result.value("options").toList();
            if (!options.isEmpty()) _selected = options.first().toMap().value("id").toString();
            refresh();
            operate("velocityView");
        }
    }

    void hideEvent(QHideEvent* event) override {_local.cancel();QWidget::hideEvent(event);updateDifference();}

  public:
    explicit SuggestionPanel(QWidget *parent) : QWidget(parent), _observer(this), _local(this) {
        setObjectName("arrangementSuggestionPanel");
        auto layout = new QVBoxLayout(this);
        layout->setContentsMargins(6, 6, 6, 6);
        auto analysis = new QHBoxLayout;
        _kind = new QComboBox;
        _kind->setObjectName("localSuggestionKind");
        _kind->addItem(tr("指法"), "fingering");
        _kind->addItem(tr("换手"), "handoff");
        _kind->addItem(tr("配音"), "voicing");
        _kind->addItem(tr("对位"), "counterpoint");
        analysis->addWidget(_kind, 1);
        auto generate = new QPushButton(tr("分析选段"));
        setComponentToolIcon(generate,"refresh");
        generate->setObjectName("localSuggestionStart");
        analysis->addWidget(generate);
        auto cancel = _cancelLocal = new QPushButton(tr("取消"));
        cancel->setObjectName("localSuggestionCancel");
        setComponentToolIcon(cancel,"stop");
        analysis->addWidget(cancel);
        auto settings = new QPushButton(tr("参数…"));
        setComponentToolIcon(settings,"settings");
        analysis->addWidget(settings);
        layout->addLayout(analysis);
        auto choices = new QHBoxLayout;
        _hand = new QComboBox;
        _hand->setObjectName("localSuggestionHand");
        _hand->addItem(tr("按谱面分手"), "auto");
        _hand->addItem(tr("选段用右手"), "right");
        _hand->addItem(tr("选段用左手"), "left");
        choices->addWidget(_hand);
        _lockFingers = new QCheckBox(tr("保留指法"));
        _lockFingers->setChecked(true);
        choices->addWidget(_lockFingers);
        _preserveMelody = new QCheckBox(tr("保留旋律"));
        _preserveMelody->setObjectName("localSuggestionPreserveMelody");
        _preserveMelody->setChecked(true);
        choices->addWidget(_preserveMelody);
        layout->addLayout(choices);
        _localSettings = QSettings().value("arrangementSuggestions/parameters").toMap();
        connect(generate, &QPushButton::clicked, this, [this] {
            stopPlaybackPreview(true);
            if(mscore&&mscore->currentScoreView())clearArrangementPreview(mscore->currentScoreView());
            _local.start(score(), _kind->currentData().toString(), inputSettings());
        });
        connect(cancel, &QPushButton::clicked, this, [this] {
            _local.cancel();
            updateButtons();
            _status->setText(tr("已取消，可以重新选段。"));
        });
        connect(&_local, &LocalSuggestionTask::progress, this,
                [this](const QString &text) { _status->setText(text);updateButtons(); });
        connect(&_local, &LocalSuggestionTask::completed, this, [this] { refresh(); });
        connect(settings, &QPushButton::clicked, this, [this] {
            QDialog dialog(this);
            dialog.setWindowTitle(tr("编配建议参数"));
            auto form = new QFormLayout(&dialog);
            QMap<QString, QSpinBox *> spans;
            QMap<QString, QDoubleSpinBox *> costs;
            auto defaults = _observer.loadConfiguration("ArrangementAssistant");
            for (auto side : QStringList{"left", "right"})
                for (auto name : QStringList{"comfort", "maximum", "white"}) {
                    auto box = new QSpinBox;
                    box->setRange(1, 36);
                    auto values = _localSettings.value(side, defaults[side]).toMap();
                    box->setValue(values.value(name, name == "comfort"   ? 14
                                                     : name == "maximum" ? 15
                                                                         : 16)
                                      .toInt());
                    spans[side + "/" + name] = box;
                    form->addRow((side == "left" ? tr("左手") : tr("右手")) + " · " +
                                     (name == "comfort"   ? tr("舒适跨度")
                                      : name == "maximum" ? tr("最大跨度")
                                                          : tr("两端白键上限")) +
                                     tr("（半音）"),
                                 box);
                }
            auto style = new QComboBox;
            style->addItems({tr("调性钢琴／流行"), tr("严格对位"), tr("爵士")});
            style->setCurrentIndex(_localSettings.value("style", defaults.value("style", 0)).toInt());
            form->addRow(tr("风格"), style);
            const QStringList names{"stretch", "move", "blackThumb", "crossing", "repeat"},
                labels{tr("和弦伸展"), tr("移动手位"), tr("黑键拇指"), tr("穿指／跨指"), tr("同音换指")};
            const QList<double> initial{1, 1, .6, 1.2, .35};
            for (int i = 0; i < names.size(); ++i) {
                auto box = new QDoubleSpinBox;
                box->setRange(0, 10);
                box->setSingleStep(.1);
                box->setValue(_localSettings["weights"].toMap().value(names[i], initial[i]).toDouble());
                costs[names[i]] = box;
                form->addRow(labels[i] + tr("的重视程度"), box);
            }
            auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
            form->addRow(buttons);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            if (dialog.exec() != QDialog::Accepted)
                return;
            for (auto side : QStringList{"left", "right"}) {
                QVariantMap values;
                for (auto name : QStringList{"comfort", "maximum", "white"})
                    values[name] = spans[side + "/" + name]->value();
                _localSettings[side] = values;
            }
            QVariantMap values;
            for (auto name : names)
                values[name] = costs[name]->value();
            _localSettings["weights"] = values;
            _localSettings["style"] = style->currentIndex();
            QSettings().setValue("arrangementSuggestions/parameters", _localSettings);
            inputsChanged();
        });
        auto velocity = new QHBoxLayout;
        layout->addLayout(velocity);
        auto propose = new QPushButton(tr("力度建议…"));
        propose->setObjectName("proposeVelocityAdjustment");
        velocity->addWidget(propose);
        connect(propose, &QPushButton::clicked, this, &SuggestionPanel::proposeVelocity);
        auto mark = new QToolButton;
        mark->setText(tr("标记选中音"));
        mark->setPopupMode(QToolButton::InstantPopup);
        auto roles = new QMenu(mark);
        const QPair<QString, QString> labels[] = {
            {tr("旋律"), "melody"}, {tr("背景"), "background"}, {tr("低音"), "bass"},
            {tr("锁定力度"), "lock"}, {tr("清除标记及锁定"), "clear"}};
        for (const auto &entry : labels)
            connect(roles->addAction(entry.first), &QAction::triggered, this,
                [this, role = entry.second] { markVelocityRole(role); });
        mark->setMenu(roles);
        velocity->addWidget(mark);
        velocity->addStretch();
        _scope = new QLabel;
        _scope->setWordWrap(true);
        layout->addWidget(_scope);
        _choices = new QTreeWidget;
        _choices->setObjectName("arrangementChoices");
        _choices->setMinimumHeight(82);
        _choices->setMaximumHeight(126);
        _choices->setHeaderLabels({tr("写法"), tr("状态")});
        _choices->setRootIsDecorated(false);
        _choices->header()->setStretchLastSection(false);
        _choices->header()->setSectionResizeMode(0,QHeaderView::Stretch);
        _choices->header()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
        layout->addWidget(_choices, 2);
        _explanation = new QTextBrowser;
        _explanation->setMinimumHeight(64);
        _explanation->setMaximumHeight(110);
        layout->addWidget(_explanation);
        _changes = new QTreeWidget;
        _changes->setObjectName("arrangementChanges");
        _changes->setHeaderLabels({tr("位置"), tr("声部"), tr("变化")});
        _changes->setRootIsDecorated(false);
        _changes->header()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
        _changes->header()->setSectionResizeMode(1,QHeaderView::ResizeToContents);
        _changes->header()->setSectionResizeMode(2,QHeaderView::Stretch);
        layout->addWidget(_changes, 2);
        auto controls = new QHBoxLayout;
        layout->addLayout(controls);
        auto button = [&](const QString &label, const QString &action, QHBoxLayout *row) {
            auto b = new QPushButton(label);
            b->setObjectName("arrangement-" + action);
            setComponentToolIcon(b, action=="stop"?"stop":action.startsWith("apply")?"apply":action=="variant"?"copy":"audition");
            row->addWidget(b);
            connect(b, &QPushButton::clicked, this, [this, action] { operate(action); });
            return b;
        };
        _showDifferences = new QPushButton(tr("谱面差异"));
        _showDifferences->setObjectName("arrangement-preview");
        _showDifferences->setCheckable(true);
        _showDifferences->setChecked(QSettings().value("arrangementSuggestions/showDifferences", true).toBool());
        _showDifferences->setToolTip(tr("选中写法后，在原谱上显示另一写法；关闭后收起差异。"));
        controls->addWidget(_showDifferences);
        connect(_showDifferences, &QPushButton::clicked, this, [this](bool checked) {
            QSettings().setValue("arrangementSuggestions/showDifferences", checked);
            updateDifference();
        });
        button(tr("试听谱中"), "original", controls);
        button(tr("试听新写法"), "audition", controls);
        button(tr("排版前后图…"), "compare", controls);
        controls->addStretch();
        auto apply = new QHBoxLayout;
        layout->addLayout(apply);
        button(tr("使用这版"), "apply", apply);
        button(tr("存为另一种写法"), "variant", apply);
        button(tr("保存排版副本…"), "copy", apply);
        button(tr("停止比较"), "stop", apply);
        apply->addStretch();
        auto velocityApply = new QHBoxLayout;
        layout->addLayout(velocityApply);
        button(tr("使用勾选改动"), "applySelection", velocityApply);
        button(tr("力度对照"), "velocityView", velocityApply);
        velocityApply->addStretch();
        _status = new QLabel;
        _status->setWordWrap(true);
        _status->setMinimumHeight(46);
        layout->addWidget(_status);
        connect(_kind,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{inputsChanged();});
        connect(_hand,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{inputsChanged();});
        connect(_lockFingers,&QCheckBox::toggled,this,[this]{inputsChanged();});
        connect(_preserveMelody,&QCheckBox::toggled,this,[this]{inputsChanged();});
        connect(_choices, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item) {
            _selected = item ? item->data(0, Qt::UserRole).toString() : QString();
            details();
            updateDifference();
        });
        connect(_changes, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item) {
            if (!attach())
                return;
            auto row = item->data(0, Qt::UserRole).toMap();
            QVariantMap target{{"start", row["tick"]}, {"end", row["tick"].toInt() + 1},
                {"firstTrack", row["track"]}, {"endTrack", row["track"].toInt() + 1}};
            const auto id = row.value("ref").toMap().value("id").toString().split(':');
            if (id.size() >= 3 && row.contains("writtenPitch")) {
                target.insert("tick", id[0].toInt());
                target.insert("track", id[1].toInt());
                target.insert("index", id[2].toInt());
                target.insert("writtenPitch", row["writtenPitch"]);
                target.insert("writtenTpc", row["writtenTpc"]);
                if (id.size() > 3)
                    target.insert("graceIndex", id[3].mid(1).toInt());
            }
            _observer.navigateWorkflow(target, false);
            if (selected().value("engravingOnly").toBool()) {
                const auto reason = row.value("reason").toString();
                _status->setText(reason.isEmpty() ? tr("点击“排版前后图”查看这一处。") : reason);
            } else if (row.contains("before"))
                _status->setText(tr("%1 · 谱表%2 · 声部%3 · 第%4遍 · %5 → %6")
                    .arg(row.value("noteName").toString()).arg(row.value("staff").toInt() + 1)
                    .arg(row.value("voice").toInt() + 1).arg(row.value("occurrence").toInt())
                    .arg(row.value("before").toInt()).arg(row.value("after").toInt()));
        });
        connect(_changes, &QTreeWidget::itemChanged, this, [this] { updateButtons(); });
        connect(workflowContext(), &WorkflowContext::activeScoreChanged, this, [this] {
            _local.cancel();
            refresh();
        });
        if(mscore)connect(mscore->getPluginEngine(),&QmlPluginEngine::endCmd,this,[this]{
            if(isVisible()&&!_local.running()&&!_items.isEmpty()&&attach()
                &&_seenRevision!=_observer.analysisContentRevision())refresh();
        });
        refresh();
    }
    void refresh() {
        _items = arrangementSuggestions(score()).value("options").toList();
        _seenRevision=attach()?_observer.analysisContentRevision():QString();
        QSignalBlocker block(_choices);
        _choices->clear();
        for (auto v : _items) {
            auto row = v.toMap();
            auto item =
                new QTreeWidgetItem(_choices, {row.value("title").toString(),
                                               row.value("used").toBool() ? tr("已采用") : sourceCurrent(row) ? tr("可比较") : tr("谱子已修改")});
            item->setData(0, Qt::UserRole, row["id"]);
            item->setToolTip(0,row.value("title").toString());
            if (row["id"] == _selected)
                _choices->setCurrentItem(item);
        }
        if (!_choices->currentItem() && _choices->topLevelItemCount())
            _choices->setCurrentItem(_choices->topLevelItem(0));
        _selected =
            _choices->currentItem() ? _choices->currentItem()->data(0, Qt::UserRole).toString() : QString();
        details();
        updateDifference();
    }
    bool auditioning() const {
        return score() && playbackPreviewPrepared(score(), _selected)
            && (playbackPreviewActive(score(), false) || playbackPreviewActive(score(), true));
    }
};
QPointer<QDockWidget> dock;
QPointer<SuggestionPanel> panel;
} // namespace
void notifyArrangementSuggestions(Score *) {
    if (panel)
        panel->refresh();
}
void showSuggestionPanel(bool visible) {
    if (!personalFeaturesEnabled()) return;
    if (!mscore)
        return;
    if (!dock && visible) {
        dock = new QDockWidget(QObject::tr("编配建议"), mscore);
        panel = new SuggestionPanel(dock);
        dock->setWidget(panel);
        dock->setMinimumWidth(280);
        arrangeWorkflowDock(mscore, dock, "ArrangementSuggestions", "rightTools", Qt::RightDockWidgetArea);
        addEditorDockActions(dock);
        installPanelWindowControls(dock)->setTransport(
            [] { return panel && panel->auditioning(); },
            [] { if (panel && panel->auditioning()) stopPlaybackPreview(true); });
        auto action = getAction("arrangement-suggestions");
    markPersonalAction(action);
        QObject::connect(dock, &QDockWidget::visibilityChanged, action, [action](bool on) {
            QSignalBlocker block(action);
            action->setChecked(on);
        });
    }
    if (dock) {
        dock->setVisible(visible);
        if (visible) {
            panel->refresh();
            dock->raise();
        }
    }
}
void addSuggestionPanelAction(QMenu *menu) {
    auto action = getAction("arrangement-suggestions");
    markPersonalAction(action);
    action->setCheckable(true);
    menu->addAction(action);
}
} // namespace Ms
