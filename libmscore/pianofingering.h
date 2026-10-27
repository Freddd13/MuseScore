// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#pragma once
#include <QString>
#include <QVector>

namespace Ms { namespace PianoFingering {
inline QVector<int> substitution(QString text)
{
      if(text.size()<3)return {};
      text=text.simplified();text.remove(QLatin1Char(' '));
      for(const auto dash:{0x2010,0x2011,0x2013,0x2014,0x2212})text.replace(QChar(dash),QLatin1Char('-'));
      if(text.size()<3 || text.size()>9 || text.size()%2==0)return {};
      QVector<int> result;
      for(int i=0;i<text.size();++i) {
            if(i%2){if(text[i]!='-')return {};}
            else {if(text[i]<'1'||text[i]>'5')return {};result.append(text[i].unicode()-'0');}
            }
      return result;
}
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
