namespace {
MasterScore *vstScore088(const QString &path) {
  MuseScore::updateUiStyleAndTheme();
  Shortcut::load();
  for (const char *key : {"undo", "redo"}) {
    auto shortcut = Shortcut::getShortcut(key);
    shortcut->setStandardKey(shortcut->standardKey());
  }
  QFile f(path);
  if (!f.open(QIODevice::WriteOnly))
    return nullptr;
  QByteArray xml(
      "<?xml version=\"1.0\"?><museScore version=\"3.02\"><Score><Division>480</Division>");
  for (int i = 1; i <= 2; ++i)
    xml += "<Part><Staff id=\"" + QByteArray::number(i) +
           "\"><StaffType group=\"pitched\"/></Staff><trackName>Piano " + QByteArray::number(i) +
           "</trackName><Instrument id=\"piano\"><Channel><program value=\"" +
           QByteArray::number(i == 1 ? 0 : 4) +
           "\"/><synti>Fluid</synti></Channel></Instrument></Part>";
  for (int i = 1; i <= 2; ++i)
    xml += "<Staff id=\"" + QByteArray::number(i) +
           "\"><Measure><voice><TimeSig><sigN>4</sigN><sigD>4</sigD></"
           "TimeSig><Chord><durationType>quarter</durationType><Note><pitch>60</pitch><tpc>14</"
           "tpc></Note></Chord><Rest><durationType>quarter</durationType></"
           "Rest><Rest><durationType>quarter</durationType></Rest><Rest><durationType>quarter</"
           "durationType></Rest></voice></Measure></Staff>";
  xml += "</Score></museScore>";
  f.write(xml);
  f.close();
  auto main = Ms::mscore;
  auto score = main->readScore(path);
  if (score)
    main->setCurrentScoreView(main->appendScore(score));
  return score;
}
bool selectFile088(const QString &path, const std::function<void()> &open) {
  bool picked = false;
  const bool old = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
  QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
  QTimer::singleShot(0, qApp, [&] {
    for (auto w : QApplication::topLevelWidgets())
      if (auto dialog = qobject_cast<QFileDialog *>(w))
        if (dialog->isVisible()) {
          dialog->setDirectory(QFileInfo(path).absolutePath());
          dialog->selectFile(path);
          picked = QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
        }
  });
  open();
  QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, old);
  return picked;
}
struct VstPanelCleanup088 {
  QPointer<QDockWidget> panel;
  ~VstPanelCleanup088() {
    if (panel) {
      delete panel.data();
    }
    restoreVstForScore(nullptr);
  }
};
QString effectFile088() {
  return qEnvironmentVariable(
      "P2_TEST_EFFECT_DLL",
      "E:/programming/funcodes/muse3_plugins/P2/out/vst-checks/Release/KumoTestEffect.dll");
}
double peak088(MasterScore *score) {
  QBuffer data;
  if (!Ms::mscore->saveAudio(score, &data))
    return -1;
  double peak = 0;
  for (int i = 0; i + 4 <= data.data().size(); i += 4) {
    float value;
    std::memcpy(&value, data.data().constData() + i, 4);
    if (!std::isfinite(value))
      return -1;
    peak = qMax(peak, qAbs(double(value)));
  }
  return peak;
}
} // namespace
void TestPluginHost::vstRestoreFollowsSelectedInstrument() {
#ifdef KUMO_VST_HOST
  auto main = Ms::mscore;
  if (auto old = main->findChild<QDockWidget *>("kumoVstPanel"))
    delete old;
  auto score = vstScore088(_settings.filePath("vst-restore-088.mscx"));
  QVERIFY(score);
  main->resize(1100, 800);
  main->show();
  std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());
  synth->setSampleRate(44100);
  QScopedValueRollback<MasterSynthesizer *> current(Ms::synti, synth.get());
  QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
  QSettings().remove("kumo/vst/catalog");
  auto first = const_cast<Channel *>(score->midiMapping()[0].articulation()),
       second = const_cast<Channel *>(score->midiMapping()[1].articulation());
  QVERIFY(first != second);
  showVstPanel(first);
  auto panel = main->findChild<QDockWidget *>("kumoVstPanel");
  VstPanelCleanup088 cleanup{panel};
  QVERIFY(panel);
  auto catalog = panel->findChild<QComboBox *>("kumoVstCatalog");
  auto scan = panel->findChild<QPushButton *>("kumoVstScan");
  QVERIFY(catalog && scan);
  QVERIFY(!catalog->isEnabled());
  QVERIFY(selectFile088(effectFile088(), [&] { QTest::mouseClick(scan, Qt::LeftButton); }));
  QTRY_VERIFY_WITH_TIMEOUT(catalog->isEnabled(), 10000);
  auto add = panel->findChild<QPushButton *>("kumoVstAddEffect");
  QTRY_VERIFY(add->isEnabled());
  QTest::mouseClick(add, Qt::LeftButton);
  QCOMPARE(first->synti(), QString("KumoVST"));
  auto remove = panel->findChild<QPushButton *>("kumoVstRemoveSource"),
       restore = panel->findChild<QPushButton *>("kumoVstRestoreSource");
  QTest::mouseClick(remove, Qt::LeftButton);
  QCOMPARE(first->synti(), QString("Fluid"));
  QVERIFY(restore->isEnabled());
  auto parts = panel->findChild<QComboBox *>("kumoVstParts");
  parts->setCurrentIndex(1);
  QVERIFY2(!restore->isEnabled(), "恢复按钮对应当前乐器，切到其它乐器时显示其自己的状态");
  parts->setCurrentIndex(0);
  QTRY_VERIFY(restore->isEnabled());
  QTest::mouseClick(restore, Qt::LeftButton);
  QCOMPARE(first->synti(), QString("KumoVST"));
  QCOMPARE(second->synti(), QString("Fluid"));
  parts->setCurrentIndex(1);
  QTest::mouseClick(add, Qt::LeftButton);
  QCOMPARE(second->synti(), QString("KumoVST"));
  QTest::mouseClick(remove, Qt::LeftButton);
  QCOMPARE(second->synti(), QString("Fluid"));
  QTRY_VERIFY(restore->isEnabled());
  parts->setCurrentIndex(0);
  QTest::mouseClick(remove, Qt::LeftButton);
  QCOMPARE(first->synti(), QString("Fluid"));
  parts->setCurrentIndex(1);
  QTest::mouseClick(restore, Qt::LeftButton);
  QCOMPARE(second->synti(), QString("KumoVST"));
  parts->setCurrentIndex(0);
  QTRY_VERIFY(restore->isEnabled());
  QTest::mouseClick(restore, Qt::LeftButton);
  QCOMPARE(first->synti(), QString("KumoVST"));
  QVERIFY(first->bank() != second->bank());
#else
  QSKIP("Windows VST host");
#endif
}
void TestPluginHost::vstEffectsHaveOwnPresets() {
#ifdef KUMO_VST_HOST
  auto main = Ms::mscore;
  if (auto old = main->findChild<QDockWidget *>("kumoVstPanel"))
    delete old;
  auto score = vstScore088(_settings.filePath("vst-effects-088.mscx"));
  QVERIFY(score);
  main->show();
  std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());
  synth->setSampleRate(44100);
  QScopedValueRollback<MasterSynthesizer *> current(Ms::synti, synth.get());
  QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
  QSettings().remove("kumo/vst/catalog");
  showVstPanel();
  auto panel = main->findChild<QDockWidget *>("kumoVstPanel");
  VstPanelCleanup088 cleanup{panel};
  QVERIFY(panel);
  auto scan = panel->findChild<QPushButton *>("kumoVstScan");
  auto catalog = panel->findChild<QComboBox *>("kumoVstCatalog");
  QVERIFY(selectFile088(effectFile088(), [&] { QTest::mouseClick(scan, Qt::LeftButton); }));
  QTRY_VERIFY_WITH_TIMEOUT(catalog->isEnabled(), 10000);
  QTest::mouseClick(panel->findChild<QPushButton *>("kumoVstAddEffect"), Qt::LeftButton);
  auto list = panel->findChild<QListWidget *>();
  QVERIFY(list);
  QTRY_COMPARE(list->count(), 1);
  list->setCurrentRow(0);
  auto save = panel->findChild<QPushButton *>("kumoVstEffectSavePreset"),
       load = panel->findChild<QPushButton *>("kumoVstEffectLoadPreset");
  QVERIFY2(save && load, "选中效果的载入与保存预设入口应放在效果链旁");
  QVERIFY(save->isEnabled() && load->isEnabled());
  auto channel = const_cast<Channel *>(score->midiMapping()[0].articulation());
  auto engine = static_cast<VstSynth *>(synth->synthesizer("KumoVST"));
  QVERIFY(engine);
  auto plugin = engine->rack().plugin(channel->bank(), 0);
  QVERIFY(plugin);
  const auto preset = _settings.filePath("gain-088.json");
  QVERIFY(selectFile088(preset, [&] { QTest::mouseClick(save, Qt::LeftButton); }));
  QFile saved(preset);
  QVERIFY(saved.open(QIODevice::ReadOnly));
  auto wrapper = QJsonDocument::fromJson(saved.readAll()).toVariant().toMap();
  QCOMPARE(wrapper["schema"].toString(), QString("kumo-vst-preset/1"));
  QVERIFY(!wrapper["plugin"].toMap()["uid"].toString().isEmpty());
  QTest::mouseClick(panel->findChild<QPushButton *>("kumoVstOpenEffect"), Qt::LeftButton);
  QPointer<QDialog> dialog = panel->findChild<QDialog *>("kumoVstParameters");
  QVERIFY(dialog && dialog->isVisible());
  auto amount = dialog->findChild<QDoubleSpinBox *>("kumoVstParameterValue");
  QVERIFY(amount);
  auto line = amount->findChild<QLineEdit *>();
  QVERIFY(line);
  QTest::keyClick(line, Qt::Key_A, Qt::ControlModifier);
  QTest::keyClicks(line, "25");
  QTest::keyClick(line, Qt::Key_Return);
  QTRY_VERIFY(
      qAbs(engine->rack().plugin(channel->bank(), 0)->parameters()[0].toMap()["value"].toDouble() -
           .25) < .0001);
  const int before = score->undoStack()->getCurIdx();
  QVERIFY(selectFile088(preset, [&] { QTest::mouseClick(load, Qt::LeftButton); }));
  QCOMPARE(score->undoStack()->getCurIdx(), before + 1);
  QVERIFY(dialog && dialog->isVisible());
  QCOMPARE(engine->rack().plugin(channel->bank(), 0), plugin);
  QVERIFY(qAbs(plugin->parameters()[0].toMap()["value"].toDouble() - .5) < .0001);
  auto position = dialog->findChild<QSlider *>("kumoVstParameterPosition");
  const int beforeDrag = score->undoStack()->getCurIdx();
  QStyleOptionSlider option;
  option.initFrom(position);
  option.orientation = Qt::Horizontal;
  option.minimum = position->minimum();
  option.maximum = position->maximum();
  option.sliderPosition = position->value();
  option.sliderValue = position->value();
  const auto handle = position->style()->subControlRect(QStyle::CC_Slider, &option,
                                                        QStyle::SC_SliderHandle, position);
  const auto target = handle.center() + QPoint(-40, 0);
  QTest::mousePress(position, Qt::LeftButton, Qt::NoModifier, handle.center());
  QMouseEvent move(QEvent::MouseMove, target, position->mapToGlobal(target), Qt::NoButton,
                   Qt::LeftButton, Qt::NoModifier);
  QApplication::sendEvent(position, &move);
  QTest::mouseRelease(position, Qt::LeftButton, Qt::NoModifier, target);
  QCOMPARE(score->undoStack()->getCurIdx(), beforeDrag + 1);
  QVERIFY(engine->rack().plugin(channel->bank(), 0)->parameters()[0].toMap()["value"].toDouble() <
          .5);
  position->setFocus();
  QTest::keyClick(position, Qt::Key_Z, Qt::ControlModifier);
  QTRY_COMPARE(score->undoStack()->getCurIdx(), beforeDrag);
  position->setFocus();
  QTest::keyClick(position, Qt::Key_Z, Qt::ControlModifier);
  QTRY_COMPARE(score->undoStack()->getCurIdx(), before);
  QVERIFY(dialog && dialog->isVisible());
  QVERIFY(
      qAbs(engine->rack().plugin(channel->bank(), 0)->parameters()[0].toMap()["value"].toDouble() -
           .25) < .0001);
  QTest::keyClick(position, Qt::Key_Y, Qt::ControlModifier);
  QTRY_COMPARE(score->undoStack()->getCurIdx(), before + 1);
  auto invalid = wrapper;
  auto identity = invalid["plugin"].toMap();
  identity["uid"] = "another-plugin";
  invalid["plugin"] = identity;
  const auto wrong = _settings.filePath("wrong-effect-088.json");
  QFile f(wrong);
  QVERIFY(f.open(QIODevice::WriteOnly));
  f.write(QJsonDocument::fromVariant(invalid).toJson());
  f.close();
  const int unchanged = score->undoStack()->getCurIdx();
  QVERIFY(selectFile088(wrong, [&] { QTest::mouseClick(load, Qt::LeftButton); }));
  QCOMPARE(score->undoStack()->getCurIdx(), unchanged);
  QVERIFY(
      qAbs(engine->rack().plugin(channel->bank(), 0)->parameters()[0].toMap()["value"].toDouble() -
           .5) < .0001);
  QTest::mouseClick(panel->findChild<QPushButton *>("kumoVstAddEffect"), Qt::LeftButton);
  QTRY_COMPARE(list->count(), 2);
  QCOMPARE(list->currentRow(), 1);
  auto key = engine->rack().effects(channel->bank())[1].toMap()["key"].toString();
  QTest::mouseClick(panel->findChild<QPushButton *>("kumoVstEffectUp"), Qt::LeftButton);
  QCOMPARE(list->currentRow(), 0);
  QCOMPARE(engine->rack().effects(channel->bank())[0].toMap()["key"].toString(), key);
  QVERIFY(dialog && dialog->isVisible());
  list->item(0)->setCheckState(Qt::Unchecked);
  QVERIFY(engine->rack().effects(channel->bank())[0].toMap()["bypass"].toBool());
  QTest::mouseClick(panel->findChild<QPushButton *>("kumoVstRemoveEffect"), Qt::LeftButton);
  QTRY_COMPARE(list->count(), 1);
  QVERIFY(dialog && dialog->isVisible());
  list->setCurrentRow(0);
  QFileInfo scoreFile(_settings.filePath("vst-effect-saved-088.mscz"));
  QVERIFY(score->saveCompressedFile(scoreFile, false, false));
  std::unique_ptr<MasterScore> reopened(main->readScore(scoreFile.absoluteFilePath()));
  QVERIFY(reopened);
  QCOMPARE(reopened->synthesizerState().group("KumoVST").front().data,
           score->synthesizerState().group("KumoVST").front().data);
  QVERIFY(peak088(score) > .001);
  const auto output = QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path()));
  panel->setFloating(true);
  panel->resize(560, 760);
  QTest::qWait(80);
  panel->grab().save(output.filePath("vst-effects-088.png"));
  if (dialog)
    dialog->grab().save(output.filePath("vst-parameters-088.png"));
  const int beforeDelete = score->undoStack()->getCurIdx();
  QTest::mouseClick(panel->findChild<QPushButton *>("kumoVstRemoveEffect"), Qt::LeftButton);
  QTRY_COMPARE(list->count(), 0);
  QTRY_VERIFY(!dialog || !dialog->isVisible());
  QCOMPARE(channel->synti(), QString("Fluid"));
  QCOMPARE(score->undoStack()->getCurIdx(), beforeDelete + 1);
  EditData edit;
  score->undoRedo(true, &edit);
  QTRY_COMPARE(list->count(), 1);
  QCOMPARE(channel->synti(), QString("KumoVST"));
  QVERIFY(peak088(score) > .001);
  score->undoRedo(false, &edit);
  QTRY_COMPARE(list->count(), 0);
  QCOMPARE(channel->synti(), QString("Fluid"));
  QVERIFY(peak088(score) > .001);
#else
  QSKIP("Windows VST host");
#endif
}

void TestPluginHost::vstInstrumentFirstScanAndRestoreSound() {
#ifdef KUMO_VST_HOST
  const auto path = qEnvironmentVariable(
      "P2_TEST_PIANOTEQ", "C:/Program Files/Common Files/VST3/Pianoteq 9/Pianoteq 9.vst3");
  QVERIFY(QFileInfo::exists(path));
  auto main = Ms::mscore;
  if (auto old = main->findChild<QDockWidget *>("kumoVstPanel"))
    delete old;
  auto score = vstScore088(_settings.filePath("vst-instrument-088.mscx"));
  QVERIFY(score);
  main->show();
  std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());
  synth->setSampleRate(44100);
  synth->setEffect(0, 0);
  synth->setEffect(1, 0);
  QScopedValueRollback<MasterSynthesizer *> current(Ms::synti, synth.get());
  QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
  QSettings().remove("kumo/vst/catalog");
  const double originalPeak = peak088(score);
  QVERIFY(originalPeak > .001);
  const auto beforeMidi = _settings.filePath("vst-original-088.mid");
  QVERIFY(main->saveMidi(score, beforeMidi));
  auto channel = const_cast<Channel *>(score->midiMapping()[0].articulation());
  const int bank = channel->bank(), program = channel->program();
  showVstPanel(channel);
  auto panel = main->findChild<QDockWidget *>("kumoVstPanel");
  VstPanelCleanup088 cleanup{panel};
  auto catalog = panel->findChild<QComboBox *>("kumoVstCatalog");
  QVERIFY(!catalog->isEnabled());
  QVERIFY(selectFile088(path, [&] {
    QTest::mouseClick(panel->findChild<QPushButton *>("kumoVstScan"), Qt::LeftButton);
  }));
  QTRY_VERIFY_WITH_TIMEOUT(catalog->isEnabled(), 20000);
  QVERIFY(catalog->currentData(Qt::UserRole + 1).toMap()["instrument"].toBool());
  QTest::mouseClick(panel->findChild<QPushButton *>("kumoVstChoose"), Qt::LeftButton);
  QCOMPARE(channel->synti(), QString("KumoVST"));
  auto engine = static_cast<VstSynth *>(synth->synthesizer("KumoVST"));
  QVERIFY(engine);
  auto plugin = engine->rack().plugin(channel->bank());
  QVERIFY(plugin);
  QVERIFY(peak088(score) > .001);
  QTest::mouseClick(panel->findChild<QPushButton *>("kumoVstOpenSource"), Qt::LeftButton);
  QTRY_VERIFY(engine->rack().hasOpenEditors());
  const auto preset = _settings.filePath("pianoteq-088.json");
  QVERIFY(selectFile088(preset, [&] {
    QTest::mouseClick(panel->findChild<QPushButton *>("kumoVstSourceSavePreset"), Qt::LeftButton);
  }));
  QVERIFY(selectFile088(preset, [&] {
    QTest::mouseClick(panel->findChild<QPushButton *>("kumoVstSourceLoadPreset"), Qt::LeftButton);
  }));
  QCOMPARE(engine->rack().plugin(channel->bank()), plugin);
  QVERIFY(engine->rack().hasOpenEditors());
  QFileInfo file(_settings.filePath("pianoteq-088.mscz"));
  QVERIFY(score->saveCompressedFile(file, false, false));
  std::unique_ptr<MasterScore> reopened(main->readScore(file.absoluteFilePath()));
  QVERIFY(reopened);
  QCOMPARE(reopened->midiMapping()[0].articulation()->synti(), QString("KumoVST"));
  QTest::mouseClick(panel->findChild<QPushButton *>("kumoVstRemoveSource"), Qt::LeftButton);
  QCOMPARE(channel->synti(), QString("Fluid"));
  QCOMPARE(channel->bank(), bank);
  QCOMPARE(channel->program(), program);
  const double restoredPeak = peak088(score);
  QVERIFY(restoredPeak > .001);
  qInfo() << "SF2 before/after peak" << originalPeak << restoredPeak;
  QVERIFY(qAbs(originalPeak - restoredPeak) < qMax(.001, originalPeak * .1));
  const auto afterMidi = _settings.filePath("vst-restored-088.mid");
  QVERIFY(main->saveMidi(score, afterMidi));
  QFile a(beforeMidi), b(afterMidi);
  QVERIFY(a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly));
  QCOMPARE(a.readAll(), b.readAll());
  QTest::mouseClick(panel->findChild<QPushButton *>("kumoVstRestoreSource"), Qt::LeftButton);
  QCOMPARE(channel->synti(), QString("KumoVST"));
  QVERIFY(peak088(score) > .001);
#else
  QSKIP("Windows VST host");
#endif
}
void TestPluginHost::vstMasterEffectsStayOutOfPartList() {
#ifdef KUMO_VST_HOST
  auto main = Ms::mscore;
  if (auto old = main->findChild<QDockWidget *>("kumoVstPanel"))
    delete old;
  auto score = vstScore088(_settings.filePath("vst-master-088.mscx"));
  QVERIFY(score);
  main->show();
  std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());
  synth->setSampleRate(44100);
  QScopedValueRollback<MasterSynthesizer *> current(Ms::synti, synth.get());
  QSettings().remove("kumo/vst/catalog");
  showVstPanel();
  auto panel = main->findChild<QDockWidget *>("kumoVstPanel");
  VstPanelCleanup088 cleanup{panel};
  auto catalog = panel->findChild<QComboBox *>("kumoVstCatalog");
  QVERIFY(selectFile088(effectFile088(), [&] {
    QTest::mouseClick(panel->findChild<QPushButton *>("kumoVstScan"), Qt::LeftButton);
  }));
  QTRY_VERIFY_WITH_TIMEOUT(catalog->isEnabled(), 10000);
  auto target = panel->findChild<QComboBox *>("kumoVstEffectTarget");
  auto list = panel->findChild<QListWidget *>("kumoVstEffectChain");
  QVERIFY(target && list);
  target->setCurrentIndex(1);
  QTest::mouseClick(panel->findChild<QPushButton *>("kumoVstAddEffect"), Qt::LeftButton);
  QTRY_COMPARE(list->count(), 1);
  target->setCurrentIndex(0);
  QCOMPARE(list->count(), 0);
  target->setCurrentIndex(1);
  QCOMPARE(list->count(), 1);
#else
  QSKIP("Windows VST host");
#endif
}
void TestPluginHost::vstParameterStartsWithReadableValue() {
#ifdef KUMO_VST_HOST
  auto main = Ms::mscore;
  if (auto old = main->findChild<QDockWidget *>("kumoVstPanel"))
    delete old;
  auto score = vstScore088(_settings.filePath("vst-parameters-088.mscx"));
  QVERIFY(score);
  main->show();
  std::unique_ptr<MasterSynthesizer> synth(synthesizerFactory());
  synth->setSampleRate(44100);
  QScopedValueRollback<MasterSynthesizer *> current(Ms::synti, synth.get());
  QVERIFY(synth->synthesizer("Fluid")->loadSoundFonts({qEnvironmentVariable("P0_TEST_SOUNDFONT")}));
  QSettings().remove("kumo/vst/catalog");
  showVstPanel();
  auto panel = main->findChild<QDockWidget *>("kumoVstPanel");
  VstPanelCleanup088 cleanup{panel};
  auto catalog = panel->findChild<QComboBox *>("kumoVstCatalog");
  QVERIFY(selectFile088(effectFile088(), [&] {
    QTest::mouseClick(panel->findChild<QPushButton *>("kumoVstScan"), Qt::LeftButton);
  }));
  QTRY_VERIFY_WITH_TIMEOUT(catalog->isEnabled(), 10000);
  QTest::mouseClick(panel->findChild<QPushButton *>("kumoVstAddEffect"), Qt::LeftButton);
  auto list = panel->findChild<QListWidget *>();
  QTRY_COMPARE(list->count(), 1);
  list->setCurrentRow(0);
  for (auto b : panel->findChildren<QPushButton *>())
    if (b->text() == QString::fromUtf8("打开"))
      QTest::mouseClick(b, Qt::LeftButton);
  auto dialog = panel->findChild<QDialog *>("kumoVstParameters");
  QVERIFY(dialog && dialog->isVisible());
  auto table = dialog->findChild<QTreeWidget *>();
  QVERIFY(table);
  QVERIFY2(table->currentItem(), "打开参数后直接选中一个可调参数");
  QVERIFY2(!table->currentItem()->text(1).isEmpty(), "插件未提供文字时也显示当前参数值");
  auto filter = dialog->findChild<QLineEdit *>("kumoVstParameterFilter");
  auto slider = dialog->findChild<QSlider *>("kumoVstParameterPosition");
  auto amount = dialog->findChild<QDoubleSpinBox *>("kumoVstParameterValue");
  filter->setText("No such parameter");
  QVERIFY(!slider->isEnabled() && !amount->isEnabled());
  refreshVstParameterWindows();
  QVERIFY(!slider->isEnabled() && !amount->isEnabled());
  filter->clear();
  QVERIFY(slider->isEnabled() && amount->isEnabled());
  QVERIFY(table->currentItem());
  const auto original = preferences.preferredGlobalStyle();
  struct RestoreTheme {
    MuseScorePreferredStyleType style;
    ~RestoreTheme() {
      preferences.setTemporaryPreference(PREF_UI_APP_GLOBALSTYLE, QVariant::fromValue(style));
      MuseScore::updateUiStyleAndTheme();
    }
  } restoreTheme{original};
  preferences.setTemporaryPreference(PREF_UI_APP_GLOBALSTYLE,
                                     QVariant::fromValue(MuseScorePreferredStyleType::DARK_FUSION));
  MuseScore::updateUiStyleAndTheme();
  panel->setFloating(true);
  panel->resize(280, 680);
  dialog->resize(360, 400);
  QTest::qWait(80);
  const auto output = QDir(qEnvironmentVariable("HARMONY_TEST_ARTIFACTS", _settings.path()));
  panel->grab().save(output.filePath("vst-narrow-088.png"));
  dialog->grab().save(output.filePath("vst-parameters-dark-088.png"));
  for (auto button : panel->findChildren<QPushButton *>())
    if (button->isVisible())
      QVERIFY2(button->width() <= button->sizeHint().width() + 1, qPrintable(button->text()));
  auto full = dialog->findChild<QToolButton *>("kumoVstParametersFullscreen");
  QVERIFY(full);
  const auto normal = dialog->size();
  QTest::mouseClick(full, Qt::LeftButton);
  QTRY_VERIFY(dialog->isFullScreen());
  QTest::mouseClick(full, Qt::LeftButton);
  QTRY_VERIFY(!dialog->isFullScreen());
  QCOMPARE(dialog->size(), normal);
#else
  QSKIP("Windows VST host");
#endif
}
void TestPluginHost::vstInstrumentListFollowsScoreEdits() {
#ifdef KUMO_VST_HOST
  auto main = Ms::mscore;
  if (auto old = main->findChild<QDockWidget *>("kumoVstPanel"))
    delete old;
  auto score = vstScore088(_settings.filePath("vst-parts-088.mscx"));
  QVERIFY(score);
  main->show();
  showVstPanel();
  auto panel = main->findChild<QDockWidget *>("kumoVstPanel");
  VstPanelCleanup088 cleanup{panel};
  auto parts = panel->findChild<QComboBox *>("kumoVstParts");
  QCOMPARE(parts->count(), 2);
  parts->setCurrentIndex(1);
  score->parts().at(1)->setPartName("Second piano");
  QTRY_VERIFY(parts->currentText().startsWith("Second piano"));
  score->startCmd();
  score->cmdRemovePart(score->parts().at(1));
  score->setInstrumentsChanged(true);
  score->endCmd();
  score->rebuildMidiMapping();
  QTRY_COMPARE(parts->count(), 1);
  QCOMPARE(parts->currentIndex(), 0);
  EditData edit;
  score->undoRedo(true, &edit);
  score->rebuildMidiMapping();
  QTRY_COMPARE(parts->count(), 2);
  const auto other = vstScore088(_settings.filePath("vst-other-088.mscx"));
  QVERIFY(other);
  QTRY_COMPARE(parts->count(), 2);
  QVERIFY(!panel->findChild<QPushButton *>("kumoVstRestoreSource")->isEnabled());
#else
  QSKIP("Windows VST host");
#endif
}
