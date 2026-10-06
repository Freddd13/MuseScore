// GPL-2.0-or-later. Screen-only layers; no score property, undo or serialization dependency.
#ifndef MS_NOTEPREVIEW_H
#define MS_NOTEPREVIEW_H

#include <QColor>
#include <QHash>
#include <QFont>
#include <QFontMetricsF>
#include <QPicture>
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
      QPicture chordPicture {-1};
      QPicture activeChordPicture {-1};
      QSizeF renderedChordSize;
      QString renderedChordKey;
      QFont chordFont;
      QFont degreeFont;
      QRectF primaryBox;
      QRectF secondaryBox;
      QColor chordColor = QColor("#343a3f");
      QColor highlightColor = QColor("#0043ce");
      QColor highlightBackground = QColor("#d0e2ff");
      bool chordMask = true;
      const Element* sourceAnchor = nullptr; // opaque identity for destruction cleanup only
      int chordTick = -1;
      int chordUntil = 0;
      int annotationTrack = 0;
      QPointer<QObject> activationTarget;
      bool operator==(const NotePreviewEntry& other) const
            {
            return color == other.color && bounds == other.bounds && label == other.label && chord == other.chord
                  && labelBox == other.labelBox && chordBox == other.chordBox && active == other.active
                  && anchor == other.anchor && spatium == other.spatium && degree == other.degree
                  && renderedChordKey == other.renderedChordKey && renderedChordSize == other.renderedChordSize
                  && chordFont == other.chordFont && degreeFont == other.degreeFont
                  && primaryBox == other.primaryBox && secondaryBox == other.secondaryBox
                  && chordColor == other.chordColor && highlightColor == other.highlightColor
                  && highlightBackground == other.highlightBackground && chordMask == other.chordMask
                  && sourceAnchor == other.sourceAnchor
                  && chordTick == other.chordTick && chordUntil == other.chordUntil
                  && annotationTrack == other.annotationTrack && activationTarget == other.activationTarget;
            }
      };
using NotePreviewColors = QHash<const Element*, NotePreviewEntry>;
using NotePreviewMarkers = QVector<NotePreviewEntry>;

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
      const auto a = entry.renderedChordSize.isEmpty() ? size(entry.chord, entry.chordFont) : entry.renderedChordSize, b = size(entry.degree, entry.degreeFont);
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
            NotePreviewMarkers markers;
            QMap<int,QVector<int>> chords;
            QSet<int> activeChords;
            QMultiHash<const Element*,int> sources;
            };
      QVector<Layer> _layers;
   public:
      bool contains(const void* owner) const
            { for (const auto& layer : _layers) if (layer.owner == owner) return true; return false; }
      bool empty() const { return _layers.isEmpty(); }
      QRectF replace(const void* owner, const NotePreviewColors& colors, const NotePreviewMarkers& markers = {})
            {
            NotePreviewMarkers next=markers;
            // Accept legacy inline chord entries, while independent markers may share a source note.
            for (auto it=colors.cbegin();it!=colors.cend();++it) if (!it->chordBox.isEmpty()) {
                  auto entry=it.value();entry.sourceAnchor=it.key();next.append(entry);
                  }
            QRectF dirty;
            int index = -1;
            for (int i=0;i<_layers.size();++i) if (_layers[i].owner==owner) {index=i;break;}
            if (index>=0) {
                  if (_layers[index].colors==colors && _layers[index].markers==next) return dirty;
                  for (const auto& entry : _layers[index].colors) dirty |= entry.bounds;
                  for (const auto& entry : _layers[index].markers) dirty |= entry.bounds;
                  }
            for (const auto& entry : colors) dirty |= entry.bounds;
            for (const auto& entry : next) dirty |= entry.bounds;
            if (colors.isEmpty() && next.isEmpty()) {if (index>=0) _layers.removeAt(index);return dirty;}
            Layer layer {owner,colors,next,{}, {}, {}};
            for (int i=0;i<next.size();++i) if (!next[i].chordBox.isEmpty()) {
                  layer.chords[next[i].annotationTrack].append(i);layer.sources.insert(next[i].sourceAnchor,i);
                  }
            for (auto& group : layer.chords)
                  std::sort(group.begin(),group.end(),[&](int a,int b) {
                        return next[a].chordTick < next[b].chordTick;
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
                  QSet<int> next;
                  if (tick>=0) for (const auto& group : layer.chords) {
                        auto it=std::upper_bound(group.cbegin(),group.cend(),tick,[&](int value,int index) {
                              return value < layer.markers[index].chordTick;
                              });
                        if (it==group.cbegin()) continue;
                        const auto index=*--it;
                        if (tick < layer.markers[index].chordUntil) next.insert(index);
                        }
                  if (next==layer.activeChords) return dirty;
                  const auto changed=(next-layer.activeChords)+(layer.activeChords-next);
                  for (int index : changed) {
                        const auto& entry=layer.markers[index];
                        dirty |= entry.chordBox.translated(entry.anchor);
                        }
                  layer.activeChords=next;
                  }
            return dirty;
            }
      template<typename Callback> void forEachChord(Callback callback) const
            {
            for (const auto& layer : _layers) for (const auto& group : layer.chords)
                  for (int index : group) callback(layer.markers[index],layer.activeChords.contains(index));
            }
      bool activate(const QPointF& canvasPosition) const
            {
            for (auto layer=_layers.crbegin();layer!=_layers.crend();++layer)
                  for (const auto& group : layer->chords) for (int index : group) {
                        const auto& entry=layer->markers[index];
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
                  layer.colors.remove(note);
                  const auto indices=layer.sources.values(note);
                  for (int index : indices) {
                        auto& entry=layer.markers[index];
                        layer.chords[entry.annotationTrack].removeAll(index);layer.activeChords.remove(index);
                        entry.chordBox={};entry.bounds={};entry.sourceAnchor=nullptr;
                        }
                  layer.sources.remove(note);
                  }
            }
      void clear() { _layers.clear(); }
      };
}
#endif
