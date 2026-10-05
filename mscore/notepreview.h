//=============================================================================
//  MuseScore
//  Music Composition & Notation
//
//  Copyright (C) 2026 Freddd13 and contributors
//
//  This program is free software; you can redistribute it and/or modify
//  it under the terms of the GNU General Public License version 2
//  as published by the Free Software Foundation and appearing in
//  the file LICENCE.GPL
//=============================================================================

// Screen-only note preview layers. No score property, undo or serialization dependency.
#ifndef MS_NOTEPREVIEW_H
#define MS_NOTEPREVIEW_H

#include <QColor>
#include <QHash>
#include <QRectF>
#include <QVector>

namespace Ms {
class Note;

struct NotePreviewEntry {
      QColor color;
      QRectF bounds;
      bool operator==(const NotePreviewEntry& other) const
            { return color == other.color && bounds == other.bounds; }
      };
using NotePreviewColors = QHash<const Note*, NotePreviewEntry>;

class NotePreviewLayers {
      struct Layer { const void* owner; NotePreviewColors colors; };
      QVector<Layer> _layers;
   public:
      bool contains(const void* owner) const
            { for (const auto& layer : _layers) if (layer.owner == owner) return true; return false; }
      bool empty() const { return _layers.isEmpty(); }
      QRectF replace(const void* owner, const NotePreviewColors& colors)
            {
            QRectF dirty;
            int index = -1;
            for (int i = 0; i < _layers.size(); ++i)
                  if (_layers[i].owner == owner) { index = i; break; }
            if (index >= 0) {
                  if (_layers[index].colors == colors) return dirty;
                  for (const auto& entry : _layers[index].colors) dirty |= entry.bounds;
                  }
            for (const auto& entry : colors) dirty |= entry.bounds;
            if (colors.isEmpty()) {
                  if (index >= 0) _layers.removeAt(index);
                  }
            else if (index >= 0) _layers[index].colors = colors;
            else _layers.append({owner, colors});
            return dirty;
            }
      QColor color(const Note* note) const
            {
            for (auto layer = _layers.crbegin(); layer != _layers.crend(); ++layer) {
                  auto entry = layer->colors.constFind(note);
                  if (entry != layer->colors.constEnd()) return entry->color;
                  }
            return QColor();
            }
      void remove(const Note* note)
            { for (auto& layer : _layers) layer.colors.remove(note); }
      void clear() { _layers.clear(); }
      };
}
#endif
