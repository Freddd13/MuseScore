// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "regionalpanel.h"
#include "toolicons.h"
#include "controlflowlayout.h"
#include "aiconnection.h"
#include "featuregate.h"
#include "libmscore/chord.h"
#include "libmscore/measure.h"
#include "libmscore/note.h"
#include "libmscore/part.h"
#include "libmscore/regionalvariants.h"
#include "libmscore/select.h"
#include "libmscore/staff.h"
#include "mscore/musescore.h"
#include "mscore/plugin/api/score.h"
#include "mscore/plugin/api/scoreobserver.h"
#include "mscore/scoretab.h"
#include "mscore/scoreview.h"
#include "mscore/seq.h"
#include "mscore/shortcut.h"
#include "paneldock.h"
#include "referencebus.h"
#include "referencefactory.h"
#include "regionalcontrols.h"
#include "regionalcomparison.h"
#include "temporaryscore.h"
#include "playbackpreview.h"
#include "libmscore/undo.h"
#include <QCheckBox>
#include <QComboBox>
#include <QBuffer>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMap>
#include <QSignalBlocker>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSet>
#include <QSpinBox>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <memory>
#include <algorithm>
namespace Ms {
namespace {
QString rangeNumbers(const QList<int>& numbers) {
      QStringList parts;
      for (int i = 0; i < numbers.size(); ++i) {
            const int first = numbers[i];
            while (i + 1 < numbers.size() && numbers[i + 1] == numbers[i] + 1) ++i;
            parts << (first == numbers[i] ? QString::number(first)
                                        : QString("%1–%2").arg(first).arg(numbers[i]));
      }
      return parts.join("、");
}
QString scopeSummary(const QVariantList& tracks) {
      QMap<int, QList<int>> staves;
      for (const auto& track : tracks) {
            auto& voices = staves[track.toInt() / VOICES + 1];
            const int voice = track.toInt() % VOICES + 1;
            if (!voices.contains(voice)) voices.append(voice);
      }
      QMap<QString, QList<int>> sameVoices;
      for (auto staff = staves.begin(); staff != staves.end(); ++staff) {
            std::sort(staff.value().begin(), staff.value().end());
            const auto voices = staff.value().size() == VOICES ? QObject::tr("全部声部")
                : QObject::tr("声部%1").arg(rangeNumbers(staff.value()));
            sameVoices[voices].append(staff.key());
      }
      QStringList summary;
      for (auto voices = sameVoices.begin(); voices != sameVoices.end(); ++voices)
            summary << QObject::tr("谱表%1 · %2").arg(rangeNumbers(voices.value()), voices.key());
      return summary.join("；");
}
class RegionalPanel : public QWidget {
      QTreeWidget *_tree;
      QLabel *_status;
      QLabel *_scope;
      QLabel *_help;
      QScrollArea *_quickStrip;
      QPushButton *_delete = nullptr, *_deleteCombination = nullptr;
      QCheckBox *_scoreButtons = nullptr;
      QHBoxLayout *_choices;
      QTimer _refresh;
      QCheckBox *_compare,*_listen;
      QComboBox *_compareWith,*_combinations;
      QTreeWidget *_differences;
      QLabel* _differenceStatus=nullptr;
      TemporaryScore _other;
      ScoreContentState _comparedState;
      QString _comparedGroup,_comparedVariant;
      QPointer<ScoreView> _compareView;
      bool _refreshing=false;
      void clearDifference(){if(_compareView)showRegionalDifference(_compareView,nullptr,{});_other.reset();_comparedGroup.clear();_comparedVariant.clear();if(_differences)_differences->clear();}
      void compare(){
            if(_refreshing)return;
            _compareWith->setVisible(_compare->isChecked());_differences->setVisible(_compare->isChecked());_differenceStatus->setVisible(_compare->isChecked());
            if(!_compare->isChecked()||!score()||!isVisible()){clearDifference();return;}
            const auto target=_compareWith->currentData().toString();auto view=mscore->currentScoreView();
            if(target.isEmpty()||!view||view->score()!=score()){clearDifference();_differenceStatus->setText(view&&view->score()!=score()?tr("请打开总谱查看这组写法的差异。"):tr("增加另一种写法后，在这里选择要对比的写法。"));return;}
            if(seq&&seq->isPlaying())return;
            if(_other&&_compareView==view&&_comparedState==score()->state()&&_comparedGroup==group()&&_comparedVariant==target)return;
            clearDifference();QString problem;_other.reset(RegionalVariants::comparison(score(),group(),target,&problem));
            if(!_other){_differenceStatus->setText(problem);return;}
            _comparedState=score()->state();_comparedGroup=group();_comparedVariant=target;_compareView=view;
            const auto rows=RegionalVariants::differences(score(),_other.get(),group());_differences->clear();
            for(auto value:rows){const auto row=value.toMap();auto item=new QTreeWidgetItem(_differences,{tr("第%1小节 · 声部%2").arg(row.value("measure").toInt()).arg(row.value("track").toInt()%4+1),row.value("types").toStringList().join("、")});item->setData(0,Qt::UserRole,row);}
            showRegionalDifference(view,_other.get(),rows);_differenceStatus->setText(rows.isEmpty()?tr("两种写法的音符、节奏、力度和附着标记相同。"):tr("%1处变化。谱面淡色显示另一写法，边框标出当前写法中变化的位置；点列表可定位。").arg(rows.size()));
            }
      void comparisonChoices(){
            _refreshing=true;const auto previous=_compareWith->currentData();const auto g=selectedGroup();
            _compareWith->clear();for(auto value:g.value("variants").toList()){auto v=value.toMap();if(v.value("id")!=g.value("active"))_compareWith->addItem(v.value("name").toString(),v.value("id"));}
            const int i=_compareWith->findData(previous);if(i>=0)_compareWith->setCurrentIndex(i);
            const auto combination=_combinations->currentData();_combinations->clear();_combinations->addItem(tr("选择组合…"),QString());
            for(auto value:RegionalVariants::state(score()).value("combinations").toList()){auto item=value.toMap();_combinations->addItem(item.value("name").toString(),item.value("id"));}
            int current=0;const auto groups=RegionalVariants::state(score()).value("groups").toList();
            for(int c=1;c<_combinations->count();++c)for(auto value:RegionalVariants::state(score()).value("combinations").toList()){auto item=value.toMap();if(item.value("id")!=_combinations->itemData(c))continue;const auto choices=item.value("choices").toMap();bool matches=choices.size()==groups.size();for(auto v:groups){auto group=v.toMap();matches&=choices.value(group.value("id").toString())==group.value("active");}if(matches&&(current==0||item.value("id")==combination))current=c;}
            _combinations->setCurrentIndex(current);
            if (_deleteCombination) _deleteCombination->setEnabled(current > 0);
            _refreshing=false;compare();
            }
      void saveCombination(){
            if(!idle())return;bool ok=false;const auto name=QInputDialog::getText(this,tr("保存组合"),tr("组合名称（同名保存会更新选择）"),QLineEdit::Normal,_combinations->currentIndex()>0?_combinations->currentText():tr("组合%1").arg(_combinations->count()),&ok);
            if(!ok)return;const auto result=RegionalVariants::storeCombination(score(),name);report(result,tr("已保存各组当前写法。Ctrl+S保存到这份乐谱。"));if(result.value("ok").toBool()){comparisonChoices();_combinations->setCurrentIndex(_combinations->findData(result.value("id")));}
            }

      QPointer<MasterScore> _master;
      QVariantMap _last;
      PluginAPI::ScoreObserver *_observer;
      MasterScore *score() const {
            return mscore && mscore->currentScore() ? mscore->currentScore()->masterScore() : nullptr;
      }
      QString group() const {
            auto item = _tree->currentItem();
            if (!item)
                  return {};
            return item->parent() ? item->parent()->data(0, Qt::UserRole).toString()
                                  : item->data(0, Qt::UserRole).toString();
      }
      QString variant() const {
            auto item = _tree->currentItem();
            return item && item->parent() ? item->data(0, Qt::UserRole).toString() : QString();
      }
      void report(const QVariantMap &result, const QString &message) {
            _status->setText(result.contains("error") ? result.value("error").toString() : message);
            refresh();
      }
      void selectTree(const QString &groupId, const QString &variantId) {
            for (int i = 0; i < _tree->topLevelItemCount(); ++i) {
                  auto item = _tree->topLevelItem(i);
                  if (item->data(0, Qt::UserRole).toString() != groupId)
                        continue;
                  if (variantId.isEmpty()) {
                        _tree->setCurrentItem(item);
                        return;
                  }
                  for (int j = 0; j < item->childCount(); ++j)
                        if (item->child(j)->data(0, Qt::UserRole).toString() == variantId) {
                              _tree->setCurrentItem(item->child(j));
                              return;
                        }
            }
      }
      bool idle() {
            stopPlaybackPreview(true);
            if (!score()) {
                  _status->setText(tr("请先打开乐谱。"));
                  return false;
            }
            auto media = referenceProject();
            if (media->property("playing").toBool())
                  QMetaObject::invokeMethod(media, "stop");
            if (seq && seq->isPlaying())
                  seq->stopWait();
            setReferenceClock({});
            setReferenceEndTick(-1);
            if (seq && !seq->backgroundRenderingIdle()) {
                  _status->setText(tr("声音正在准备，请稍后再切换。"));
                  return false;
            }
            if (mscore->currentScore() != score()) {
                  _status->setText(tr("请先回到总谱，再切换或建立分段方案。"));
                  return false;
            }
            return true;
      }
      QVariantMap selectedGroup() const {
            auto data = RegionalVariants::state(score());
            for (auto value : data.value("groups").toList())
                  if (value.toMap().value("id").toString() == group())
                        return value.toMap();
            return {};
      }
      void choices() {
            while (auto item = _choices->takeAt(0)) {
                  if (item->widget())
                        item->widget()->deleteLater();
                  delete item;
            }
            auto g = selectedGroup();
            _help->setVisible(g.isEmpty());
            _scope->setVisible(!g.isEmpty());
            _quickStrip->setVisible(!g.isEmpty());
            if (_delete) {
                  _delete->setEnabled(!g.isEmpty());
                  _delete->setToolTip(variant().isEmpty() ? tr("删除选中的方案组，保留当前谱面内容。")
                                                        : tr("删除选中的写法；Ctrl+Z可恢复。"));
            }
            if (g.isEmpty()) {
                  _scope->setText(tr("选中一段乐谱，点“保存为A”开始。"));
                  if (_compareWith) comparisonChoices();
                  return;
            }
            QString current;
            for (auto entry : g.value("variants").toList())
                  if (entry.toMap().value("id") == g.value("active"))
                        current = entry.toMap().value("name").toString();
            auto first = score()->tick2measure(Fraction::fromTicks(g.value("start").toInt())),
                 last = score()->tick2measure(Fraction::fromTicks(g.value("end").toInt() - 1));
            _scope->setText(tr("%1 · 第%2–%3小节\n%4 · 正在编辑：%5")
                                .arg(g.value("name").toString())
                                .arg(first ? first->no() + 1 : 0)
                                .arg(last ? last->no() + 1 : 0)
                                .arg(scopeSummary(g.value("tracks").toList()), current));
            for (auto entry : g.value("variants").toList()) {
                  auto v = entry.toMap();
                  auto button = new QPushButton(this);
                  button->setObjectName("regionalQuick/" + v.value("id").toString());
                  button->setText(
                      button->fontMetrics().elidedText(v.value("name").toString(), Qt::ElideRight, 110));
                  button->setToolTip(v.value("name").toString());
                  button->setCheckable(true);
                  button->setChecked(v.value("id") == g.value("active"));
                  _choices->addWidget(button);
                  connect(button, &QPushButton::clicked, this, [this, g, v] {
                        selectTree(g.value("id").toString(), v.value("id").toString());
                        switchTo();
                  });
            }
            _choices->addStretch();
            if(_compareWith)comparisonChoices();
      }
      void refresh() {
            auto master = score();
            auto data = RegionalVariants::state(master);
            if (master == _master && data == _last){compare();return;}
            QString selectedGroupId = group(), selectedVariantId = variant();
            if (master == _master && !selectedVariantId.isEmpty()) {
                  QString previousActive, nextActive;
                  for (auto value : _last.value("groups").toList())
                        if (value.toMap().value("id").toString() == selectedGroupId)
                              previousActive=value.toMap().value("active").toString();
                  for (auto value : data.value("groups").toList())
                        if (value.toMap().value("id").toString() == selectedGroupId)
                              nextActive=value.toMap().value("active").toString();
                  if (!nextActive.isEmpty() && previousActive != nextActive)
                        selectedVariantId=nextActive;
            }
            _master = master;
            _last = data;
            _tree->clear();
            QTreeWidgetItem *restore = nullptr;
            for (auto value : data.value("groups").toList()) {
                  auto g = value.toMap();
                  auto first = master->tick2measure(Fraction::fromTicks(g.value("start").toInt())),
                       last = master->tick2measure(Fraction::fromTicks(g.value("end").toInt() - 1));
                  auto item = new QTreeWidgetItem(
                      _tree, {g.value("name").toString(),
                              first && last ? tr("第%1–%2小节").arg(first->no() + 1).arg(last->no() + 1)
                                            : tr("范围需核对")});
                  item->setData(0, Qt::UserRole, g.value("id"));
                  QStringList scope;
                  for (auto value : g.value("tracks").toList()) {
                        int track = value.toInt();
                        scope << tr("谱表%1／声部%2").arg(track / VOICES + 1).arg(track % VOICES + 1);
                  }
                  item->setToolTip(0, scope.join("、"));
                  item->setToolTip(1, scope.join("、"));
                  item->setText(1, item->text(1) + tr(" · %1个声部").arg(scope.size()));
                  item->setExpanded(true);
                  for (auto entry : g.value("variants").toList()) {
                        auto v = entry.toMap();
                        QStringList flags;
                        if (v.value("id") == g.value("active"))
                              flags << tr("当前");
                        if (v.value("id") == g.value("main"))
                              flags << tr("主方案");
                        auto child =
                            new QTreeWidgetItem(item, {v.value("name").toString(), flags.join(" · ")});
                        child->setData(0, Qt::UserRole, v.value("id"));
                        if (v.value("id") == g.value("active")) {
                              auto font = child->font(0);
                              font.setBold(true);
                              child->setFont(0, font);
                        }
                        if (g.value("id").toString() == selectedGroupId &&
                            v.value("id").toString() == selectedVariantId)
                              restore = child;
                  }
            }
            if (restore)
                  _tree->setCurrentItem(restore);
            else if (_tree->topLevelItemCount())
                  _tree->setCurrentItem(_tree->topLevelItem(0));
            choices();
      }
      void create() {
            if (!idle())
                  return;
            auto master = score();
            int start = 0, end = master->firstMeasure()->endTick().ticks();
            auto selected = master->selection().noteList();
            if (master->selection().isRange()) {
                  start = master->selection().tickStart().ticks();
                  end = master->selection().tickEnd().ticks();
            } else if (!selected.empty()) {
                  start = selected.front()->tick().ticks();
                  end = start + 1;
                  for (auto n : selected) {
                        start = qMin(start, n->tick().ticks());
                        end = qMax(end, n->tick().ticks() + n->chord()->actualTicks().ticks());
                  }
            }
            auto first = master->tick2measure(Fraction::fromTicks(start)),
                 last = master->tick2measure(Fraction::fromTicks(qMax(start, end - 1)));
            QDialog dialog(this);
            dialog.setObjectName("regionalCreate");
            dialog.setWindowTitle(tr("建立分段方案"));
            auto layout = new QVBoxLayout(&dialog);
            auto name = new QLineEdit(tr("新方案组"), &dialog);
            name->setObjectName("regionalName");
            layout->addWidget(name);
            auto numbers = new QHBoxLayout;
            auto from = new QSpinBox(&dialog), until = new QSpinBox(&dialog);
            from->setObjectName("regionalFromMeasure");
            until->setObjectName("regionalUntilMeasure");
            for (auto box : {from, until})
                  box->setRange(1, master->nmeasures());
            from->setValue(first ? first->no() + 1 : 1);
            until->setValue(last ? last->no() + 1 : 1);
            numbers->addWidget(new QLabel(tr("从小节")));
            numbers->addWidget(from);
            numbers->addWidget(new QLabel(tr("到")));
            numbers->addWidget(until);
            layout->addLayout(numbers);
            auto tracks = new QTreeWidget(&dialog);
            tracks->setObjectName("regionalTracks");
            tracks->setColumnCount(5);
            tracks->setHeaderLabels({tr("谱表"), "1", "2", "3", "4"});
            QSet<int> selectedTracks;
            for (auto n : selected)
                  selectedTracks.insert(n->track());
            if (selectedTracks.isEmpty())
                  for (int t = 0; t < master->ntracks(); ++t)
                        selectedTracks.insert(t);
            for (int staff = 0; staff < master->nstaves(); ++staff) {
                  auto row = new QTreeWidgetItem(
                      tracks,
                      {tr("%1 · 谱表%2").arg(master->staff(staff)->part()->partName()).arg(staff + 1)});
                  row->setData(0, Qt::UserRole, staff);
                  for (int v = 0; v < VOICES; ++v)
                        row->setCheckState(
                            v + 1, selectedTracks.contains(staff * VOICES + v) ? Qt::Checked : Qt::Unchecked);
            }
            layout->addWidget(tracks);
            auto hint = new QLabel(
                tr("速度、踏板、调号、谱表文字和力度记号共同使用；音符及其附着标记随写法保存。"), &dialog);
            hint->setWordWrap(true);
            layout->addWidget(hint);
            auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
            layout->addWidget(buttons);
            connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
            connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
            if (dialog.exec() != QDialog::Accepted)
                  return;
            auto a = master->firstMeasure(), b = a;
            for (int i = 1; a && i < from->value(); ++i)
                  a = a->nextMeasure();
            for (int i = 1; b && i < until->value(); ++i)
                  b = b->nextMeasure();
            QVariantList allowed;
            for (int i = 0; i < tracks->topLevelItemCount(); ++i) {
                  auto row = tracks->topLevelItem(i);
                  for (int v = 0; v < VOICES; ++v)
                        if (row->checkState(v + 1) == Qt::Checked)
                              allowed << row->data(0, Qt::UserRole).toInt() * VOICES + v;
            }
            if (!a || !b || until->value() < from->value()) {
                  _status->setText(tr("结束小节不能在起点之前。"));
                  return;
            }
            auto result = RegionalVariants::create(master, a->tick().ticks(), b->endTick().ticks(), allowed,
                                                   name->text());
            report(result, tr("已建立一组方案。复制当前写法后就可以尝试另一种编配。"));
            if (result.value("ok").toBool())
                  selectTree(result.value("group").toString(), result.value("variant").toString());
      }
      void copy() {
            if (!idle())
                  return;
            if (group().isEmpty()) {
                  _status->setText(tr("请先选中一组方案。"));
                  return;
            }
            bool ok = false;
            QString name = QInputDialog::getText(
                this, tr("复制当前写法"), tr("新写法名称"), QLineEdit::Normal,
                QString(QChar('A' + qMin(25, selectedGroup().value("variants").toList().size()))) +
                    tr(" · 新写法"),
                &ok);
            if (!ok)
                  return;
            auto result = RegionalVariants::duplicate(score(), group(), name, true);
            const QString groupId = group(), variantId = result.value("variant").toString();
            report(result, tr("新写法已打开，直接在谱面编辑。原写法仍保留。"));
            if (result.value("ok").toBool())
                  selectTree(groupId, variantId);
      }
      void switchTo() {
            if(variant().isEmpty()){_status->setText(tr("请选中组内的一种写法。"));return;}
            const auto result=switchRegionalWriting(mscore->currentScoreView(),group(),variant());
            if (result.value("unchanged").toBool()) {
                  _status->setText(tr("正在编辑“%1”。修改随这份写法保留，Ctrl+S保存到文件。")
                                       .arg(_tree->currentItem()->text(0)));
                  return;
            }
            report(result,result.contains("playbackError")?tr("写法已切换。%1").arg(result.value("playbackError").toString()):regionalAuditionEnabled()?tr("正在从同一片段起点试听。"):tr("已切换写法，可以继续编辑。"));
      }
      void mainChoice() {
            if (!idle())
                  return;
            if (variant().isEmpty()) {
                  _status->setText(tr("请选中组内的一种写法。"));
                  return;
            }
            report(RegionalVariants::chooseMain(score(), group(), variant()),
                   tr("已选为这一组的主方案。普通 MuseScore 打开文件时会看到各组主方案的组合。"));
      }
      void rename() {
            if (!idle())
                  return;
            if (group().isEmpty()) {
                  _status->setText(tr("请先选中一组方案。"));
                  return;
            }
            bool ok = false;
            QString text = QInputDialog::getText(this, tr("名称"), tr("输入名称"), QLineEdit::Normal,
                                                 _tree->currentItem()->text(0), &ok);
            if (ok)
                  report(RegionalVariants::rename(score(), group(), variant(), text), tr("名称已修改。"));
      }
      void removeSelected() {
            if (!idle() || group().isEmpty()) return;
            const auto g = selectedGroup();
            if (g.isEmpty()) { refresh(); return; }
            const auto variants = g.value("variants").toList();
            const auto key = group(), selected = variant();
            QString name, replacement;
            int index = -1;
            for (int i=0; i<variants.size(); ++i)
                  if (variants[i].toMap().value("id").toString() == selected) {
                        index = i; name = variants[i].toMap().value("name").toString(); break;
                  }
            const bool wholeGroup = selected.isEmpty() || variants.size() == 1;
            if (!selected.isEmpty() && index < 0) { refresh(); return; }
            QString message;
            if (wholeGroup)
                  message = tr("删除“%1”及其中的写法，当前谱面内容继续保留。Ctrl+Z可以恢复。")
                                .arg(g.value("name").toString());
            else {
                  replacement = g.value("active").toString();
                  if (replacement == selected)
                        replacement = variants[index+1 < variants.size() ? index+1 : index-1]
                                          .toMap().value("id").toString();
                  QString next;
                  for (auto value : variants)
                        if (value.toMap().value("id").toString() == replacement)
                              next = value.toMap().value("name").toString();
                  message = tr("删除写法“%1”。%2\nCtrl+Z可以恢复。")
                                .arg(name, g.value("active") == selected
                                               ? tr("接着显示“%1”。").arg(next)
                                               : tr("当前谱面继续保留。"));
                  int references = 0;
                  for (auto value : RegionalVariants::state(score()).value("combinations").toList())
                        references += value.toMap().value("choices").toMap().value(key).toString() == selected;
                  if (references)
                        message += tr("\n使用这份写法的%1个组合会改用“%2”。").arg(references).arg(next);
            }
            QMessageBox prompt(QMessageBox::Question, wholeGroup ? tr("删除方案组") : tr("删除写法"),
                               message, QMessageBox::Cancel, this);
            prompt.setObjectName("regionalDeleteDialog");
            auto confirm = prompt.addButton(tr("删除"), QMessageBox::AcceptRole);
            confirm->setObjectName("regionalConfirmDelete");
            prompt.setDefaultButton(QMessageBox::Cancel);
            prompt.exec(); if (prompt.clickedButton() != confirm) return;
            const auto result = RegionalVariants::remove(score(), key, selected);
            clearDifference();
            report(result, wholeGroup ? tr("方案组已删除，当前谱面保留。Ctrl+Z可恢复。")
                                      : tr("写法已删除。Ctrl+Z可恢复。"));
            if (result.value("ok").toBool() && !result.value("removedGroup").toBool())
                  selectTree(key, result.value("active").toString());
      }
      void removeSavedCombination() {
            if (!idle() || _combinations->currentIndex() <= 0) return;
            report(RegionalVariants::removeCombination(score(), _combinations->currentData().toString()),
                   tr("组合已删除，当前各段写法保持原样。Ctrl+Z可恢复。"));
      }
      void audition() {
            if (!idle())
                  return;
            auto g = selectedGroup();
            if (g.isEmpty()) {
                  _status->setText(tr("请先选中一组方案。"));
                  return;
            }
            PluginAPI::Score wrapped(score());
            _observer->setScore(&wrapped);
            QVariantMap range{{"start", g.value("start")},
                              {"end", g.value("end")},
                              {"firstTrack", g.value("tracks").toList().front()},
                              {"occurrence", 1}};
            report(_observer->navigateWorkflow(range, true),
                   tr("正在从这段的进入前文试听当前组合。停止仍使用乐谱播放按钮或空格。"));
      }
      void showMain() {
            if (!idle())
                  return;
            QVariantMap choices;for(auto value:RegionalVariants::state(score()).value("groups").toList()){auto g=value.toMap();choices[g.value("id").toString()]=g.value("main");}
            const auto result=RegionalVariants::selectMany(score(),choices);if(result.contains("error")){report(result,{});return;}
            _status->setText(tr("已显示主方案组合。打印和常规导出使用当前屏幕所示组合。"));
            refresh();
      }
      void expand() {
            if (!idle())
                  return;
            auto g = selectedGroup();
            if (g.isEmpty()) {
                  _status->setText(tr("请先选中一组方案。"));
                  return;
            }
            auto first = score()->tick2measure(Fraction::fromTicks(g.value("start").toInt()));
            auto last = score()->tick2measure(Fraction::fromTicks(g.value("end").toInt() - 1));
            bool accepted = false;
            int from = QInputDialog::getInt(this, tr("扩大方案范围"), tr("开始小节"), first->no() + 1, 1,
                                            first->no() + 1, 1, &accepted);
            if (!accepted)
                  return;
            int until = QInputDialog::getInt(this, tr("扩大方案范围"), tr("结束小节"), last->no() + 1,
                                             last->no() + 1, score()->nmeasures(), 1, &accepted);
            if (!accepted)
                  return;
            auto a = score()->firstMeasure(), b = a;
            for (int i = 1; i < from; ++i)
                  a = a->nextMeasure();
            for (int i = 1; i < until; ++i)
                  b = b->nextMeasure();
            report(RegionalVariants::expand(score(), group(), a->tick().ticks(), b->endTick().ticks()),
                   tr("范围已扩大；新加入的小节复制到每一种写法，当前谱面没有改变。"));
      }
      void ordinary() {
            if (!idle())
                  return;
            bool accepted = false;
            const QString choice =
                QInputDialog::getItem(this, tr("导出普通版副本"), tr("副本使用哪种组合？"),
                                      {tr("当前屏幕组合"), tr("普通版主方案组合")}, 0, false, &accepted);
            if (!accepted)
                  return;
            QString path = QFileDialog::getSaveFileName(this, tr("导出普通版副本"), QString(),
                                                        tr("MuseScore乐谱 (*.mscz)"));
            if (path.isEmpty())
                  return;
            if (!path.endsWith(".mscz", Qt::CaseInsensitive))
                  path += ".mscz";
            for (auto open : mscore->scores())
                  if (QFileInfo(path) == *open->fileInfo()) {
                        _status->setText(tr("请选择另一个文件名，保留当前文件中的全部方案。"));
                        return;
                  }
            QByteArray archive;
            QBuffer buffer(&archive);
            buffer.open(QIODevice::WriteOnly);
            if (!RegionalVariants::exportOrdinary(score(), &buffer,
                                                  QFileInfo(path).completeBaseName() + ".mscx",
                                                  choice == tr("普通版主方案组合"))) {
                  _status->setText(MScore::lastError);
                  return;
            }
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly) || file.write(archive) != archive.size() || !file.commit()) {
                  _status->setText(
                      tr("副本未能保存：%1")
                          .arg(file.errorString().isEmpty() ? MScore::lastError : file.errorString()));
                  return;
            }
            _status->setText(tr("普通版副本已保存，不含其它写法。当前乐谱和文件位置没有改变。"));
      }
      void importLegacy() {
            if (!idle() || group().isEmpty()) {
                  _status->setText(tr("先建立并选中接收旧写法的方案组。"));
                  return;
            }
            const QString path = QFileDialog::getOpenFileName(this, tr("导入旧方案"), QString(),
                                                              tr("旧工作区或乐谱 (*.json *.mscz *.mscx)"));
            if (path.isEmpty())
                  return;
            QVariantList entries;
            if (QFileInfo(path).suffix().compare("json", Qt::CaseInsensitive) == 0) {
                  QFile file(path);
                  if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024 * 1024) {
                        _status->setText(tr("旧工作区无法读取或过大。"));
                        return;
                  }
                  auto data = QJsonDocument::fromJson(file.readAll()).toVariant().toMap();
                  auto groups = data.value("variants").toMap().value("groups").toList();
                  if (groups.isEmpty()) {
                        _status->setText(tr("文件中没有旧方案组。"));
                        return;
                  }
                  QStringList names;
                  for (auto value : groups)
                        names << tr("%1 · %2")
                                     .arg(names.size() + 1)
                                     .arg(value.toMap().value("name").toString());
                  bool accepted = false;
                  auto chosen = QInputDialog::getItem(this, tr("旧方案组"), tr("导入哪一组写法？"), names, 0,
                                                      false, &accepted);
                  if (!accepted)
                        return;
                  entries = groups[names.indexOf(chosen)].toMap().value("entries").toList();
            } else
                  entries << QVariantMap{{"path", path}, {"name", QFileInfo(path).completeBaseName()}};
            int imported = 0;
            QStringList failures;
            for (auto entry : entries) {
                  auto record = entry.toMap();
                  QString source = record.value("path").toString();
                  if (QFileInfo(source).isRelative())
                        source = QFileInfo(path).dir().absoluteFilePath(source);
                  if (!QFileInfo(source).isFile()) {
                        failures << tr("%1：找不到谱文件").arg(record.value("name").toString());
                        continue;
                  }
                  auto expected = record.value("hash").toString();
                  if (!expected.isEmpty()) {
                        QFile file(source);
                        if (!file.open(QIODevice::ReadOnly) ||
                            QString::fromLatin1(
                                QCryptographicHash::hash(file.readAll(), QCryptographicHash::Sha256)
                                    .toHex()) != expected) {
                              failures << tr("%1：文件已改变或找不到").arg(record.value("name").toString());
                              continue;
                        }
                  }
                  TemporaryScore old(mscore->readScore(source));
                  if (!old) {
                        failures << tr("%1：不能打开").arg(QFileInfo(source).fileName());
                        continue;
                  }
                  auto result = RegionalVariants::importVersion(score(), old.get(), group(),
                                                                record.value("name").toString());
                  if (result.value("ok").toBool())
                        ++imported;
                  else
                        failures << result.value("error").toString();
            }
            _status->setText(
                tr("已导入%1种写法。旧文件保留；只导入本组的小节和声部，不改变共同速度、踏板或力度记号。")
                    .arg(imported) +
                (failures.isEmpty() ? QString() : "\n" + failures.join("\n")));
            refresh();
      }

    protected:
      void showEvent(QShowEvent *) override {
            refresh();
            _refresh.start();
      }
      void hideEvent(QHideEvent *) override {
            _refresh.stop();clearDifference();
      }

    public:
      void focusGroup(const QString &id) {
            refresh();
            _status->setText(tr("已打开这段的写法。点名称切换，按Ctrl+S保存。"));
            for (auto value : RegionalVariants::state(score()).value("groups").toList())
                  if (value.toMap().value("id").toString() == id) {
                        selectTree(id, value.toMap().value("active").toString());
                        break;
                  }
      }
      explicit RegionalPanel(QWidget *parent) : QWidget(parent),_compare(nullptr),_listen(nullptr),_compareWith(nullptr),_combinations(nullptr),_differences(nullptr) {
            setObjectName("regionalVariantPanel");
            auto layout = new QVBoxLayout(this);
            auto help = new QLabel(
                tr("保存当前写法为A，再增加B。修改后，点写法名称切换比较。Ctrl+S保存全部写法。"), this);
            help->setWordWrap(true);
            _help=help;
            layout->addWidget(help);
            auto primary = new ControlFlowLayout;
            layout->addLayout(primary);
            auto button = [&](QLayout *row, const QString &text, const char *id, auto call) {
                  auto b = new QPushButton(text, this);
                  b->setObjectName(id);
                  const QHash<QString,QString> iconKeys{{"regionalCreateButton","save"},{"regionalCopy","add"},
                      {"regionalDelete","delete"},{"regionalSaveCombination","save"},
                      {"regionalDeleteCombination","delete"},{"regionalSwitch","refresh"},{"regionalAudition","audition"}};
                  if(iconKeys.contains(id))setComponentToolIcon(b,iconKeys.value(id));
                  row->addWidget(b);
                  connect(b, &QPushButton::clicked, this, call);
                  return b;
            };
            button(primary, tr("保存为A"), "regionalCreateButton", [this] { create(); });
            button(primary, tr("增加写法"), "regionalCopy", [this] { copy(); });
            _delete = button(primary, tr("删除"), "regionalDelete", [this] { removeSelected(); });
            _delete->setEnabled(false);
            auto more = new QToolButton(this);
            more->setText(tr("更多"));
            more->setObjectName("regionalMore");
            more->setPopupMode(QToolButton::InstantPopup);
            auto menu = new QMenu(more);
            more->setMenu(menu);
            primary->addWidget(more);
            auto action = [&](const QString &text, const char *id, auto call) {
                  auto a = menu->addAction(text);
                  a->setObjectName(id);
                  connect(a, &QAction::triggered, this, call);
            };
            action(tr("改名…"), "regionalRename", [this] { rename(); });
            action(tr("扩大范围…"), "regionalExpand", [this] { expand(); });
            action(tr("恢复这组谱面按钮与位置"), "regionalResetControl", [this] {
                  if (!idle()) return;
                  report(RegionalVariants::setControlPlacement(score(), group(), {{"reset",true}}),
                         tr("这组切换按钮已显示，并恢复自动位置。"));
                  _scoreButtons->setChecked(true);
            });
            menu->addSeparator();
            action(tr("设为主方案"), "regionalMain", [this] { mainChoice(); });
            action(tr("显示主方案组合"), "regionalShowMain", [this] { showMain(); });
            action(tr("导出普通版副本…"), "regionalOrdinary", [this] { ordinary(); });
            action(tr("导入旧写法…"), "regionalImport", [this] { importLegacy(); });
            action(tr("使用说明"), "regionalHelp", [] { showPersonalGuide("docs/VARIANTS_070_GUIDE.md"); });
            menu->addSeparator();
            auto controls = menu->addAction(tr("谱上显示切换按钮"));
            controls->setObjectName("regionalScoreButtons");
            controls->setCheckable(true);
            controls->setChecked(regionalControlsVisible());
            connect(controls, &QAction::toggled, this,
                    [](bool checked) { setRegionalControlsVisible(checked); });
            _scoreButtons = new QCheckBox(tr("谱面显示切换按钮"), this);
            _scoreButtons->setObjectName("regionalScoreButtonsToggle");
            _scoreButtons->setChecked(regionalControlsVisible());
            layout->addWidget(_scoreButtons);
            connect(_scoreButtons, &QCheckBox::toggled, this, [controls](bool checked) {
                  setRegionalControlsVisible(checked); QSignalBlocker blocked(controls); controls->setChecked(checked);
            });
            connect(controls, &QAction::toggled, _scoreButtons, &QCheckBox::setChecked);
            _scope = new QLabel(this);
            _scope->setObjectName("regionalCurrent");
            _scope->setWordWrap(true);
            layout->addWidget(_scope);
            auto strip = new QScrollArea(this);
            _quickStrip=strip;
            strip->setWidgetResizable(true);
            strip->setFixedHeight(58);
            strip->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            auto host = new QWidget(strip);
            _choices = new QHBoxLayout(host);
            _choices->setContentsMargins(0, 0, 0, 0);
            _choices->setSizeConstraint(QLayout::SetMinimumSize);
            strip->setWidget(host);
            layout->addWidget(strip);
            layout->removeWidget(_scope);layout->removeWidget(strip);
            layout->insertWidget(0,_scope);layout->insertWidget(1,strip);
            _tree = new QTreeWidget(this);
            _tree->setObjectName("regionalTree");
            _tree->setColumnCount(2);
            _tree->setHeaderLabels({tr("方案与写法"), tr("范围／用途")});
            _tree->setSelectionMode(QAbstractItemView::SingleSelection);
            layout->addWidget(_tree, 1);
            _tree->setToolTip(tr("单击写法名称即可切换；也可以用方向键选中后按回车。加粗并标为“当前”的写法正在谱面中编辑。"));
            connect(_tree, &QTreeWidget::itemClicked, this,
                    [this](QTreeWidgetItem *item, int) { if (item && item->parent()) switchTo(); });
            connect(_tree, &QTreeWidget::itemActivated, this,
                    [this](QTreeWidgetItem *item, int) { if (item && item->parent()) switchTo(); });
            connect(_tree, &QTreeWidget::currentItemChanged, this, [this] { choices(); });
            _compare=new QCheckBox(tr("显示差异"),this);_compare->setObjectName("regionalCompare");
            _compareWith=new QComboBox(this);_compareWith->setObjectName("regionalCompareWith");_compareWith->hide();
            _compareWith->setMinimumContentsLength(10);_compareWith->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            auto comparisonRow=new ControlFlowLayout;comparisonRow->addWidget(_compare);comparisonRow->addWidget(_compareWith);layout->addLayout(comparisonRow);
            _differenceStatus=new QLabel(this);_differenceStatus->setWordWrap(true);_differenceStatus->hide();layout->addWidget(_differenceStatus);
            _differences=new QTreeWidget(this);_differences->setObjectName("regionalDifferences");_differences->setHeaderLabels({tr("位置"),tr("变化")});_differences->setMaximumHeight(160);_differences->hide();layout->addWidget(_differences);
            connect(_compare,&QCheckBox::toggled,this,[this]{comparisonChoices();});connect(_compareWith,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{compare();});
            connect(_differences,&QTreeWidget::itemClicked,this,[this](QTreeWidgetItem* item,int){const auto row=item->data(0,Qt::UserRole).toMap();PluginAPI::Score wrapped(score());_observer->setScore(&wrapped);_observer->navigateWorkflow({{"start",row.value("tick")},{"end",row.value("tick").toInt()+1},{"firstTrack",row.value("track")}},false);});
            auto combinationRow=new ControlFlowLayout;_combinations=new QComboBox(this);_combinations->setObjectName("regionalCombinations");
            _combinations->setMinimumContentsLength(10);_combinations->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
            combinationRow->addWidget(_combinations);button(combinationRow,tr("保存组合…"),"regionalSaveCombination",[this]{saveCombination();});layout->addLayout(combinationRow);
            _deleteCombination=button(combinationRow,tr("删除"),"regionalDeleteCombination",[this]{removeSavedCombination();});
            _deleteCombination->setToolTip(tr("删除所选组合；各组写法继续保留，Ctrl+Z可恢复。"));
            _deleteCombination->setEnabled(false);
            connect(_combinations,QOverload<int>::of(&QComboBox::currentIndexChanged),this,
                    [this](int index){ _deleteCombination->setEnabled(index > 0); });
            connect(_combinations,QOverload<int>::of(&QComboBox::activated),this,[this](int i){if(i<=0||!idle())return;report(RegionalVariants::selectCombination(score(),_combinations->itemData(i).toString()),tr("已切换整组选择；Ctrl+Z可一起恢复。"));if(regionalAuditionEnabled())audition();});
            _listen=new QCheckBox(tr("试听比较：切换后从同处听"),this);_listen->setObjectName("regionalAuditionCompare");_listen->setChecked(regionalAuditionEnabled());layout->addWidget(_listen);connect(_listen,&QCheckBox::toggled,this,[](bool value){setRegionalAuditionEnabled(value);});
            auto transport = new ControlFlowLayout;
            layout->addLayout(transport);
            button(transport, tr("切换所选写法"), "regionalSwitch", [this] { switchTo(); });
            button(transport, tr("从同处听"), "regionalAudition", [this] { audition(); });
            _status = new QLabel(tr("选中要尝试的几小节，点“保存为A”。"), this);
            _status->setObjectName("regionalStatus");
            _status->setWordWrap(true);
            _status->setMinimumHeight(48);
            layout->addWidget(_status);
            _observer = new PluginAPI::ScoreObserver(this);
            _refresh.setInterval(500);
            connect(&_refresh, &QTimer::timeout, this, [this] { refresh(); });
      }
};
QPointer<QDockWidget> dock;
} // namespace
void addRegionalVariantAction(QMenu *menu) {
      markPersonalAction(getAction("regional-variants"));
      menu->addAction(getAction("regional-variants"));
}
void showRegionalVariantPanel(const QString &group) {
      if (!personalFeaturesEnabled()) return;
      if (!dock) {
            dock = new QDockWidget(QObject::tr("分段试写方案"), mscore);
            dock->setObjectName("regionalVariantsDock");
            dock->setAllowedAreas(Qt::AllDockWidgetAreas);
            auto scroll = new QScrollArea(dock);
            scroll->setObjectName("regionalScrollArea");
            scroll->setFrameShape(QFrame::NoFrame);
            scroll->setWidgetResizable(true);
            scroll->setMinimumSize(0, 0);
            scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
            scroll->setWidget(new RegionalPanel(scroll));
            dock->setWidget(scroll);
            mscore->addDockWidget(Qt::RightDockWidgetArea, dock);
            arrangeWorkflowDock(mscore, dock, "regionalVariantsDock", "rightTools", Qt::RightDockWidgetArea);
      }
      dock->show();
      dock->raise();
      if (!group.isEmpty())
            if (auto content = dock->findChild<QWidget *>("regionalVariantPanel"))
                  static_cast<RegionalPanel *>(content)->focusGroup(group);
}
} // namespace Ms
