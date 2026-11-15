// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QDir>
#include <QFileInfo>
#include <QUrl>
#include <QVariantMap>

namespace Ms {

inline QString referenceLocalPath(const QString& value)
{
    const QUrl url(value);
    return url.isLocalFile() ? url.toLocalFile() : value;
}

inline QString referenceAbsolutePath(const QString& value, const QString& scorePath)
{
    const auto path = referenceLocalPath(value);
    if (path.isEmpty())
        return {};
    if (QDir::isAbsolutePath(path))
        return QDir::cleanPath(path);
    if (scorePath.isEmpty())
        return {};
    return QDir::cleanPath(QFileInfo(scorePath).absoluteDir().absoluteFilePath(path));
}

inline QString resolveReferenceTrack(const QVariantMap& track, const QString& scorePath)
{
    QString missing;
    for (const auto& field : {"relativePath", "originalPath", "path"}) {
        const auto path = referenceAbsolutePath(track.value(field).toString(), scorePath);
        const QFileInfo file(path);
        if (file.isFile() && file.isReadable())
            return file.absoluteFilePath();
        if (missing.isEmpty() && !path.isEmpty())
            missing = path;
    }
    return missing;
}

inline QString referenceRelativePath(const QString& absolutePath, const QString& scorePath)
{
    if (scorePath.isEmpty() || absolutePath.isEmpty())
        return {};
    const auto relative = QFileInfo(scorePath).absoluteDir().relativeFilePath(absolutePath);
    // Qt returns an absolute path when the target is on a different Windows drive.
    return QDir::isAbsolutePath(relative) ? QString() : relative;
}

inline QVariantMap referenceDocumentForSave(QVariantMap document, const QString& sourceScorePath,
                                            const QString& targetScorePath)
{
    QVariantList tracks;
    for (const auto& value : document.value("tracks").toList()) {
        auto track = value.toMap();
        auto original = referenceAbsolutePath(track.value("originalPath").toString(), sourceScorePath);
        const auto oldPath = referenceLocalPath(track.value("path").toString());
        if (original.isEmpty() && QDir::isAbsolutePath(oldPath))
            original = QDir::cleanPath(oldPath);
        const auto path = resolveReferenceTrack(track, sourceScorePath);
        const QFileInfo file(path);
        if (file.isFile() && file.isReadable()) {
            track["path"] = path;
            track["originalPath"] = path;
            track["relativePath"] = referenceRelativePath(path, targetScorePath);
        } else {
            // For a missing file, keep the last absolute path and rebase its expected relative location.
            if (!path.isEmpty())
                track["relativePath"] = referenceRelativePath(path, targetScorePath);
            track["path"] = original.isEmpty() ? path : original;
            track["originalPath"] = track["path"];
        }
        track.remove("missing");
        tracks.append(track);
    }
    document["tracks"] = tracks;
    return document;
}

} // namespace Ms
