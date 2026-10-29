#include "mscore/scoretab.h"
#include "libmscore/textbase.h"
#include "libmscore/box.h"
#include <QSaveFile>
#include <QElapsedTimer>

static MasterScore* identityScore140(const QString& label) {
    auto score=mscore->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");
    if(!score)return nullptr;
    PluginAPI::Score wrapped(score);score->startCmd();
    wrapped.addText("title",QString::fromUtf8("谱面主标题")+label);
    wrapped.addText("subtitle",QString::fromUtf8("谱面副标题")+label);
    score->endCmd();score->setMetaTag("workTitle",QString::fromUtf8("作品信息标题")+label);score->doLayout();return score;
}
static TextBase* identityTitle140(Score* score) {
    for(auto mb=score->first();mb;mb=mb->next())if(!mb->isMeasure())
        for(auto element:mb->el())if(element->isTextBase()&&toTextBase(element)->tid()==Tid::TITLE)return toTextBase(element);
    return nullptr;
}
void TestPluginHost::scoreIdentityTitles140() {
    auto main=mscore;auto score=identityScore140(QString::fromUtf8("甲"));QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));main->resize(1200,820);main->show();
    const auto filename=_settings.filePath(QString::fromUtf8("文件名字甲.mscz"));QVERIFY(main->saveAs(score,false,filename,"mscz"));
    PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
    auto info=observer.scoreIdentity();QCOMPARE(info["visibleTitle"].toString(),QString::fromUtf8("谱面主标题甲"));
    QCOMPARE(info["workTitle"].toString(),QString::fromUtf8("作品信息标题甲"));QCOMPARE(info["fileName"].toString(),QString::fromUtf8("文件名字甲.mscz"));
    QCOMPARE(info["displayTitle"],info["visibleTitle"]);QCOMPARE(info["titleSource"].toString(),QString("visibleTitle"));
    QVERIFY(info["isMaster"].toBool());QVERIFY(info["isCurrentScore"].toBool());QVERIFY(!info["modified"].toBool());QVERIFY(info["visibleTexts"].toList().size()>=2);
    auto state=observer.arrangementState({{"whole",true}});QCOMPARE(state["identity"].toMap(),info);QCOMPARE(state["title"],info["displayTitle"]);
    auto note=toChord(score->findCR(Fraction::fromTicks(480),0))->upNote();score->select(note);score->setPlayPos(Fraction::fromTicks(960));
    const auto contents=score->state();const int undo=score->undoStack()->getCurIdx();const bool dirty=score->dirty();
    QElapsedTimer watch;watch.start();for(int i=0;i<500;++i)QCOMPARE(observer.scoreIdentity()["scoreSessionId"],info["scoreSessionId"]);
    qInfo()<<"140 identity: 500 reads ms"<<watch.nsecsElapsed()/1e6;
    QCOMPARE(score->undoStack()->getCurIdx(),undo);QCOMPARE(score->dirty(),dirty);QVERIFY(score->state()==contents);QVERIFY(note->selected());QCOMPARE(score->playPos().ticks(),960);
    auto title=identityTitle140(score);QVERIFY(title);score->startCmd();title->undoChangeProperty(Pid::TEXT,QString::fromUtf8("屏幕标题已修改"));score->endCmd();
    auto changed=observer.scoreIdentity();QCOMPARE(changed["displayTitle"].toString(),QString::fromUtf8("屏幕标题已修改"));QVERIFY(changed["modified"].toBool());
    QCOMPARE(changed["scoreSessionId"],info["scoreSessionId"]);QCOMPARE(changed["workTitle"],info["workTitle"]);
    const auto moved=_settings.filePath(QString::fromUtf8("另存 文件甲.mscz"));QVERIFY(main->saveAs(score,false,moved,"mscz"));
    auto saved=observer.scoreIdentity();QCOMPARE(saved["fileName"].toString(),QString::fromUtf8("另存 文件甲.mscz"));QCOMPARE(saved["scoreSessionId"],info["scoreSessionId"]);QVERIFY(!saved["modified"].toBool());
    std::unique_ptr<MasterScore> reopened(main->readScore(moved));QVERIFY(reopened);
    const auto fresh=PluginAPI::ScoreObserver::scoreIdentityFor(reopened.get());QVERIFY(fresh["scoreSessionId"]!=info["scoreSessionId"]);QCOMPARE(fresh["visibleTitle"],saved["visibleTitle"]);
    score->startCmd();title->undoChangeProperty(Pid::VISIBLE,false);score->endCmd();QCOMPARE(observer.scoreIdentity()["titleSource"].toString(),QString("workTitle"));
    score->setMetaTag("workTitle",QString());QCOMPARE(observer.scoreIdentity()["titleSource"].toString(),QString("tabLabel"));
    score->setCreated(true);auto unsaved=observer.scoreIdentity();QVERIFY(unsaved["newUnsaved"].toBool());QVERIFY(unsaved["filePath"].toString().isEmpty());
    const auto empty=PluginAPI::ScoreObserver::scoreIdentityFor(nullptr);QVERIFY(!empty["hasScore"].toBool());QCOMPARE(empty["application"].toMap()["instanceId"],info["application"].toMap()["instanceId"]);
}
void TestPluginHost::scoreIdentityParts140() {
    auto main=mscore;auto score=identityScore140(QString::fromUtf8("总谱"));QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));
    const auto whole=PluginAPI::ScoreObserver::scoreIdentityFor(score);
    auto excerpt=new Excerpt(score);excerpt->setTitle(QString::fromUtf8("钢琴分谱"));excerpt->parts().append(score->parts().front());
    auto part=new Score(score);excerpt->setPartScore(part);part->setExcerpt(excerpt);
    score->startCmd();score->undo(new AddExcerpt(excerpt));Excerpt::createExcerpt(excerpt);score->endCmd();
    auto tab=main->getTab1();QVERIFY(tab);tab->updateExcerpts();tab->setExcerpt(1);QTRY_COMPARE(main->currentScore(),part);
    const auto piano=PluginAPI::ScoreObserver::scoreIdentityFor(part);QVERIFY(!piano["isMaster"].toBool());QVERIFY(piano["isCurrentScore"].toBool());
    QCOMPARE(piano["partName"].toString(),QString::fromUtf8("钢琴分谱"));QCOMPARE(piano["masterSessionId"],whole["masterSessionId"]);QVERIFY(piano["scoreSessionId"]!=whole["scoreSessionId"]);
    QCOMPARE(piano["tabLabel"],piano["partName"]);QVERIFY(!PluginAPI::ScoreObserver::scoreIdentityFor(score)["isCurrentScore"].toBool());
    tab->setExcerpt(0);QTRY_COMPARE(main->currentScore(),static_cast<Score*>(score));QCOMPARE(PluginAPI::ScoreObserver::scoreIdentityFor(score)["scoreSessionId"],whole["scoreSessionId"]);
}
void TestPluginHost::scoreIdentityTransport140() {
    const auto folder=qEnvironmentVariable("P2_IDENTITY_140_CONTROL");if(folder.isEmpty())QSKIP("Run with the E1 multi-window client.");
    const auto label=qEnvironmentVariable("P2_IDENTITY_140_LABEL");auto main=mscore;auto score=identityScore140(label);QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));QVERIFY(main->saveAs(score,false,folder+"/file-"+label+".mscz","mscz"));
    main->resize(1100,780);main->show();initializeBuiltinAI();
    auto write=[&](const QString& name,const QJsonObject& value){QSaveFile file(folder+"/"+name);if(!file.open(QIODevice::WriteOnly))return false;file.write(QJsonDocument(value).toJson());return file.commit();};
    QVERIFY(write("ready-"+label+".json",QJsonObject::fromVariantMap(PluginAPI::ScoreObserver::scoreIdentityFor(score))));
    int handled=0;QElapsedTimer timeout;timeout.start();
    while(timeout.elapsed()<180000) {
        QFile request(folder+"/command-"+label+".json");
        if(request.open(QIODevice::ReadOnly)) {
            auto job=QJsonDocument::fromJson(request.readAll()).object();request.close();const int id=job["id"].toInt();
            if(id>handled) {
                handled=id;const auto action=job["action"].toString();
                if(action=="finish") { QVERIFY(write("reply-"+label+".json",{{"id",id},{"ok",true}}));break; }
                if(action=="selectWindow") {
                    main->raise();main->activateWindow();showAIConnection();QTest::qWait(1700);
                    auto dialog=main->findChild<QDialog*>("kumoAIConnection");QVERIFY(dialog);
                    auto list=dialog->findChild<QListWidget*>("aiConnectionWindows");QVERIFY(list);bool found=false;
                    for(int i=0;i<list->count();++i)if(list->item(i)->data(Qt::UserRole).toString()==job["instanceId"].toString()){list->setCurrentRow(i);found=true;break;}
                    QVERIFY(found);auto use=dialog->findChild<QPushButton*>("aiUseWindow");QVERIFY(use);QTest::mouseClick(use,Qt::LeftButton);
                    const auto output=qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",folder);QVERIFY(dialog->grab().save(output+"/ai-identity-140-"+label+".png"));dialog->close();
                } else if(action=="rename") {
                    auto title=identityTitle140(score);QVERIFY(title);score->startCmd();title->undoChangeProperty(Pid::TEXT,job["title"].toString());score->endCmd();
                } else if(action=="saveAs") {
                    QVERIFY(main->saveAs(score,false,folder+"/saved-"+label+".mscz","mscz"));
                } else if(action=="switchTab") {
                    auto other=identityScore140(label+"-other");QVERIFY(other);
                    other->setCreated(true);other->setImportedFilePath(QString());other->setName(label+"-other");
                    main->setCurrentScoreView(main->appendScore(other));QCOMPARE(main->scores().size(),2);
                } else if(action=="restart") {
                    shutdownBuiltinAI();initializeBuiltinAI();
                } else if(action=="closeScores") {
                    auto all=main->scores();for(auto item:all){item->undoStack()->setClean();main->closeScore(item);}
                }
                auto response=QJsonObject::fromVariantMap(PluginAPI::ScoreObserver::scoreIdentityFor(main->currentScore()));response["id"]=id;response["ok"]=true;QVERIFY(write("reply-"+label+".json",response));
            }
        }
        QTest::qWait(20);
    }
    QVERIFY(timeout.elapsed()<180000);shutdownBuiltinAI();
}
