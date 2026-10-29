// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#include <QTextBrowser>
#include <QTextBlock>
#include <QTextFragment>
#include <QTextImageFormat>

namespace {
bool clickGuideLink146(QTextBrowser* browser, const QString& href)
{
    for (auto block=browser->document()->begin(); block.isValid(); block=block.next())
        for (auto item=block.begin(); !item.atEnd(); ++item) {
            const auto fragment=item.fragment();
            if (!fragment.isValid() || fragment.charFormat().anchorHref()!=href) continue;
            QTextCursor cursor(browser->document()); cursor.setPosition(fragment.position()+qMin(1,fragment.length()-1));
            browser->setTextCursor(cursor); browser->ensureCursorVisible();
            const auto point=browser->cursorRect(cursor).center()+QPoint(2,0);
            QTest::mouseClick(browser->viewport(),Qt::LeftButton,Qt::NoModifier,point);
            return true;
        }
    return false;
}
QDialog* guideDialog146() {
    if(auto named=Ms::mscore->findChild<QDialog*>("kumoUserGuide"))return named;
    QList<QDialog*> candidates;
    for(auto dialog:Ms::mscore->findChildren<QDialog*>())if(dialog->findChild<QTextBrowser*>())candidates.append(dialog);
    return candidates.size()==1?candidates.front():nullptr;
}
}

void TestPluginHost::guideNavigation146()
{
    auto main=Ms::mscore; main->resize(1280,900); main->show();
    QTemporaryDir directory; QVERIFY(directory.isValid()); QDir root(directory.path()); QVERIFY(root.mkpath("personal/docs"));
    auto write=[&](const QString& name,const QByteArray& text){QFile f(root.filePath(name));return f.open(QIODevice::WriteOnly)&&f.write(text)==text.size();};
    QVERIFY(write("personal/USER_GUIDE.md","# Guide home\n\n[Chapter](docs/chapter.md)\n\n![Wide screenshot](wide.png)\n\n[Missing](docs/missing.md)\n"));
    QByteArray chapter="# Chapter one\n\n[Details](#details)\n\n";
    for(int line=0;line<60;++line)chapter+="Reading line "+QByteArray::number(line)+(line==10||line==45?" needle\n\n":" ordinary\n\n");
    chapter+="## Details\n\nTarget paragraph.\n";
    QVERIFY(write("personal/docs/chapter.md",chapter));
    QImage wide(1600,600,QImage::Format_RGB32);wide.fill(QColor("#a7bccf"));QVERIFY(wide.save(root.filePath("personal/wide.png")));
    QScopedValueRollback<QString> share(mscoreGlobalShare,directory.path()+"/");
    showPersonalGuide(); auto box=guideDialog146();QVERIFY(box);
    const auto close=qScopeGuard([&]{box->deleteLater();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);});
    auto browser=box->findChild<QTextBrowser*>();QVERIFY(browser);
    auto back=box->findChild<QPushButton*>("kumoGuideBack"),forward=box->findChild<QPushButton*>("kumoGuideForward");
    auto home=box->findChild<QPushButton*>("kumoGuideHome"),find=box->findChild<QPushButton*>("kumoGuideFind");
    auto search=box->findChild<QLineEdit*>("kumoGuideSearch");auto message=box->findChild<QLabel*>("kumoGuideMessage");
    if(!back)box->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("guide-before-146.png"));
    QVERIFY2(back&&forward&&home&&find&&search&&message,"The reader needs visible navigation and search controls");
    QTest::qWait(80);QCOMPARE(browser->source().toLocalFile(),root.filePath("personal/USER_GUIDE.md"));
    QVERIFY(!back->isEnabled());QVERIFY(!forward->isEnabled());QVERIFY(browser->toPlainText().contains("Guide home"));
    auto checkImages=[&]{int found=0;for(auto block=browser->document()->begin();block.isValid();block=block.next())
        for(auto item=block.begin();!item.atEnd();++item){auto format=item.fragment().charFormat();if(!format.isImageFormat())continue;
            ++found;auto image=format.toImageFormat();if(image.width()<=0||image.width()>browser->viewport()->width()||qAbs(image.width()/image.height()-1600./600.)>.01)return false;}
        return found==1&&browser->horizontalScrollBar()->maximum()==0;};
    QVERIFY(checkImages());
    box->resize(600,480);QTest::qWait(40);QVERIFY(checkImages());
    for(auto button:{back,forward,home,find})QVERIFY(box->rect().contains(QRect(button->mapTo(box,QPoint()),button->size())));
    QVERIFY(clickGuideLink146(browser,"docs/chapter.md"));
    QTRY_VERIFY(browser->toPlainText().startsWith("Chapter one"));QVERIFY(back->isEnabled());
    browser->verticalScrollBar()->setValue(250);const int readingPosition=browser->verticalScrollBar()->value();QVERIFY(readingPosition>0);
    QTest::mouseClick(home,Qt::LeftButton);QTRY_VERIFY(browser->toPlainText().startsWith("Guide home"));
    QTest::mouseClick(back,Qt::LeftButton);QTRY_VERIFY(browser->toPlainText().startsWith("Chapter one"));
    QCOMPARE(browser->verticalScrollBar()->value(),readingPosition);QVERIFY(forward->isEnabled());
    QTest::mouseClick(forward,Qt::LeftButton);QTRY_VERIFY(browser->toPlainText().startsWith("Guide home"));
    QTest::mouseClick(back,Qt::LeftButton);QTRY_VERIFY(browser->toPlainText().startsWith("Chapter one"));
    browser->setFocus();QTest::keyClick(browser,Qt::Key_F,Qt::ControlModifier);QTRY_VERIFY(search->hasFocus());
    QTest::keyClicks(search,"needle");QTest::keyClick(search,Qt::Key_Return);
    QCOMPARE(browser->textCursor().selectedText(),QString("needle"));const int first=browser->textCursor().selectionStart();
    QTest::keyClick(search,Qt::Key_Return);QVERIFY(browser->textCursor().selectionStart()>first);
    QTest::keyClick(search,Qt::Key_Return);QCOMPARE(browser->textCursor().selectionStart(),first);
    search->selectAll();QTest::keyClicks(search,"unmatched word");QTest::mouseClick(find,Qt::LeftButton);QVERIFY(message->isVisible());
    QVERIFY(clickGuideLink146(browser,"#details"));QCOMPARE(browser->source().fragment(),QString("details"));
    QVERIFY(browser->verticalScrollBar()->value()>0);
    QTest::mouseClick(home,Qt::LeftButton);const auto before=browser->toPlainText();const auto source=browser->source();
    QVERIFY(clickGuideLink146(browser,"docs/missing.md"));QVERIFY(message->isVisible());QCOMPARE(browser->source(),source);QCOMPARE(browser->toPlainText(),before);
    QTest::keyClick(box,Qt::Key_Escape);QVERIFY(!box->isVisible());
    showPersonalGuide("docs/chapter.md");QCOMPARE(guideDialog146(),box);QVERIFY(box->isVisible());
    QCOMPARE(browser->source().toLocalFile(),root.filePath("personal/docs/chapter.md"));
    QVERIFY(box->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("guide-navigation-146.png")));
}

void TestPluginHost::guidePublishedBook146()
{
    auto main=Ms::mscore;auto score=main->readScore(QString(TESTROOT)+"/mtest/mscore/scoreobserver/piano.mscx");QVERIFY(score);
    main->setCurrentScoreView(main->appendScore(score));main->resize(1280,900);main->show();
    auto first=score->firstSegment(SegmentType::ChordRest)->element(4);
    QVERIFY(first&&first->isChord());score->select(toChord(first)->upNote());main->endCmd();
    const int undo=score->undoStack()->getCurIdx();const auto position=score->playPos();const auto state=score->state();
    auto action=main->findChild<QAction*>("kumo-user-guide");QVERIFY(action);QMenu* menu=nullptr;
    for(auto candidate:main->findChildren<QMenu*>())if(candidate->actions().contains(action)){menu=candidate;break;}
    QVERIFY(menu);menu->popup(main->mapToGlobal(QPoint(180,50)));QTRY_VERIFY(menu->isVisible());
    QTest::mouseClick(menu,Qt::LeftButton,Qt::NoModifier,menu->actionGeometry(action).center());
    auto box=guideDialog146();QVERIFY(box&&box->isVisible());
    const auto close=qScopeGuard([&]{box->deleteLater();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);});
    auto browser=box->findChild<QTextBrowser*>("kumoGuideText");QVERIFY(browser);
    QVERIFY(browser->toPlainText().contains("MuseScore Evolution"));
    QVERIFY(clickGuideLink146(browser,"docs/FOLLOW_VARIANTS_142_GUIDE.md"));
    QTRY_VERIFY(browser->source().path().endsWith("FOLLOW_VARIANTS_142_GUIDE.md"));
    QVERIFY(browser->toPlainText().size()>300);
    for(auto block=browser->document()->begin();block.isValid();block=block.next())
        for(auto item=block.begin();!item.atEnd();++item)
            if(item.fragment().charFormat().isImageFormat())QVERIFY(item.fragment().charFormat().toImageFormat().width()<=browser->viewport()->width());
    QTest::qWait(40);
    QVERIFY(box->grab().save(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS",_settings.path())).filePath("guide-published-146.png")));
    QTest::mouseClick(box->findChild<QPushButton*>("kumoGuideBack"),Qt::LeftButton);
    QTRY_VERIFY(browser->source().path().endsWith("USER_GUIDE.md"));
    QCOMPARE(score->undoStack()->getCurIdx(),undo);QCOMPARE(score->playPos(),position);QCOMPARE(score->state(),state);
    QCOMPARE(int(score->selection().elements().size()),1);
}
