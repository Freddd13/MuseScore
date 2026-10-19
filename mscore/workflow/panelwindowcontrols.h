// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QEvent>
#include <QGuiApplication>
#include <QMainWindow>
#include <QKeyEvent>
#include <QPointer>
#include <QScreen>
#include <QSignalBlocker>
#include <QStyle>
#include <QTimer>

namespace Ms {
// Shared window behaviour for personal tool panels. Dock layout remains owned
// by QMainWindow; this class only manages a panel's temporary full-screen state
// and recovery of floating windows that were saved on a different screen.
class PanelWindowControls final : public QObject {
    QPointer<QDockWidget> _dock;
    QByteArray _geometry;
    QSize _dockedSize;
    bool _wasFloating = false, _fitting = false;
    QAction* _fullscreen;
    QAction* _escape;

    void fullscreen(bool enabled)
    {
        if (!_dock)
            return;
        _dock->setProperty("kumoWindowTransition", true);
        _dock->setProperty("kumoPanelFullScreen", enabled);
        if (enabled) {
            _wasFloating = _dock->isFloating();
            _geometry = _dock->saveGeometry();
            _dockedSize = _dock->size();
            _dock->setFloating(true);
            _dock->showFullScreen();
            _dock->raise();
            _dock->activateWindow();
        } else {
            _dock->showNormal();
            if (!_wasFloating) {
                _dock->setFloating(false);
                if (auto main = qobject_cast<QMainWindow*>(_dock->parentWidget())) {
                    const auto area = main->dockWidgetArea(_dock);
                    const bool side = area == Qt::LeftDockWidgetArea || area == Qt::RightDockWidgetArea;
                    main->resizeDocks({_dock.data()}, {side ? _dockedSize.width() : _dockedSize.height()},
                                      side ? Qt::Horizontal : Qt::Vertical);
                }
            } else if (!_geometry.isEmpty())
                _dock->restoreGeometry(_geometry);
            fitOnScreen();
        }
        _fullscreen->setText(enabled ? tr("退出面板全屏") : tr("面板全屏"));
        _fullscreen->setIcon(_dock->style()->standardIcon(
            enabled ? QStyle::SP_TitleBarNormalButton : QStyle::SP_TitleBarMaxButton));
        _escape->setEnabled(enabled);
        QTimer::singleShot(0, this, [this] {
            if (_dock)
                _dock->setProperty("kumoWindowTransition", false);
        });
    }
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (watched == _dock && (event->type() == QEvent::Show ||
                                event->type() == QEvent::ScreenChangeInternal))
            QTimer::singleShot(0, this, [this] { fitOnScreen(); });
        if (watched == _dock && event->type() == QEvent::Close) {
            if (_fullscreen->isChecked())
                _fullscreen->setChecked(false);
            _dock->setProperty("kumoWindowTransition", false);
        }
        if (watched == _dock && event->type() == QEvent::KeyPress && _fullscreen->isChecked()) {
            auto key = static_cast<QKeyEvent*>(event);
            if (key->key() == Qt::Key_Escape && key->modifiers() == Qt::NoModifier) {
                _fullscreen->setChecked(false);
                key->accept();
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }

public:
    explicit PanelWindowControls(QDockWidget* dock) : QObject(dock), _dock(dock)
    {
        setObjectName("kumoPanelWindowControls");
        _fullscreen = new QAction(tr("面板全屏"), this);
        _fullscreen->setObjectName("kumoPanelFullscreen");
        _fullscreen->setCheckable(true);
        _fullscreen->setShortcut(QKeySequence("Ctrl+Shift+F11"));
        _fullscreen->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        _fullscreen->setIcon(dock->style()->standardIcon(QStyle::SP_TitleBarMaxButton));
        _fullscreen->setToolTip(tr("此面板单独全屏；再次点击或按 Esc 返回原位置。Ctrl+Shift+F11"));
        dock->addAction(_fullscreen);
        connect(_fullscreen, &QAction::toggled, this, [this](bool enabled) { fullscreen(enabled); });
        _escape = new QAction(this);
        _escape->setShortcut(QKeySequence(Qt::Key_Escape));
        _escape->setShortcutContext(Qt::WindowShortcut);
        _escape->setEnabled(false);
        dock->addAction(_escape);
        connect(_escape, &QAction::triggered, this, [this] { _fullscreen->setChecked(false); });
        dock->installEventFilter(this);
        connect(dock, &QDockWidget::topLevelChanged, this, [this] {
            QTimer::singleShot(0, this, [this] { fitOnScreen(); });
        });
    }
    QAction* fullscreenAction() const { return _fullscreen; }
    void fitOnScreen()
    {
        if (!_dock || !_dock->isFloating() || !_dock->isVisible() ||
            _dock->isFullScreen() || _dock->isMaximized() || _fitting ||
            QApplication::mouseButtons() != Qt::NoButton)
            return;
        auto screen = QGuiApplication::screenAt(_dock->frameGeometry().center());
        if (!screen)
            screen = _dock->parentWidget() ? _dock->parentWidget()->screen() : QGuiApplication::primaryScreen();
        if (!screen)
            return;
        _fitting = true;
        const auto area = screen->availableGeometry().adjusted(6, 6, -6, -6);
        const auto frame = _dock->frameGeometry();
        const auto border = frame.size() - _dock->size();
        const auto wanted = frame.size().boundedTo(area.size()) - border;
        if (_dock->size() != wanted)
            _dock->resize(wanted);
        const auto size = _dock->frameGeometry().size();
        _dock->move(qBound(area.left(), frame.left(), qMax(area.left(), area.right() - size.width() + 1)),
                    qBound(area.top(), frame.top(), qMax(area.top(), area.bottom() - size.height() + 1)));
        _fitting = false;
    }
};
inline PanelWindowControls* installPanelWindowControls(QDockWidget* dock)
{
    auto existing = dock->findChild<QObject*>("kumoPanelWindowControls", Qt::FindDirectChildrenOnly);
    if (existing)
        return static_cast<PanelWindowControls*>(existing);
    auto controls = new PanelWindowControls(dock);
    dock->setContextMenuPolicy(Qt::ActionsContextMenu);
    return controls;
}
} // namespace Ms
