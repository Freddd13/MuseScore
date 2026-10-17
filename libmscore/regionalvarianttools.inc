// Value-based combinations and read-only comparison copies. GPL v2.
QVariantMap selectMany(Score* score,const QVariantMap& choices)
      {
      if(!score||!score->isMaster()||score->readOnly())return error(QObject::tr("请选择可编辑的总谱。"));
      QString problem;auto data=capturedState(score,&problem);if(!problem.isEmpty())return error(problem);
      auto groups=data.value("groups").toList();QVector<int> changes;QVector<QByteArray> fragments;QVector<QVariantMap> writings;
      for(auto i=choices.begin();i!=choices.end();++i){
            const int index=groupIndex(groups,i.key());if(index<0)return error(QObject::tr("组合中的方案组已经改变，请重新保存组合。"));
            auto g=groups[index].toMap();const auto variants=g.value("variants").toList();const int selected=variantIndex(variants,i.value().toString());
            if(selected<0)return error(QObject::tr("组合中的写法已经改变，请重新选择。"));
            if(g.value("active")==i.value())continue;
            const auto writing=variants[selected].toMap();const auto xml=unpacked(writing.value("data"),&problem);
            if(!problem.isEmpty()||!validateFragment(g,xml,&problem))return error(problem);
            g["active"]=i.value();groups[index]=g;changes.append(index);fragments.append(xml);writings.append(writing);
            }
      if(changes.isEmpty())return {{"ok",true},{"unchanged",true}};
      data["groups"]=groups;score->startCmd();score->undo(new ChangeState(score->masterScore(),data));bool ok=true;
      for(int i=0;i<changes.size()&&ok;++i)ok=replace(score,groups[changes[i]].toMap(),fragments[i],&problem,writings[i]);
      score->endCmd(!ok);return ok?QVariantMap{{"ok",true},{"count",changes.size()}}:error(problem);
      }
QVariantMap storeCombination(Score* score,const QString& name)
      {
      if(!score||!score->isMaster()||score->readOnly()||name.trimmed().isEmpty())return error(QObject::tr("请输入组合名称。"));
      auto data=state(score);const auto groups=data.value("groups").toList();if(groups.isEmpty())return error(QObject::tr("先保存一组写法，再保存组合。"));
      auto list=data.value("combinations").toList();int index=-1;for(int i=0;i<list.size();++i)if(list[i].toMap().value("name").toString()==name.trimmed().left(80))index=i;
      if(index<0&&list.size()>=64)return error(QObject::tr("已有64个组合，可用同名保存来更新其中一个。"));
      QVariantMap choices;for(auto v:groups){auto g=v.toMap();choices[g.value("id").toString()]=g.value("active");}
      QVariantMap item{{"id",index<0?id():list[index].toMap().value("id").toString()},{"name",name.trimmed().left(80)},{"choices",choices}};
      if(index<0)list.append(item);else list[index]=item;data["combinations"]=list;
      score->startCmd();score->undo(new ChangeState(score->masterScore(),data));score->endCmd();return {{"ok",true},{"id",item.value("id")}};
      }
QVariantMap selectCombination(Score* score,const QString& combination)
      {
      for(auto value:state(score).value("combinations").toList()){const auto item=value.toMap();if(item.value("id").toString()==combination)return selectMany(score,item.value("choices").toMap());}
      return error(QObject::tr("请先选择一个已保存组合。"));
      }
MasterScore* comparison(Score* score,const QString& group,const QString& variant,QString* problem)
      {
      if(!score||!score->isMaster()){*problem=QObject::tr("请回到总谱查看写法差异。");return nullptr;}
      auto data=capturedState(score,problem);if(!problem->isEmpty())return nullptr;
      QScopedValueRollback<bool> guard(saving,true);QByteArray bytes;QBuffer output(&bytes);output.open(QIODevice::WriteOnly);
      if(!score->saveCompressedFile(&output,"comparison.mscx",false,false)){*problem=MScore::lastError;return nullptr;}output.close();
      QBuffer input(&bytes);input.open(QIODevice::ReadOnly);std::unique_ptr<MasterScore> copy(new MasterScore(score->style()));
      if(copy->loadCompressedMsc(&input,true)!=Score::FileError::FILE_NO_ERROR){*problem=QObject::tr("读取另一写法时遇到问题，请重新打开这份乐谱。");return nullptr;}
      copy->QObject::setProperty(property,data);const auto result=select(copy.get(),group,variant);
      if(result.contains("error")){*problem=result.value("error").toString();return nullptr;}
      copy->doLayout();return copy.release();
      }
namespace {
QString markingValue(Element* element){return QString::fromLatin1(QCryptographicHash::hash(element->mimeData(QPointF()),QCryptographicHash::Sha256).toHex());}
QVariantMap notationValues(ChordRest* cr)
      {
      if(!cr)return {};QVariantMap result{{"duration",cr->actualTicks().ticks()},{"rest",cr->isRest()}};QVariantList notes;
      if(cr->isChord())for(auto note:toChord(cr)->notes()){
            QStringList marks;for(auto item:note->el())marks<<markingValue(item);
            notes<<QVariantMap{{"pitch",note->pitch()},{"tpc",note->tpc()},{"type",int(note->veloType())},{"velocity",note->veloOffset()},{"marks",marks},{"tie",note->tieFor()!=nullptr}};
            }
      result["notes"]=notes;QStringList marks;for(auto e:cr->el())marks<<markingValue(e);
      if(cr->isChord())for(auto a:toChord(cr)->articulations())marks<<markingValue(a);
      result["marks"]=marks;return result;
      }
}
QVariantList differences(Score* current,Score* other,const QString& group)
      {
      QVariantMap scope;for(auto value:state(current).value("groups").toList())if(value.toMap().value("id").toString()==group)scope=value.toMap();
      if(!current||!other||scope.isEmpty())return {};QVariantList result;const int from=scope.value("start").toInt(),until=scope.value("end").toInt();
      for(auto v:scope.value("tracks").toList()){
            const int track=v.toInt();QSet<int> ticks;
            for(auto score:{current,other})for(auto seg=score->tick2segment(Fraction::fromTicks(from),false,SegmentType::ChordRest);seg&&seg->tick().ticks()<until;seg=seg->next1(SegmentType::ChordRest))if(seg->element(track))ticks.insert(seg->tick().ticks());
            auto ordered=ticks.values();std::sort(ordered.begin(),ordered.end());for(int tick:ordered){
                  auto segment=[&](Score* score)->ChordRest*{auto s=score->tick2segment(Fraction::fromTicks(tick),false,SegmentType::ChordRest);auto e=s&&s->tick().ticks()==tick?s->element(track):nullptr;return e&&e->isChordRest()?toChordRest(e):nullptr;};
                  const auto a=notationValues(segment(current)),b=notationValues(segment(other));if(a==b)continue;
                  QStringList types;if(a.isEmpty())types<<QObject::tr("另一写法新增音");else if(b.isEmpty())types<<QObject::tr("另一写法删去音");else{
                        if(a.value("duration")!=b.value("duration")||a.value("rest")!=b.value("rest"))types<<QObject::tr("节奏");
                        auto x=a.value("notes").toList(),y=b.value("notes").toList();if(x.size()!=y.size())types<<QObject::tr("增减音");
                        bool pitches=false,velocity=false,marks=a.value("marks")!=b.value("marks");
                        for(int n=0;n<qMin(x.size(),y.size());++n){auto p=x[n].toMap(),q=y[n].toMap();pitches|=p.value("pitch")!=q.value("pitch")||p.value("tpc")!=q.value("tpc");velocity|=p.value("type")!=q.value("type")||p.value("velocity")!=q.value("velocity");marks|=p.value("marks")!=q.value("marks")||p.value("tie")!=q.value("tie");}
                        if(pitches)types<<QObject::tr("音高");if(velocity)types<<QObject::tr("力度");if(marks)types<<QObject::tr("标记");
                        }
                  auto measure=current->tick2measure(Fraction::fromTicks(tick));result<<QVariantMap{{"tick",tick},{"track",track},{"measure",measure?measure->no()+1:0},{"types",types},{"current",a},{"other",b}};
                  }
            }
      return result;
      }
