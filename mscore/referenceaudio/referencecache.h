// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QDateTime>
#include <QMutex>
#include <QMutexLocker>
#include <algorithm>

namespace KumoAudio {

inline QString referenceCacheDirectory()
{
#ifdef Q_OS_WIN
    if (QDir("E:/").exists())
        return "E:/MuseScoreCache";
#endif
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
}

inline void pruneReferenceCache(const QString& root, qint64 limit, const QString& keep = {})
{
    // Only hashed data from these two reference tools participates in the shared budget.
    static QMutex eviction;
    QMutexLocker lock(&eviction);
    const QDir parent(root);
    QFileInfoList files;
    auto add = [&](const QString& subdir, const QString& glob) {
        const auto candidates = QDir(parent.filePath(subdir))
                                    .entryInfoList({glob}, QDir::Files | QDir::NoSymLinks);
        for (const auto& candidate : candidates) {
            const auto hash = candidate.completeBaseName();
            if (hash.size() != 64 || std::any_of(hash.cbegin(), hash.cend(), [](QChar c) {
                    return !(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f');
                }))
                continue;
            files.append(candidate);
        }
    };
    add("kumo-spectrum-v1", "*.kspec");
    add("kumo-reference-audio-v1", "*.wav");
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        return a.lastModified() < b.lastModified();
    });
    qint64 used = 0;
    for (const auto& file : files)
        used += file.size();
    for (const auto& file : files) {
        if (used <= limit)
            break;
        if (file.absoluteFilePath() != keep && QFile::remove(file.absoluteFilePath()))
            used -= file.size();
    }
}

} // namespace KumoAudio
