// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include <QWidget>
#include <QPointer>
#include <QProcess>
#include <memory>
#include "libmscore/score.h"
class QDoubleSpinBox;class QToolButton;class QLabel;class QProgressBar;class QTemporaryDir;
namespace Ms {
class Score;
bool writeAudioPeak(Score*,const QString&);
class AudioCalibrationPanel final : public QWidget {
    Q_OBJECT
    QPointer<Score> owner;
    QPointer<Score> jobOwner;
    QToolButton *toggle,*measure,*cancel,*preview,*stop,*apply,*restore;
    QDoubleSpinBox* target;
    QLabel* message;QProgressBar* progress;
    QWidget* contents;
    QProcess process;
    std::unique_ptr<QTemporaryDir> files;
    QVariantMap result;
    QByteArray output;
    ScoreContentState revision;
    QByteArray fingerprint;
    QString renderer;
    float measuredGain=0;
    bool cancelled=false;
    void selectScore(Score*);
    void start();void complete(int);
    void updateResult();void saveResult();
    void previewChanged(bool);
    float proposedGain()const;
  public:
    explicit AudioCalibrationPanel(QWidget*,const QString& rendererProgram = QString());
    ~AudioCalibrationPanel()override;
};
}
