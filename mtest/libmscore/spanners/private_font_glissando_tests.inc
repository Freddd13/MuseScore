// Exercise external metadata, missing-primary fallback, copies and actual ink.
void TestFontEngraving::privateFontWavyGlissando_data()
      {
      QTest::addColumn<QString>("glyph");
      QTest::addColumn<bool>("accepted");
      QTest::newRow("absent") << QString() << false;
      QTest::newRow("unknown") << QString("notARealGlyph") << false;
      QTest::newRow("missing-primary") << QString("wiggleGlissando") << false;
      QTest::newRow("empty-ink") << QString("space") << false;
      QTest::newRow("valid") << QString("wiggleArpeggiatoUp") << true;
      }

void TestFontEngraving::privateFontWavyGlissando()
      {
      QFETCH(QString, glyph);
      QFETCH(bool, accepted);
      QTemporaryDir directory;
      QVERIFY(directory.isValid());
      const QString originalPath = preferences.getString(PREF_APP_PATHS_MYSCOREFONTS);
      const auto restoreFonts = qScopeGuard([&]() { ScoreFont::scanUserFonts(originalPath); });
      ScoreFont* builtin = ScoreFont::fontFactory("Leland");
      QCOMPARE(builtin->wavyGlissandoSym(), SymId::noSym);
      QVERIFY(!builtin->isValid(SymId::wiggleGlissando));
      const QString name("Private Glissando Test");
      const QString path = directory.path() + "/" + name;
      QVERIFY(QDir().mkpath(path));
      QVERIFY(QFile::copy(QDir(builtin->fontPath()).filePath("Leland.otf"), path + "/" + name + ".otf"));
      QJsonObject metadata;
      metadata.insert("fontName", name);
      if (!glyph.isNull())
            metadata.insert("kumoEngraving", QJsonObject{{"wavyGlissandoGlyph", glyph}});
      QFile json(path + "/" + name + ".json");
      QVERIFY(json.open(QIODevice::WriteOnly));
      json.write(QJsonDocument(metadata).toJson());
      json.close();
      ScoreFont::scanUserFonts(directory.path());
      ScoreFont* font = ScoreFont::fontFactory(name);
      QVERIFY(font);
      QCOMPARE(font->name(), name);
      QCOMPARE(font->wavyGlissandoSym(), accepted ? SymId::wiggleArpeggiatoUp : SymId::noSym);
      ScoreFont copy(*font);
      QCOMPARE(copy.wavyGlissandoSym(), font->wavyGlissandoSym());
      if (!accepted)
            return;

      std::unique_ptr<MasterScore> score(Ms::mscore->readScore(QString(TESTROOT) + "/mtest/libmscore/spanners/glissando01.mscx"));
      QVERIFY(score);
      score->setStyleValue(Sid::musicalSymbolFont, name);
      score->setScoreFont(font);
      QCOMPARE(score->scoreFont()->wavyGlissandoSym(), SymId::wiggleArpeggiatoUp);
      Glissando gliss(score.get());
      gliss.setTrack(0);
      gliss.setTick(Fraction(0, 1));
      gliss.setTicks(Fraction(1, 4));
      gliss.setGlissandoType(GlissandoType::WAVY);
      gliss.setShowText(false);
      GlissandoSegment segment(&gliss, score.get());
      segment.setTrack(0);
      const qreal sp = score->spatium();
      const QList<QPointF> spans{{12.0, 0.0}, {7.0, -6.0}, {8.0, 10.0}, {.15, .05}, {-3.0, 1.0}, {0.0, 0.0}};
      for (const QPointF& span : spans) {
            segment.setPos2(span * sp);
            segment.layout();
            QImage image(1600, 1600, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            QPainter painter(&image);
            painter.translate(600.0, 600.0);
            painter.scale(2.0, 2.0);
            const QTransform transform = painter.worldTransform();
            segment.draw(&painter);
            painter.end();
            QRect ink;
            for (int y = 0; y < image.height(); ++y) {
                  const QRgb* pixels = reinterpret_cast<const QRgb*>(image.constScanLine(y));
                  for (int x = 0; x < image.width(); ++x) {
                        if (qAlpha(pixels[x]))
                              ink = ink.united(QRect(x, y, 1, 1));
                        }
                  }
            if (span.isNull()) {
                  QVERIFY(ink.isNull());
                  continue;
                  }
            QVERIFY2(!ink.isNull(), qPrintable(QString("No ink for %1,%2").arg(span.x()).arg(span.y())));
            const QRectF expected = transform.mapRect(segment.bbox()).adjusted(-2.0, -2.0, 2.0, 2.0);
            QVERIFY2(expected.contains(QRectF(ink)), qPrintable(QString("Ink outside bbox for %1,%2").arg(span.x()).arg(span.y())));
            }
      }
