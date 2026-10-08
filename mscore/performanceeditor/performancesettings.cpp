// Copyright (C) 2026 Freddd13 and contributors; GPL version 2, see LICENCE.GPL.
#include "performancesettings.h"
#include <QSettings>
#include <QDialog>
#include <QDialogButtonBox>
#include <QColorDialog>
#include <QPushButton>
#include <QLabel>
#include <QSpinBox>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QMessageBox>
#include <algorithm>
namespace Ms {
PerformanceAppearance::PerformanceAppearance()
      {
      colors = {{QColor("#333333"), QColor("#464646"), QColor("#3b3b3b"), QColor("#626262"), QColor("#e5e5e5"), QColor("#ffffff"), QColor("#a4d39a"), QColor("#d2a874"), QColor("#99afba"), QColor("#b2a5bd")}};
      gradient = {{QColor("#8194a6"), QColor("#94a987"), QColor("#b3ad83"), QColor("#bc958c")}};
      voices = {{QColor("#819eaf"), QColor("#b69d86"), QColor("#96ac8f"), QColor("#a69bb5")}};
      staves = {{voices[0], voices[1], voices[2], voices[3], QColor("#b2ac87"), QColor("#8faeb0"), QColor("#b294a3"), QColor("#a0ad8e")}};
      }
QColor PerformanceAppearance::noteColor(int value, int track) const
      {
      if (mode == 1) return voices[track % 4];
      if (mode == 2) return staves[(track / 4) % 8];
      value = qBound(1, value, 127);
      for (int i = 1; i < 4; ++i) if (value <= stops[i]) {
            const double f = qBound(0.0, double(value - stops[i - 1]) / qMax(1, stops[i] - stops[i - 1]), 1.0);
            auto mix = [f](int a, int b) { return qRound(a + (b - a) * f); };
            return QColor(mix(gradient[i - 1].red(), gradient[i].red()), mix(gradient[i - 1].green(), gradient[i].green()), mix(gradient[i - 1].blue(), gradient[i].blue()));
            }
      return gradient.back();
      }
void PerformanceAppearance::load()
      {
      QSettings settings; settings.beginGroup("performanceEditor/appearance");
      auto read = [&settings](const QString& key, QColor& color) { const QColor candidate(settings.value(key, color.name()).toString()); if (candidate.isValid()) color = candidate; };
      for (int i = 0; i < RoleCount; ++i) read(QString("role%1").arg(i), colors[i]);
      for (int i = 0; i < 4; ++i) { read(QString("gradient%1").arg(i), gradient[i]); read(QString("voice%1").arg(i), voices[i]); }
      for (int i = 0; i < 8; ++i) read(QString("staff%1").arg(i), staves[i]);
      std::array<int, 4> candidate;
      for (int i = 0; i < 4; ++i) candidate[i] = settings.value(QString("stop%1").arg(i), stops[i]).toInt();
      if (candidate[0] == 1 && candidate[3] == 127 && candidate[0] < candidate[1] && candidate[1] < candidate[2] && candidate[2] < candidate[3]) stops = candidate;
      mode = qBound(0, settings.value("mode", 0).toInt(), 2);
      // Upgrade only the complete previous factory palette; retain every custom palette.
      const std::array<QColor, RoleCount> oldColors {{QColor("#26282b"), QColor("#303338"), QColor("#282b30"), QColor("#484d54"), QColor("#e0e4e9"), QColor("#ffffff"), QColor("#81e9ac"), QColor("#f0ad51"), QColor("#66badd"), QColor("#c49af0")}};
      const std::array<QColor, 4> oldGradient {{QColor("#4f80db"), QColor("#48bfa5"), QColor("#dab759"), QColor("#d66354")}};
      const std::array<QColor, 4> oldVoices {{QColor("#70a4df"), QColor("#db9564"), QColor("#6bc795"), QColor("#ba91de")}};
      const std::array<QColor, 8> oldStaves {{oldVoices[0], oldVoices[1], oldVoices[2], oldVoices[3], QColor("#ddba62"), QColor("#72c4c9"), QColor("#d783a9"), QColor("#97b576")}};
      if (colors == oldColors && gradient == oldGradient && voices == oldVoices && staves == oldStaves && stops == std::array<int, 4>{{1, 43, 85, 127}}) {
            const int savedMode = mode; *this = PerformanceAppearance(); mode = savedMode; settings.endGroup(); save();
            }

      }
void PerformanceAppearance::save() const
      {
      QSettings settings; settings.beginGroup("performanceEditor/appearance");
      for (int i = 0; i < RoleCount; ++i) settings.setValue(QString("role%1").arg(i), colors[i].name());
      for (int i = 0; i < 4; ++i) { settings.setValue(QString("gradient%1").arg(i), gradient[i].name()); settings.setValue(QString("voice%1").arg(i), voices[i].name()); settings.setValue(QString("stop%1").arg(i), stops[i]); }
      for (int i = 0; i < 8; ++i) settings.setValue(QString("staff%1").arg(i), staves[i].name());
      settings.setValue("mode", mode);
      }
bool PerformanceAppearance::edit(QWidget* parent)
      {
      PerformanceAppearance draft = *this;
      QDialog dialog(parent); dialog.setWindowTitle(QObject::tr("演奏编辑器配色")); dialog.resize(410, 570);
      auto outer = new QVBoxLayout(&dialog); auto scroll = new QScrollArea; scroll->setWidgetResizable(true); outer->addWidget(scroll);
      auto contents = new QWidget; auto grid = new QGridLayout(contents); scroll->setWidget(contents);
      QVector<QPair<QPushButton*, QColor*>> buttons;
      auto add = [&](const QString& label, QColor& color) {
            const int row = grid->rowCount(); grid->addWidget(new QLabel(label), row, 0);
            auto button = new QPushButton; grid->addWidget(button, row, 1); buttons.append({button, &color});
            QObject::connect(button, &QPushButton::clicked, &dialog, [button, &color, &dialog] {
                  const QColor selected = QColorDialog::getColor(color, &dialog); if (selected.isValid()) color = selected;
                  button->setText(color.name()); button->setStyleSheet(QString("background:%1; color:%2").arg(color.name(), color.lightness() > 140 ? "black" : "white"));
                  });
            return row;
            };
      const QStringList roles {QObject::tr("背景"), QObject::tr("白键行"), QObject::tr("黑键行"), QObject::tr("网格"), QObject::tr("文字"), QObject::tr("选择边框"), QObject::tr("播放线"), QObject::tr("预览边框"), QObject::tr("速度线"), QObject::tr("踏板")};
      for (int i = 0; i < RoleCount; ++i) add(roles[i], draft.colors[i]);
      std::array<QSpinBox*, 4> stopSpins;
      for (int i = 0; i < 4; ++i) {
            int row = add(QObject::tr("力度渐变 %1").arg(i + 1), draft.gradient[i]);
            stopSpins[i] = new QSpinBox; stopSpins[i]->setRange(1, 127); stopSpins[i]->setValue(draft.stops[i]); stopSpins[i]->setEnabled(i == 1 || i == 2); grid->addWidget(stopSpins[i], row, 2);
            }
      for (int i = 0; i < 4; ++i) add(QObject::tr("声部 %1").arg(i + 1), draft.voices[i]);
      for (int i = 0; i < 8; ++i) add(QObject::tr("谱表色 %1").arg(i + 1), draft.staves[i]);
      auto update = [&buttons] { for (const auto& pair : buttons) { pair.first->setText(pair.second->name()); pair.first->setStyleSheet(QString("background:%1; color:%2").arg(pair.second->name(), pair.second->lightness() > 140 ? "black" : "white")); } };
      update();
      auto box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults); outer->addWidget(box);
      QObject::connect(box->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, &dialog, [&] { draft = PerformanceAppearance(); for (int i = 0; i < 4; ++i) stopSpins[i]->setValue(draft.stops[i]); update(); });
      QObject::connect(box, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
      QObject::connect(box, &QDialogButtonBox::accepted, &dialog, [&] {
            if (stopSpins[1]->value() >= stopSpins[2]->value() || stopSpins[1]->value() <= 1 || stopSpins[2]->value() >= 127) { QMessageBox::information(&dialog, QObject::tr("力度渐变"), QObject::tr("渐变位置须按 1 < 中间值 < 127 递增。")); return; }
            for (int i = 0; i < 4; ++i) draft.stops[i] = stopSpins[i]->value(); dialog.accept();
            });
      if (dialog.exec() != QDialog::Accepted) return false;
      *this = draft; save(); return true;
      }
}
