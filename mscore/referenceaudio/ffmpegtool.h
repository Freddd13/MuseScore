// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace KumoAudio {

inline QString ffmpegExecutable()
{
    const auto configured = QSettings().value("scoreVideo/ffmpeg").toString();
    if (!configured.isEmpty()) {
        const QFileInfo file(configured);
        if (file.isFile() && file.isExecutable())
            return file.absoluteFilePath();
    }
    const QDir directory(QCoreApplication::applicationDirPath());
    for (const auto& name : {"tools/video/ffmpeg.exe", "ffmpeg.exe", "tools/video/ffmpeg", "ffmpeg"}) {
        const QFileInfo file(directory.filePath(name));
        if (file.isFile() && file.isExecutable())
            return file.absoluteFilePath();
    }
    return QStandardPaths::findExecutable("ffmpeg");
}

} // namespace KumoAudio
