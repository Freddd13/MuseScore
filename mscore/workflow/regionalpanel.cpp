// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "regionalpanel.h"
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
#include <QBuffer>
#include <QCryptographicHash>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSet>
#include <QSpinBox>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <memory>
namespace Ms {
namespace {
class RegionalPanel : public QWidget {
      QTreeWidget *_tree;
      QLabel *_status;
      QTimer _refresh;
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
      void refresh() {
            auto master = score();
            auto data = RegionalVariants::state(master);
            if (master == _master && data == _last)
                  return;
            QString selectedGroupId = group(), selectedVariantId = variant();
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
                              flags << tr("普通版主方案");
                        auto child =
                            new QTreeWidgetItem(item, {v.value("name").toString(), flags.join(" · ")});
                        child->setData(0, Qt::UserRole, v.value("id"));
                        if (g.value("id").toString() == selectedGroupId &&
                            v.value("id").toString() == selectedVariantId)
                              restore = child;
                  }
            }
            if (restore)
                  _tree->setCurrentItem(restore);
            else if (_tree->topLevelItemCount())
                  _tree->setCurrentItem(_tree->topLevelItem(0));
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
            QString name = QInputDialog::getText(this, tr("复制当前写法"), tr("新写法名称"),
                                                 QLineEdit::Normal, tr("B · 试写"), &ok);
            if (!ok)
                  return;
            auto result = RegionalVariants::duplicate(score(), group(), name, true);
            const QString groupId = group(), variantId = result.value("variant").toString();
            report(result, tr("新写法已打开，直接在谱面编辑。原写法仍保留。"));
            if (result.value("ok").toBool())
                  selectTree(groupId, variantId);
      }
      void switchTo() {
            if (!idle())
                  return;
            if (variant().isEmpty()) {
                  _status->setText(tr("请选中组内的一种写法。"));
                  return;
            }
            report(RegionalVariants::select(score(), group(), variant()),
                   tr("已切换这一段的写法；其它段和声部继续使用原选择。"));
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
            auto groups = RegionalVariants::state(score()).value("groups").toList();
            for (auto value : groups) {
                  auto g = value.toMap();
                  auto result =
                      RegionalVariants::select(score(), g.value("id").toString(), g.value("main").toString());
                  if (result.contains("error")) {
                        report(result, {});
                        return;
                  }
            }
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
                  std::unique_ptr<MasterScore> old(mscore->readScore(source));
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
            _refresh.stop();
      }

    public:
      explicit RegionalPanel(QWidget *parent) : QWidget(parent) {
            setObjectName("regionalVariantPanel");
            auto layout = new QVBoxLayout(this);
            auto help = new QLabel(
                tr("每组只保存选定小节和声部。当前组合直接在谱面编辑；Ctrl+S把全部写法存入一个MSCZ。"), this);
            help->setWordWrap(true);
            layout->addWidget(help);
            auto actions = new QGridLayout;
            layout->addLayout(actions);
            int i = 0;
            auto button = [&](const QString &name, const char *id, auto call) {
                  auto b = new QPushButton(name, this);
                  b->setObjectName(id);
                  actions->addWidget(b, i / 3, i % 3);
                  ++i;
                  connect(b, &QPushButton::clicked, this, call);
            };
            button(tr("建立组"), "regionalCreateButton", [this] { create(); });
            button(tr("复制写法"), "regionalCopy", [this] { copy(); });
            button(tr("切换写法"), "regionalSwitch", [this] { switchTo(); });
            button(tr("设为主方案"), "regionalMain", [this] { mainChoice(); });
            button(tr("改名"), "regionalRename", [this] { rename(); });
            button(tr("同处试听"), "regionalAudition", [this] { audition(); });
            button(tr("显示主方案组合"), "regionalShowMain", [this] { showMain(); });
            button(tr("普通版副本"), "regionalOrdinary", [this] { ordinary(); });
            button(tr("导入旧写法"), "regionalImport", [this] { importLegacy(); });
            button(tr("扩大范围"), "regionalExpand", [this] { expand(); });
            _tree = new QTreeWidget(this);
            _tree->setObjectName("regionalTree");
            _tree->setColumnCount(2);
            _tree->setHeaderLabels({tr("方案与写法"), tr("范围／用途")});
            _tree->setSelectionMode(QAbstractItemView::SingleSelection);
            layout->addWidget(_tree, 1);
            connect(_tree, &QTreeWidget::itemDoubleClicked, this,
                    [this](QTreeWidgetItem *, int) { switchTo(); });
            _status = new QLabel(tr("先选择谱段，再建立方案组。"), this);
            _status->setObjectName("regionalStatus");
            _status->setWordWrap(true);
            layout->addWidget(_status);
            _observer = new PluginAPI::ScoreObserver(this);
            _refresh.setInterval(500);
            connect(&_refresh, &QTimer::timeout, this, [this] { refresh(); });
      }
};
QPointer<QDockWidget> dock;
} // namespace
void addRegionalVariantAction(QMenu *menu) {
      menu->addAction(getAction("regional-variants"));
}
void showRegionalVariantPanel() {
      if (!dock) {
            dock = new QDockWidget(QObject::tr("分段试写方案"), mscore);
            dock->setObjectName("regionalVariantsDock");
            dock->setAllowedAreas(Qt::AllDockWidgetAreas);
            dock->setWidget(new RegionalPanel(dock));
            mscore->addDockWidget(Qt::RightDockWidgetArea, dock);
            arrangeWorkflowDock(mscore, dock, "regionalVariantsDock", "rightTools", Qt::RightDockWidgetArea);
      }
      dock->show();
      dock->raise();
}
} // namespace Ms
