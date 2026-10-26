// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "libmscore/symbol.h"
#include "libmscore/note.h"
#include <QVariant>

namespace Ms { namespace PluginAPI {
// Shared value conversion for written snapshots and actual-playback context.
inline QVariantList readHandMarkers(const ElementList& list,int tick,int track,int index,int graceIndex=-1)
{
      QVariantList result;QList<const Ms::Symbol*> symbols;
      for(const auto element:list)if(element->isSymbol())symbols.append(toSymbol(element));
      for(int i=0;i<symbols.size();++i) {
            const auto symbol=symbols[i];for(const auto child:symbol->leafs())if(child->isSymbol())symbols.append(toSymbol(child));
            if(!symbol->handEligible())continue;
            QVariantMap row{{"tick",tick},{"track",track},{"index",index},{"left",symbol->handLeft()},{"end",symbol->handEnd()},{"bracket",symbol->handBracket()},
                  {"hand",symbol->handLeft()?"left":"right"},{"marker",symbol->handEnd()?"end":"start"},{"attached",index>=0?"note":"chord"}};
            auto anchor=symbol->parent();while(anchor&&anchor->isSymbol())anchor=anchor->parent();
            if(index>=0&&anchor&&anchor->isNote())row.insert("pitch",toNote(anchor)->pitch());
            if(graceIndex>=0){row.insert("grace",true);row.insert("graceIndex",graceIndex);}
            result.append(row);
            }
      return result;
}
}}
