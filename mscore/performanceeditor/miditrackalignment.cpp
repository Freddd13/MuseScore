// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "miditrackpane.h"
#include "performanceeditor.h"
#include "rollwriting.h"
#include "mscore/workflow/referencefactory.h"
#include "libmscore/staff.h"
#include "libmscore/select.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>
namespace Ms
{
void MidiTrackPane::alignTrack()
{
    if (!active())
        return;
    if (auto existing = findChild<QDialog*>("midiTrackAlignment")) {
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }
    auto dialog = new QDialog(this);
    dialog->setObjectName("midiTrackAlignment");
    dialog->setWindowTitle(tr("MIDI与原曲对齐"));
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModal(false);
    auto layout = new QVBoxLayout(dialog);
    auto form = new QFormLayout;
    layout->addLayout(form);
    const auto id = _activeId;
    const QPointer<MasterScore> score = _document.score();
    const auto track = _document.track(id);
    auto audio = new QComboBox(dialog);
    audio->setObjectName("midiTrackAlignmentAudio");
    audio->addItem(tr("独立录音时间"), QVariantMap());
    if (auto project = referenceProject())
        for (const auto& value : project->property("tracks").toList()) {
            const auto t = value.toMap();
            audio->addItem(t.value("name").toString(), QVariantMap{{"audioId", t.value("id")}});
            for (const auto& item : t.value("associations").toList()) {
                const auto association = item.toMap();
                audio->addItem(tr("%1 · 关联 %2–%3拍")
                                   .arg(t.value("name").toString())
                                   .arg(association.value("start").toDouble() / 480 + 1, 0, 'g', 4)
                                   .arg(association.value("end").toDouble() / 480 + 1, 0, 'g', 4),
                               QVariantMap{{"audioId", t.value("id")},
                                           {"associationId", association.value("id")}});
            }
        }
    for (int i = 0; i < audio->count(); ++i) {
        const auto item = audio->itemData(i).toMap();
        if (item.value("audioId") == track.value("audioId") &&
            item.value("associationId").toString() == track.value("associationId").toString())
            audio->setCurrentIndex(i);
    }
    form->addRow(tr("对应录音／谱段"), audio);
    auto offset = new QDoubleSpinBox(dialog);
    offset->setObjectName("midiTrackOffset");
    offset->setRange(-86400, 86400);
    offset->setDecimals(3);
    offset->setSingleStep(.01);
    offset->setSuffix(tr(" 秒"));
    offset->setValue(track.value("offsetMs").toDouble() / 1000);
    form->addRow(tr("整体偏移"), offset);
    auto help =
        new QLabel(tr("正偏移将MIDI向后移。内部对齐点表示“MIDI这一秒，对应原曲这一秒”；两个点之间逐"
                      "渐调整速度。原曲已关联乐谱时，选择该关联并切到“按谱对齐”。"));
    help->setWordWrap(true);
    layout->addWidget(help);
    auto table = new QTableWidget(0, 2, dialog);
    table->setObjectName("midiTrackAnchorTable");
    table->setHorizontalHeaderLabels({tr("MIDI时间（秒）"), tr("原曲时间（秒）")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(table, 1);
    auto addPoint = [table](double source, double target) {
        const int row = table->rowCount();
        table->insertRow(row);
        for (int c = 0; c < 2; ++c) {
            auto value = new QDoubleSpinBox(table);
            value->setDecimals(3);
            value->setRange(0, 86400);
            value->setSingleStep(.01);
            value->setFrame(false);
            value->setValue(c ? target : source);
            table->setCellWidget(row, c, value);
        }
    };
    for (const auto& value : track.value("anchors").toList()) {
        const auto anchor = value.toMap();
        addPoint(anchor.value("sourceMs").toDouble() / 1000,
                 anchor.value("targetMs").toDouble() / 1000);
    }
    auto row = new QHBoxLayout;
    layout->addLayout(row);
    auto add = new QPushButton(tr("添加对齐点"));
    row->addWidget(add);
    connect(add, &QPushButton::clicked, dialog,
            [this, addPoint] { addPoint(_cursorSeconds, _cursorSeconds); });
    auto remove = new QPushButton(tr("删除选中点"));
    row->addWidget(remove);
    connect(remove, &QPushButton::clicked, dialog, [table] {
        if (table->currentRow() >= 0)
            table->removeRow(table->currentRow());
    });
    auto clear = new QPushButton(tr("解除关联"));
    row->addWidget(clear);
    connect(clear, &QPushButton::clicked, dialog, [audio, offset, table] {
        audio->setCurrentIndex(0);
        offset->setValue(0);
        table->setRowCount(0);
    });
    row->addStretch();
    auto message = new QLabel(dialog);
    message->setWordWrap(true);
    layout->addWidget(message);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close, dialog);
    layout->addWidget(buttons);
    connect(buttons->button(QDialogButtonBox::Close), &QPushButton::clicked, dialog,
            &QDialog::close);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, dialog,
            [this, score, id, table, audio, offset, message] {
                if (!score) {
                    message->setText(tr("原乐谱已经关闭。"));
                    return;
                }
                QVariantList points;
                for (int r = 0; r < table->rowCount(); ++r)
                    points.append(QVariantMap{
                        {"sourceMs",
                         qobject_cast<QDoubleSpinBox*>(table->cellWidget(r, 0))->value() * 1000},
                        {"targetMs",
                         qobject_cast<QDoubleSpinBox*>(table->cellWidget(r, 1))->value() * 1000}});
                const auto chosen = audio->currentData().toMap();
                MidiTrackDocument target;
                target.setScore(score);
                if (!target.setAlignment(id, chosen.value("audioId").toString(),
                                         offset->value() * 1000, points,
                                         chosen.value("associationId").toString()))
                    message->setText(target.error());
                else {
                    contextChanged();
                    message->setText(tr("已更新对齐，Ctrl+Z可以恢复。"));
                }
            });
    dialog->resize(470, 400);
    dialog->show();
}
void MidiTrackPane::adoptToScore()
{
    if (!active() || !_editor->_rollWriting)
        return;
    finishGesture(true);
    auto dialog = new QDialog(this);
    dialog->setObjectName("midiTrackAdoptDialog");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(tr("将MIDI放入乐谱草稿"));
    auto form = new QFormLayout(dialog);
    auto trackBox = new QComboBox(dialog);
    for (int t = 0; t < _editor->_score->nstaves() * 4; ++t)
        trackBox->addItem(tr("谱表%1 · 声部%2").arg(t / 4 + 1).arg(t % 4 + 1), t);
    form->addRow(tr("写入位置"), trackBox);
    auto scope = new QComboBox(dialog);
    scope->setObjectName("midiTrackAdoptScope");
    scope->addItem(tr("当前选区"));
    scope->addItem(tr("MIDI覆盖的小节"));
    if (_editor->_score->selection().isNone())
        scope->setCurrentIndex(1);
    form->addRow(tr("范围"), scope);
    auto help =
        new QLabel(tr("先生成可试听的写谱草稿。检查网格、休止和声部后，点“使用草稿”写入乐谱。"));
    help->setWordWrap(true);
    form->addRow(help);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("查看草稿"));
    form->addRow(buttons);
    const QPointer<Score> score = _editor->_score;
    const auto id = _activeId;
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    connect(buttons, &QDialogButtonBox::accepted, dialog,
            [this, dialog, score, id, trackBox, scope] {
                if (!score || _editor->_score != score) {
                    showMessage(tr("切回原乐谱后继续生成草稿。"));
                    dialog->close();
                    return;
                }
                QVariantList rows;
                const auto track = _document.track(id);
                for (const auto& value : track.value("notes").toList()) {
                    const auto note = value.toMap();
                    if (!_selected.isEmpty() && !_selected.contains(note.value("id").toString()))
                        continue;
                    const int start = tickForSeconds(track, note.value("start").toDouble());
                    const int end =
                        qMax(start + 1, tickForSeconds(track, note.value("end").toDouble()));
                    rows.append(QVariantMap{{"pitch", note.value("pitch")},
                                            {"start", start},
                                            {"end", end},
                                            {"rawStart", start},
                                            {"rawEnd", end},
                                            {"velocity", note.value("velocity")}});
                }
                const int target = trackBox->currentData().toInt();
                const bool selectedScope = scope->currentIndex() == 0;
                setWorkspaceVisible(false);
                _editor->_rollWriting->openMidiDraft(rows, target, selectedScope);
                dialog->close();
            });
    dialog->resize(410, 180);
    dialog->show();
}
} // namespace Ms
