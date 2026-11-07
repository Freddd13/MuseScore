// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#pragma once
#include <QByteArray>
#include <QPointer>
#include <QVariantMap>
#include <QString>
#include <atomic>

namespace Ms
{
class Score;
class MasterScore;

// File parsing, persistence and time mapping use values only. The document
// adapter below owns the GUI-thread score/undo operations.
QVariantMap prepareMidiTracks(const QByteArray&, const QString& name,
                              const std::atomic_bool& canceled);
QVariantMap decodeMidiTracks(const QString&, const QVariantMap& knownAssets = {});
QString encodeMidiTracks(const QVariantMap&, QString* error);
double midiTrackTime(const QVariantMap&, double seconds, bool inverse = false);
QVariantMap exportMidiTrack(const QVariantMap& document, const QString& id, const QString& file,
                            bool mappedTime = false);

class MidiTrackDocument
{
    QPointer<MasterScore> _score;
    QVariantMap _data;
    QVariantMap _knownAssets;
    QString _serialized, _error, _revision;
    bool write(const QVariantMap&);
    int indexOf(const QString&) const;

  public:
    void setScore(Score*);
    bool reload();
    MasterScore* score() const;
    QVariantMap state() const { return _data; }
    QString error() const { return _error; }
    QString revision() const { return _revision; }
    QVariantMap track(const QString&) const;
    QVariantList tracks() const { return _data.value("tracks").toList(); }
    bool addPrepared(const QVariantMap&);
    bool appendRecording(const QString& id, const QVariantMap& prepared, double startSeconds);
    bool setScoreOptions(const QVariantMap&);
    QString create(const QString& name);
    QString duplicate(const QString& id);
    bool remove(const QString& id);
    bool restore(const QString& id = {});
    bool update(const QString& id, const QVariantMap& fields);
    bool replaceNotes(const QString& id, const QVariantList&);
    bool setAlignment(const QString& id, const QString& audioId, double offsetMs,
                      const QVariantList& anchors, const QString& associationId = {});
};
} // namespace Ms
