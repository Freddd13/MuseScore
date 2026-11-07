// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "referencecontrols.h"
#include "featuregate.h"
#include "workflowcontext.h"
#include "toolicons.h"
#include "mscore/scoreview.h"
#include "mscore/musescore.h"
#include "mscore/seq.h"
#include "mscore/plugin/qmlpluginengine.h"
#include "mscore/plugin/api/scoreobserver.h"
#include "mscore/plugin/api/score.h"
#include "libmscore/measure.h"
#include "libmscore/segment.h"
#include "libmscore/system.h"
#include "libmscore/page.h"
#include <QApplication>
#include <QEvent>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QSet>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <functional>
namespace Ms
{
namespace
{
double tickX(Measure* measure, int tick)
{
    double left = 0, right = measure->width();
    int from = measure->tick().ticks(), until = measure->endTick().ticks();
    for (auto segment = measure->first(SegmentType::ChordRest); segment;
         segment = segment->next(SegmentType::ChordRest)) {
        if (!segment->enabled())
            continue;
        const int at = segment->tick().ticks();
        if (at == tick)
            return segment->canvasPos().x();
        if (at > tick) {
            right = segment->x();
            until = at;
            break;
        }
        left = segment->x();
        from = at;
    }
    return measure->canvasPos().x() + left +
           (right - left) * qBound(0., double(tick - from) / qMax(1, until - from), 1.);
}
QString identity(const QVariantMap& row)
{
    return row.value("trackId").toString() + "/" + row.value("id").toString();
}
class RangeLayer final : public QWidget
{
  public:
    QVector<QRectF> spans;
    explicit RangeLayer(QWidget* parent) : QWidget(parent)
    {
        setObjectName("referenceScoreRanges");
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }
    void paintEvent(QPaintEvent*) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        auto line = palette().highlight().color();
        line.setAlpha(175);
        auto fill = line;
        fill.setAlpha(10);
        for (const auto& box : spans) {
            painter.fillRect(box, fill);
            painter.setPen(QPen(line, 1.2));
            const double y = box.bottom();
            painter.drawLine(QPointF(box.left(), y), QPointF(box.right(), y));
            painter.drawLine(QPointF(box.left(), y - 5), QPointF(box.left(), y + 2));
            painter.drawLine(QPointF(box.right(), y - 5), QPointF(box.right(), y + 2));
        }
    }
};
class ReferenceScoreControls final : public QObject
{
    Q_OBJECT
    QPointer<QObject> _project;
    QPointer<ScoreView> _view;
    QPointer<Score> _owner, _dragScore;
    QPointer<RangeLayer> _layer;
    QHash<QString, QPointer<QToolButton>> _buttons;
    QMap<QString, QVariantList> _groups;
    QHash<QString, QString> _choices;
    QVariantMap _placements;
    QHash<QString, QVector<QRectF>> _ranges;
    QTimer _timer;
    QString _hovered;
    QPointer<QToolButton> _drag;
    QPoint _pressGlobal, _pressPosition;
    bool _moving = false, _playing = false;
    QVariantMap placement(const QVariantMap& row) const
    {
        if (!_view || !_view->score())
            return {};
        return _placements.value(identity(row)).toMap();
    }
    bool modify(const QVariantList& rows, const QVariantMap& fields)
    {
        if (!_view || !_view->score())
            return false;
        PluginAPI::Score wrapped(_view->score());
        PluginAPI::ScoreObserver observer;
        observer.setScore(&wrapped);
        auto data = observer.workflowData("referenceControls"),
             controls = data.value("controls").toMap();
        for (auto value : rows) {
            const auto key = identity(value.toMap());
            auto item = controls.value(key).toMap();
            if (fields.value("reset").toBool())
                item = {};
            else
                for (auto it = fields.cbegin(); it != fields.cend(); ++it)
                    item[it.key()] = it.value();
            if (item.isEmpty())
                controls.remove(key);
            else
                controls[key] = item;
        }
        data["schema"] = 1;
        data["controls"] = controls;
        const auto result = observer.setWorkflowData("referenceControls", data);
        if (result.contains("error")) {
            QToolTip::showText(QCursor::pos(), result.value("error").toString(), _view);
            return false;
        }
        schedule();
        return true;
    }
    QVariantMap current(const QString& group) const
    {
        const auto rows = _groups.value(group);
        const auto chosen = _choices.value(group);
        if (_project && _project->property("playing").toBool())
            for (auto row : rows)
                if (row.toMap().value("id").toString() ==
                    _project->property("activeAssociation").toString())
                    return row.toMap();
        for (auto row : rows)
            if (identity(row.toMap()) == chosen)
                return row.toMap();
        if (_project) {
            const auto tracks = _project->property("tracks").toList();
            const auto track = tracks.value(_project->property("selectedTrack").toInt()).toMap();
            const auto selection = track.value("associations")
                                       .toList()
                                       .value(track.value("selectedAssociation", -1).toInt())
                                       .toMap()
                                       .value("id");
            for (auto row : rows)
                if (row.toMap().value("trackId") == track.value("id") &&
                    row.toMap().value("id") == selection)
                    return row.toMap();
        }
        for (auto row : rows)
            if (row.toMap().value("active", true).toBool())
                return row.toMap();
        return rows.isEmpty() ? QVariantMap() : rows.front().toMap();
    }
    bool playingHere(const QString& group) const
    {
        if (!_project || !_project->property("playing").toBool())
            return false;
        for (auto row : _groups.value(group))
            if (row.toMap().value("id").toString() ==
                _project->property("activeAssociation").toString())
                return true;
        return false;
    }
    void play(const QString& group)
    {
        if (!_project)
            return;
        if (playingHere(group)) {
            if (_project->property("joint").toBool()) {
                PluginAPI::ScoreObserver observer;
                observer.stopReferencePlayback();
            } else
                QMetaObject::invokeMethod(_project, "stop");
            _project->setProperty("activeAssociation", QString());
            return;
        }
        const auto row = current(group);
        if (row.isEmpty())
            return;
        QMetaObject::invokeMethod(_project, "requestAssociation",
                                  Q_ARG(QString, row.value("trackId").toString()),
                                  Q_ARG(QString, row.value("id").toString()));
    }
    QVector<QRectF> range(const QVariantMap& row) const
    {
        QVector<QRectF> result;
        if (!_view || !_view->score())
            return result;
        auto score = _view->score();
        const int from = row.value("start").toInt(), until = row.value("end").toInt();
        const int first = qBound(0, row.value("firstTrack").toInt() / VOICES, score->nstaves() - 1),
                  last = qBound(first,
                                (row.value("endTrack", (first + 1) * VOICES).toInt() - 1) / VOICES,
                                score->nstaves() - 1);
        for (auto system : score->systems()) {
            if (!system || !system->page() || !system->firstMeasure() || !system->lastMeasure())
                continue;
            const int start = qMax(from, system->firstMeasure()->tick().ticks());
            const int end = qMin(until, system->lastMeasure()->endTick().ticks());
            if (end <= start)
                continue;
            double top = 1e20, bottom = -1e20;
            for (int staff = first; staff <= last; ++staff)
                if (system->staff(staff)->show()) {
                    const auto box = system->staff(staff)->bbox().translated(system->canvasPos());
                    top = qMin(top, box.top());
                    bottom = qMax(bottom, box.bottom());
                }
            if (top > bottom)
                continue;
            const auto band = _view->matrix().mapRect(
                QRectF(system->canvasPos().x(), top - score->spatium(), system->width(),
                       bottom - top + score->spatium() * 2));
            if (!_view->rect().intersects(band.toAlignedRect()))
                continue;
            auto firstMeasure = score->tick2measure(Fraction::fromTicks(start));
            auto lastMeasure = score->tick2measure(Fraction::fromTicks(qMax(start, end - 1)));
            if (!firstMeasure || !lastMeasure)
                continue;
            const double x = tickX(firstMeasure, start), right = tickX(lastMeasure, end);
            auto box =
                _view->matrix().mapRect(QRectF(QPointF(x, top - score->spatium() * .25),
                                               QPointF(right, bottom + score->spatium() * .8)));
            if (!_view->rect().intersects(box.toAlignedRect()))
                continue;
            result.append(box);
        }
        return result;
    }
    void showRanges()
    {
        if (!_layer)
            return;
        QVector<QRectF> shown;
        const auto mode = _project->property("scoreRangeDisplay").toString();
        for (auto it = _ranges.cbegin(); it != _ranges.cend(); ++it)
            if (mode == "always" || (mode == "hover" && it.key() == _hovered) ||
                (mode == "playing" && playingHere(it.key())))
                shown += it.value();
        if (shown == _layer->spans)
            return;
        _layer->spans = shown;
        _layer->update();
        _layer->setVisible(!shown.isEmpty());
    }
    void cancelDrag()
    {
        if (_drag) {
            _drag->setDown(false);
            if (QWidget::mouseGrabber() == _drag)
                _drag->releaseMouse();
        }
        _drag.clear();
        _dragScore.clear();
        _moving = false;
    }
    bool eventFilter(QObject* object, QEvent* event) override
    {
        auto button = qobject_cast<QToolButton*>(object);
        if (event->type() == QEvent::ApplicationDeactivate ||
            (event->type() == QEvent::Hide && button == _drag)) {
            cancelDrag();
            return false;
        }
        if (object == _view && (event->type() == QEvent::Show || event->type() == QEvent::Resize))
            schedule();
        if (button && button->property("referenceGroup").isValid()) {
            if (event->type() == QEvent::Enter) {
                _hovered = button->property("referenceGroup").toString();
                showRanges();
            }
            if (event->type() == QEvent::Leave && !_drag) {
                _hovered.clear();
                showRanges();
            }
            if (event->type() == QEvent::MouseButtonPress) {
                auto mouse = static_cast<QMouseEvent*>(event);
                const int arrow =
                    button->style()->pixelMetric(QStyle::PM_MenuButtonIndicator, nullptr, button) +
                    5;
                if (mouse->button() == Qt::LeftButton && mouse->x() < button->width() - arrow) {
                    _drag = button;
                    _dragScore = _view ? _view->score() : nullptr;
                    _moving = false;
                    _pressGlobal = mouse->globalPos();
                    _pressPosition = button->pos();
                }
            }
            if (button == _drag && event->type() == QEvent::MouseMove) {
                auto mouse = static_cast<QMouseEvent*>(event);
                if (!(mouse->buttons() & Qt::LeftButton)) {
                    cancelDrag();
                    schedule();
                    return false;
                }
                const auto delta = mouse->globalPos() - _pressGlobal;
                _moving |= delta.manhattanLength() >= QApplication::startDragDistance();
                if (_moving && _view) {
                    button->setDown(false);
                    const auto next = _pressPosition + delta;
                    button->move(qBound(0, next.x(), qMax(0, _view->width() - button->width())),
                                 qBound(0, next.y(), qMax(0, _view->height() - button->height())));
                    event->accept();
                    return true;
                }
            }
            if (button == _drag && event->type() == QEvent::MouseButtonRelease) {
                const bool moved = _moving;
                const auto rows = button->property("referenceClips").toList();
                if (moved && _view && _dragScore == _view->score()) {
                    const auto shift =
                        (_view->toLogical(button->pos()) -
                         _view->toLogical(
                             button->property("referenceAutomaticPosition").toPoint())) /
                        _dragScore->spatium();
                    cancelDrag();
                    modify(rows, {{"x", shift.x()}, {"y", shift.y()}});
                    event->accept();
                    return true;
                }
                _drag.clear();
                _dragScore.clear();
            }
            if (button == _drag &&
                (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress) &&
                static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
                cancelDrag();
                schedule();
                event->accept();
                return true;
            }
        }
        return false;
    }
    void bind()
    {
        auto next = mscore ? mscore->currentScoreView() : nullptr;
        if (_view == next && _owner == (next ? next->score() : nullptr))
            return;
        cancelDrag();
        if (_view) {
            _view->removeEventFilter(this);
            disconnect(_view, nullptr, this, nullptr);
        }
        for (auto button : _buttons)
            if (button)
                button->deleteLater();
        _buttons.clear();
        _ranges.clear();
        _hovered.clear();
        delete _layer.data();
        _layer.clear();
        _view = next;
        _owner = next ? next->score() : nullptr;
        _choices.clear();
        if (_view) {
            _view->installEventFilter(this);
            _layer = new RangeLayer(_view);
            _layer->hide();
            connect(_view, &ScoreView::viewRectChanged, this, &ReferenceScoreControls::schedule);
            connect(_view, &ScoreView::scaleChanged, this, [this](double) { schedule(); });
        }
    }
    void menu(QToolButton* button, const QString& group)
    {
        auto menu = button->menu();
        menu->clear();
        const auto selected = identity(current(group));
        for (auto value : _groups.value(group)) {
            const auto row = value.toMap();
            auto action = menu->addAction(row.value("trackName").toString() + " · " +
                                          row.value("name").toString());
            action->setCheckable(true);
            action->setChecked(identity(row) == selected);
            action->setData(identity(row));
            connect(action, &QAction::triggered, this, [this, group, row] {
                _choices[group] = identity(row);
                schedule();
            });
        }
        menu->addSeparator();
        auto reset = menu->addAction(tr("恢复自动位置"));
        reset->setObjectName("referenceResetPosition");
        connect(reset, &QAction::triggered, this, [this, group] {
            modify(_groups.value(group), {{"reset", true}});
        });
        auto hide = menu->addAction(tr("隐藏此处按钮"));
        hide->setObjectName("referenceHideControl");
        connect(hide, &QAction::triggered, this, [this, group] {
            modify(_groups.value(group), {{"hidden", true}});
        });
    }
  public slots:
    void schedule()
    {
        if (!_timer.isActive())
            _timer.start();
    }
    void playbackChanged()
    {
        const bool playing = _project && _project->property("playing").toBool();
        if (playing != _playing) {
            _playing = playing;
            schedule();
        }
    }
    void refresh()
    {
        bind();
        QSet<QString> shown, existing;
        _groups.clear();
        _ranges.clear();
        if (_project)
            for (auto trackValue : _project->property("tracks").toList())
                for (auto value : trackValue.toMap().value("associations").toList()) {
                    const auto row = value.toMap();
                    existing.insert(QString("%1:%2")
                                        .arg(row.value("start").toInt())
                                        .arg(row.value("firstTrack").toInt()));
                }
        _placements =
            _view && _view->score()
                ? QJsonDocument::fromJson(
                      _view->score()->masterScore()->metaTag("kumo.referenceControls").toUtf8())
                      .toVariant()
                      .toMap()
                      .value("controls")
                      .toMap()
                : QVariantMap();
        if (_layer)
            _layer->setGeometry(_view->rect());
        if (_view && _view->score() && _view->isVisible() && _project &&
            _project->property("scoreControls").toBool() && personalFeaturesEnabled() &&
            workflowContext()->referenceCurrent(_view->score()) && !_view->score()->printing() &&
            !_view->fotoMode()) {
            auto score = _view->score();
            for (auto trackValue : _project->property("tracks").toList()) {
                const auto track = trackValue.toMap();
                for (auto value : track.value("associations").toList()) {
                    auto row = value.toMap();
                    if (row.value("stale").toBool())
                        continue;
                    row["trackId"] = track.value("id");
                    row["trackName"] = track.value("name");
                    if (placement(row).value("hidden").toBool())
                        continue;
                    _groups[QString("%1:%2")
                                .arg(row.value("start").toInt())
                                .arg(row.value("firstTrack").toInt())]
                        .append(row);
                }
            }
            QVector<QRect> occupied;
            const auto markers = _view->notePreviewMarkerBoxes();
            for (auto it = _groups.cbegin(); it != _groups.cend(); ++it) {
                const auto row = current(it.key());
                const auto spans = range(row);
                if (spans.isEmpty())
                    continue;
                _ranges[it.key()] = spans;
                const auto where = spans.front().topLeft().toPoint();
                auto button = _buttons.value(it.key());
                if (!button) {
                    button = new QToolButton(_view);
                    button->setObjectName("referenceScoreClip/" + it.key());
                    button->setProperty("referenceGroup", it.key());
                    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
                    button->setPopupMode(QToolButton::MenuButtonPopup);
                    button->setAutoRaise(false);
                    button->installEventFilter(this);
                    button->setMenu(new QMenu(button));
                    _buttons[it.key()] = button;
                    connect(button, &QToolButton::clicked, this,
                            [this, key = it.key()] { play(key); });
                    connect(button->menu(), &QMenu::aboutToShow, this,
                            [this, button, key = it.key()] { menu(button, key); });
                }
                button->setProperty("referenceClips", *it);
                QVariantList ranges;
                for (auto span : spans)
                    ranges.append(span);
                button->setProperty("referenceActualRange", ranges);
                button->setProperty("referenceSelectedClip", identity(row));
                const bool playing = playingHere(it.key());
                button->setText(playing ? tr("停止") : tr("原曲"));
                setComponentToolIcon(button, playing ? "stop" : "play");
                button->setToolTip(row.value("trackName").toString() + " · " +
                                   row.value("name").toString() +
                                   tr("\n主按钮播放／停止，箭头选择片段；拖动按钮可移位。"));
                button->adjustSize();
                QRect box(where + QPoint(0, -button->height() - 9), button->size());
                for (int lane = 0; lane < 6; ++lane) {
                    bool overlap = false;
                    for (const auto& other : occupied)
                        overlap |= other.intersects(box);
                    for (const auto& other : markers)
                        overlap |= _view->matrix().mapRect(other).intersects(box);
                    if (!overlap)
                        break;
                    box.translate(0, -button->height() - 3);
                }
                occupied.append(box);
                const QPoint automatic(
                    qBound(0, box.x(), qMax(0, _view->width() - button->width())),
                    qBound(0, box.y(), qMax(0, _view->height() - button->height())));
                button->setProperty("referenceAutomaticPosition", automatic);
                const auto saved = placement(row);
                const auto offset = _view->toPhysical(QPointF(saved.value("x").toDouble(),
                                                              saved.value("y").toDouble()) *
                                                      score->spatium()) -
                                    _view->toPhysical(QPointF());
                if (button != _drag) {
                    const auto point = automatic + offset.toPoint();
                    button->move(qBound(0, point.x(), qMax(0, _view->width() - button->width())),
                                 qBound(0, point.y(), qMax(0, _view->height() - button->height())));
                }
                button->show();
                button->raise();
                shown.insert(it.key());
            }
        }
        for (auto it = _buttons.begin(); it != _buttons.end();) {
            if (!existing.contains(it.key())) {
                if (it.value())
                    it.value()->deleteLater();
                it = _buttons.erase(it);
            } else {
                if (it.value() && !shown.contains(it.key()))
                    it.value()->hide();
                ++it;
            }
        }
        showRanges();
        if (_layer) {
            _layer->raise();
            for (auto button : _buttons)
                if (button && button->isVisible())
                    button->raise();
        }
    }

  public:
    explicit ReferenceScoreControls(QObject* project) : QObject(qApp), _project(project)
    {
        _timer.setSingleShot(true);
        _timer.setInterval(40);
        connect(&_timer, &QTimer::timeout, this, &ReferenceScoreControls::refresh);
        connect(project, SIGNAL(tracksChanged()), this, SLOT(schedule()));
        connect(project, SIGNAL(scoreControlsChanged()), this, SLOT(schedule()));
        connect(project, SIGNAL(activeAssociationChanged()), this, SLOT(schedule()));
        connect(project, SIGNAL(changed()), this, SLOT(playbackChanged()));
        connect(workflowContext(), &WorkflowContext::activeScoreChanged, this,
                &ReferenceScoreControls::schedule);
        connect(workflowContext(), &WorkflowContext::changed, this,
                &ReferenceScoreControls::schedule);
        if (mscore)
            connect(mscore->getPluginEngine(), &QmlPluginEngine::endCmd, this,
                    &ReferenceScoreControls::schedule);
        connect(qApp, &QGuiApplication::applicationStateChanged, this,
                [this](Qt::ApplicationState state) {
                    if (state != Qt::ApplicationActive) {
                        cancelDrag();
                        schedule();
                    }
                });
        schedule();
    }
};
} // namespace
void installReferenceScoreControls(QObject* project) { new ReferenceScoreControls(project); }
} // namespace Ms
#include "referencecontrols.moc"
