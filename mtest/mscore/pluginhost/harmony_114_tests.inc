// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
void TestPluginHost::harmonyUnknownAndPoints114()
{
    const auto source=_settings.filePath("harmony-unknown-114.mscx");QFile file(source);QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("<museScore version=\"3.02\"><Score><Division>480</Division><Part><Staff id=\"1\"><StaffType group=\"pitched\"/></Staff><trackName>Piano</trackName><Instrument id=\"piano\"><Channel/></Instrument></Part><Staff id=\"1\"><Measure><voice><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig><Chord><durationType>whole</durationType><Note><pitch>60</pitch><tpc>14</tpc></Note><Note><pitch>61</pitch><tpc>21</tpc></Note><Note><pitch>62</pitch><tpc>16</tpc></Note></Chord></voice></Measure><Measure><voice><Rest><durationType>measure</durationType><duration>4/4</duration></Rest></voice></Measure></Staff></Score></museScore>");file.close();
    auto main=Ms::mscore;auto score=main->readScore(source);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->resize(1200,900);main->show();score->select(toChord(score->firstSegment(SegmentType::ChordRest)->element(0))->notes().front());
    showHarmonyPanel(true);auto panel=harmonyPanel077();QVERIFY(panel);auto config=panel->property("configuration").value<QJSValue>().toVariant().toMap();config["auto"]=true;config["scope"]=0;config["window"]=0;config["resultMode"]=1;config["chordLabels"]=true;config["holdLastHarmony"]=false;QVERIFY(QMetaObject::invokeMethod(panel,"applyPreferences",Q_ARG(QVariant,config)));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);
    auto observer=panel->findChild<PluginAPI::ScoreObserver*>();QVERIFY(observer);auto view=main->currentScoreView();
    const auto box=view->notePreviewMarkerBox(observer,0,0);QVERIFY2(!box.isEmpty(),"Unresolved chord must have a marker that can be opened");
    QTest::mouseDClick(view,Qt::LeftButton,Qt::NoModifier,view->matrix().map(box.center()).toPoint());QTRY_COMPARE(panel->property("annotationTick").toInt(),0);QTRY_VERIFY(panel->property("scoreRangeEditing").toBool());
    panel->setPanelFloating(true);panel->resizePanel(380,840);QTest::qWait(150);QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);
    auto click=[&](const QString& text) {
        QList<QQuickItem*> items;
        std::function<void(QQuickItem*)> visit=[&](QQuickItem* item){items.append(item);for(auto child:item->childItems())visit(child);};
        visit(panel);QQuickItem* flick=nullptr;
        for(auto item:items)if(item->objectName()=="harmonyDetailScroll"&&item->isVisible())flick=item;
        for(auto item:items) {
            if(!item->isVisible()||!item->isEnabled()||item->metaObject()->indexOfSignal("clicked()")<0||item->property("text").toString()!=text)continue;
            if(flick) {
                auto content=qobject_cast<QQuickItem*>(flick->property("contentItem").value<QObject*>());
                if(content) {
                    const auto y=item->mapToItem(content,QPointF()).y();
                    flick->setProperty("contentY",qBound(0.,y-flick->height()/2,qMax(0.,flick->property("contentHeight").toDouble()-flick->height())));
                    QTest::qWait(60);
                }
            }
            const auto point=item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint();
            if(point.y()<0||point.y()>=item->window()->height())continue;
            QTest::mouseClick(item->window(),Qt::LeftButton,Qt::NoModifier,point);return true;
        }
        return false;
    };
    auto edit=[&](const QString& action,int start,int end){return QMetaObject::invokeMethod(panel,"editRange",Q_ARG(QVariant,action),Q_ARG(QVariant,start),Q_ARG(QVariant,end),Q_ARG(QVariant,0),Q_ARG(QVariant,0),Q_ARG(QVariant,0));};
    QVERIFY(click(QString::fromUtf8(QByteArray::fromHex("e68c87e5ae9ae5928ce5bca6"))));QTRY_COMPARE(panel->property("summaryChordText").toString(),QString("C"));
    QVERIFY(QMetaObject::invokeMethod(panel,"openAnnotation",Q_ARG(QVariant,960),Q_ARG(QVariant,0)));QTest::qWait(80);QVERIFY(click(QString::fromUtf8(QByteArray::fromHex("e59ca8e5bd93e5898de4bd8de7bdaee68b86e58886"))));
    auto regions=[&]{return panel->property("analysisRegions").value<QJSValue>().toVariant().toList();};QTRY_COMPARE(regions().size(),2);QCOMPARE(regions().back().toMap().value("start").toInt(),960);
    QVERIFY(click(QString::fromUtf8(QByteArray::fromHex("e59088e5b9b6e5898de4b880e6aeb5"))));QTRY_COMPARE(regions().size(),1);QCOMPARE(regions().front().toMap().value("start").toInt(),0);QCOMPARE(regions().front().toMap().value("end").toInt(),1920);
    QVERIFY(click(QString::fromUtf8(QByteArray::fromHex("e68e92e999a4e6ada4e88c83e59bb4"))));QTRY_VERIFY(view->notePreviewMarkerBox(observer,0,0).isEmpty());QVERIFY(click(QString::fromUtf8(QByteArray::fromHex("e692a4e99480e4bfaee6ada3"))));QTRY_VERIFY(!view->notePreviewMarkerBox(observer,0,0).isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);QVERIFY(click(QString::fromUtf8(QByteArray::fromHex("e681a2e5a48de887aae58aa8"))));QTRY_VERIFY(!view->notePreviewMarkerBox(observer,0,0).isEmpty());QVERIFY(panel->property("manualOverrides").value<QJSValue>().toVariant().toList().isEmpty());
    panel->setProperty("scoreRangeEditing",false);QTest::qWait(60);const QDir out(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QVERIFY(view->grab().copy(70,60,500,220).save(out.filePath("harmony-unknown-114.png")));
    QVERIFY(QMetaObject::invokeMethod(panel,"openAnnotation",Q_ARG(QVariant,1920),Q_ARG(QVariant,0)));QVERIFY(edit("assign",1920,2400));QTRY_VERIFY(!view->notePreviewMarkerBox(observer,1920,0).isEmpty());QVERIFY(main->saveFile());showHarmonyPanel(false);main->closeScore(score);auto reopened=main->readScore(source);QVERIFY(reopened);main->setCurrentScoreView(main->appendScore(reopened));showHarmonyPanel(true);panel=harmonyPanel077();QVERIFY(panel);QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);observer=panel->findChild<PluginAPI::ScoreObserver*>();QVERIFY(observer);QTRY_VERIFY(!main->currentScoreView()->notePreviewMarkerBox(observer,1920,0).isEmpty());showHarmonyPanel(false);main->closeScore(reopened);
}

void TestPluginHost::harmonyPanelRangeDrag114()
{
    const auto source=_settings.filePath("harmony-panel-range-114.mscx");QVERIFY(QFile::copy(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx",source));auto main=Ms::mscore;auto score=main->readScore(source);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->resize(1200,900);main->show();score->select(toChord(score->firstSegment(SegmentType::ChordRest)->element(4))->notes().front());showHarmonyPanel(true);auto panel=harmonyPanel077();QVERIFY(panel);
    auto config=panel->property("configuration").value<QJSValue>().toVariant().toMap();config["resultMode"]=1;config["chordLabels"]=true;config["scope"]=0;config["auto"]=true;QVERIFY(QMetaObject::invokeMethod(panel,"applyPreferences",Q_ARG(QVariant,config)));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);
    QVERIFY(QMetaObject::invokeMethod(panel,"editRange",Q_ARG(QVariant,"assign"),Q_ARG(QVariant,0),Q_ARG(QVariant,960),Q_ARG(QVariant,0),Q_ARG(QVariant,0),Q_ARG(QVariant,0)));QVERIFY(QMetaObject::invokeMethod(panel,"openAnnotation",Q_ARG(QVariant,0),Q_ARG(QVariant,0)));panel->setPanelFloating(true);panel->resizePanel(380,840);QTest::qWait(150);
    QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);QQuickItem* handle=nullptr;QQuickItem* editor=nullptr;std::function<void(QQuickItem*)> visit=[&](QQuickItem* item){if(item->objectName()=="harmonyRangeEditor"&&item->isVisible())editor=item;if(item->objectName()=="harmonyRangeEnd"&&item->isVisible())handle=item;for(auto child:item->childItems())visit(child);};visit(panel);
    QVERIFY(handle&&handle->isVisible());QVERIFY(editor);auto window=handle->window();QVERIFY(window);
    const auto before=handle->x();const auto origin=handle->mapToScene(QPointF(handle->width()/2,handle->height()/2)).toPoint();QVERIFY(origin.y()>0&&origin.y()<window->height());
    QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,origin);QTest::mouseMove(window,origin-QPoint(15,0),30);QTest::mouseMove(window,origin-QPoint(45,0),30);QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,origin-QPoint(45,0));
    QTRY_VERIFY(editor->property("region").value<QJSValue>().property("end").toInt()!=960);main->cmd(getAction("undo"));QTRY_COMPARE(editor->property("region").value<QJSValue>().property("end").toInt(),960);QTRY_VERIFY2(qAbs(handle->x()-before)<1.,"Undo must restore the range handle position");
    main->cmd(getAction("redo"));QTRY_VERIFY(editor->property("region").value<QJSValue>().property("end").toInt()!=960);QVERIFY(main->saveFile());panel->setPanelFloating(false);showHarmonyPanel(false);main->closeScore(score);
}

void TestPluginHost::harmonyCrossSystemRange114()
{
    const auto source=_settings.filePath("harmony-cross-system-114.mscx");QFile file(source);QVERIFY(file.open(QIODevice::WriteOnly));QByteArray xml="<museScore version=\"3.02\"><Score><Division>480</Division><Part><Staff id=\"1\"><StaffType group=\"pitched\"/></Staff><trackName>Piano</trackName><Instrument id=\"piano\"><Channel/></Instrument></Part><Staff id=\"1\">";
    for(int bar=0;bar<3;++bar){xml+="<Measure><voice>";if(!bar)xml+="<TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>";for(int beat=0;beat<4;++beat)xml+="<Chord><durationType>quarter</durationType><Note><pitch>60</pitch><tpc>14</tpc></Note><Note><pitch>64</pitch><tpc>18</tpc></Note><Note><pitch>67</pitch><tpc>15</tpc></Note></Chord>";xml+="</voice>";if(!bar)xml+="<LayoutBreak><subtype>line</subtype></LayoutBreak>";xml+="</Measure>";}file.write(xml+"</Staff></Score></museScore>");file.close();
    auto main=Ms::mscore;auto score=main->readScore(source);QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));main->resize(1200,950);main->show();score->select(toChord(score->firstSegment(SegmentType::ChordRest)->element(0))->notes().front());showHarmonyPanel(true);auto panel=harmonyPanel077();QVERIFY(panel);auto config=panel->property("configuration").value<QJSValue>().toVariant().toMap();config["resultMode"]=1;config["chordLabels"]=true;config["scope"]=0;config["auto"]=true;QVERIFY(QMetaObject::invokeMethod(panel,"applyPreferences",Q_ARG(QVariant,config)));QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);
    QVERIFY(QMetaObject::invokeMethod(panel,"editRange",Q_ARG(QVariant,"assign"),Q_ARG(QVariant,0),Q_ARG(QVariant,1920),Q_ARG(QVariant,0),Q_ARG(QVariant,0),Q_ARG(QVariant,0)));QVERIFY(QMetaObject::invokeMethod(panel,"openAnnotation",Q_ARG(QVariant,0),Q_ARG(QVariant,0)));auto view=main->currentScoreView();view->setPhysicalZoomLevel(qMin(.7,qreal(view->width()-40)/(score->firstMeasure()->canvasPos().x()+score->firstMeasure()->width()+60.)));view->repaint();QTest::qWait(80);auto handles=view->findChild<QWidget*>("harmonyScoreRangeEditor");QVERIFY(handles);QTRY_VERIFY(handles->isVisible());const auto before=handles->property("rangeEndHandle").toPointF();QVERIFY(view->rect().contains(before.toPoint()));
    auto target=score->tick2segment(Fraction::fromTicks(4800),false,SegmentType::ChordRest);QVERIFY(target);QVERIFY(target->measure()->system()!=score->firstMeasure()->system());auto system=target->measure()->system();const auto at=view->matrix().map(QPointF(target->canvasPos().x(),system->canvasPos().y()+system->staffYpage(0)-system->pagePos().y()-score->spatium()*2.5));QVERIFY(view->rect().contains(at.toPoint()));
    QTest::mousePress(handles,Qt::LeftButton,Qt::NoModifier,before.toPoint());QTest::mouseMove(handles,at.toPoint(),40);QTest::mouseRelease(handles,Qt::LeftButton,Qt::NoModifier,at.toPoint());
    auto region=[&]{return panel->property("ownedRegion").value<QJSValue>().toVariant().toMap();};QTRY_COMPARE(region().value("end").toInt(),4800);QVERIFY(qAbs(handles->property("rangeEndHandle").toPointF().y()-before.y())>score->spatium()*4);
    main->cmd(getAction("undo"));QTRY_COMPARE(region().value("end").toInt(),1920);QTRY_VERIFY(QLineF(handles->property("rangeEndHandle").toPointF(),before).length()<1.);main->cmd(getAction("redo"));QTRY_COMPARE(region().value("end").toInt(),4800);
    const auto startHandle=handles->property("rangeStartHandle").toPointF(),endHandle=handles->property("rangeEndHandle").toPointF();QVERIFY(QMetaObject::invokeMethod(panel,"moveAnnotation",Q_ARG(QVariant,0),Q_ARG(QVariant,0),Q_ARG(QVariant,2.),Q_ARG(QVariant,-2.)));QCOMPARE(handles->property("rangeStartHandle").toPointF(),startHandle);QCOMPARE(handles->property("rangeEndHandle").toPointF(),endHandle);
    QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);QTRY_VERIFY(handles->isVisible());const QDir out(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QVERIFY(view->grab().save(out.filePath("harmony-cross-system-114.png")));QVERIFY(main->saveFile());showHarmonyPanel(false);main->closeScore(score);auto reopened=main->readScore(source);QVERIFY(reopened);main->setCurrentScoreView(main->appendScore(reopened));showHarmonyPanel(true);panel=harmonyPanel077();QVERIFY(panel);QTRY_VERIFY_WITH_TIMEOUT(!panel->property("analysisDirty").toBool(),10000);QTRY_COMPARE(region().value("end").toInt(),4800);showHarmonyPanel(false);main->closeScore(reopened);
}
