// SPDX-License-Identifier: GPL-2.0-or-later
#include "tapmark.h"
#include "../workflow/featuregate.h"
#include "taptempo.h"
#include "mscore/musescore.h"
#include "mscore/scoreview.h"
#include "mscore/seq.h"
#include "mscore/shortcut.h"
#include "mscore/performanceeditor/parameteredit.h"
#include "mscore/workflow/tapwrite.h"
#include "libmscore/score.h"
#include "libmscore/segment.h"
#include "libmscore/tempotext.h"
#include "libmscore/textline.h"
#include "libmscore/spanner.h"
#include "libmscore/tempoexpression.h"
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QRegularExpression>
#include <QTimer>

namespace Ms {
namespace {
struct TempoSource {
      QString text;
      QVector<int> positions;
      };
TempoSource tempoSource(const QString& xml)
      {
      TempoSource result;
      static const QString spaceSymbol=QStringLiteral("<sym>space</sym>");
      for(int i=0;i<xml.size();) {
            if(xml.midRef(i,spaceSymbol.size())==spaceSymbol) { result.text+=' ';result.positions.append(i);i+=spaceSymbol.size();continue; }
            if(xml[i]=='<') {
                  const int end=xml.indexOf('>',i);
                  if(end>=0) {
                        const auto tag=xml.midRef(i,end-i+1);
                        if(tag!="<sym>"&&tag!="</sym>"){i=end+1;continue;}
                        }
                  }
            result.text+=xml[i];result.positions.append(i++);
            }
      return result;
      }
double tapBeatUnit(const QString& original)
      {
      const double value=TempoText::findTempoValue(tempoSource(original).text);
      return value>0?value*60.:1.;
      }
}
QString tapTempoText(const QString& original, double quarterBpm, bool appendNumber)
      {
      QString result = original;
      const auto source=tempoSource(original);
      static const QRegularExpression number("[=≒≈~](?:\\s|&nbsp;|&#160;)*(?:(?:ca\\.|c\\.|approx\\.)\\s*)?([0-9]+(?:[.,][0-9]+)?)",QRegularExpression::CaseInsensitiveOption);
      const auto match = number.match(source.text);
      if (match.hasMatch()) {
            const auto value=QString::number(quarterBpm/tapBeatUnit(original),'f',2);
            // Keep the surrounding rich-text tags, including formatting between digits.
            for(int i=match.capturedEnd(1)-1;i>=match.capturedStart(1);--i)
                  result.replace(source.positions[i],1,i==match.capturedStart(1)?value:QString());
            }
      else if (appendNumber) result += QString(" <sym>metNoteQuarterUp</sym> = %1").arg(quarterBpm, 0, 'f', 2);
      return result;
      }

namespace {
class TapMarkDialog : public QDialog {
      QPointer<Score> _score;
      QPointer<ScoreView> _view;
      const Element* _identity;
      int _tick, _track, _until;
      bool _curve;
      QString _original;
      double _oldTempo = 0, _oldStart = 0, _oldTarget = 0, _oldShape = 0;
      int _oldMode = 0;
      bool _oldFollow = false, _oldRelative = false;
      TapEstimate _estimate;
      qint64 _last = -1;
      QElapsedTimer _clock;
      QComboBox* _unit;
      QComboBox* _endpoint = nullptr;
      QLabel *_status, *_value, *_meaning;
      QPushButton* _save;
      QPushButton* _separate = nullptr;
      double _bpm = 0;

      Element* resolve() const {
            if (!_score || !mscore || mscore->currentScore() != _score) return nullptr;
            if (_curve) {
                  const auto range = _score->spannerMap().map().equal_range(_tick);
                  for (auto it = range.first; it != range.second; ++it) {
                        if (!it->second->isTextLine()) continue;
                        auto line = toTextLine(it->second);
                        if (line == _identity && line->tick().ticks() == _tick && line->tick2().ticks() == _until
                            && line->beginText() == _original && line->ritStartBpm() == _oldStart
                            && line->ritTarget() == _oldTarget && line->ritTargetMode() == _oldMode
                            && line->ritCurve() == _oldShape && line->ritEnabled()) return line;
                        }
                  }
            else {
                  auto segment = _score->tick2segment(Fraction::fromTicks(_tick), false, SegmentType::ChordRest);
                  if (segment) for (auto e : segment->annotations()) if (e == _identity && e->isTempoText()) {
                        auto text = toTempoText(e);
                        if (text->track() == _track && text->xmlText() == _original && text->tempo() == _oldTempo
                            && text->restoreMode() == _oldMode && text->followText() == _oldFollow
                            && text->isRelative() == _oldRelative) return text;
                        }
                  }
            return nullptr;
            }
      void clearPreview() { if (_view) _view->setNotePreviewColors(this, {}); }
      bool supported() const { return _curve || (!_oldRelative && (_oldMode == 0 || _oldMode == 3)); }
      void updatePreview() {
            auto element = resolve();
            _bpm = _estimate.interval() > 0 ? 60000. * _unit->currentData().toDouble() / _estimate.interval() : 0;
            const bool inRange = _bpm >= 5 && _bpm <= 999;
            _value->setText(_bpm ? tr("四分音符：%1 BPM · 本次点击单位：%2 BPM")
                  .arg(_bpm, 0, 'f', 2).arg(_bpm / _unit->currentData().toDouble(), 0, 'f', 2)
                  : tr("敲击下方按钮开始测速；每次点击代表所选的一拍。"));
            const bool stopped = _score && !_score->readOnly() && !_score->isPlaying()
                  && (!seq || (!seq->isPlaying() && seq->backgroundRenderingIdle()));
            _save->setEnabled(element && supported() && _estimate.ready() && inRange && stopped);
            if (_separate) _separate->setEnabled(element && _estimate.ready() && inRange && stopped);
            if (!element) { clearPreview(); _status->setText(tr("目标已改变或已换谱，请关闭后重新选中速度标记。")); return; }
            _status->setText(!_estimate.ready() ? tr("已点击 %1 次；至少四次才可保存。当前数值仅供观察。").arg(_estimate.count())
                  : !inRange ? tr("测量值超出可写入的5–999 BPM，请更换点击单位或重新测量。")
                  : _score->readOnly() ? tr("这份乐谱不能修改；可继续测速，写入请先打开可编辑的乐谱。")
                  : !stopped ? tr("测量已更新；请停止播放并等待乐谱准备完成后保存。")
                  : tr("%1 · 已点击 %2 次；点“保存结果”一次写入，Esc取消。")
                        .arg(_estimate.stable() ? tr("点击稳定") : tr("点击间隔不均匀")).arg(_estimate.count()));
            if (!supported()) {
                  if (_estimate.ready() && inRange && stopped) _status->setText(tr("测量已更新；原记号保持原来的意思。可把结果写入另一个位置。"));
                  clearPreview(); return;
                  }
            if (!_view || _view->score() != _score || !inRange) { clearPreview(); return; }
            NotePreviewEntry entry;
            entry.onTop = true; entry.avoidScoreContent = false; entry.spatium = element->spatium();
            entry.anchor = element->canvasPos(); entry.sourceAnchor = element;
            if (_curve) {
                  const auto segments = static_cast<TextLine*>(element)->spannerSegments();
                  if (segments.empty()) { clearPreview(); return; }
                  auto segment = _endpoint->currentIndex() ? segments.back() : segments.front();
                  entry.anchor = segment->canvasPos(); entry.sourceAnchor = segment;
                  if (_endpoint->currentIndex()) entry.anchor += segment->pos2();
                  }
            entry.chordTick = _tick; entry.chordUntil = _curve ? _until : _tick + 1;
            entry.annotationTrack = _track; entry.chordColor = palette().color(QPalette::Text);
            if (!_curve && _oldMode == 0) {
                  // A scratch text draws the original music symbols and font. The real
                  // annotation, tempo map and undo stack remain untouched while tapping.
                  auto scratch = toTempoText(element)->clone();
                  scratch->setSelected(element->selected()); scratch->setParent(nullptr);
                  scratch->setPos(QPointF()); scratch->setOffset(QPointF());
                  scratch->setFollowText(false); scratch->setXmlText(tapTempoText(_original, _bpm)); scratch->layout();
                  entry.renderedChordKey = scratch->xmlText();
                  const auto bounds = scratch->bbox(); QPicture picture;
                  QPainter painter(&picture); painter.translate(-bounds.topLeft()); scratch->draw(&painter); painter.end();
                  const QRectF mask = bounds.united(element->bbox()).adjusted(-4,-4,4,4);
                  entry.chordPicture = picture; entry.primaryBox = QRectF(bounds.topLeft()-mask.topLeft(), bounds.size());
                  entry.chordBox = mask;
                  delete scratch;
                  }
            else {
                  entry.chord = tr("%1：%2 BPM（预览）").arg(_curve ? _endpoint->currentText() : tr("指定恢复速度")).arg(_bpm,0,'f',2);
                  entry.chordFont = font(); const QFontMetricsF metrics(entry.chordFont);
                  entry.primaryBox = QRectF(4,4,metrics.horizontalAdvance(entry.chord),metrics.height());
                  entry.chordBox = QRectF(0,-metrics.height()-10,entry.primaryBox.width()+8,metrics.height()+8);
                  }
            entry.bounds = entry.chordBox.translated(entry.anchor);
            _view->setNotePreviewColors(this, {}, {entry});
            }
      void save() {
            updatePreview(); if (!_save->isEnabled()) return;
            auto target = resolve(); if (!target) return;
            QString error; bool changed = false;
            if (_curve) {
                  auto line = static_cast<TextLine*>(target);
                  const auto values = _endpoint->currentIndex() == 0 ? QMap<Pid,QVariant>{{Pid::RIT_START_BPM,_bpm}}
                        : QMap<Pid,QVariant>{{Pid::RIT_TARGET_MODE,1},{Pid::RIT_TARGET,_bpm}};
                  if ((_endpoint->currentIndex() == 0 && qFuzzyCompare(_oldStart,_bpm))
                      || (_endpoint->currentIndex() == 1 && _oldMode == 1 && qFuzzyCompare(_oldTarget,_bpm))) { accept(); return; }
                  changed = ParameterEdit::editTempoCurve(line, values, &error);
                  }
            else if (_oldMode == 3) {
                  if (qFuzzyCompare(_oldTempo,_bpm/60.)) { accept(); return; }
                  _score->startCmd(); toTempoText(target)->undoSetTempo(_bpm/60.); _score->endCmd(); changed = true;
                  }
            else {
                  changed = ParameterEdit::writeTapTempo(toTempoText(target), _bpm, &error);
                  }
            if (!changed) { _status->setText(error.isEmpty() ? tr("未写入，请检查目标和乐谱状态。") : error); return; }
            mscore->endCmd(); accept();
            }
   public:
      TapMarkDialog(Element* target) : QDialog(mscore), _score(target->score()), _view(mscore->currentScoreView()),
            _identity(target), _tick(target->tick().ticks()), _track(target->track()), _until(_tick), _curve(target->isTextLine()) {
            setObjectName("tapMarkDialog"); setWindowTitle(tr("用 Tap 设置速度")); setAttribute(Qt::WA_DeleteOnClose);
            auto layout = new QFormLayout(this);
            auto tempo = _curve ? nullptr : toTempoText(target);
            if (_curve) {
                  auto line = static_cast<TextLine*>(target); _until = line->tick2().ticks();
                  _oldStart = line->ritStartBpm(); _oldTarget = line->ritTarget(); _oldMode = line->ritTargetMode(); _oldShape = line->ritCurve();
                  _original = line->beginText();
                  _endpoint = new QComboBox(this); _endpoint->setObjectName("tapMarkEndpoint");
                  _endpoint->addItems({tr("渐变起始速度"),tr("渐变结束速度")}); layout->addRow(tr("修改哪一端"),_endpoint);
                  connect(_endpoint,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{updatePreview();});
                  }
            else { _original = tempo->xmlText(); _oldTempo = tempo->tempo(); _oldMode = tempo->restoreMode(); _oldRelative = tempo->isRelative(); _oldFollow = tempo->followText(); }
            _meaning = new QLabel(this); _meaning->setWordWrap(true);
            _meaning->setText(_curve ? tr("只预览所选端点的速度。保存会把这一端设为明确BPM，保留文字和另一端。")
                  : !supported() ? tr("这个记号表示恢复速度或速度关系，直接改成数字会改变意思。可以保留它，并把测量结果写到另一个位置。")
                  : _oldMode == 3 ? tr("修改这个记号的指定恢复速度，保留其文字和恢复方式。")
                  : tr("预览所选速度标记；保存时保留原拍单位和说明文字。"));
            layout->addRow(_meaning);
            _unit = new QComboBox(this); _unit->setObjectName("tapMarkUnit");
            TapTempo::populateUnits(_unit);
            double beats = tempo ? tapBeatUnit(_original) : 1.;
            int index = _unit->findData(beats); _unit->setCurrentIndex(index >= 0 ? index : _unit->findData(1.));
            layout->addRow(tr("每次点击代表"),_unit);
            _value = new QLabel(this); _value->setObjectName("tapMarkValue"); _value->setMinimumWidth(440); layout->addRow(_value);
            _status = new QLabel(this); _status->setObjectName("tapMarkStatus"); _status->setWordWrap(true); layout->addRow(_status);
            auto tap = TapTempo::instance(mscore)->createTapControl(this); tap->setObjectName("tapMarkButton"); tap->setMinimumHeight(42); tap->setFocusPolicy(Qt::NoFocus); layout->addRow(tap);
            auto reset = new QPushButton(tr("重新测量"),this); layout->addRow(reset);
            auto buttons = new QDialogButtonBox(QDialogButtonBox::Save|QDialogButtonBox::Cancel,this);
            _save = buttons->button(QDialogButtonBox::Save); _save->setText(tr("保存结果")); _save->setObjectName("tapMarkSave"); _save->setDefault(true);
            buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消")); layout->addRow(buttons);
            connect(buttons,&QDialogButtonBox::accepted,this,[this]{save();}); connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
            connect(reset,&QPushButton::clicked,this,[this]{_estimate.reset();_last=-1;updatePreview();});
            connect(_unit,QOverload<int>::of(&QComboBox::currentIndexChanged),this,[this]{updatePreview();});
            connect(TapTempo::instance(mscore),&TapTempo::tapRecorded,this,[this](qint64 timestamp){if(_estimate.add(timestamp)){_last=_clock.elapsed();updatePreview();}});
            if (!supported()) {
                  _separate = new QPushButton(tr("保留原记号，写入另一个位置…"),this); _separate->setObjectName("tapMarkSeparate"); layout->addRow(_separate);
                  connect(_separate,&QPushButton::clicked,this,[this]{updatePreview();if(_separate->isEnabled())showTapWriteDialog(_score,_bpm);});
                  }
            _clock.start(); auto timer = new QTimer(this); timer->setInterval(100);
            connect(timer,&QTimer::timeout,this,[this]{updatePreview();if(resolve()&&_last>=0&&_clock.elapsed()-_last>qMax(5000.,_estimate.interval()*4.)){
                  if(!_estimate.ready())_status->setText(tr("已停止敲击；请重新测量至少四拍。"));
                  else if(_save->isEnabled())_status->setText(tr("已停止敲击；可保存这次结果，或重新测量。"));
                  else if(_separate&&_separate->isEnabled())_status->setText(tr("已停止敲击；可把结果写到另一个位置，或重新测量。"));}});timer->start();
            connect(this,&QDialog::finished,this,[this]{clearPreview();}); updatePreview();
            }
      ~TapMarkDialog() override { clearPreview(); }
      };
}
void showTapMarkDialog(Element* target)
      {
      if (!personalFeaturesEnabled() || !target || !target->score()) return;
      if (target->isTextLineSegment()) target = static_cast<TextLine*>(target->propertyDelegate(Pid::RIT_PLAY));
      if (!target || (!target->isTempoText() && (!target->isTextLine() || !static_cast<TextLine*>(target)->ritEnabled()))) return;
      if (auto existing = mscore->findChild<QDialog*>("tapMarkDialog",Qt::FindDirectChildrenOnly)) { existing->setObjectName("closingTapMarkDialog"); existing->close(); }
      auto dialog = new TapMarkDialog(target); dialog->show(); dialog->raise(); dialog->activateWindow();
      }
}
