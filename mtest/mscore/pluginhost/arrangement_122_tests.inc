// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include "libmscore/pedal.h"
#include "libmscore/pedalplayback.h"
#include "libmscore/playbacktiming.h"
#include "libmscore/noteevent.h"
#include <climits>
void TestPluginHost::arrangementPlaybackRows122()
{
    const auto path=_settings.filePath("arrangement-playback-122.mscx");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(arrangementScore115({QVariantList{72,0,1920,0,14},QVariantList{36,0,480,4,14},QVariantList{37,0,480,4,21}}));file.close();
    auto main=Ms::mscore;auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
    auto chord=toChord(score->firstSegment(SegmentType::ChordRest)->element(4));chord->setPlayEventType(PlayEventType::User);
    NoteEventList shortEvent;shortEvent.clear();shortEvent.append(NoteEvent(0,0,50));
    NoteEventList lateEvent;lateEvent.clear();lateEvent.append(NoteEvent(0,500,500));
    chord->notes()[0]->setPlayEvents(shortEvent);chord->notes()[1]->setPlayEvents(lateEvent);
    PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
    const auto state=score->state();const int undo=score->undoStack()->getCurIdx();
    auto read=[&]{
        const auto revision=observer.analysisPlaybackRevision();QVariantMap preparation;int batches=0;
        do {preparation=observer.analysisPlaybackPrepare(0,1920,revision);if(preparation.contains("error"))return QVariantList{preparation};++batches;}while(!preparation.value("ready").toBool()&&batches<1000);
        if(!preparation.value("ready").toBool())return QVariantList{QVariantMap{{"error","prepare-limit"}}};
        QVariantList result;int from=INT_MIN;
        for(int page=0;page<1000;++page){const auto value=observer.analysisPlaybackPage(from,0,8,1,revision);if(value.contains("error"))return QVariantList{value};result.append(value.value("events").toList());if(value.value("done").toBool())return result;from=value.value("nextTick").toInt();}
        return QVariantList{QVariantMap{{"error","page-limit"}}};
    };
    auto key=[](int at,const NPlayEvent& event){return QString("%1:%2:%3:%4:%5:%6").arg(at).arg(event.channel()).arg(event.type()==ME_CONTROLLER?"pedal":event.velo()?"on":"off").arg(event.type()==ME_CONTROLLER?0:event.pitch()).arg(event.type()==ME_CONTROLLER?event.value():event.velo()).arg(event.type()==ME_CONTROLLER?0:event.discard());};
    auto expected=[&]{EventMap midi;score->renderMidi(&midi,false,score->masterScore()->expandRepeats(),main->synthesizerState());QStringList result;for(const auto& row:midi){const auto& event=row.second;if(event.type()==ME_NOTEON&&event.note() || event.type()==ME_CONTROLLER&&event.controller()==CTRL_SUSTAIN)result.append(key(row.first,event));}result.sort();return result;};
    auto actual=[&](const QVariantList& rows){QStringList result;for(const auto& value:rows){const auto row=value.toMap();if(row.contains("error")){result.append(row.value("error").toString());continue;}const bool pedal=row.value("kind")=="pedal";result.append(QString("%1:%2:%3:%4:%5:%6").arg(row.value("utick").toInt()).arg(row.value("channel").toInt()).arg(row.value("kind").toString()).arg(pedal?0:row.value("pitch").toInt()).arg(pedal?row.value("value").toInt():row.value("velocity").toInt()).arg(pedal?0:row.value("discard").toInt()));}result.sort();return result;};
    auto rows=read();QVERIFY(!rows.isEmpty());QCOMPARE(actual(rows),expected());
    bool shortOff=false,lateOn=false;
    for(const auto& value:rows){auto row=value.toMap();if(row.value("pitch").toInt()==36&&row.value("kind")=="off"){QCOMPARE(row.value("utick").toInt(),23);shortOff=true;}if(row.value("pitch").toInt()==37&&row.value("kind")=="on"){QCOMPARE(row.value("utick").toInt(),240);lateOn=true;}QCOMPARE(row.value("seconds").toDouble(),PlaybackTiming::time(score,row.value("utick").toInt()));}
    QVERIFY(shortOff&&lateOn);QVERIFY(score->state()==state);QCOMPARE(score->undoStack()->getCurIdx(),undo);
    const auto old=observer.analysisPlaybackRevision();auto pedal=new Pedal(score);pedal->setTrack(4);pedal->setTrack2(4);pedal->setTick(Fraction(0,1));pedal->setTick2(Fraction::fromTicks(960));score->startCmd();score->undoAddElement(pedal);score->endCmd();main->endCmd();
    QVERIFY(observer.analysisPlaybackPrepare(0,1920,old).contains("error"));rows=read();QCOMPARE(actual(rows),expected());int originalDown=-1;
    for(const auto& value:rows){auto row=value.toMap();if(row.value("kind")=="pedal"&&row.value("value").toInt()>0)originalDown=row.value("utick").toInt();}QVERIFY(originalDown>=0);
    PedalPlayback::Settings settings;settings.piano=true;settings.downMs=80;settings.releaseMs=40;
    score->startCmd();score->parts()[0]->undoChangeProperty(Pid::PEDAL_PLAYBACK,PedalPlayback::encode(settings));score->endCmd();main->endCmd();rows=read();QCOMPARE(actual(rows),expected());int pianoDown=-1;
    for(const auto& value:rows){auto row=value.toMap();if(row.value("kind")=="pedal"&&row.value("value").toInt()>0)pianoDown=row.value("utick").toInt();}QVERIFY(pianoDown>originalDown);
    main->cmd(getAction("undo"));rows=read();QCOMPARE(actual(rows),expected());QVERIFY(main->saveFile());
    auto reopened=main->readScore(path);QVERIFY(reopened);auto restored=toChord(reopened->firstSegment(SegmentType::ChordRest)->element(4));QCOMPARE(restored->notes()[0]->playEvents()[0].len(),50);QCOMPARE(restored->notes()[1]->playEvents()[0].ontime(),500);delete reopened;
    const auto beforeMute=observer.analysisPlaybackRevision();
    auto channel=const_cast<Channel*>(score->masterScore()->midiMapping().front().articulation());QVERIFY(channel);channel->setMute(true);
    QVERIFY(observer.analysisPlaybackRevision()!=beforeMute);QVERIFY(observer.analysisPlaybackPrepare(0,1920,beforeMute).contains("error"));
    rows=read();int muted=0;for(const auto& value:rows){const auto row=value.toMap();if(row.value("kind")=="on"){QVERIFY(row.value("muted").toBool());++muted;}}QVERIFY(muted>0);
    channel->setMute(false);score->staff(1)->setPlaybackVoice(0,false);rows=read();
    for(const auto& value:rows){const auto row=value.toMap();if(row.value("kind")=="on")QCOMPARE(row.value("muted").toBool(),row.value("track").toInt()==4);}
    score->staff(1)->setPlaybackVoice(0,true);rows=read();for(const auto& value:rows)if(value.toMap().value("kind")=="on")QVERIFY(!value.toMap().value("muted").toBool());
}

static QVariantMap playbackRead122(PluginAPI::ScoreObserver& observer,int end,int firstTrack=0,int endTrack=8)
{
    const auto version=observer.analysisPlaybackRevision();QVariantMap prepare;double worst=0;int batches=0;
    do {prepare=observer.analysisPlaybackPrepare(0,end,version);if(prepare.contains("error"))return prepare;worst=qMax(worst,prepare.value("milliseconds").toDouble());if(++batches>10000)return {{"error","prepare-limit"}};}while(!prepare.value("ready").toBool());
    QVariantList events;int from=INT_MIN;
    for(int pages=0;pages<10000;++pages){auto page=observer.analysisPlaybackPage(from,firstTrack,endTrack,16,version);if(page.contains("error"))return page;events.append(page.value("events").toList());worst=qMax(worst,page.value("milliseconds").toDouble());if(page.value("done").toBool()){prepare["events"]=events;prepare["worstMilliseconds"]=worst;prepare["batches"]=batches;return prepare;}from=page.value("nextTick").toInt();}
    return {{"error","page-limit"}};
}
void TestPluginHost::arrangementPlaybackPasses122()
{
    auto xml=arrangementScore115({QVariantList{72,0,1920,0,14},QVariantList{36,0,480,4,14},QVariantList{37,0,480,4,21}});
    const int at=xml.indexOf("<Chord>");QVERIFY(at>=0);
    xml.insert(at,"<Chord><acciaccatura/><durationType>eighth</durationType><Note><pitch>72</pitch><tpc>14</tpc></Note></Chord>");
    const auto path=_settings.filePath("playback-repeat-122.mscx");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(xml);file.close();
    auto main=Ms::mscore;auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
    score->firstMeasure()->setRepeatStart(true);score->firstMeasure()->setRepeatEnd(true);score->firstMeasure()->setRepeatCount(2);score->setPlaylistDirty();score->setExpandRepeats(true);
    PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
    auto data=playbackRead122(observer,1920);QVERIFY2(!data.contains("error"),qPrintable(data.value("error").toString()));
    const auto rows=data.value("events").toList();QSet<int> passes,gracePasses;QSet<QString> ids;
    for(const auto& value:rows){auto row=value.toMap();if(row.value("kind")=="on"){passes.insert(row.value("occurrence").toInt());if(row.value("grace").toBool()){gracePasses.insert(row.value("occurrence").toInt());QCOMPARE(row.value("id").toString(),QString("0:0:g0:0"));QCOMPARE(row.value("offset").toInt(),row.value("occurrence").toInt()==1?0:1920);}}}
    QCOMPARE(passes,QSet<int>({1,2}));QCOMPARE(gracePasses,QSet<int>({1,2}));QCOMPARE(data.value("ranges").toList().size(),2);
    score->setExpandRepeats(false);data=playbackRead122(observer,1920);QCOMPARE(data.value("ranges").toList().size(),1);
    for(const auto& value:data.value("events").toList())QVERIFY(value.toMap().value("utick").toInt()<1920);
}
void TestPluginHost::arrangementPlaybackLarge122()
{
    const auto base=arrangementScore115({QVariantList{72,0,1920,0,14},QVariantList{36,0,480,4,14},QVariantList{37,0,480,4,21}});
    QByteArray xml=base;
    for(int staff:{1,2}){
        const int start=xml.indexOf("<Measure>",xml.indexOf(QByteArray("<Staff id=\"")+QByteArray::number(staff)+"\"><Measure>"));
        QVERIFY(start>=0);const int end=xml.indexOf("</Measure>",start)+10;const auto measure=xml.mid(start,end-start);QByteArray extra;
        for(int bar=1;bar<256;++bar)extra+=measure;
        xml.insert(end,extra);
    }
    const auto path=_settings.filePath("playback-large-122.mscx");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(xml);file.close();
    auto main=Ms::mscore;auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
    PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);const auto state=score->state();
    QElapsedTimer timer;timer.start();auto data=playbackRead122(observer,256*1920);QVERIFY2(!data.contains("error"),qPrintable(data.value("error").toString()));
    const auto events=data.value("events").toList();QCOMPARE(events.size(),256*6);QVERIFY(data.value("batches").toInt()>1);QVERIFY(score->state()==state);
    qInfo()<<"Playback122 256 bars / events / total ms / worst batch ms / initial preparation ms"<<events.size()<<timer.elapsed()<<data.value("worstMilliseconds")<<data.value("preparationMilliseconds");
    // Compare the complete renderer with the paged reader across musical chunks,
    // an early attack, adjacent pedals and a repeated first measure.
    auto next=toChord(score->firstMeasure()->nextMeasure()->first(SegmentType::ChordRest)->element(4));QVERIFY(next);
    NoteEventList early;early.clear();early.append(NoteEvent(0,-250,1250));next->setPlayEventType(PlayEventType::User);next->notes()[1]->setPlayEvents(early);
    score->firstMeasure()->setRepeatStart(true);score->firstMeasure()->setRepeatEnd(true);score->firstMeasure()->setRepeatCount(2);score->setExpandRepeats(true);
    score->startCmd();for(int at:{0,1920}){auto pedal=new Pedal(score);pedal->setTrack(4);pedal->setTrack2(4);pedal->setTick(Fraction::fromTicks(at));pedal->setTick2(Fraction::fromTicks(at+1920));score->undoAddElement(pedal);}
    PedalPlayback::Settings settings;settings.piano=true;settings.downMs=80;settings.releaseMs=40;score->parts()[0]->undoChangeProperty(Pid::PEDAL_PLAYBACK,PedalPlayback::encode(settings));score->endCmd();main->endCmd();
    data=playbackRead122(observer,256*1920);QVERIFY2(!data.contains("error"),qPrintable(data.value("error").toString()));
    QStringList actual,expected;for(const auto& value:data.value("events").toList()){const auto row=value.toMap();const bool pedal=row.value("kind")=="pedal";actual.append(QString("%1:%2:%3:%4:%5:%6").arg(row.value("utick").toInt()).arg(row.value("channel").toInt()).arg(row.value("kind").toString()).arg(pedal?0:row.value("pitch").toInt()).arg(pedal?row.value("value").toInt():row.value("velocity").toInt()).arg(pedal?0:row.value("discard").toInt()));}
    EventMap midi;score->renderMidi(&midi,false,true,main->synthesizerState());for(const auto& entry:midi){const auto& event=entry.second;const bool pedal=event.type()==ME_CONTROLLER&&event.controller()==CTRL_SUSTAIN;if(pedal||event.type()==ME_NOTEON&&event.note())expected.append(QString("%1:%2:%3:%4:%5:%6").arg(entry.first).arg(event.channel()).arg(pedal?"pedal":event.velo()?"on":"off").arg(pedal?0:event.pitch()).arg(pedal?event.value():event.velo()).arg(pedal?0:event.discard()));}
    actual.sort();expected.sort();QCOMPARE(actual,expected);
}
void TestPluginHost::arrangementActualPanel122()
{
    const auto path=_settings.filePath("arrangement-actual-panel-122.mscx");QFile file(path);QVERIFY(file.open(QIODevice::WriteOnly));file.write(arrangementScore115({QVariantList{72,0,1920,0,14},QVariantList{36,0,480,4,14},QVariantList{37,0,480,4,21}}));file.close();
    auto main=Ms::mscore;auto score=main->readScore(path);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->resize(1280,920);main->show();
    auto chord=toChord(score->firstSegment(SegmentType::ChordRest)->element(4));score->select(chord->upNote());
    main->pluginTriggered(QString(TESTROOT)+"/share/plugins/ArrangementAssistant/ArrangementAssistant_MS3.qml");QPointer<QmlPlugin> panel=arrangementPanel116();QVERIFY(panel);panel->setPanelFloating(true);panel->resizePanel(430,740);QTest::qWait(100);
    struct Close {QPointer<QmlPlugin> panel;~Close(){if(panel)panel->disposePanelForReload();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);}} close{panel};
    auto values=[&](const char* name){return panel->property(name).value<QJSValue>().toVariant();};
    auto low=[&]{QVariantList rows;for(const auto& value:values("issues").toList())if(value.toMap().value("code")=="low-cluster")rows.append(value);return rows;};
    auto update=[&]{auto item=panel->findChild<QQuickItem*>("arrangementUpdate");QVERIFY(item&&item->isVisible());QTest::mouseClick(panel->window(),Qt::LeftButton,Qt::NoModifier,item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint());};
    update();QTRY_VERIFY_WITH_TIMEOUT(panel->property("analyzed").toBool()&&!panel->property("running").toBool(),15000);QCOMPARE(low().size(),1);
    auto detail=low()[0].toMap().value("lowDetails").toList()[0].toMap();QVERIFY(qAbs(detail.value("seconds").toDouble()-479./960)<1e-9);QVERIFY(low()[0].toMap().value("basis").toString().contains(QString::fromUtf8("实际起音")));
    const auto undo=score->undoStack()->getCurIdx();NoteEventList shortEvent;shortEvent.clear();shortEvent.append(NoteEvent(0,0,50));NoteEventList lateEvent;lateEvent.clear();lateEvent.append(NoteEvent(0,500,500));
    score->startCmd();score->undo(new ChangeNoteEventList(chord->notes()[0],shortEvent));score->undo(new ChangeNoteEventList(chord->notes()[1],lateEvent));score->endCmd();main->endCmd();
    update();QTRY_VERIFY_WITH_TIMEOUT(!panel->property("running").toBool(),15000);QCOMPARE(low().size(),0);
    auto pedal=new Pedal(score);pedal->setTrack(4);pedal->setTrack2(4);pedal->setTick(Fraction());pedal->setTick2(Fraction::fromTicks(960));score->startCmd();score->undoAddElement(pedal);score->endCmd();main->endCmd();
    update();QTRY_VERIFY_WITH_TIMEOUT(!panel->property("running").toBool(),15000);QCOMPARE(low().size(),1);detail=low()[0].toMap().value("lowDetails").toList()[0].toMap();QCOMPARE(detail.value("source").toString(),QString("pedal"));
    PedalPlayback::Settings settings;settings.piano=true;settings.downMs=80;settings.releaseMs=40;
    score->startCmd();score->parts()[0]->undoChangeProperty(Pid::PEDAL_PLAYBACK,PedalPlayback::encode(settings));score->endCmd();main->endCmd();
    update();QTRY_VERIFY_WITH_TIMEOUT(!panel->property("running").toBool(),15000);QCOMPARE(low().size(),0);
    main->cmd(getAction("undo"));update();QTRY_VERIFY_WITH_TIMEOUT(!panel->property("running").toBool(),15000);QCOMPARE(low().size(),1);
    const auto cached=values("cachedData").toMap();QVERIFY(cached.value("playback").toMap().value("events").toList().size()>0);
    const auto out=QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QFile dataFile(out.filePath("actual-playback-122.json"));QVERIFY(dataFile.open(QIODevice::WriteOnly));dataFile.write(QJsonDocument::fromVariant(cached).toJson());dataFile.close();
    QVERIFY(panel->window()->grabWindow().save(out.filePath("arrangement-actual-panel-122.png")));
    main->cmd(getAction("undo"));update();QTRY_VERIFY_WITH_TIMEOUT(!panel->property("running").toBool(),15000);QCOMPARE(low().size(),0);
    main->cmd(getAction("undo"));update();QTRY_VERIFY_WITH_TIMEOUT(!panel->property("running").toBool(),15000);QCOMPARE(low().size(),1);QCOMPARE(score->undoStack()->getCurIdx(),undo);
    main->cmd(getAction("redo"));main->cmd(getAction("redo"));QVERIFY(main->saveFile());auto reopened=main->readScore(path);QVERIFY(reopened);main->setCurrentScoreView(main->appendScore(reopened));QTest::qWait(120);update();QTRY_VERIFY_WITH_TIMEOUT(!panel->property("running").toBool(),15000);QCOMPARE(low().size(),1);
}

void TestPluginHost::midiEventPostprocessing122()
{
    EventMap events;
    auto add=[&](int tick,int channel,int pitch,int velocity,int staff){NPlayEvent event(ME_NOTEON,channel,pitch,velocity);event.setOriginatingStaff(staff);events.insert({tick,event});};
    add(0,0,60,80,0);add(10,0,60,90,1);add(20,0,60,0,0);add(30,0,60,0,0);add(40,0,62,0,0);add(50,18,64,70,2);add(60,18,64,0,2);
    events.registerChannel(18);events.fixupMIDI();
    QCOMPARE(events.find(0)->second.discard(),0);QCOMPARE(events.find(10)->second.discard(),1);QCOMPARE(events.find(20)->second.discard(),1);QCOMPARE(events.find(30)->second.discard(),0);QCOMPARE(events.find(30)->second.getOriginatingStaff(),1);QCOMPARE(events.find(40)->second.discard(),1);QCOMPARE(events.find(60)->second.getOriginatingStaff(),2);
    MidiControllerFilter filter;
    QVERIFY(!filter.redundant(NPlayEvent(ME_CONTROLLER,0,CTRL_SUSTAIN,127)));
    QVERIFY(!filter.redundant(NPlayEvent(ME_NOTEON,0,60,80)));
    QVERIFY(filter.redundant(NPlayEvent(ME_CONTROLLER,0,CTRL_SUSTAIN,127)));
    QVERIFY(!filter.redundant(NPlayEvent(ME_CONTROLLER,1,CTRL_SUSTAIN,127)));
    QVERIFY(!filter.redundant(NPlayEvent(ME_CONTROLLER,0,CTRL_SUSTAIN,0)));
}
