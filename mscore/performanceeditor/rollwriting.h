// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "mscore/plugin/api/scoreobserver.h"
#include <QFutureWatcher>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QWidget>
#include <atomic>
#include <memory>

class QComboBox;
class QLabel;
class QToolButton;
class QPainter;
class QAction;
class QMenu;
class QSpinBox;
namespace Ms {
class Score;
class MasterScore;
class PerformanceEditor;
class PerformanceIntervalIndex;
class ControlFlowLayout;
QAction *rollWritingAction();
class RollWritingBar final : public QWidget {
  Q_OBJECT
  PerformanceEditor *_editor;
  PluginAPI::ScoreObserver _reader;
  QPointer<Score> _score;
  QToolButton *_toggle, *_apply, *_original, *_proposed, *_stop;
  QComboBox *_mode, *_tool, *_grid, *_track;
  QLabel *_range, *_message;
  ControlFlowLayout *_layout;
  QMenu *_lockMenu, *_savedMenu;
  QAction *_autoVoice, *_precision, *_followDynamics;
  QSpinBox *_fixedVelocity;
  QVector<QWidget *> _controls;
  QTimer _pages;
  QFutureWatcher<QVariantMap> _calculation;
  std::shared_ptr<std::atomic_bool> _cancel;
  std::shared_ptr<MasterScore> _preview;
  QVariantMap _state, _data, _built;
  QVariantList _rows, _before, _moveBase, _originalRows, _pendingLocations,
      _measures, _tuplets;
  QSet<QString> _selected, _beforeSelected;
  std::unique_ptr<PerformanceIntervalIndex> _intervals;
  bool _indexDirty = true;
  QVector<int> _gestureIndices;
  void ensureIndex();
  QSet<int> _locked;
  QString _revision, _identity, _savedText, _previewKey;
  bool _hasChanges = false, _beforeHasChanges = false, _refreshOnly = false;
  QVariantMap _previousBuilt;
  bool _enabled = false, _ready = false, _busy = false, _internal = false,
       _closing = false, _moved = false;
  bool _gesture = false, _marquee = false, _copy = false, _tail = false,
       _changed = false;
  QPointF _press, _marqueeEnd;
  int _pressTick = 0, _pressPitch = 0, _cursorTick = 0, _generation = 0,
      _jobGeneration = 0, _cursor = 0;
  QString scopeId() const;
  QVariantMap settings() const;
  QVariantMap stored() const;
  QVariantMap record(bool draft) const;

  bool editableTrack(int) const;
  void setWritingEnabled(bool);
  void setMessage(const QString &);
  void readScope(const QVariantMap & = {});
  void readPage();
  void finishRead();
  void calculate(bool refreshOnly = false);
  void calculated();
  void saveDraft();
  void restoreFromScore();
  void discardDraft();
  void stopAudition();
  bool preparePreview();
  void audition(bool);
  void applyDraft();
  void updateActions();
  void lockMenu();
  void savedMenu();
  void selectSourceNotes();
  void eraseSelected();
  void quantizeSelected();
  void duplicateSelected(int offset);
  int quantize(int tick, int track, const QVariantMap &note = {}) const;
  int pitchAt(double) const;
  QRectF rectangle(const QVariantMap &) const;
  int hit(QPointF) const;
  void begin(QPointF, Qt::KeyboardModifiers);
  void move(QPointF);
  void finish(QPointF);
  void cancelGesture();
  void hideEvent(QHideEvent *) override;
  void showEvent(QShowEvent *) override;
  bool eventFilter(QObject *, QEvent *) override;

public:
  RollWritingBar(PerformanceEditor *, QToolButton *, QWidget *parent);
  ~RollWritingBar() override;
  void shutdown();
  bool handle(QEvent *);
  void paint(QPainter &, const QRectF &);
  bool dimsOriginal(int tick, int track) const;
  void contextChanged();
  Q_INVOKABLE QVariantMap state() const;
};
} // namespace Ms
