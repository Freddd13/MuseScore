// SPDX-License-Identifier: GPL-2.0-or-later
#include "vstparameters.h"
#ifdef KUMO_VST_HOST
#include "audio/vst/plugin.h"
#include <QtWidgets>
#include <cmath>
namespace Ms {
namespace {
class PositionSlider final : public QSlider {
  std::function<void(bool)> _gesture;
  bool _keys = false;
  void keyPressEvent(QKeyEvent *event) override {
    if (event->key() == Qt::Key_Left || event->key() == Qt::Key_Right ||
        event->key() == Qt::Key_Home || event->key() == Qt::Key_End ||
        event->key() == Qt::Key_PageUp || event->key() == Qt::Key_PageDown) {
      _keys = true;
      _gesture(true);
    }
    QSlider::keyPressEvent(event);
  }
  void keyReleaseEvent(QKeyEvent *event) override {
    QSlider::keyReleaseEvent(event);
    if (_keys && !event->isAutoRepeat()) {
      _keys = false;
      _gesture(false);
    }
  }
  void focusOutEvent(QFocusEvent *event) override {
    QSlider::focusOutEvent(event);
    if (_keys) {
      _keys = false;
      _gesture(false);
    }
  }

public:
  PositionSlider(QWidget *parent, std::function<void(bool)> gesture)
      : QSlider(Qt::Horizontal, parent), _gesture(std::move(gesture)) {}
};
class ParameterWindow final : public QDialog {
  std::function<VstHost::Plugin *()> resolve;
  std::function<void(bool)> changed;
  QTreeWidget *table;
  QSlider *slider;
  QDoubleSpinBox *value;
  bool updating = false, dirty = false, forced = false, gesture = false;
  QAction *undoAction, *redoAction;
  QString binding;
  bool eventFilter(QObject *object, QEvent *event) override {
    if (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) {
      auto key = static_cast<QKeyEvent *>(event);
      const QKeySequence sequence(key->key() | int(key->modifiers()));
      QAction *action = nullptr;
      for (auto candidate : {undoAction, redoAction})
        if (candidate && candidate->shortcuts().contains(sequence)) {
          action = candidate;
          break;
        }
      if (action) {
        event->accept();
        if (event->type() == QEvent::KeyPress) {
          commit();
          if (action->isEnabled())
            action->trigger();
        }
        return true;
      }
    }
    return QDialog::eventFilter(object, event);
  }
  void commit() {
    if (!forced && dirty) {
      dirty = false;
      if (resolve())
        changed(true);
    }
  }
  void moveTo(int at) {
    if (updating || forced || !table->currentItem())
      return;
    auto plugin = resolve();
    if (!plugin) {
      retire();
      return;
    }
    auto item = table->currentItem();
    auto p = item->data(0, Qt::UserRole).toMap();
    if (p["readOnly"].toBool())
      return;
    p["value"] = at / 10000.;
    plugin->setParameter(p["id"].toUInt(), at / 10000.);
    item->setData(0, Qt::UserRole, p);
    item->setText(1, QString::number(at / 100., 'f', 2) + "%");
    QSignalBlocker a(slider), b(value);
    slider->setValue(at);
    value->setValue(at / 100.);
    dirty = true;
    changed(false);
  }
  void choose(QTreeWidgetItem *item) {
    if (!item) {
      slider->setEnabled(false);
      value->setEnabled(false);
      return;
    }
    const auto p = item->data(0, Qt::UserRole).toMap();
    QSignalBlocker a(slider), b(value);
    slider->setValue(qRound(p["value"].toDouble() * 10000));
    value->setValue(p["value"].toDouble() * 100);
    slider->setEnabled(!p["readOnly"].toBool());
    value->setEnabled(!p["readOnly"].toBool());
  }
  void closeEvent(QCloseEvent *event) override {
    commit();
    QDialog::closeEvent(event);
  }

public:
  ParameterWindow(QWidget *parent, QString key, QString title,
                  std::function<VstHost::Plugin *()> query, std::function<void(bool)> onChange,
                  QAction *undo, QAction *redo)
      : QDialog(parent, Qt::Tool), resolve(std::move(query)), changed(std::move(onChange)),
        undoAction(undo), redoAction(redo), binding(std::move(key)) {
    setObjectName("kumoVstParameters");
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(title);
    setWindowFlag(Qt::WindowMaximizeButtonHint, true);
    setWindowFlag(Qt::WindowCloseButtonHint, true);
    auto layout = new QVBoxLayout(this);
    auto top = new QHBoxLayout;
    auto filter = new QLineEdit;
    filter->setObjectName("kumoVstParameterFilter");
    filter->setPlaceholderText(tr("查找参数"));
    top->addWidget(filter, 1);
    auto full = new QToolButton;
    full->setObjectName("kumoVstParametersFullscreen");
    full->setText(tr("全屏"));
    full->setToolTip(tr("全屏查看；再次点击返回窗口"));
    top->addWidget(full);
    layout->addLayout(top);
    for (auto action : {undo, redo}) {
      auto button = new QToolButton;
      button->setDefaultAction(action);
      button->setToolButtonStyle(Qt::ToolButtonIconOnly);
      top->insertWidget(top->count() - 1, button);
    }
    connect(full, &QToolButton::clicked, this, [this, full] {
      if (isFullScreen()) {
        showNormal();
        full->setText(tr("全屏"));
      } else {
        showFullScreen();
        full->setText(tr("返回窗口"));
      }
    });
    table = new QTreeWidget;
    table->setObjectName("kumoVstParameterList");
    table->setHeaderLabels({tr("参数"), tr("旋钮位置")});
    table->setRootIsDecorated(false);
    table->setAlternatingRowColors(true);
    table->header()->setStretchLastSection(false);
    table->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    layout->addWidget(table, 1);
    auto row = new QHBoxLayout;
    row->addWidget(new QLabel("0%"));
    slider = new PositionSlider(this, [this](bool active) {
      gesture = active;
      if (!active)
        commit();
    });
    slider->setObjectName("kumoVstParameterPosition");
    slider->setRange(0, 10000);
    slider->setSingleStep(100);
    slider->setPageStep(1000);
    row->addWidget(slider, 1);
    row->addWidget(new QLabel("100%"));
    value = new QDoubleSpinBox;
    value->setObjectName("kumoVstParameterValue");
    value->setRange(0, 100);
    value->setDecimals(2);
    value->setSingleStep(1);
    value->setSuffix("%");
    row->addWidget(value);
    layout->addLayout(row);
    auto help =
        new QLabel(tr("选参数后拖动滑块，或输入旋钮位置百分比。修改随乐谱保存，可按Ctrl+Z撤销。"));
    help->setWordWrap(true);
    layout->addWidget(help);
    connect(filter, &QLineEdit::textChanged, this, [this](const QString &text) {
      QTreeWidgetItem *first = nullptr;
      for (int i = 0; i < table->topLevelItemCount(); ++i) {
        auto item = table->topLevelItem(i);
        item->setHidden(!item->text(0).contains(text, Qt::CaseInsensitive));
        if (!first && !item->isHidden())
          first = item;
      }
      if (!table->currentItem() || table->currentItem()->isHidden())
        table->setCurrentItem(first);
      choose(table->currentItem());
    });
    connect(table, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem *item, QTreeWidgetItem *) {
              commit();
              choose(item);
            });
    connect(slider, &QSlider::sliderPressed, this, [this] { gesture = true; });
    connect(slider, &QSlider::sliderReleased, this, [this] {
      gesture = false;
      commit();
    });
    connect(slider, &QSlider::valueChanged, this, &ParameterWindow::moveTo);
    connect(value, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this](double v) { moveTo(qRound(v * 100)); });
    connect(value, &QDoubleSpinBox::editingFinished, this, [this] { commit(); });
    for (auto widget : findChildren<QWidget *>())
      widget->installEventFilter(this);
    resize(480, 540);
    refresh();
  }
  ~ParameterWindow() override {
    forced = true;
    for (auto child : findChildren<QObject *>())
      disconnect(child, nullptr, this, nullptr);
  }
  bool matches(QWidget *owner, const QString &key) const {
    return parentWidget() == owner && binding == key;
  }
  bool activeGesture() const {
    auto focus = QApplication::focusWidget();
    return gesture || (focus && value->isAncestorOf(focus));
  }
  void retire() {
    forced = true;
    setEnabled(false);
    close();
  }
  void refresh() {
    if (forced)
      return;
    auto plugin = resolve();
    if (!plugin) {
      retire();
      return;
    }
    if (activeGesture())
      return;
    const auto parameters = plugin->parameters();
    const auto query = findChild<QLineEdit *>("kumoVstParameterFilter")->text();
    updating = true;
    QSignalBlocker block(table);
    for (int i = 0; i < parameters.size(); ++i) {
      auto p = parameters[i].toMap();
      auto item =
          i < table->topLevelItemCount() ? table->topLevelItem(i) : new QTreeWidgetItem(table);
      item->setText(0, p["name"].toString());
      item->setHidden(!p["name"].toString().contains(query, Qt::CaseInsensitive));
      item->setText(1, QString::number(p["value"].toDouble() * 100, 'f', 2) + "%");
      item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
      item->setData(0, Qt::UserRole, p);
      item->setToolTip(0, p["text"].toString() + " " + p["unit"].toString());
    }
    while (table->topLevelItemCount() > parameters.size())
      delete table->takeTopLevelItem(table->topLevelItemCount() - 1);
    if (!table->currentItem() || table->currentItem()->isHidden()) {
      QTreeWidgetItem *first = nullptr;
      for (int i = 0; i < table->topLevelItemCount(); ++i)
        if (!table->topLevelItem(i)->isHidden()) {
          first = table->topLevelItem(i);
          break;
        }
      table->setCurrentItem(first);
    }
    choose(table->currentItem());
    updating = false;
  }
};
} // namespace
void showVstParameters(QWidget *parent, const QString &binding, const QString &title,
                       std::function<VstHost::Plugin *()> resolve,
                       std::function<void(bool)> changed, QAction *undo, QAction *redo) {
  for (auto widget : QApplication::topLevelWidgets())
    if (auto window = dynamic_cast<ParameterWindow *>(widget))
      if (window->matches(parent, binding) && window->isEnabled()) {
        window->refresh();
        window->show();
        window->raise();
        window->activateWindow();
        return;
      }
  auto window = new ParameterWindow(parent, binding, title, std::move(resolve), std::move(changed),
                                    undo, redo);
  window->show();
  VstHost::placeEditorOnScreen(window);
}
void refreshVstParameterWindows() {
  for (auto widget : QApplication::topLevelWidgets())
    if (auto window = dynamic_cast<ParameterWindow *>(widget))
      window->refresh();
}
bool vstParameterGestureActive() {
  for (auto widget : QApplication::topLevelWidgets())
    if (auto window = dynamic_cast<ParameterWindow *>(widget))
      if (window->isVisible() && window->activeGesture())
        return true;
  return false;
}
void closeVstParameterWindows(QWidget *owner) {
  for (auto widget : QApplication::topLevelWidgets())
    if (auto window = dynamic_cast<ParameterWindow *>(widget))
      if (window->parentWidget() == owner)
        window->retire();
}
} // namespace Ms
#endif
