// GPL-2.0-or-later. Screen-only layers; no score property, undo or serialization dependency.
#ifndef MS_NOTEPREVIEW_H
#define MS_NOTEPREVIEW_H

#include <QColor>
#include <QHash>
#include <QFont>
#include <QFontMetricsF>
#include <QMap>
#include <QPointer>
#include <QRectF>
#include <QSet>
#include <QVector>
#include <algorithm>

namespace Ms {
class Element;

struct NotePreviewEntry {
      QColor color;
      QRectF bounds;
      QString label;
      QString chord;
      QRectF labelBox;
      QRectF chordBox;
      bool active = false;
      QPointF anchor;
      qreal spatium = 0;
      QString degree;
      QFont chordFont;
      QFont degreeFont;
      QRectF primaryBox;
      QRectF secondaryBox;
      QColor chordColor = QColor("#343a3f");
      QColor highlightColor = QColor("#0043ce");
      QColor highlightBackground = QColor("#d0e2ff");
      bool chordActive = false;
      int chordTick = -1;
      int chordUntil = 0;
      int annotationTrack = 0;
      QPointer<QObject> activationTarget;
      bool operator==(const NotePreviewEntry& other) const
            {
            return color == other.color && bounds == other.bounds && label == other.label && chord == other.chord
                  && labelBox == other.labelBox && chordBox == other.chordBox && active == other.active
                  && anchor == other.anchor && spatium == other.spatium && degree == other.degree
                  && chordFont == other.chordFont && degreeFont == other.degreeFont
                  && primaryBox == other.primaryBox && secondaryBox == other.secondaryBox
                  && chordColor == other.chordColor && highlightColor == other.highlightColor
                  && highlightBackground == other.highlightBackground && chordActive == other.chordActive
                  && chordTick == other.chordTick && chordUntil == other.chordUntil
                  && annotationTrack == other.annotationTrack && activationTarget == other.activationTarget;
            }
      };
using NotePreviewColors = QHash<const Element*, NotePreviewEntry>;

inline QFont notePreviewFont(qreal spatium, bool chord)
      {
      QFont font("Arial");
      font.setPixelSize(qMax(5, qRound(spatium * (chord ? 1.35 : 1.0))));
      font.setBold(chord);
      return font;
      }

// Rectangles are relative to the fixed annotation box, independent of its anchor note.
inline QSizeF layoutPreviewChord(NotePreviewEntry& entry, int order)
      {
      const qreal padding = entry.spatium * .25, gap = entry.spatium * .55;
      auto size = [](const QString& text, const QFont& font) {
            if (text.isEmpty()) return QSizeF();
            QFontMetricsF metrics(font);
            return QSizeF(metrics.horizontalAdvance(text), metrics.height());
            };
      const auto a = size(entry.chord, entry.chordFont), b = size(entry.degree, entry.degreeFont);
      if (a.isEmpty() || b.isEmpty()) {
            const auto single = a.isEmpty() ? b : a;
            (a.isEmpty() ? entry.secondaryBox : entry.primaryBox) = QRectF(QPointF(padding,padding),single);
            return single + QSizeF(2*padding,2*padding);
            }
      const bool vertical = order >= 2, reversed = order == 1 || order == 3;
      const QSizeF combined(vertical ? qMax(a.width(),b.width()) : a.width()+b.width()+gap,
                            vertical ? a.height()+b.height()+gap*.5 : qMax(a.height(),b.height()));
      if (vertical) {
            entry.primaryBox = QRectF(padding+(combined.width()-a.width())/2,
                  padding+(reversed ? b.height()+gap*.5 : 0),a.width(),a.height());
            entry.secondaryBox = QRectF(padding+(combined.width()-b.width())/2,
                  padding+(reversed ? 0 : a.height()+gap*.5),b.width(),b.height());
            }
      else {
            entry.primaryBox = QRectF(padding+(reversed ? b.width()+gap : 0),
                  padding+(combined.height()-a.height())/2,a.width(),a.height());
            entry.secondaryBox = QRectF(padding+(reversed ? 0 : a.width()+gap),
                  padding+(combined.height()-b.height())/2,b.width(),b.height());
            }
      return combined + QSizeF(2*padding,2*padding);
      }

class NotePreviewLayers {
      struct Layer {
            const void* owner;
            NotePreviewColors colors;
            QMap<int,QVector<const Element*>> chords;
            QSet<const Element*> activeChords;
            };
      QVector<Layer> _layers;
   public:
      bool contains(const void* owner) const
            { for (const auto& layer : _layers) if (layer.owner == owner) return true; return false; }
      bool empty() const { return _layers.isEmpty(); }
      QRectF replace(const void* owner, const NotePreviewColors& colors)
            {
            QRectF dirty;
            int index = -1;
            for (int i=0;i<_layers.size();++i) if (_layers[i].owner==owner) {index=i;break;}
            if (index>=0) {
                  if (_layers[index].colors==colors) return dirty;
                  for (const auto& entry : _layers[index].colors) dirty |= entry.bounds;
                  }
            for (const auto& entry : colors) dirty |= entry.bounds;
            if (colors.isEmpty()) {if (index>=0) _layers.removeAt(index);return dirty;}
            Layer layer {owner,colors,{}, {}};
            for (auto it=colors.cbegin();it!=colors.cend();++it)
                  if (!it->chordBox.isEmpty()) layer.chords[it->annotationTrack].append(it.key());
            for (auto& group : layer.chords)
                  std::sort(group.begin(),group.end(),[&](const Element* a,const Element* b) {
                        return colors.constFind(a)->chordTick < colors.constFind(b)->chordTick;
                        });
            if (index>=0) _layers[index]=layer; else _layers.append(layer);
            return dirty;
            }
      const NotePreviewEntry* entry(const Element* note) const
            {
            for (auto layer=_layers.crbegin();layer!=_layers.crend();++layer) {
                  auto it=layer->colors.constFind(note);
                  if (it!=layer->colors.constEnd()) return &it.value();
                  }
            return nullptr;
            }
      QColor color(const Element* note) const
            { const auto item=entry(note); return item ? item->color : QColor(); }
      QRectF setActiveChord(const void* owner, int tick)
            {
            QRectF dirty;
            for (auto& layer : _layers) {
                  if (layer.owner!=owner) continue;
                  QSet<const Element*> next;
                  if (tick>=0) for (const auto& group : layer.chords) {
                        auto it=std::upper_bound(group.cbegin(),group.cend(),tick,[&](int value,const Element* key) {
                              return value < layer.colors.constFind(key)->chordTick;
                              });
                        if (it==group.cbegin()) continue;
                        const auto key=*--it;
                        if (tick < layer.colors.constFind(key)->chordUntil) next.insert(key);
                        }
                  if (next==layer.activeChords) return dirty;
                  const auto changed=(next-layer.activeChords)+(layer.activeChords-next);
                  for (const auto key : changed) {
                        auto it=layer.colors.constFind(key);
                        if (it==layer.colors.constEnd()) continue;
                        dirty |= it->chordBox.translated(it->anchor);
                        }
                  layer.activeChords=next;
                  }
            return dirty;
            }
      template<typename Callback> void forEachChord(Callback callback) const
            {
            for (const auto& layer : _layers) for (const auto& group : layer.chords)
                  for (const auto key : group) callback(layer.colors.constFind(key).value(),layer.activeChords.contains(key));
            }
      bool activate(const QPointF& canvasPosition) const
            {
            for (auto layer=_layers.crbegin();layer!=_layers.crend();++layer)
                  for (const auto& group : layer->chords) for (const auto key : group) {
                        const auto& entry=layer->colors.constFind(key).value();
                        if (entry.activationTarget && entry.chordBox.translated(entry.anchor).contains(canvasPosition))
                              return QMetaObject::invokeMethod(entry.activationTarget,"activatePreview",Qt::DirectConnection,
                                    Q_ARG(int,entry.chordTick),Q_ARG(int,entry.annotationTrack));
                        }
            return false;
            }
      void remove(const Element* note)
            {
            // Destruction callbacks must never inspect the partially destroyed Element.
            for (auto& layer : _layers) {
                  auto it=layer.colors.find(note);
                  if (it==layer.colors.end()) continue;
                  layer.chords[it->annotationTrack].removeAll(note);
                  layer.activeChords.remove(note);
                  layer.colors.erase(it);
                  }
            }
      void clear() { _layers.clear(); }
      };
}
#endif
