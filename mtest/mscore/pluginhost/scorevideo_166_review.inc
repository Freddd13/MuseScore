// GPL-2.0-or-later. Compare video pictures with the native score capture.
static QImage nativeVideoImage166(Score* score, const VideoPicture& picture,
                                 QRgb background = qRgba(0, 0, 0, 0))
{
    QImage image(picture.pixels, QImage::Format_ARGB32_Premultiplied);
    const int resolution = qRound(DPI * picture.rasterScale * 1000 / INCH);
    image.setDotsPerMeterX(resolution);
    image.setDotsPerMeterY(resolution);
    image.fill(background);
    QScopedValueRollback<qreal> ratio(MScore::pixelRatio, 1 / picture.rasterScale);
    QScopedValueRollback<bool> pdf(MScore::pdfPrinting, false);
    const bool wasPrinting = score->printing();
    auto page = score->tick2measure(Fraction::fromTicks(picture.firstTick))->system()->page();
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.scale(picture.rasterScale, picture.rasterScale);
    painter.translate(page->canvasPos() - picture.sourceRect.topLeft());
    // Native PNG export draws the page's complete element list. Use that
    // route here: PDF printing uses a different music-glyph rasterizer.
    score->setPrinting(true);
    auto elements = page->elements();
    std::stable_sort(elements.begin(), elements.end(), elementLessThan);
    for (auto element : elements) {
        if (!element->visible())
            continue;
        painter.save();
        painter.translate(element->pagePos());
        element->draw(&painter);
        painter.restore();
    }
    score->setPrinting(wasPrinting);
    return image;
}
static double videoImageDifference166(const QImage& a, const QImage& b)
{
    const auto left = a.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const auto right = b.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const int width = qMin(a.width(), b.width()), height = qMin(a.height(), b.height());
    qint64 different = 0;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            auto white = [](QRgb p, int channel) {
                const int value = channel == 0 ? qRed(p) : channel == 1 ? qGreen(p) : qBlue(p);
                return value + 255 - qAlpha(p);
            };
            for (int c = 0; c < 3; ++c)
                if (qAbs(white(left.pixel(x, y), c) - white(right.pixel(x, y), c)) > 16) {
                    ++different;
                    break;
                }
        }
    return double(different) / qMax(qint64(1), qint64(width) * height);
}
void TestPluginHost::scoreVideoFidelity166()
{
    const auto path = qEnvironmentVariable("SCORE_VIDEO_REVIEW_SCORE");
    if (path.isEmpty())
        QSKIP("Set SCORE_VIDEO_REVIEW_SCORE to a read-only source score.");
    const auto out = QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path()));
    preferences.setPreference(PREF_MIGRATION_DO_NOT_ASK_ME_AGAIN, true);
    auto score = Ms::mscore->readScore(path);
    QVERIFY2(score, qPrintable(path));
    auto sourceView = Ms::mscore->appendScore(score);
    Ms::mscore->setCurrentScoreView(sourceView);
    const auto before = score->state();
    const auto dirty = score->dirty();
    ScoreVideoDocument document;
    document.setScore(score);
    QVariantList staves;
    for (int i = 0; i < score->nstaves(); ++i)
        staves.append(i);
    document.prepare({{"staves", staves}, {"audioKind", "none"}, {"cursor", false}});
    QTRY_VERIFY_WITH_TIMEOUT(document.ready(), 60000);
    const auto& snapshot = document.snapshot();
    QVERIFY(!snapshot.pictures.isEmpty());
    QVariantList pictures;
    QList<int> selected{0, snapshot.pictures.size() / 2, snapshot.pictures.size() - 1};
    double maximumDifference = 0;
    for (int index : selected) {
        const auto& picture = snapshot.pictures[index];
        const QImage video(picture.file);
        QVERIFY(!video.isNull());
        const auto expected = nativeVideoImage166(document.preparedScore(), picture);
        const auto original = nativeVideoImage166(score, picture);
        const auto suffix = QString::number(index + 1);
        QVERIFY(video.save(out.filePath("video-line-" + suffix + ".png")));
        QVERIFY(expected.save(out.filePath("native-copy-line-" + suffix + ".png")));
        QVERIFY(original.save(out.filePath("native-source-line-" + suffix + ".png")));
        const double difference = videoImageDifference166(video, expected);
        const double sourceDifference = videoImageDifference166(video, original);
        maximumDifference = qMax(maximumDifference, qMax(difference, sourceDifference));
        for (const auto& item : {qMakePair(QString("video"), video),
                                 qMakePair(QString("source"), original)}) {
            QImage paper(item.second.size(), QImage::Format_ARGB32_Premultiplied);
            paper.fill(Qt::white);
            QPainter paint(&paper);
            paint.drawImage(QPoint(), item.second);
            paint.end();
            QVERIFY(paper.save(out.filePath(item.first + "-paper-" + suffix + ".png")));
        }
        pictures.append(QVariantMap{{"line", index + 1}, {"label", picture.label},
            {"scale", picture.rasterScale}, {"dpi", video.logicalDpiX()},
            {"nativeCopyDifference", difference}, {"sourceDifference", sourceDifference},
            {"width", video.width()}, {"height", video.height()}});
        qInfo() << "Native score / video" << index + 1 << difference << sourceDifference
                << picture.sourceRect << picture.rasterScale;
        int cue = -1;
        for (int i = 0; i < snapshot.cues.size(); ++i)
            if (snapshot.cues[i].picture == index) { cue = i; break; }
        QVERIFY(cue >= 0);
        VideoFrameCache cache;
        QVERIFY(scoreVideoFrame(snapshot, snapshot.cues[cue].from, &cache)
                    .save(out.filePath("video-frame-" + suffix + ".png")));
    }
    QFile report(out.filePath("native-video-comparison.json"));
    QVERIFY(report.open(QIODevice::WriteOnly));
    report.write(QJsonDocument::fromVariant(QVariantMap{{"source", path}, {"pictures", pictures},
        {"pictureCount", snapshot.pictures.size()}, {"cueCount", snapshot.cues.size()},
        {"preparation", document.state()}}).toJson(QJsonDocument::Indented));
    QVERIFY(maximumDifference < .002);
    PluginAPI::Score wrapper(score);
    PluginAPI::ScoreObserver reader;
    reader.setScore(&wrapper);
    const auto context = reader.arrangementState({{"firstMeasure", 1}, {"lastMeasure", 4}});
    const auto readImage = reader.arrangementImage({{"snapshotId", context["snapshotId"]},
                                                   {"scope", context["scope"]}});
    QVERIFY2(!readImage.contains("error"), qPrintable(readImage.value("error").toString()));
    QImage ai;
    QVERIFY(ai.loadFromData(QByteArray::fromBase64(readImage["pngBase64"].toString().toLatin1())));
    VideoPicture aiPicture;
    const auto rect = readImage["pageRect"].toMap();
    aiPicture.sourceRect = QRectF(rect["x"].toDouble(), rect["y"].toDouble(),
                                  rect["width"].toDouble(), rect["height"].toDouble());
    aiPicture.sourceRect.translate(score->pages()[readImage["page"].toInt()-1]->canvasPos());
    aiPicture.firstTick = context["scope"].toMap()["start"].toInt();
    aiPicture.pixels = ai.size();
    aiPicture.rasterScale = readImage["scale"].toDouble();
    const auto nativeAi = nativeVideoImage166(score, aiPicture, qRgb(255, 255, 255));
    const auto aiDifference = videoImageDifference166(ai, nativeAi);
    QVERIFY(ai.save(out.filePath("ai-native-score.png")));
    QVERIFY(nativeAi.save(out.filePath("ai-original-score.png")));
    QFile aiReport(out.filePath("ai-image-comparison.json"));
    QVERIFY(aiReport.open(QIODevice::WriteOnly));
    aiReport.write(QJsonDocument::fromVariant(QVariantMap{{"difference", aiDifference},
        {"image", readImage.value("pageRect")}, {"page", readImage.value("page")},
        {"scope", context.value("scope")}}).toJson(QJsonDocument::Indented));
    qInfo() << "AI native score / PNG" << aiDifference;
    QVERIFY2(aiDifference < .002, qPrintable(QString::number(aiDifference)));
    QVERIFY(score->state() == before);
    QCOMPARE(score->dirty(), dirty);
    // Different raster resolutions must still produce equal native staff sizes.
    VideoSnapshot geometry;
    geometry.settings = defaultScoreVideoSettings();
    VideoPicture lowRes, highRes;
    lowRes.pixels = QSize(1000, 400); lowRes.rasterScale = 1;
    lowRes.firstStaffX = 10; lowRes.firstStaffY = 50;
    highRes.pixels = QSize(2000, 800); highRes.rasterScale = 2;
    highRes.firstStaffX = 20; highRes.firstStaffY = 100;
    geometry.pictures = {lowRes, highRes};
    QCOMPARE(scoreVideoPictureRect(geometry, 0, QSize(1920, 1080)),
             scoreVideoPictureRect(geometry, 1, QSize(1920, 1080)));
}
