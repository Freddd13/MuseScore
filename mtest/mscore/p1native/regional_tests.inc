void TestP1Native::regionalSwitchUndoAndSharedSettings() {
      auto s = mscore->readScore(QString(TESTROOT) + "/mtest/mscore/p0native/longroot.mscx");
      QVERIFY(s);
      mscore->setCurrentScoreView(mscore->appendScore(s));
      int start = 0, end = s->firstMeasure()->endTick().ticks();
      QVariantList tracks{4};
      auto first = toChord(s->firstMeasure()->first(SegmentType::ChordRest)->element(4))->notes().front();
      int pitch = first->pitch();
      auto created = RegionalVariants::create(s, start, end, tracks, "左手第一小节");
      QVERIFY2(created["ok"].toBool(), qPrintable(created["error"].toString()));
      auto original = created["variant"].toString(), group = created["group"].toString();
      auto copied = RegionalVariants::duplicate(s, group, "B");
      QVERIFY(copied["ok"].toBool());
      auto b = copied["variant"].toString();
      QString problem;
      auto fragment = RegionalVariants::capture(s, start, end, tracks, &problem);
      QFile captured(settings.filePath("fragment.xml"));
      QVERIFY(captured.open(QIODevice::WriteOnly));
      captured.write(fragment);
      captured.close();
      auto selected = RegionalVariants::select(s, group, b);
      QVERIFY2(selected["ok"].toBool(), qPrintable(selected["error"].toString()));
      first = toChord(s->firstMeasure()->first(SegmentType::ChordRest)->element(4))->notes().front();
      s->startCmd();
      first->undoChangeProperty(Pid::VELO_OFFSET, 23);
      s->endCmd();
      int undo = s->undoStack()->getCurIdx();
      int pedals = 0;
      for (auto p : s->spannerMap().map())
            if (p.second->isPedal())
                  ++pedals;
      auto reverted = RegionalVariants::select(s, group, original);
      QVERIFY2(reverted["ok"].toBool(), qPrintable(reverted["error"].toString()));
      QCOMPARE(s->undoStack()->getCurIdx(), undo + 1);
      first = toChord(s->firstMeasure()->first(SegmentType::ChordRest)->element(4))->notes().front();
      QCOMPARE(first->pitch(), pitch);
      QCOMPARE(first->veloOffset(), 0);
      int afterPedals = 0;
      for (auto p : s->spannerMap().map())
            if (p.second->isPedal())
                  ++afterPedals;
      QCOMPARE(afterPedals, pedals);
      EditData edit;
      s->undoRedo(true, &edit);
      first = toChord(s->firstMeasure()->first(SegmentType::ChordRest)->element(4))->notes().front();
      QCOMPARE(first->veloOffset(), 23);
      QCOMPARE(RegionalVariants::state(s)["groups"].toList()[0].toMap()["active"].toString(), b);
      s->undoRedo(false, &edit);
      first = toChord(s->firstMeasure()->first(SegmentType::ChordRest)->element(4))->notes().front();
      QCOMPARE(first->veloOffset(), 0);
      QVERIFY(RegionalVariants::create(s, start, end, tracks, "重叠").contains("error"));
      QVERIFY(RegionalVariants::create(s, start, end, QVariantList{0}, "右手独立")["ok"].toBool());
}
void TestP1Native::regionalSaveMainAndRestoreCurrent() {
      auto s = mscore->readScore(QString(TESTROOT) + "/mtest/mscore/p0native/longroot.mscx");
      QVERIFY(s);
      mscore->setCurrentScoreView(mscore->appendScore(s));
      auto created =
          RegionalVariants::create(s, 0, s->firstMeasure()->endTick().ticks(), QVariantList{4}, "低音");
      QVERIFY(created["ok"].toBool());
      QString group = created["group"].toString(), original = created["variant"].toString();
      auto copied = RegionalVariants::duplicate(s, group, "B");
      QString b = copied["variant"].toString();
      QVERIFY(RegionalVariants::select(s, group, b)["ok"].toBool());
      auto first = toChord(s->firstMeasure()->first(SegmentType::ChordRest)->element(4))->notes().front();
      s->startCmd();
      first->undoChangeProperty(Pid::VELO_OFFSET, 19);
      s->endCmd();
      QVERIFY(RegionalVariants::chooseMain(s, group, original)["ok"].toBool());
      int undo = s->undoStack()->getCurIdx();
      auto state = s->state();
      QByteArray archive;
      QBuffer target(&archive);
      target.open(QIODevice::WriteOnly);
      QVERIFY2(s->saveCompressedFile(&target, "score.mscx", false, false), qPrintable(MScore::lastError));
      target.close();
      QSaveFile compatibility(QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", settings.path()))
                                  .filePath("regional-main-current.mscz"));
      QVERIFY(compatibility.open(QIODevice::WriteOnly));
      QCOMPARE(compatibility.write(archive), qint64(archive.size()));
      QVERIFY(compatibility.commit());
      QVERIFY(s->state() == state);
      QCOMPARE(s->undoStack()->getCurIdx(), undo);
      QCOMPARE(first->veloOffset(), 19);
      QBuffer input(&archive);
      input.open(QIODevice::ReadOnly);
      MQZipReader zip(&input);
      QVERIFY(!zip.fileData("Kumo/variants/index.json").isEmpty());
      QByteArray ordinary;
      QBuffer output(&ordinary);
      output.open(QIODevice::WriteOnly);
      MQZipWriter writer(&output);
      for (auto f : zip.fileInfoList())
            if (f.isFile && !f.filePath.startsWith("Kumo/"))
                  writer.addFile(f.filePath, zip.fileData(f.filePath));
      writer.close();
      output.close();
      QBuffer normalInput(&ordinary);
      normalInput.open(QIODevice::ReadOnly);
      auto normal = std::unique_ptr<MasterScore>(new MasterScore(s->style()));
      QCOMPARE(normal->loadCompressedMsc(&normalInput, true), Score::FileError::FILE_NO_ERROR);
      auto mainNote =
          toChord(normal->firstMeasure()->first(SegmentType::ChordRest)->element(4))->notes().front();
      QCOMPARE(mainNote->veloOffset(), 0);
      QVERIFY(!RegionalVariants::active(normal.get()));
      QBuffer personalInput(&archive);
      personalInput.open(QIODevice::ReadOnly);
      auto restored = std::unique_ptr<MasterScore>(new MasterScore(s->style()));
      QVERIFY2(restored->loadCompressedMsc(&personalInput, true) == Score::FileError::FILE_NO_ERROR,
               qPrintable(MScore::lastError));
      auto activeNote =
          toChord(restored->firstMeasure()->first(SegmentType::ChordRest)->element(4))->notes().front();
      QCOMPARE(activeNote->veloOffset(), 19);
      QCOMPARE(RegionalVariants::state(restored.get())["groups"].toList()[0].toMap()["active"].toString(), b);
      QCOMPARE(restored->undoStack()->getCurIdx(), 0);
}
void TestP1Native::regionalIndependentVoicesExpandImportAndExport() {
      auto s = mscore->readScore(QString(TESTROOT) + "/mtest/mscore/p0native/longroot.mscx");
      QVERIFY(s);
      mscore->setCurrentScoreView(mscore->appendScore(s));
      s->startCmd();
      s->insertMeasure(ElementType::MEASURE, nullptr);
      s->endCmd();
      const int end = s->firstMeasure()->endTick().ticks();
      auto a = RegionalVariants::create(s, 0, end, {4}, "左手"),
           r = RegionalVariants::create(s, 0, end, {0}, "右手");
      QVERIFY(a["ok"].toBool());
      QVERIFY(r["ok"].toBool());
      const auto group = a["group"].toString(), original = a["variant"].toString();
      auto b = RegionalVariants::duplicate(s, group, "B");
      QVERIFY(b["ok"].toBool());
      QVERIFY(RegionalVariants::select(s, group, b["variant"].toString())["ok"].toBool());
      auto note = [&](int track) -> Note * {
            for (auto segment = s->firstMeasure()->first(SegmentType::ChordRest);
                 segment && segment->tick().ticks() < end; segment = segment->next1(SegmentType::ChordRest)) {
                  auto e = segment->element(track);
                  if (e && e->isChord())
                        return toChord(e)->notes().front();
            }
            return nullptr;
      };
      QVERIFY(note(0));
      QVERIFY(note(4));
      s->startCmd();
      note(4)->undoChangeProperty(Pid::VELO_OFFSET, 27);
      note(0)->undoChangeProperty(Pid::VELO_OFFSET, 31);
      s->endCmd();
      for (int i = 0; i < 4; ++i) {
            QVERIFY(RegionalVariants::select(s, group, original)["ok"].toBool());
            QCOMPARE(note(4)->veloOffset(), 0);
            QCOMPARE(note(0)->veloOffset(), 31);
            QVERIFY(RegionalVariants::select(s, group, b["variant"].toString())["ok"].toBool());
            QCOMPARE(note(4)->veloOffset(), 27);
            QCOMPARE(note(0)->veloOffset(), 31);
      }
      int before = s->undoStack()->getCurIdx();
      QVERIFY(s->firstMeasure()->nextMeasure());
      auto expanded =
          RegionalVariants::expand(s, group, 0, s->firstMeasure()->nextMeasure()->endTick().ticks());
      QVERIFY2(expanded["ok"].toBool(), qPrintable(expanded["error"].toString()));
      QCOMPARE(s->undoStack()->getCurIdx(), before + 1);
      QCOMPARE(note(4)->veloOffset(), 27);
      EditData ed;
      s->undoRedo(true, &ed);
      QCOMPARE(RegionalVariants::state(s)["groups"].toList()[0].toMap()["end"].toInt(), end);
      s->undoRedo(false, &ed);
      std::unique_ptr<MasterScore> old(
          mscore->readScore(QString(TESTROOT) + "/mtest/mscore/p0native/longroot.mscx"));
      QVERIFY(old);
      old->startCmd();
      old->insertMeasure(ElementType::MEASURE, nullptr);
      old->endCmd();
      auto imported = RegionalVariants::importVersion(s, old.get(), group, "旧方案");
      QVERIFY2(imported["ok"].toBool(), qPrintable(imported["error"].toString()));
      QVERIFY(RegionalVariants::select(s, group, imported["variant"].toString())["ok"].toBool());
      QCOMPARE(note(4)->veloOffset(), 0);
      QCOMPARE(note(0)->veloOffset(), 31);
      QByteArray bytes;
      QBuffer output(&bytes);
      output.open(QIODevice::WriteOnly);
      QVERIFY(RegionalVariants::exportOrdinary(s, &output, "copy.mscx", false));
      output.close();
      QBuffer input(&bytes);
      input.open(QIODevice::ReadOnly);
      MQZipReader zip(&input);
      QVERIFY(zip.fileData("Kumo/variants/index.json").isEmpty());
}
void TestP1Native::regionalRejectForeignVoiceAndKeepFile() {
      auto s = mscore->readScore(QString(TESTROOT) + "/mtest/mscore/p0native/longroot.mscx");
      QVERIFY(s);
      mscore->setCurrentScoreView(mscore->appendScore(s));
      QVERIFY(
          RegionalVariants::create(s, 0, s->firstMeasure()->endTick().ticks(), {4}, "左手")["ok"].toBool());
      const QString path = settings.filePath("keep.mscx");
      QFile file(path);
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write("keep this file");
      file.close();
      QFileInfo target(path);
      QVERIFY(!s->Score::saveFile(target));
      QVERIFY(file.open(QIODevice::ReadOnly));
      QCOMPARE(file.readAll(), QByteArray("keep this file"));
      file.close();
      auto data = RegionalVariants::state(s);
      auto groups = data["groups"].toList();
      auto g = groups[0].toMap();
      auto variants = g["variants"].toList();
      auto v = variants[0].toMap();
      auto fragment = qUncompress(QByteArray::fromBase64(v["data"].toByteArray()));
      fragment.replace("<voice id=\"0\">0</voice>", "<voice id=\"3\">0</voice>");
      v["data"] = QString::fromLatin1(qCompress(fragment).toBase64());
      variants[0] = v;
      g["variants"] = variants;
      groups[0] = g;
      data["groups"] = groups;
      QString problem;
      QVERIFY(!RegionalVariants::restore(s, QJsonDocument::fromVariant(data).toJson(), &problem));
      QVERIFY(!problem.isEmpty());
      QCOMPARE(RegionalVariants::state(s)["groups"].toList().size(), 1);
}
void TestP1Native::regionalAtomicFileSaveAndReopen() {
      auto s = mscore->readScore(QString(TESTROOT) + "/mtest/mscore/p0native/longroot.mscx");
      QVERIFY(s);
      auto made = RegionalVariants::create(s, 0, s->firstMeasure()->endTick().ticks(), {4}, "文件保存");
      QVERIFY(made["ok"].toBool());
      const QString path = settings.filePath("all-variants.mscz");
      QFileInfo info(path);
      QVERIFY2(s->saveCompressedFile(info, false, false), qPrintable(MScore::lastError));
      std::unique_ptr<MasterScore> reopened(mscore->readScore(path));
      QVERIFY(reopened);
      QVERIFY(RegionalVariants::active(reopened.get()));
      QVERIFY2(s->saveCompressedFile(info, false, false), qPrintable(MScore::lastError));
      QByteArray archive;
      QBuffer buffer(&archive);
      buffer.open(QIODevice::WriteOnly);
      QVERIFY(RegionalVariants::exportOrdinary(s, &buffer, "ordinary.mscx", false));
      QSaveFile output(settings.filePath("ordinary.mscz"));
      QVERIFY(output.open(QIODevice::WriteOnly));
      QCOMPARE(output.write(archive), qint64(archive.size()));
      QVERIFY(output.commit());
      std::unique_ptr<MasterScore> ordinary(mscore->readScore(output.fileName()));
      QVERIFY(ordinary);
      QVERIFY(!RegionalVariants::active(ordinary.get()));
}
void TestP1Native::regionalPanelMouseOperations() {
      QMenu menu;
      addRegionalVariantAction(&menu);
      auto s = mscore->readScore(QString(TESTROOT) + "/mtest/mscore/p0native/longroot.mscx");
      QVERIFY(s);
      mscore->setCurrentScoreView(mscore->appendScore(s));
      auto group = RegionalVariants::create(s, 0, s->firstMeasure()->endTick().ticks(), {4}, "左手");
      QVERIFY(group["ok"].toBool());
      auto copied = RegionalVariants::duplicate(s, group["group"].toString(), "B");
      QVERIFY(copied["ok"].toBool());
      getAction("regional-variants")->trigger();
      QTest::qWait(100);
      auto dock = mscore->findChild<QDockWidget *>("kumoPanel/regionalVariantsDock");
      QVERIFY(dock);
      dock->setFloating(true);
      dock->resize(680, 650);
      dock->show();
      dock->raise();
      QTest::qWait(100);
      auto tree = dock->findChild<QTreeWidget *>("regionalTree");
      QVERIFY(tree);
      QCOMPARE(tree->topLevelItemCount(), 1);
      auto child = tree->topLevelItem(0)->child(1);
      QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                        tree->visualItemRect(child).center());
      auto button = dock->findChild<QPushButton *>("regionalSwitch");
      QVERIFY(button && button->isVisible());
      QTest::mouseClick(button, Qt::LeftButton);
      QCOMPARE(RegionalVariants::state(s)["groups"].toList()[0].toMap()["active"].toString(),
               copied["variant"].toString());
      QTest::mouseClick(dock->findChild<QPushButton *>("regionalMain"), Qt::LeftButton);
      QCOMPARE(RegionalVariants::state(s)["groups"].toList()[0].toMap()["main"].toString(),
               copied["variant"].toString());
      QTest::qWait(100);
      dock->grab().save(
          QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", settings.path())).filePath("regional-042.png"));
      bool copiedByMouse = false;
      QTimer::singleShot(100, [&] {
            for (auto widget : QApplication::topLevelWidgets())
                  if (auto dialog = qobject_cast<QInputDialog *>(widget)) {
                        dialog->setTextValue("C · 鼠标复制");
                        copiedByMouse = true;
                        dialog->accept();
                        return;
                  }
      });
      QTest::mouseClick(dock->findChild<QPushButton *>("regionalCopy"), Qt::LeftButton);
      QVERIFY(copiedByMouse);
      QCOMPARE(RegionalVariants::state(s)["groups"].toList()[0].toMap()["variants"].toList().size(), 3);
      auto current = RegionalVariants::state(s)["groups"].toList()[0].toMap()["active"].toString();
      QVERIFY(current != copied["variant"].toString());
      bool renamedByMouse = false;
      QTimer::singleShot(100, [&] {
            for (auto widget : QApplication::topLevelWidgets())
                  if (auto dialog = qobject_cast<QInputDialog *>(widget)) {
                        dialog->setTextValue("C · 新名称");
                        renamedByMouse = true;
                        dialog->accept();
                        return;
                  }
      });
      QTest::mouseClick(dock->findChild<QPushButton *>("regionalRename"), Qt::LeftButton);
      QVERIFY(renamedByMouse);
      auto versions = RegionalVariants::state(s)["groups"].toList()[0].toMap()["variants"].toList();
      QCOMPARE(versions.back().toMap()["name"].toString(), QString("C · 新名称"));
      QTest::mouseClick(dock->findChild<QPushButton *>("regionalShowMain"), Qt::LeftButton);
      QCOMPARE(RegionalVariants::state(s)["groups"].toList()[0].toMap()["active"].toString(),
               copied["variant"].toString());
      QTest::mouseClick(dock->findChild<QPushButton *>("regionalAudition"), Qt::LeftButton);
      QVERIFY(!dock->findChild<QLabel *>("regionalStatus")->text().isEmpty());
      dock->hide();
}
void TestP1Native::regionalNotationFixturesAndCrossBoundary() {
      for (const auto name :
           {"readwriteundoreset/slurs.mscx", "tuplet/save-load.mscx", "split/split184061-keep-tie.mscx"}) {
            auto s = mscore->readScore(QString(TESTROOT) + "/mtest/libmscore/" + name);
            QVERIFY2(s, name);
            mscore->setCurrentScoreView(mscore->appendScore(s));
            QVariantList tracks;
            for (int t = 0; t < s->ntracks(); ++t)
                  tracks << t;
            const int end = s->lastMeasure()->endTick().ticks();
            auto contents = [&]() {
                  QVariantList values;
                  for (auto segment = s->firstMeasure()->first(SegmentType::ChordRest); segment;
                       segment = segment->next1(SegmentType::ChordRest))
                        for (int track = 0; track < s->ntracks(); ++track) {
                              auto e = segment->element(track);
                              if (!e || !e->isChordRest())
                                    continue;
                              auto cr = toChordRest(e);
                              values << QVariantMap{{"tick", segment->tick().ticks()},
                                                    {"track", track},
                                                    {"duration", cr->actualTicks().ticks()},
                                                    {"rest", e->isRest()}};
                              if (e->isChord())
                                    for (auto n : toChord(e)->notes())
                                          values << QVariantMap{{"pitch", n->pitch()},
                                                                {"tpc", n->tpc()},
                                                                {"tieFor", bool(n->tieFor())},
                                                                {"tieBack", bool(n->tieBack())}};
                        }
                  return values;
            };
            const auto before = contents();
            auto made = RegionalVariants::create(s, 0, end, tracks, name);
            QVERIFY2(made["ok"].toBool(), qPrintable(made["error"].toString()));
            auto group = made["group"].toString();
            auto b = RegionalVariants::duplicate(s, group, "B");
            QVERIFY(b["ok"].toBool());
            auto switched = RegionalVariants::select(s, group, b["variant"].toString());
            QVERIFY2(switched["ok"].toBool(), qPrintable(switched["error"].toString()));
            QCOMPARE(contents(), before);
            auto back = RegionalVariants::select(s, group, made["variant"].toString());
            QVERIFY2(back["ok"].toBool(), qPrintable(back["error"].toString()));
            QCOMPARE(contents(), before);
            auto cut = RegionalVariants::check(s, 0, s->firstMeasure()->endTick().ticks(), tracks);
            if (QString(name).startsWith("readwriteundoreset")) {
                  QVERIFY(cut.contains("error"));
                  QVERIFY(cut["expandEnd"].toInt() > s->firstMeasure()->endTick().ticks());
            }
      }
}
void TestP1Native::regionalResourcesLinkedPartsAndManualEvents() {
      auto s = mscore->readScore(QString(TESTROOT) + "/mtest/mscore/scoreobserver/piano.mscx");
      QVERIFY(s);
      mscore->setCurrentScoreView(mscore->appendScore(s));
      auto segment = s->firstMeasure()->first(SegmentType::ChordRest);
      Note *note = nullptr;
      for (int t = 0; t < s->ntracks() && !note; ++t) {
            auto e = segment->element(t);
            if (e && e->isChord())
                  note = toChord(e)->notes().front();
      }
      QVERIFY(note);
      int track = note->track();
      note->setVeloType(Note::ValueType::USER_VAL);
      note->setVeloOffset(77);
      note->chord()->setPlayEventType(PlayEventType::User);
      NoteEventList events;
      events.clear();
      events.append(NoteEvent(0, -15, 820));
      note->setPlayEvents(events);
      auto fermata = new Fermata(SymId::fermataAbove, s);
      fermata->setTrack(track);
      fermata->setParent(segment);
      s->startCmd();
      s->undoAddElement(fermata);
      s->endCmd();
      s->startCmd();
      s->insertMeasure(ElementType::VBOX, s->first());
      s->endCmd();
      s->startCmd();
      auto part = new Score(s);
      auto ex = new Excerpt(s);
      ex->setPartScore(part);
      ex->setParts({s->parts().front()});
      ex->setTitle("Piano linked");
      Excerpt::createExcerpt(ex);
      s->excerpts().append(ex);
      s->endCmd();
      auto image = new Image(s);
      QImage pixel(8, 8, QImage::Format_ARGB32);
      pixel.fill(Qt::darkGray);
      QByteArray png;
      QBuffer bytes(&png);
      bytes.open(QIODevice::WriteOnly);
      QVERIFY(pixel.save(&bytes, "PNG"));
      QVERIFY(image->loadFromData("regional.png", png));
      image->setParent(s->first());
      image->setTrack(0);
      s->first()->add(image);
      auto audio = new Audio();
      audio->setPath("input.wav");
      audio->setData("RIFF-regional-resource");
      s->setAudio(audio);
      QVariantList tracks;
      for (int t = 0; t < s->ntracks(); ++t)
            tracks << t;
      auto made = RegionalVariants::create(s, 0, s->lastMeasure()->endTick().ticks(), tracks, "全段");
      QVERIFY2(made["ok"].toBool(), qPrintable(made["error"].toString()));
      auto b = RegionalVariants::duplicate(s, made["group"].toString(), "B");
      QVERIFY(b["ok"].toBool());
      auto switched = RegionalVariants::select(s, made["group"].toString(), b["variant"].toString());
      QVERIFY2(switched["ok"].toBool(), qPrintable(switched["error"].toString()));
      auto n = toChord(s->firstMeasure()->first(SegmentType::ChordRest)->element(track))->notes().front();
      QCOMPARE(n->veloOffset(), 77);
      QCOMPARE(n->veloType(), Note::ValueType::USER_VAL);
      QCOMPARE(n->playEvents().front().ontime(), -15);
      QCOMPARE(n->playEvents().front().len(), 820);
      QCOMPARE(n->chord()->playEventType(), PlayEventType::User);
      QVERIFY(n->links());
      bool linkedPart = false;
      for (auto linked : *n->links())
            if (linked->isNote() && linked->score() == part) {
                  linkedPart = true;
                  QCOMPARE(toNote(linked)->veloOffset(), 77);
                  QCOMPARE(toNote(linked)->pitch(), n->pitch());
            }
      QVERIFY(linkedPart);
      int fermatas = 0;
      for (auto e : s->firstMeasure()->first(SegmentType::ChordRest)->annotations())
            if (e->isFermata())
                  ++fermatas;
      QCOMPARE(fermatas, 1);
      QByteArray archive;
      QBuffer output(&archive);
      output.open(QIODevice::WriteOnly);
      QVERIFY2(s->saveCompressedFile(&output, "regional.mscx", false, false), qPrintable(MScore::lastError));
      output.close();
      QBuffer input(&archive);
      input.open(QIODevice::ReadOnly);
      std::unique_ptr<MasterScore> restored(new MasterScore(s->style()));
      QVERIFY(restored->loadCompressedMsc(&input, true) == Score::FileError::FILE_NO_ERROR);
      QCOMPARE(restored->excerpts().size(), 1);
      QVERIFY(restored->audio());
      QCOMPARE(restored->audio()->data(), audio->data());
      bool found = false;
      for (auto e : restored->first()->el())
            if (e->isImage()) {
                  QVERIFY(toImage(e)->isValid());
                  found = true;
            }
      QVERIFY(found);
}
