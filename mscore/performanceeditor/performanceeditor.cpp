#include "performanceeditor.h"
#include "mscore/musescore.h"
#include "mscore/scoreview.h"
#include "mscore/seq.h"
#include "libmscore/chord.h"
#include "libmscore/measure.h"
#include "libmscore/segment.h"
#include "libmscore/staff.h"
#include "libmscore/part.h"
#include "libmscore/system.h"
#include "libmscore/pedal.h"
#include "libmscore/spanner.h"
#include "libmscore/tempotext.h"
#include "libmscore/synthesizerstate.h"
#include "libmscore/select.h"
#include <QPainter>
#include <QApplication>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QResizeEvent>
#include <QComboBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QDoubleSpinBox>
#include <QMenu>
#include <algorithm>

namespace Ms {
extern bool graceNotesMerged(Chord*);
namespace {
QVector<PerformanceEditor*> editors;
const QColor accent("#3199ce"), pendingColor("#e99d32");
int selectionTrack(Score* score, int fallback)
      {
      const auto notes = score->selection().noteList();
      if (!notes.empty()) return notes.front()->track();
      for (auto element : score->selection().elements()) if (element->isChordRest()) return element->track();
      return qBound(0, fallback, qMax(0, score->nstaves() * VOICES - 1));
      }
}
class PerformanceCanvas : public QWidget {
      PerformanceEditor* _editor;
      void paintEvent(QPaintEvent*) override { QPainter painter(this); _editor->paintTimeline(painter); }
   public:
      explicit PerformanceCanvas(PerformanceEditor* editor) : QWidget(editor), _editor(editor)
            { setMinimumHeight(190); setMouseTracking(true); setFocusPolicy(Qt::StrongFocus); }
      };

PerformanceEditor::PerformanceEditor(QWidget* parent) : QWidget(parent)
      {
      _score = nullptr;
      editors.append(this);
      setObjectName("performanceEditor");
      auto layout = new QVBoxLayout(this);
      layout->setContentsMargins(6, 4, 6, 4);
      auto row = new QGridLayout; _topRow = row;
      auto combo = [this, row](const QStringList& labels, const char* name) {
            auto box = new QComboBox(this); box->addItems(labels); box->setObjectName(name); row->addWidget(box, row->count() / 5, row->count() % 5); return box;
            };
      _parameter = combo({tr("力度"), tr("速度 · 阶梯"), tr("延音踏板")}, "performanceParameter");
      _axis = combo({tr("MIDI 力度"), tr("相对 %")}, "performanceAxis");
      _scope = combo({tr("当前乐器"), tr("当前谱表"), tr("全谱")}, "performanceScope");
      _voice = combo({tr("全部声部"), "1", "2", "3", "4"}, "performanceVoice");
      _tool = combo({tr("拖动"), tr("铅笔"), tr("直线")}, "performanceTool");
      layout->addLayout(row);
      auto second = new QGridLayout; _actionsRow = second;
      auto button = [this, second](const QString& label, auto callback) {
            auto result = new QPushButton(label, this); second->addWidget(result, second->count() / 6, second->count() % 6); connect(result, &QPushButton::clicked, this, callback); return result;
            };
      _handles = new QCheckBox(tr("音旁手柄"), this); second->addWidget(_handles, second->count() / 6, second->count() % 6);
      _band = new QCheckBox(tr("谱行参数带"), this); second->addWidget(_band, second->count() / 6, second->count() % 6);
      button(tr("转换为相对"), [this] { convertSelected(Note::ValueType::OFFSET_VAL); })->setObjectName("performanceConvertRelative");
      button(tr("转换为绝对"), [this] { convertSelected(Note::ValueType::USER_VAL); })->setObjectName("performanceConvertAbsolute");
      _number = new QDoubleSpinBox(this); _number->setObjectName("performanceValue"); _number->setRange(1, 127); _number->setDecimals(0); second->addWidget(_number, second->count() / 6, second->count() % 6);
      button(tr("写入选中"), [this] { setSelectedValue(_number->value()); })->setObjectName("performanceWriteValue");
      button(tr("全曲"), [this] { fit(); });
      button(tr("选区"), [this] { fit(true); });
      button(tr("取消预览"), [this] { cancelGesture(); _pending.clear(); status(); _canvas->update(); if (_view) _view->update(); });
      layout->addLayout(second);
      _canvas = new PerformanceCanvas(this); _canvas->installEventFilter(this); layout->addWidget(_canvas, 1);
      _scroll = new QScrollBar(Qt::Horizontal, this); layout->addWidget(_scroll);
      _status = new QLabel(this); _status->setWordWrap(true); layout->addWidget(_status);
      _refreshTimer.setSingleShot(true);
      connect(&_refreshTimer, &QTimer::timeout, this, &PerformanceEditor::refresh);
      _commitTimer.setSingleShot(true);
      connect(&_commitTimer, &QTimer::timeout, this, &PerformanceEditor::applyPending);
      connect(_scroll, &QScrollBar::valueChanged, _canvas, QOverload<>::of(&QWidget::update));
      for (auto box : {_scope, _voice}) connect(box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { cancelGesture(); _fitOnRefresh = true; _dirty = true; scheduleRefresh(); });
      for (auto box : {_axis, _parameter, _tool}) connect(box, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
            cancelGesture();
            const bool velocity = _parameter->currentIndex() == 0;
            _axis->setEnabled(velocity);
            findChild<QPushButton*>("performanceConvertRelative")->setEnabled(velocity);
            findChild<QPushButton*>("performanceConvertAbsolute")->setEnabled(velocity);
            findChild<QPushButton*>("performanceWriteValue")->setEnabled(_parameter->currentIndex() != 2);
            _number->setEnabled(_parameter->currentIndex() != 2);
            _number->setSuffix(velocity && _axis->currentIndex() ? " %" : QString());
            _number->setPrefix(_parameter->currentIndex() == 1 ? "BPM " : QString());
            _number->setRange(velocity ? (_axis->currentIndex() ? NoteVelocity::minOffset : 1) : 5,
                  velocity ? (_axis->currentIndex() ? NoteVelocity::maxOffset : 127) : 999);
            _number->setDecimals(velocity ? 0 : 2);
            updateSelection(); status(); _canvas->update(); if (_view) _view->update();
            });
      for (auto box : {_handles, _band}) connect(box, &QCheckBox::toggled, this, [this] { cancelGesture(); if (_view) _view->update(); });
      if (seq) {
            connect(seq, &Seq::stopped, this, [this] { _commitTimer.start(0); });
            connect(seq, &Seq::started, this, [this] { cancelGesture(); status(); });
            }
      status();
      }

void PerformanceEditor::resizeEvent(QResizeEvent*)
      {
      for (auto grid : {_topRow, _actionsRow}) {
            if (!grid) continue;
            QVector<QWidget*> widgets;
            while (auto item = grid->takeAt(0)) { if (item->widget()) widgets.append(item->widget()); delete item; }
            const int columns = width() >= 950 ? (grid == _topRow ? 5 : 6) : (width() >= 600 ? 3 : 2);
            for (int i = 0; i < widgets.size(); ++i) grid->addWidget(widgets[i], i / columns, i % columns);
            }
      }

PerformanceEditor::~PerformanceEditor()
      {
      editors.removeAll(this);
      if (_score) _score->removeViewer(this);
      }

void PerformanceEditor::setView(ScoreView* view)
      {
      if (_view == view && _score == (view ? view->score() : nullptr)) return;
      Score* next = view ? view->score() : nullptr;
      if (_score && _score != next && hasPending()) flushPending();
      cancelGesture();
      if (_view) _view->removeEventFilter(this);
      if (_score) _score->removeViewer(this);
      disconnect(_scoreDestroyed);
      _view = view; _score = next;
      _notes.clear(); _segments.clear(); _tempos.clear(); _pedals.clear(); _noteIndex.clear(); _layoutIndex.clear(); _selectedIndices.clear(); _systemIndices.clear();
      _system = nullptr; _hover = -1;
      if (_score) {
            _score->addViewer(this);
            _scoreDestroyed = connect(_score, &QObject::destroyed, this, [this] {
                  _score = nullptr; _notes.clear(); _segments.clear(); _tempos.clear(); _pedals.clear();
                  _noteIndex.clear(); _layoutIndex.clear(); _pending.clear(); _selectedIndices.clear(); _systemIndices.clear(); _system = nullptr; status(); _canvas->update();
                  });
            }
      if (_view) _view->installEventFilter(this);
      _fitOnRefresh = true; _dirty = true; scheduleRefresh();
      }

void PerformanceEditor::scheduleRefresh()
      {
      if (_committing) return;
      if (!_refreshTimer.isActive()) _refreshTimer.start(0);
      }

void PerformanceEditor::refresh()
      {
      if (!_score) { status(); _canvas->update(); return; }
      if (!isVisible()) return;
      // The renderer may regenerate NoteEvents and velocity maps in the background.
      if (_score->isPlaying() || (seq && (seq->isPlaying() || !seq->backgroundRenderingIdle()))) { status(); return; }
      const auto state = _score->masterScore()->state();
      if (hasPending() && _state != state) { _pending.clear(); status(tr("乐谱已被其他操作修改，待提交预览已取消。")); }
      if (_score->nstaves() == 0) return;
      if (!_dirty && _state == state) { updateSelection(); return; }
      if (_dragging) cancelGesture();
      _notes.clear(); _segments.clear(); _tempos.clear(); _pedals.clear(); _noteIndex.clear(); _layoutIndex.clear();
      _score->createPlayEvents();
      _score->updateVelo();
      const int track = selectionTrack(_score, _contextTrack);
      _contextTrack = track;
      auto part = _score->staff(track / VOICES)->part();
      const int globalMethod = mscore ? mscore->synthesizerState().method() : 1;
      _relativeMax = 100; _tempoMax = 240;
      for (auto segment = _score->firstSegment(SegmentType::ChordRest); segment; segment = segment->next1(SegmentType::ChordRest)) {
            const int tick = segment->tick().ticks();
            auto system = segment->measure()->system();
            if (system) _layoutIndex.insert(system);
            _segments.append({tick, segment->canvasPos(), system, _score->tempo(segment->tick()) * 60.0});
            for (Element* annotation : segment->annotations())
                  if (annotation->isTempoText()) {
                        auto text = toTempoText(annotation);
                        _tempos.append({text, tick, text->tempo() * 60.0, text->visible()});
                        _tempoMax = qMax(_tempoMax, text->tempo() * 60.0);
                        }
            for (int t = 0; t < _score->nstaves() * VOICES; ++t) {
                  if (_scope->currentIndex() == 0 && _score->staff(t / VOICES)->part() != part) continue;
                  if (_scope->currentIndex() == 1 && t / VOICES != track / VOICES) continue;
                  if (_voice->currentIndex() && t % VOICES != _voice->currentIndex() - 1) continue;
                  Element* element = segment->element(t);
                  if (!element || !element->isChord()) continue;
                  auto addChord = [this, tick, globalMethod](Chord* chord) {
                        for (Note* note : chord->notes()) {
                              const int index = _notes.size();
                              _noteIndex.insert(note, index);
                              const bool audible = note->play() && !note->hidden() && (!note->tieBack() || note->playEvents().size() != 1)
                                    && (!chord->isGrace() || !graceNotesMerged(toChord(chord->parent())));
                              _notes.append({note, tick, chord->endTick().ticks(), note->track(), note->pitch(),
                                    NoteVelocity::referenceBase(note, globalMethod), note->veloOffset(), 0, 0, note->veloType(),
                                    note->selected(), audible, note->canvasBoundingRect(), chord->measure()->system()});
                              auto& cached = _notes.back(); cached.baseMin = cached.baseMax = cached.base;
                              for (int event = 1; event < note->playEvents().size(); ++event) {
                                    const int base = NoteVelocity::referenceBase(note, globalMethod, event);
                                    cached.baseMin = qMin(cached.baseMin, base); cached.baseMax = qMax(cached.baseMax, base);
                                    }
                              if (note->veloType() == Note::ValueType::OFFSET_VAL) _relativeMax = qMax(_relativeMax, note->veloOffset());
                              }
                        };
                  Chord* chord = toChord(element);
                  for (Chord* grace : chord->graceNotesBefore()) addChord(grace);
                  addChord(chord);
                  for (Chord* grace : chord->graceNotesAfter()) addChord(grace);
                  }
            }
      for (const auto& pair : _score->spannerMap().map()) {
            auto span = pair.second;
            if (span->isPedal() && (_scope->currentIndex() == 2 || span->part() == part))
                  _pedals.append({toPedal(span), span->tick().ticks(), span->tick2().ticks(), span->track()});
            }
      _endTick = _score->lastMeasure() ? _score->lastMeasure()->endTick().ticks() : 1;
      if (!_segments.isEmpty()) {
            auto measure = _score->lastMeasure();
            _segments.append({_endTick, measure->canvasPos() + QPointF(measure->width(), 0), measure->system(), _segments.back().bpm});
            }
      _hover = -1;
      _state = state; _dirty = false;
      updateSelection();
      if (_fitOnRefresh) { fit(); _fitOnRefresh = false; }
      else _scroll->setRange(0, qMax(0, _endTick - _scroll->pageStep()));
      }

void PerformanceEditor::updateSelection()
      {
      // Layout callbacks can precede selection notifications: never inspect a stale System.
      if (_dirty || (_score && _score->masterScore()->state() != _state)) {
            _dirty = true; scheduleRefresh(); return;
            }
      _selectedIndices.clear(); _systemIndices.clear();
      _system = nullptr;
      for (auto& note : _notes) {
            note.selected = note.note->selected();
            if (note.selected && note.audible) _selectedIndices.append(int(&note - _notes.data()));
            if (note.selected && !_system) _system = note.system;
            }
      if (!_system && _score) for (auto element : _score->selection().elements())
            if (element->isChordRest()) { _system = toChordRest(element)->measure()->system(); break; }
      if (!_system && _hover >= 0 && _hover < _notes.size()) _system = _notes[_hover].system;
      if (!_system && !_notes.isEmpty()) _system = _notes.front().system;
      for (int i = 0; i < _notes.size(); ++i) if (_notes[i].system == _system) _systemIndices.append(i);
      _systemSegments.clear();
      for (const auto& segment : _segments) if (segment.system == _system) _systemSegments.append(segment);
      if (_system && !_systemSegments.isEmpty()) {
            auto last = _system->lastMeasure();
            if (last && _systemSegments.back().tick < last->endTick().ticks())
                  _systemSegments.append({last->endTick().ticks(), last->canvasPos() + QPointF(last->width(), 0), _system, _systemSegments.back().bpm});
            }
      if (_parameter->currentIndex() == 0 && !_selectedIndices.isEmpty()) {
            QSignalBlocker blocker(_number); _number->setValue(noteValue(_selectedIndices.front()));
            }
      _canvas->update(); if (_view) _view->update();
      }

void PerformanceEditor::selectionChanged()
      {
      if (_dragging) return;
      const int track = _score ? selectionTrack(_score, _contextTrack) : 0;
      if (_score && _score->nstaves() && _scope->currentIndex() != 2
            && (_scope->currentIndex() == 1 ? track / VOICES != _contextTrack / VOICES
                  : _score->staff(track / VOICES)->part() != _score->staff(_contextTrack / VOICES)->part())) {
            _dirty = true; scheduleRefresh();
            }
      else updateSelection();
      }

void PerformanceEditor::layoutChanged()
      {
      if (!_committing) cancelGesture();
      _system = nullptr; _systemSegments.clear();
      _dirty = true; scheduleRefresh();
      }

void PerformanceEditor::onElementDestruction(Element* element)
      {
      // Destruction callbacks must never call virtual methods on the dying Element.
      if (_layoutIndex.remove(element)) {
            cancelGesture(); _system = nullptr; _systemSegments.clear(); _dirty = true;
            }
      if (_noteIndex.contains(element)) {
            cancelGesture(); _pending.clear(); _notes.clear(); _noteIndex.clear(); _layoutIndex.clear(); _selectedIndices.clear(); _systemIndices.clear(); _system = nullptr;
            _segments.clear(); _systemSegments.clear();
            }
      bool matched = false;
      for (const auto& tempo : _tempos) if (tempo.text == element) matched = true;
      for (const auto& pedal : _pedals) if (pedal.pedal == element) matched = true;
      if (!matched && !_notes.isEmpty()) return;
      _tempos.erase(std::remove_if(_tempos.begin(), _tempos.end(), [element](const TempoInfo& t) { return t.text == element; }), _tempos.end());
      _pedals.erase(std::remove_if(_pedals.begin(), _pedals.end(), [element](const PedalInfo& p) { return p.pedal == element; }), _pedals.end());
      if (_pedalTarget == element) { cancelGesture(); _pedalTarget = nullptr; }
      _dirty = true;
      }

void PerformanceEditor::status(const QString& message)
      {
      const QString mode = _parameter->currentIndex() == 1 ? tr("四分音符 BPM · 实际速度阶梯")
            : (_parameter->currentIndex() == 2 ? tr("原生延音踏板 · CC64 开/关")
                  : (_axis->currentIndex() ? tr("相对整数百分比") : tr("参考 Note-on 力度 1–127")));
      const QString pending = hasPending() ? tr(" · %1 音预览中；检视器仍为已保存值").arg(_pending.size()) : QString();
      _status->setText(message.isEmpty() ? (_score ? mode + pending + tr(" · 拖柱调整，铅笔/直线画线；Esc 取消。速度/踏板停播编辑。") : tr("打开乐谱后使用演奏编辑器。")) : message);
      }

QVector<int> PerformanceEditor::selectedNotes() const
      {
      return _selectedIndices;
      }

double PerformanceEditor::noteValue(int index) const
      {
      const auto& note = _notes[index];
      const auto edit = _pending.value(note.note, {note.type, note.raw});
      if (!_axis->currentIndex()) return NoteVelocity::effective(note.base, edit.type, edit.raw);
      return edit.type == Note::ValueType::OFFSET_VAL ? edit.raw : NoteVelocity::offsetFor(note.base, edit.raw);
      }

void PerformanceEditor::setNoteValue(int index, double value, bool report)
      {
      const auto& note = _notes[index];
      if (!note.audible) return;
      const auto type = _pending.value(note.note, {note.type, note.raw}).type;
      const int raw = _axis->currentIndex() ? qBound(NoteVelocity::minOffset, qRound(value), NoteVelocity::maxOffset) : qBound(1, qRound(value), 127);
      const int stored = type == Note::ValueType::OFFSET_VAL
            ? (_axis->currentIndex() ? raw : NoteVelocity::offsetFor(note.base, raw))
            : (_axis->currentIndex() ? NoteVelocity::effective(note.base, Note::ValueType::OFFSET_VAL, raw) : raw);
      if (type == note.type && stored == note.raw) _pending.remove(note.note);
      else _pending.insert(note.note, {type, stored});
      if (!report) return;
      const int actual = NoteVelocity::effective(note.base, type, stored);
      const QString range = note.baseMin != note.baseMax ? tr(" · 多次发声范围 %1–%2").arg(NoteVelocity::effective(note.baseMin, type, stored)).arg(NoteVelocity::effective(note.baseMax, type, stored)) : QString();
      status(tr("音高 %1 · 基准 %2 · 保存 %3%4 · MIDI %5%6")
            .arg(note.pitch).arg(note.base).arg(stored).arg(type == Note::ValueType::OFFSET_VAL ? "%" : "")
            .arg(actual).arg((!_axis->currentIndex() && actual != raw ? tr("（整数百分比舍入：目标 %1）").arg(raw) : QString()) + range));
      }

void PerformanceEditor::convertSelected(Note::ValueType type)
      {
      for (int index : selectedNotes()) {
            const auto& note = _notes[index];
            const auto old = _pending.value(note.note, {note.type, note.raw});
            const int value = NoteVelocity::effective(note.base, old.type, old.raw);
            _pending.insert(note.note, {type, type == Note::ValueType::USER_VAL ? value : NoteVelocity::offsetFor(note.base, value)});
            }
      applyPending(); _canvas->update(); if (_view) _view->update();
      }

void PerformanceEditor::setSelectedValue(double value)
      {
      if (_parameter->currentIndex() == 1 && _selectedTempoTick >= 0) {
            QString error;
            _committing = true;
            ParameterEdit::tempos(_score, {{_selectedTempoTick, value}}, _selectedTempoTick, &error);
            _committing = false; _dirty = true; scheduleRefresh(); status(error); return;
            }
      if (_parameter->currentIndex() != 0) { status(tr("先单击速度节点再写入 BPM；踏板请拖动端点。")); return; }
      for (int index : selectedNotes()) setNoteValue(index, value);
      applyPending(); _canvas->update(); if (_view) _view->update();
      }

void PerformanceEditor::applyPending()
      {
      if (_dragging || !_score || !hasPending()) { status(); return; }
      if (_score->isPlaying() || (seq && seq->isPlaying())) { status(); return; }
      if (seq && !seq->backgroundRenderingIdle()) { _commitTimer.start(20); return; }
      if (_score->masterScore()->state() != _state) {
            _pending.clear(); _dirty = true; scheduleRefresh();
            status(tr("乐谱已被其他操作修改，待提交预览已取消。")); return;
            }
      _committing = true;
      ParameterEdit::velocities(_score, _pending);
      _pending.clear(); _committing = false;
      _dirty = true; scheduleRefresh(); status();
      }

void PerformanceEditor::flushPending()
      {
      cancelGesture();
      if (!hasPending()) return;
      if (seq) {
            seq->stopWait(); seq->waitForStoppedRendering();
            // The GUI stopped signal can still be queued while saveFile is synchronous.
            // Transport and background rendering are already stopped at this boundary.
            _score->setIsPlaying(false);
            }
      applyPending();
      }

void PerformanceEditor::flushForScore(Score* score)
      {
      for (auto editor : editors) if (editor->_score && score && editor->_score->masterScore() == score->masterScore()) editor->flushPending();
      }

void PerformanceEditor::fit(bool selection)
      {
      int from = 0, until = _endTick;
      if (selection) {
            const auto chosen = selectedNotes();
            if (chosen.isEmpty()) return;
            from = _notes[chosen.front()].tick; until = from + 1;
            for (int index : chosen) { from = qMin(from, _notes[index].tick); until = qMax(until, _notes[index].end); }
            }
      _pixelsPerQuarter = qBound(0.01, double(qMax(100, _canvas->width() - 70)) * DIVISION / qMax(1, until - from), 1000.0);
      _scroll->setRange(0, qMax(0, _endTick - (until - from))); _scroll->setPageStep(until - from); _scroll->setValue(from);
      _canvas->update();
      }

bool PerformanceEditor::overlayAllowed() const
      {
      return isVisible() && _view && _score && !_score->printing() && !_view->fotoMode()
            && !_view->editMode() && !_view->noteEntryMode();
      }

QRectF PerformanceEditor::laneRect() const { return QRectF(48, 96, qMax(10, _canvas->width() - 58), qMax(65, _canvas->height() - 120)); }

double PerformanceEditor::xForTick(int tick, bool onScore) const
      {
      if (!onScore) return 48 + (tick - _scroll->value()) * _pixelsPerQuarter / DIVISION;
      if (!_view) return 0;
      if (_systemSegments.isEmpty()) return 0;
      auto next = std::lower_bound(_systemSegments.cbegin(), _systemSegments.cend(), tick, [](const SegmentInfo& s, int t) { return s.tick < t; });
      if (next == _systemSegments.cend()) return _view->matrix().map(_systemSegments.back().position).x();
      if (next == _systemSegments.cbegin()) return _view->matrix().map(next->position).x();
      const auto before = next - 1;
      const double x = before->position.x() + (next->position.x() - before->position.x()) * (tick - before->tick) / (next->tick - before->tick);
      return _view->matrix().map(QPointF(x, 0)).x();
      }

int PerformanceEditor::tickForX(double x, bool onScore) const
      {
      if (!onScore) return qBound(0, qRound((x - 48) * DIVISION / _pixelsPerQuarter) + _scroll->value(), _endTick);
      if (_systemSegments.isEmpty()) return 0;
      const double logical = _view->matrix().inverted().map(QPointF(x, 0)).x();
      auto next = std::lower_bound(_systemSegments.cbegin(), _systemSegments.cend(), logical, [](const SegmentInfo& s, double v) { return s.position.x() < v; });
      if (next == _systemSegments.cend()) return _systemSegments.back().tick;
      if (next == _systemSegments.cbegin()) return next->tick;
      const auto before = next - 1;
      const double width = next->position.x() - before->position.x();
      return qRound(before->tick + (width > 0 ? (logical - before->position.x()) * (next->tick - before->tick) / width : 0));
      }

int PerformanceEditor::snap(int tick, bool end) const
      {
      const auto& grid = end ? _pedalEnds : _pedalStarts;
      if (grid.isEmpty()) return 0;
      auto next = std::lower_bound(grid.cbegin(), grid.cend(), tick);
      if (next == grid.cend()) return grid.back();
      if (next == grid.cbegin()) return *next;
      const auto previous = next - 1;
      return tick - *previous < *next - tick ? *previous : *next;
      }

double PerformanceEditor::valueForY(double y, const QRectF& rect) const
      {
      const double min = _parameter->currentIndex() == 1 ? 5 : (_axis->currentIndex() ? -100 : 1);
      const double max = _parameter->currentIndex() == 1 ? _tempoMax : (_axis->currentIndex() ? _relativeMax : 127);
      return qBound(min, min + (rect.bottom() - y) * (max - min) / rect.height(), max);
      }

double PerformanceEditor::yForValue(double value, const QRectF& rect) const
      {
      const double min = _parameter->currentIndex() == 1 ? 5 : (_axis->currentIndex() ? -100 : 1);
      const double max = _parameter->currentIndex() == 1 ? _tempoMax : (_axis->currentIndex() ? _relativeMax : 127);
      return rect.bottom() - (qBound(min, value, max) - min) * rect.height() / (max - min);
      }

void PerformanceEditor::paintLane(QPainter& painter, const QRectF& rect, bool onScore)
      {
      painter.save(); painter.setClipRect(rect.adjusted(-2, -20, 2, 2));
      painter.fillRect(rect, palette().base());
      painter.setPen(palette().mid().color());
      for (int i = 0; i <= 4; ++i) painter.drawLine(QPointF(rect.left(), rect.top() + rect.height() * i / 4), QPointF(rect.right(), rect.top() + rect.height() * i / 4));
      const int parameter = _parameter->currentIndex();
      if (parameter == 0) {
            int from = tickForX(rect.left() - 30, onScore), until = tickForX(rect.right() + 30, onScore);
            auto first = std::lower_bound(_notes.cbegin(), _notes.cend(), from, [](const NoteInfo& n, int t) { return n.tick < t; });
            const bool dense = !onScore && _pixelsPerQuarter < 8;
            QVector<QPair<double, double>> density;
            if (dense) density.fill({rect.bottom(), rect.bottom()}, int(rect.width() / 2) + 1);
            int lastTick = -1, simultaneous = 0;
            for (auto it = first; it != _notes.cend() && it->tick <= until; ++it) {
                  if (onScore && it->system != _system) continue;
                  if (it->tick != lastTick) { simultaneous = 0; lastTick = it->tick; }
                  const int index = int(it - _notes.cbegin());
                  const double x = xForTick(it->tick, onScore) + simultaneous++ * 7;
                  const double y = yForValue(noteValue(index), rect);
                  if (dense && !it->selected && !_pending.contains(it->note)) {
                        const int column = int((x - rect.left()) / 2);
                        if (column >= 0 && column < density.size()) {
                              density[column].first = qMin(density[column].first, y);
                              density[column].second = qMax(density[column].second == rect.bottom() ? y : density[column].second, y);
                              }
                        continue;
                        }
                  QColor color = _pending.contains(it->note) ? pendingColor : (it->selected ? accent : palette().text().color());
                  color.setAlpha(it->audible ? 230 : 70); painter.setPen(QPen(color, it->selected ? 2 : 1));
                  painter.drawLine(QPointF(x, rect.bottom()), QPointF(x, y));
                  painter.setBrush(it->audible ? QBrush(color) : Qt::NoBrush); painter.drawEllipse(QPointF(x, y), 3.5, 3.5);
                  }
            if (dense) {
                  painter.setPen(QPen(palette().text().color(), 1));
                  for (int i = 0; i < density.size(); ++i) if (density[i].first < rect.bottom()) {
                        const double x = rect.left() + i * 2;
                        painter.drawLine(QPointF(x, rect.bottom()), QPointF(x, density[i].first));
                        painter.drawLine(QPointF(x - 1, density[i].second), QPointF(x + 1, density[i].second));
                        }
                  }
            }
      else if (parameter == 1) {
            auto bpmAt = [this](const SegmentInfo& segment) {
                  const auto original = std::upper_bound(_tempos.cbegin(), _tempos.cend(), segment.tick, [](int t, const TempoInfo& info) { return t < info.tick; });
                  const int originalTick = original == _tempos.cbegin() ? -1 : (original - 1)->tick;
                  auto draft = _tempoDraft.upperBound(segment.tick);
                  if (draft != _tempoDraft.cbegin()) {
                        --draft;
                        if (draft.key() >= originalTick) return draft.value();
                        }
                  return segment.bpm;
                  };
            const int from = tickForX(rect.left(), onScore), until = tickForX(rect.right(), onScore);
            auto first = std::lower_bound(_segments.cbegin(), _segments.cend(), from, [](const SegmentInfo& s, int t) { return s.tick < t; });
            if (first != _segments.cbegin()) --first;
            const SegmentInfo* previous = nullptr;
            for (auto it = first; it != _segments.cend(); ++it) {
                  const auto& segment = *it;
                  if (onScore && segment.system != _system) continue;
                  const double bpm = bpmAt(segment);
                  if (previous) {
                        const double x0 = xForTick(previous->tick, onScore), x1 = xForTick(segment.tick, onScore);
                        painter.setPen(QPen(accent, 2));
                        painter.drawLine(QPointF(x0, yForValue(bpmAt(*previous), rect)), QPointF(x1, yForValue(bpmAt(*previous), rect)));
                        painter.drawLine(QPointF(x1, yForValue(bpmAt(*previous), rect)), QPointF(x1, yForValue(bpm, rect)));
                        }
                  previous = &segment;
                  if (segment.tick > until) break;
                  }
            for (const auto& tempo : _tempos) {
                  const double x = xForTick(tempo.tick, onScore), y = yForValue(_tempoDraft.value(tempo.tick, tempo.bpm), rect);
                  if (onScore && (tempo.tick < tickForX(rect.left(), true) || tempo.tick > tickForX(rect.right(), true))) continue;
                  painter.setPen(QPen(tempo.visible ? pendingColor : accent, 2)); painter.setBrush(palette().base());
                  painter.drawRect(QRectF(x - 4, y - 4, 8, 8));
                  }
            }
      else {
            painter.setPen(QPen(accent, 2));
            const double y = rect.center().y();
            for (const auto& pedal : _pedals) {
                  int from = pedal.from, until = pedal.until;
                  if (_dragging && _pedalTarget == pedal.pedal) { from = _pedalFrom; until = _pedalUntil; }
                  const double x0 = xForTick(from, onScore), x1 = xForTick(until, onScore);
                  painter.drawLine(QPointF(x0, y), QPointF(x1, y));
                  painter.drawRect(QRectF(x0 - 4, y - 4, 8, 8)); painter.drawRect(QRectF(x1 - 4, y - 4, 8, 8));
                  }
            if (_dragging && !_pedalTarget) {
                  painter.setPen(QPen(pendingColor, 3));
                  painter.drawLine(QPointF(xForTick(_pedalFrom, onScore), y), QPointF(xForTick(_pedalUntil, onScore), y));
                  }
            }
      painter.restore();
      }

void PerformanceEditor::paintTimeline(QPainter& painter)
      {
      painter.setRenderHint(QPainter::Antialiasing);
      painter.fillRect(_canvas->rect(), palette().window());
      _lane = laneRect();
      painter.setPen(palette().text().color());
      painter.drawText(QRectF(6, 2, _canvas->width() - 12, 22), tr("音符参考 · 时间轴（记谱时间）"));
      const int from = tickForX(0, false), until = tickForX(_canvas->width(), false);
      const int step = qMax(1, int(std::ceil(65.0 / _pixelsPerQuarter)));
      painter.setPen(palette().mid().color());
      for (int quarter = (from / DIVISION / step) * step; quarter * DIVISION <= until; quarter += step) {
            const double x = xForTick(quarter * DIVISION, false);
            painter.drawLine(QPointF(x, 84), QPointF(x, 94));
            painter.drawText(QPointF(x + 3, 93), QString::number(quarter + 1));
            }
      auto first = std::lower_bound(_notes.cbegin(), _notes.cend(), from, [](const NoteInfo& n, int t) { return n.tick < t; });
      QVector<bool> occupied(_canvas->width() * 64, false);
      for (auto it = first; it != _notes.cend() && it->tick <= until; ++it) {
            const double x = xForTick(it->tick, false), y = 82 - (it->pitch - 36) * 0.75;
            if (_pixelsPerQuarter < 8 && !it->selected) {
                  const int column = qBound(0, int(x), _canvas->width() - 1), row = qBound(0, int(y) - 25, 63);
                  const int cell = column * 64 + row;
                  if (occupied[cell]) continue;
                  occupied[cell] = true;
                  }
            painter.fillRect(QRectF(x, qBound(25.0, y, 84.0), qMax(3.0, xForTick(it->end, false) - x - 1), 4), it->selected ? accent : palette().mid().color());
            }
      painter.drawText(5, 112, _parameter->currentIndex() == 1 ? "BPM" : (_parameter->currentIndex() == 2 ? "CC64" : (_axis->currentIndex() ? "%" : "127")));
      paintLane(painter, _lane, false);
      }

void PerformanceEditor::paintOverlay(QPainter& painter)
      {
      if (!overlayAllowed()) { _scoreLane = QRectF(); return; }
      painter.save(); painter.resetTransform(); painter.setRenderHint(QPainter::Antialiasing);
      if (_handles->isChecked() && _parameter->currentIndex() == 0) {
            QVector<int> handles = _selectedIndices;
            if (_hover >= 0 && !handles.contains(_hover)) handles.append(_hover);
            for (int i : handles) {
                  if (i >= _notes.size()) continue;
                  const auto& note = _notes[i];
                  const QRectF bounds = _view->matrix().mapRect(note.bounds);
                  if (!_view->rect().intersects(bounds.toRect())) continue;
                  const QPointF handle = bounds.topRight() + QPointF(12, -12);
                  painter.setPen(QPen(_pending.contains(note.note) ? pendingColor : accent, 2));
                  painter.setBrush(note.audible ? QBrush(palette().base()) : Qt::NoBrush);
                  painter.drawEllipse(handle, 5, 5);
                  if (_view->matrix().m11() > 0.5) painter.drawText(handle + QPointF(8, -4), QString::number(noteValue(i)) + (_axis->currentIndex() ? "%" : ""));
                  }
            }
      _scoreLane = QRectF();
      if (_band->isChecked() && _system) {
            double left = _view->width(), right = 0;
            for (int index : _systemIndices) {
                  const auto& note = _notes[index];
                  const QRectF bounds = _view->matrix().mapRect(note.bounds);
                  left = qMin(left, bounds.left()); right = qMax(right, bounds.right());
                  }
            const double y = _view->height() - 114; // Floating strip: never participates in score layout.
            _scoreLane = QRectF(qBound(48.0, left - 8, double(qMax(48, _view->width() - 100))), y, qMax(60.0, qMin(double(_view->width() - 10), right + 25) - left), 88);
            painter.setPen(palette().text().color());
            painter.fillRect(_scoreLane.adjusted(-4, -22, 4, 4), palette().window());
            painter.drawText(_scoreLane.topLeft() - QPointF(0, 5), tr("当前谱行 · %1 · 浮动参数带").arg(_parameter->currentText()));
            paintLane(painter, _scoreLane, true);
            }
      painter.restore();
      }

void PerformanceEditor::paintForView(ScoreView* view, QPainter& painter)
      { for (auto editor : editors) if (editor->_view == view) editor->paintOverlay(painter); }

void PerformanceEditor::beginGesture(QPointF point, QRectF rect, bool onScore, int forcedAnchor)
      {
      if (!_score || _score->readOnly() || _dirty || _segments.isEmpty()) return;
      if (_parameter->currentIndex() && (_score->isPlaying() || (seq && (seq->isPlaying() || !seq->backgroundRenderingIdle())))) { status(tr("速度与踏板请停播后编辑。")); return; }
      _gestureBefore = _pending; _tempoDraft.clear(); _gestureLane = rect; _press = _last = point;
      _scoreGesture = onScore; _dragging = true; _anchor = forcedAnchor; _offsetDrag = false; _unlockedTempo = -1;
      if (_parameter->currentIndex() == 0) {
            double distance = 13;
            int lastTick = -1, simultaneous = 0;
            const int from = tickForX(rect.left() - 32, onScore), until = tickForX(rect.right() + 32, onScore);
            auto first = std::lower_bound(_notes.cbegin(), _notes.cend(), from, [](const NoteInfo& n, int t) { return n.tick < t; });
            for (auto it = first; forcedAnchor < 0 && it != _notes.cend() && it->tick <= until; ++it) {
                  const int i = int(it - _notes.cbegin());
                  const auto& note = *it;
                  if (onScore && note.system != _system) continue;
                  if (note.tick != lastTick) { lastTick = note.tick; simultaneous = 0; }
                  const QPointF center(xForTick(note.tick, onScore) + simultaneous++ * 7, yForValue(noteValue(i), rect));
                  const double d = QLineF(point, center).length();
                  if (note.audible && d < distance) { distance = d; _anchor = i; }
                  }
            if (_tool->currentIndex() == 0 && _anchor < 0) { _dragging = false; return; }
            if (_tool->currentIndex() == 0 && _anchor >= 0) {
                  _offsetDrag = true;
                  _dragTargets = _notes[_anchor].selected ? selectedNotes() : QVector<int>{_anchor};
                  _dragOriginal.clear(); for (int index : _dragTargets) _dragOriginal.insert(index, noteValue(index));
                  }
            }
      else if (_parameter->currentIndex() == 1) {
            for (const auto& tempo : _tempos)
                  if (QLineF(point, QPointF(xForTick(tempo.tick, onScore), yForValue(tempo.bpm, rect))).length() < 10) _unlockedTempo = tempo.tick;
            }
      else {
            _pedalTarget = nullptr;
            _pedalFrom = _pedalUntil = tickForX(point.x(), onScore);
            for (const auto& pedal : _pedals) {
                  if (std::abs(point.y() - rect.center().y()) > 14) continue;
                  if (std::abs(point.x() - xForTick(pedal.from, onScore)) < 10 || std::abs(point.x() - xForTick(pedal.until, onScore)) < 10) {
                        _pedalTarget = pedal.pedal; _pedalFrom = pedal.from; _pedalUntil = pedal.until;
                        _pedalEnd = std::abs(point.x() - xForTick(pedal.until, onScore)) < std::abs(point.x() - xForTick(pedal.from, onScore));
                        break;
                        }
                  }
            }
      if (_parameter->currentIndex() == 2) {
            _pedalTrack = _pedalTarget ? _pedalTarget->track() : _score->staffIdx(_score->staff(_contextTrack / VOICES)->part()) * VOICES;
            _pedalStarts.clear(); _pedalEnds.clear();
            const int staffTrack = (_pedalTrack / VOICES) * VOICES;
            for (auto segment = _score->firstSegment(SegmentType::ChordRest); segment; segment = segment->next1(SegmentType::ChordRest))
                  for (int voice = 0; voice < VOICES; ++voice) {
                        auto element = segment->element(staffTrack + voice);
                        if (!element || !element->isChordRest()) continue;
                        auto cr = toChordRest(element);
                        _pedalStarts.append(cr->tick().ticks()); _pedalEnds.append((cr->tick() + cr->actualTicks()).ticks());
                        }
            for (auto grid : {&_pedalStarts, &_pedalEnds}) {
                  std::sort(grid->begin(), grid->end()); grid->erase(std::unique(grid->begin(), grid->end()), grid->end());
                  }
            if (!_pedalTarget) { _pedalFrom = snap(_pedalFrom); _pedalUntil = snap(_pedalUntil, true); }
            }
      if (_parameter->currentIndex() == 1) {
            _selectedTempoTick = _unlockedTempo;
            for (const auto& tempo : _tempos) if (tempo.tick == _selectedTempoTick) {
                  QSignalBlocker blocker(_number); _number->setValue(tempo.bpm); break;
                  }
            }
      // The host's application filter dispatches Escape directly to QWidget::event.
      // Observe the application only for our active gesture, ahead of that filter.
      qApp->installEventFilter(this);
      if (onScore) { _view->setFocus(Qt::MouseFocusReason); _view->grabMouse(); }
      else { _canvas->setFocus(Qt::MouseFocusReason); _canvas->grabMouse(); }
      }

void PerformanceEditor::moveGesture(QPointF point)
      {
      if (!_dragging) return;
      if (_parameter->currentIndex() == 2) {
            const int tick = tickForX(point.x(), _scoreGesture);
            if (_pedalTarget) {
                  if (_pedalEnd) { const int end = snap(tick, true); if (end > _pedalFrom) _pedalUntil = end; }
                  else { const int from = snap(tick); if (from < _pedalUntil) _pedalFrom = from; }
                  }
            else {
                  const int pressed = tickForX(_press.x(), _scoreGesture);
                  _pedalFrom = snap(qMin(pressed, tick)); _pedalUntil = snap(qMax(pressed, tick), true);
                  }
            }
      else if (_offsetDrag) {
            const double delta = valueForY(point.y(), _gestureLane) - valueForY(_press.y(), _gestureLane);
            for (int index : _dragTargets) setNoteValue(index, _dragOriginal.value(index) + delta, false);
            }
      else {
            const bool line = _tool->currentIndex() == 2;
            const QPointF start = line ? _press : _last;
            if (line) { _pending = _gestureBefore; _tempoDraft.clear(); }
            int from = tickForX(start.x(), _scoreGesture), until = tickForX(point.x(), _scoreGesture);
            double v0 = valueForY(start.y(), _gestureLane), v1 = valueForY(point.y(), _gestureLane);
            if (until < from) { std::swap(from, until); std::swap(v0, v1); }
            auto value = [from, until, v0, v1](int tick) { return until == from ? v1 : v0 + (v1 - v0) * (tick - from) / (until - from); };
            if (_parameter->currentIndex() == 0) {
                  const bool selected = !_selectedIndices.isEmpty();
                  auto first = std::lower_bound(_notes.cbegin(), _notes.cend(), from, [](const NoteInfo& n, int t) { return n.tick < t; });
                  for (auto it = first; it != _notes.cend() && it->tick <= until; ++it)
                        if ((!selected || it->selected) && (!_scoreGesture || it->system == _system)) setNoteValue(int(it - _notes.cbegin()), value(it->tick), false);
                  }
            else {
                  if (_unlockedTempo >= 0 && _tool->currentIndex() == 0) _tempoDraft[_unlockedTempo] = v1;
                  else {
                        auto first = std::lower_bound(_segments.cbegin(), _segments.cend(), from, [](const SegmentInfo& s, int t) { return s.tick < t; });
                        for (auto it = first; it != _segments.cend() && it->tick <= until; ++it)
                              if (it->tick < _endTick && (!_scoreGesture || it->system == _system)) {
                                    bool locked = false;
                                    auto tempo = std::lower_bound(_tempos.cbegin(), _tempos.cend(), it->tick, [](const TempoInfo& info, int t) { return info.tick < t; });
                                    for (; tempo != _tempos.cend() && tempo->tick == it->tick; ++tempo)
                                          if (tempo->visible && tempo->tick != _unlockedTempo) locked = true;
                                    if (!locked) _tempoDraft[it->tick] = value(it->tick);
                                    }
                        }
                  }
            }
      if (_parameter->currentIndex() == 0) {
            if (_anchor >= 0) setNoteValue(_anchor, noteValue(_anchor)); else status();
            }
      _last = point; _canvas->update(); if (_view) _view->update();
      }

void PerformanceEditor::finishGesture()
      {
      if (!_dragging) return;
      _dragging = false; qApp->removeEventFilter(this);
      if (_view) _view->releaseMouse(); _canvas->releaseMouse();
      if (_parameter->currentIndex() == 0) applyPending();
      else {
            QString error;
            _committing = true;
            if (_parameter->currentIndex() == 1) {
                  if (_unlockedTempo < 0 && !_tempoDraft.isEmpty()) {
                        const int end = _tempoDraft.lastKey();
                        for (const auto& segment : _segments) if (segment.tick > end && segment.tick < _endTick) {
                              _tempoDraft[segment.tick] = segment.bpm; break;
                              }
                        }
                  ParameterEdit::tempos(_score, _tempoDraft, _unlockedTempo, &error);
                  }
            else {
                  ParameterEdit::pedal(_score, _pedalTrack, _pedalFrom, _pedalUntil, _pedalTarget, &error);
                  }
            _committing = false; _tempoDraft.clear(); _dirty = true; scheduleRefresh(); status(error);
            }
      _gestureBefore.clear();
      }

void PerformanceEditor::cancelGesture()
      {
      if (!_dragging) return;
      _pending = _gestureBefore; _gestureBefore.clear(); _tempoDraft.clear(); _dragging = false; qApp->removeEventFilter(this);
      if (_view) _view->releaseMouse(); if (_canvas) _canvas->releaseMouse();
      }

bool PerformanceEditor::eventFilter(QObject* object, QEvent* event)
      {
      if (_dragging && (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)
            && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            event->accept();
            if (event->type() == QEvent::KeyPress) { cancelGesture(); _canvas->update(); if (_view) _view->update(); }
            return true;
            }
      const bool onScore = object == _view;
      if (object != _canvas && !onScore) return false;
      if (event->type() == QEvent::Show && object == _canvas) { scheduleRefresh(); return false; }
      if (onScore && !overlayAllowed()) return false;
      if (event->type() == QEvent::ShortcutOverride && _dragging && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) { event->accept(); return true; }
      if (event->type() == QEvent::Hide) cancelGesture();
      if (event->type() == QEvent::KeyPress && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape && _dragging) {
            cancelGesture(); _canvas->update(); if (_view) _view->update(); return true;
            }
      if (event->type() == QEvent::MouseMove) {
            auto mouse = static_cast<QMouseEvent*>(event);
            if (_dragging) { moveGesture(mouse->pos()); return true; }
            if (onScore && _handles->isChecked()) {
                  Element* element = _view->elementNear(_view->toLogical(mouse->pos()));
                  const int hover = _noteIndex.value(element, -1);
                  if (hover != _hover) { _hover = hover; updateSelection(); }
                  }
            }
      if (event->type() == QEvent::MouseButtonPress) {
            auto mouse = static_cast<QMouseEvent*>(event);
            const QRectF contextLane = onScore ? _scoreLane : laneRect();
            if (mouse->button() == Qt::RightButton && contextLane.contains(mouse->pos()) && _parameter->currentIndex() == 0) {
                  QMenu menu;
                  for (int i = 0; i < _notes.size(); ++i) {
                        const auto& note = _notes[i];
                        if ((onScore && note.system != _system) || std::abs(xForTick(note.tick, onScore) - mouse->x()) > 22) continue;
                        auto action = menu.addAction(tr("音高 %1 · 声部 %2 · %3%4").arg(note.pitch).arg(note.track % VOICES + 1).arg(noteValue(i))
                              .arg(note.audible ? QString() : tr(" · 不独立发声")));
                        action->setEnabled(note.audible && !(seq && seq->isPlaying()));
                        connect(action, &QAction::triggered, this, [this, i] {
                              _score->select(_notes[i].note); _score->update(); if (mscore) mscore->endCmd(); updateSelection();
                              });
                        }
                  if (!menu.isEmpty()) menu.exec(mouse->globalPos());
                  return true;
                  }
            if (mouse->button() != Qt::LeftButton) return false;
            if (onScore && _handles->isChecked() && _parameter->currentIndex() == 0) {
                  QVector<int> handles = _selectedIndices;
                  if (_hover >= 0 && !handles.contains(_hover)) handles.append(_hover);
                  for (int i : handles) {
                        if (i >= _notes.size()) continue;
                        const auto& note = _notes[i];
                        const QPointF center = _view->matrix().mapRect(note.bounds).topRight() + QPointF(12, -12);
                        if (note.audible && QLineF(center, mouse->pos()).length() < 11) {
                              if (!note.selected && !_score->isPlaying() && !(seq && seq->isPlaying())) {
                                    _score->select(note.note, mouse->modifiers() & Qt::ControlModifier ? SelectType::ADD : SelectType::SINGLE);
                                    _score->update(); if (mscore) mscore->endCmd(); updateSelection();
                                    }
                              beginGesture(mouse->pos(), QRectF(mouse->x() - 20, mouse->y() - 80, 40, 160), true, i);
                              if (_dragging) {
                                    _anchor = i; _offsetDrag = true;
                                    _dragTargets = note.selected ? selectedNotes() : QVector<int>{i};
                                    _dragOriginal.clear(); for (int index : _dragTargets) _dragOriginal[index] = noteValue(index);
                                    }
                              return _dragging;
                              }
                        }
                  }
            const QRectF rect = onScore ? _scoreLane : laneRect();
            if (rect.contains(mouse->pos())) { beginGesture(mouse->pos(), rect, onScore); return _dragging; }
            if (!onScore && mouse->y() < 90 && _score && !(seq && seq->isPlaying())) {
                  const int tick = tickForX(mouse->x(), false);
                  for (const auto& note : _notes) if (note.tick <= tick && tick <= note.end) {
                        const double y = qBound(25.0, 82 - (note.pitch - 36) * 0.75, 84.0);
                        if (std::abs(mouse->y() - y) < 5) {
                              _score->select(note.note, mouse->modifiers() & Qt::ControlModifier ? SelectType::ADD : SelectType::SINGLE);
                              _score->update(); if (mscore) mscore->endCmd(); updateSelection(); return true;
                              }
                        }
                  }
            }
      if (event->type() == QEvent::MouseButtonRelease && _dragging) { finishGesture(); return true; }
      if (event->type() == QEvent::Wheel && !onScore) {
            auto wheel = static_cast<QWheelEvent*>(event);
            if (wheel->modifiers() & Qt::ControlModifier) {
                  const int tick = tickForX(wheel->position().x(), false);
                  _pixelsPerQuarter = qBound(0.01, _pixelsPerQuarter * (wheel->angleDelta().y() > 0 ? 1.2 : 1 / 1.2), 1000.0);
                  const int span = qMax(1, qRound((_canvas->width() - 58) * DIVISION / _pixelsPerQuarter));
                  _scroll->setRange(0, qMax(0, _endTick - span)); _scroll->setPageStep(span);
                  _scroll->setValue(qMax(0, tick - qRound((wheel->position().x() - 48) * DIVISION / _pixelsPerQuarter)));
                  }
            else _scroll->setValue(_scroll->value() - wheel->angleDelta().y() * DIVISION / 120);
            _canvas->update(); return true;
            }
      return false;
      }
}
