// Copyright (C) 2026 Freddd13 and contributors; GPL-2.0-or-later.
#include "documentsettings.h"
#include "featuregate.h"
#include "scoremixstate.h"
#include "libmscore/score.h"
#include <QCoreApplication>
#include <QDataStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QTimer>

namespace Ms {
namespace {
const QString stateTag = QStringLiteral("kumo.toolState");
const char* cacheProperty = "kumoToolStateCache";
QPointer<DocumentUiState> state;
bool documentKey(const QString& key)
{
    for (const auto& prefix : {"personalWorkspaceViews/", "performanceEditor/", "kumoPanels/",
                              "spectrum/", "rhythmWriting/", "regionalVariants/",
                              "arrangementSuggestions/", "toolProjects/", "rollWriting/", "scoreVideoPanel/", "audioCalibration/"})
        if (key.startsWith(QLatin1String(prefix))) return true;
    return key == "kumo/vst/commonPianoDetails";
}
Score* masterOf(Score* score) { return score ? score->masterScore() : nullptr; }
QVariantMap valuesFor(Score* score)
{
    auto master = masterOf(score);
    if (!master) return {};
    auto cache = master->QObject::property(cacheProperty);
    if (cache.isValid()) return cache.toMap();
    QVariantMap values;
    const auto saved = master->metaTag(stateTag);
    const auto document = QJsonDocument::fromJson(saved.toUtf8()).object();
    if (document.value("version").toInt() == 1) {
        const auto data = QByteArray::fromBase64(document.value("values").toString().toLatin1());
        QDataStream stream(data);
        stream.setVersion(QDataStream::Qt_5_12);
        stream >> values;
        if (stream.status() != QDataStream::Ok) values.clear();
    } else {
        QSettings defaults;
        for (const auto& key : defaults.allKeys())
            if (documentKey(key)) values.insert(key, defaults.value(key));
    }
    master->QObject::setProperty(cacheProperty, values);
    return values;
}
void storeValues(Score* score, const QVariantMap& values)
{
    if (auto master = masterOf(score)) master->QObject::setProperty(cacheProperty, values);
}
}
DocumentUiState::DocumentUiState(QObject* parent) : QObject(parent) {}
Score* DocumentUiState::score() const { return _score; }
DocumentUiState* documentUiState()
{
    if (!state) state = new DocumentUiState(qApp);
    return state;
}
void DocumentUiState::capture(Score* score)
{
    if (!_restoring && score && masterOf(score) == masterOf(_score)) {
        emit captureRequested(score);
    }
}
void DocumentUiState::activate(Score* score)
{
    if(!personalFeaturesEnabled()){_score=score;return;}
    if (_score == score) return;
    // MuseScore collects the outgoing view before replacing cv and cs.
    if (masterOf(_score) == masterOf(score)) { _score = score; return; }
    _score = score;
    if (_score) valuesFor(_score);
    _restoring = true;
    const auto serial = ++_serial;
    emit scoreChanged(_score);
    QTimer::singleShot(0, this, [this, serial] { if (_serial == serial) _restoring = false; });
}
void prepareDocumentUiSave(Score* score)
{
    if (!score || !personalFeaturesEnabled()) return;
    captureScoreMix(score);
    documentUiState()->capture(score);
    const auto values = valuesFor(score);
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_5_12);
    stream << values;
    if (stream.status() == QDataStream::Ok)
        score->masterScore()->metaTags().insert(stateTag,
            QString::fromUtf8(QJsonDocument(QJsonObject{{"version",1},
                {"values",QString::fromLatin1(bytes.toBase64())}}).toJson(QJsonDocument::Compact)));
}
DocumentSettings::DocumentSettings(Score* score)
    : _score(masterOf(score ? score : documentUiState()->score())) {}
QString DocumentSettings::fullKey(const QString& key) const
{ return group().isEmpty() ? key : group() + (key.isEmpty() ? QString() : "/" + key); }
QVariant DocumentSettings::value(const QString& key, const QVariant& fallback) const
{
    const auto full = fullKey(key);
    return _score && documentKey(full) ? valuesFor(_score).value(full, fallback) : QSettings::value(key, fallback);
}
void DocumentSettings::setValue(const QString& key, const QVariant& value)
{
    const auto full = fullKey(key);
    if (!_score || !documentKey(full)) { QSettings::setValue(key,value); return; }
    if (documentUiState()->restoring()) return;
    auto values = valuesFor(_score);
    if (values.value(full) == value && values.contains(full)) return;
    values.insert(full,value); storeValues(_score, values);
}
bool DocumentSettings::contains(const QString& key) const
{
    const auto full = fullKey(key);
    return _score && documentKey(full) ? valuesFor(_score).contains(full) : QSettings::contains(key);
}
void DocumentSettings::remove(const QString& key)
{
    const auto full = fullKey(key);
    if (!_score || !(documentKey(full) || documentKey(full + "/"))) { QSettings::remove(key); return; }
    auto values = valuesFor(_score);
    for (auto it = values.begin(); it != values.end();)
        if (it.key() == full || it.key().startsWith(full + "/")) it = values.erase(it);
        else ++it;
    storeValues(_score, values);
}
QStringList DocumentSettings::allKeys() const
{
    if (!_score || !documentKey(fullKey(QString()) + "/")) return QSettings::allKeys();
    const auto prefix = group().isEmpty() ? QString() : group() + "/";
    QStringList result;
    const auto values = valuesFor(_score);
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        if (it.key().startsWith(prefix)) result.append(it.key().mid(prefix.size()));
    return result;
}
QStringList DocumentSettings::childKeys() const
{
    if (!_score || !documentKey(fullKey(QString()) + "/")) return QSettings::childKeys();
    QStringList result;
    for (const auto& key : allKeys()) if (!key.contains('/')) result.append(key);
    return result;
}
QStringList DocumentSettings::childGroups() const
{
    if (!_score || !documentKey(fullKey(QString()) + "/")) return QSettings::childGroups();
    QSet<QString> result;
    for (const auto& key : allKeys()) if (key.contains('/')) result.insert(key.section('/',0,0));
    return result.values();
}
}
