// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QVariantMap>

namespace KumoAudio {

inline QString audioFingerprint(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0)
        return {};
    const auto size = file.size();
    const auto length = qMin<qint64>(size, 64 * 1024);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData("kumo-audio-samples-1:");
    hash.addData(QByteArray::number(size));
    // Probe the beginning, middle and end of the compressed or uncompressed recording.
    for (const auto offset : {qint64(0), (size - length) / 2, size - length}) {
        if (!file.seek(offset))
            return {};
        const auto bytes = file.read(length);
        if (bytes.size() != length)
            return {};
        hash.addData(bytes);
    }
    return QString::fromLatin1(hash.result().toHex());
}

inline QVariantMap audioIdentity(const QFileInfo& file)
{
    return {{"size", file.size()}, {"modified", file.lastModified().toMSecsSinceEpoch()},
            {"fingerprint", audioFingerprint(file.filePath())}};
}

inline bool matchesAudioIdentity(const QFileInfo& file, const QVariantMap& saved)
{
    if (saved.isEmpty())
        return true;
    if (file.size() != saved.value("size").toLongLong())
        return false;
    if (file.lastModified().toMSecsSinceEpoch() == saved.value("modified").toLongLong())
        return true;
    const auto fingerprint = saved.value("fingerprint").toString();
    return !fingerprint.isEmpty() && fingerprint == audioFingerprint(file.filePath());
}

} // namespace KumoAudio
