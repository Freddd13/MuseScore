// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include <QBuffer>
#include "mscore/globals.h"

static QVariantMap imageRequest141(PluginAPI::ScoreObserver& observer,const QVariantMap& range=QVariantMap()) {
    const auto state=observer.arrangementState(range);
    return {{"snapshotId",state["snapshotId"]},{"scope",state["scope"]},{"imageIndex",0}};
}
static MasterScore* imageScore141(const QString& file) {
    QFile target(file);if(!target.open(QIODevice::WriteOnly))return nullptr;target.write(systemLockFixture138());target.close();
    return mscore->readScore(file);
}
void TestPluginHost::nativeScoreImageContent141() {
    auto main=mscore;auto score=imageScore141(_settings.filePath("native-images-141.mscx"));QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));score->doLayout();
    PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);
    auto request=imageRequest141(observer,{{"whole",true},{"firstMeasure",1},{"lastMeasure",2}});
    auto note=toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0))->upNote();score->select(note);score->setPlayPos(Fraction::fromTicks(960));
    const auto state=score->state();const int undo=score->undoStack()->getCurIdx();const bool dirty=score->dirty(),printing=score->printing();const auto ratio=MScore::pixelRatio;
    auto image=observer.arrangementImage(request);QVERIFY2(!image.contains("error"),qUtf8Printable(image["error"].toString()));
    QCOMPARE(image["schemaVersion"].toString(),QString("score-image/1"));QCOMPARE(image["snapshotId"],request["snapshotId"]);
    const auto bytes=QByteArray::fromBase64(image["pngBase64"].toString().toLatin1());QImage png;QVERIFY(png.loadFromData(bytes,"PNG"));
    QCOMPARE(png.width(),image["width"].toInt());QCOMPARE(png.height(),image["height"].toInt());QVERIFY(png.width()<=2400&&png.height()<=1800);
    const auto notes=image["notes"].toList();QCOMPARE(notes.size(),16);QSet<QString> references;
    for(auto value:notes) {
        const auto row=value.toMap(),box=row["pixelRect"].toMap();references.insert(row["ref"].toMap()["id"].toString());
        const QPoint center(qRound(box["x"].toDouble()+box["width"].toDouble()/2),qRound(box["y"].toDouble()+box["height"].toDouble()/2));
        QVERIFY(png.rect().contains(center));bool ink=false;
        for(int y=qMax(0,center.y()-3);y<=qMin(png.height()-1,center.y()+3);++y)
            for(int x=qMax(0,center.x()-3);x<=qMin(png.width()-1,center.x()+3);++x)ink|=qGray(png.pixel(x,y))<100;
        QVERIFY2(ink,qUtf8Printable(row["ref"].toMap()["id"].toString()));
    }
    auto page=observer.arrangementRange({{"snapshotId",request["snapshotId"]},{"scope",request["scope"]},{"fromTick",0},{"limit",64}});
    for(auto value:page["notes"].toList())QVERIFY(references.contains(value.toMap()["id"].toString()));
    auto cached=observer.arrangementImage(request);QVERIFY(cached["cacheHit"].toBool());QCOMPARE(cached["pngBase64"],image["pngBase64"]);
    QCOMPARE(score->printing(),printing);QCOMPARE(MScore::pixelRatio,ratio);QVERIFY(score->state()==state);QCOMPARE(score->undoStack()->getCurIdx(),undo);QCOMPARE(score->dirty(),dirty);QVERIFY(note->selected());QCOMPARE(score->playPos().ticks(),960);
    qInfo()<<"141 native image dimensions"<<png.size()<<"bytes"<<bytes.size()<<"render ms"<<image["milliseconds"]<<"cached ms"<<cached["milliseconds"];
    const QDir out(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QVERIFY(png.save(out.filePath("native-score-141.png")));
    QFile data(out.filePath("native-score-141.json"));QVERIFY(data.open(QIODevice::WriteOnly));auto metadata=image;metadata.remove("pngBase64");data.write(QJsonDocument::fromVariant(metadata).toJson());data.close();
    auto stale=request;stale["snapshotId"]="another-score:1";QVERIFY(observer.arrangementImage(stale).contains("error"));
    auto missing=request;missing["imageIndex"]=-1;QVERIFY(observer.arrangementImage(missing).contains("error"));
    score->startCmd();note->undoChangeProperty(Pid::OFFSET,QPointF(7,0));note->chord()->undoChangeProperty(Pid::STAFF_MOVE,1);score->endCmd();score->doLayout();
    QVERIFY(observer.arrangementImage(request).contains("error"));auto moved=observer.arrangementImage(imageRequest141(observer,{{"whole",true},{"firstMeasure",1},{"lastMeasure",2}}));
    QVERIFY(!moved.contains("error"));QVERIFY(moved["geometryVersion"]!=image["geometryVersion"]);QVERIFY(moved["pngBase64"]!=image["pngBase64"]);
    QCOMPARE(note->chord()->staffMove(),1);const auto crop=moved["pageRect"].toMap();const double scale=moved["scale"].toDouble();
    const auto location=moved["notes"].toList().first().toMap()["pixelRect"].toMap();
    QVERIFY(qAbs(location["x"].toDouble()-(note->pageBoundingRect().x()-crop["x"].toDouble())*scale)<.01);
    QVERIFY(qAbs(location["y"].toDouble()-(note->pageBoundingRect().y()-crop["y"].toDouble())*scale)<.01);
}
void TestPluginHost::nativeScoreImageRanges141() {
    auto main=mscore;auto score=imageScore141(_settings.filePath("native-images-ranges-141.mscx"));QVERIFY(score);main->setCurrentScoreView(main->appendScore(score));score->doLayout();
    PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);auto request=imageRequest141(observer,{{"whole",true}});
    auto first=observer.arrangementImage(request);QVERIFY2(!first.contains("error"),qUtf8Printable(first["error"].toString()));QVERIFY(first["imageCount"].toInt()>=2);
    QSet<QString> refs;QSet<int> pages;
    for(int i=0;i<first["imageCount"].toInt();++i) {
        request["imageIndex"]=i;auto image=observer.arrangementImage(request);QVERIFY(!image.contains("error"));pages.insert(image["page"].toInt());
        for(auto value:image["notes"].toList()) {auto id=value.toMap()["ref"].toMap()["id"].toString();QVERIFY(!refs.contains(id));refs.insert(id);}
        QImage png;QVERIFY(png.loadFromData(QByteArray::fromBase64(image["pngBase64"].toString().toLatin1()),"PNG"));
        QVERIFY(png.save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath(QString("native-page-141-%1.png").arg(i))));
    }
    QCOMPARE(refs.size(),64);QVERIFY(pages.size()>=2);
    const auto range=request["scope"].toMap();auto left=range;left["firstTrack"]=4;left["endTrack"]=5;request["scope"]=left;request["imageIndex"]=0;
    auto filtered=observer.arrangementImage(request);QVERIFY(!filtered.contains("error"));for(auto value:filtered["notes"].toList())QCOMPARE(value.toMap()["track"].toInt(),4);
    score->setLayoutMode(LayoutMode::LINE);score->doLayout();auto line=observer.arrangementImage(request);QVERIFY2(!line.contains("error"),qUtf8Printable(line["error"].toString()));
    QVERIFY(line["geometryVersion"]!=filtered["geometryVersion"]);QVERIFY(line["width"].toInt()<=2400);QVERIFY(line["imageCount"].toInt()>=2);
}
void TestPluginHost::nativeScoreImageNotation141() {
    const QStringList files{"/mtest/capella/io/testSlurTie.capx-ref.mscx","/mtest/libmscore/tuplet/nestedTuplets_addStaff.mscx","/mtest/libmscore/note/grace-ref.mscx"};
    const auto resolution=preferences.getDouble(PREF_EXPORT_PNG_RESOLUTION);
    const auto background=preferences.getInt(PREF_EXPORT_BG_STYLE);const auto color=preferences.getColor(PREF_EXPORT_BG_CUSTOM_COLOR);const int trim=trimMargin;
    auto restore=qScopeGuard([&]{preferences.setPreference(PREF_EXPORT_PNG_RESOLUTION,resolution);preferences.setPreference(PREF_EXPORT_BG_STYLE,background);preferences.setPreference(PREF_EXPORT_BG_CUSTOM_COLOR,color);trimMargin=trim;});
    int number=0;
    for(const auto& filename:files) {
        const QString path=QString(TESTROOT)+filename;QFile original(path);QVERIFY(original.open(QIODevice::ReadOnly));const auto before=QCryptographicHash::hash(original.readAll(),QCryptographicHash::Sha256);original.close();
        auto main=mscore;auto score=main->readScore(path);QVERIFY2(score,qUtf8Printable(filename));main->setCurrentScoreView(main->appendScore(score));score->doLayout();
        PluginAPI::Score wrapped(score);PluginAPI::ScoreObserver observer;observer.setScore(&wrapped);const auto request=imageRequest141(observer,{{"whole",true}});
        auto value=observer.arrangementImage(request);QVERIFY2(!value.contains("error"),qUtf8Printable(value.value("error").toString()));
        if(filename.contains("/note/grace")) {
            const auto page=observer.arrangementRange({{"snapshotId",request["snapshotId"]},{"scope",request["scope"]},{"fromTick",0},{"limit",64}});
            QSet<QString> ids;for(auto row:page["notes"].toList())ids.insert(row.toMap()["id"].toString());bool grace=false;
            for(auto row:value["notes"].toList())if(row.toMap()["graceIndex"].toInt()>=0){grace=true;QVERIFY(ids.contains(row.toMap()["ref"].toMap()["id"].toString()));}
            QVERIFY(grace);
        }
        QImage actual;QVERIFY(actual.loadFromData(QByteArray::fromBase64(value["pngBase64"].toString().toLatin1()),"PNG"));
        const double scale=value["scale"].toDouble();preferences.setPreference(PREF_EXPORT_PNG_RESOLUTION,scale*DPI);preferences.setPreference(PREF_EXPORT_BG_STYLE,2);preferences.setPreference(PREF_EXPORT_BG_CUSTOM_COLOR,QColor(Qt::white));trimMargin=-1;
        QByteArray bytes;QBuffer buffer(&bytes);QVERIFY(buffer.open(QIODevice::WriteOnly));QVERIFY(main->savePng(score,&buffer,value["page"].toInt()-1,true));
        QImage full;QVERIFY(full.loadFromData(bytes,"PNG"));QImage expected(actual.size(),QImage::Format_ARGB32_Premultiplied);expected.fill(Qt::white);
        const auto crop=value["pageRect"].toMap();const QRectF pixels(crop["x"].toDouble()*scale,crop["y"].toDouble()*scale,crop["width"].toDouble()*scale,crop["height"].toDouble()*scale);
        QPainter painter(&expected);painter.setRenderHint(QPainter::SmoothPixmapTransform);painter.drawImage(QRectF(expected.rect()),full,pixels);painter.end();
        double difference=0;for(int y=0;y<actual.height();++y)for(int x=0;x<actual.width();++x)difference+=qAbs(qGray(actual.pixel(x,y))-qGray(expected.pixel(x,y)));
        difference/=actual.width()*actual.height();qInfo()<<"141 original PNG comparison"<<filename<<"gray difference"<<difference;QVERIFY2(difference<12,qPrintable(QString::number(difference)));
        const QDir out(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path()));QVERIFY(actual.save(out.filePath(QString("native-notation-141-%1.png").arg(number))));QVERIFY(expected.save(out.filePath(QString("native-export-141-%1.png").arg(number++))));
        QVERIFY(original.open(QIODevice::ReadOnly));QCOMPARE(QCryptographicHash::hash(original.readAll(),QCryptographicHash::Sha256),before);
    }
}
