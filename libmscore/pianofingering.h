// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#pragma once
#include <QString>

namespace Ms { namespace PianoFingering {
// Piano fingering text remains unchanged in the score. A bit is one possible
// finger for this key: 2(3) permits either 2 or 3; [4] is an editorial 4.
inline int choices(QString text)
{
      text=text.simplified();text.remove(QLatin1Char(' '));
      if(text.size()==3 && ((text.front()=='['&&text.back()==']') || (text.front()=='('&&text.back()==')')))
            text=text.mid(1,1);
      if(text.isEmpty() || text.front()<'1' || text.front()>'5')return 0;
      int mask=1<<(text.front().unicode()-'1');
      for(int i=1;i<text.size();i+=3) {
            if(i+2>=text.size() || text[i]!='(' || text[i+1]<'1' || text[i+1]>'5' || text[i+2]!=')')return 0;
            mask|=1<<(text[i+1].unicode()-'1');
            }
      return mask;
}
}}
