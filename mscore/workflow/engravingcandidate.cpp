// SPDX-License-Identifier: GPL-2.0-or-later
#include "engravingcandidate.h"
#include "engravingedit.h"
#include "referencepaths.h"
#include "libmscore/element.h"
#include "libmscore/layoutbreak.h"
#include "libmscore/measure.h"
#include "libmscore/page.h"
#include "libmscore/score.h"
#include "libmscore/segment.h"
#include "libmscore/spacer.h"
#include "libmscore/spanner.h"
#include "libmscore/system.h"
#include "mscore/musescore.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QScopedValueRollback>
#include <QSet>
#include <cmath>

namespace Ms {
namespace {
QString tr(const char* text)
{
    return QCoreApplication::translate("EngravingCandidate", text);
}

QVariantMap error(const QString& message)
{
    return {{"error", message}};
}

enum class EditKind { Symbol, Column, Stretch, Gap, Break };
struct Edit {
    EditKind kind = EditKind::Symbol;
    Element* element = nullptr;
    Measure* measure = nullptr;
    int staff = -1;
    QVariantMap change;
};

bool numeric(const QVariantMap& row, const char* name, double minimum, double maximum, double* result)
{
    bool ok = false;
    const auto value = row.value(name);
    const double number = value.toDouble(&ok);
    if (!ok || !std::isfinite(number) || number < minimum || number > maximum)
        return false;
    *result = number;
    return true;
}

int index(const QVariantMap& row, const char* name)
{
    double result;
    return numeric(row, name, 0, 2147483647., &result) && std::floor(result) == result ? int(result) : -1;
}

double precise(double value)
{
    return std::round(value * 1000000.) / 1000000.;
}

QVariant position(double x, double y)
{
    return QVariantMap{{"x", precise(x)}, {"y", precise(y)}};
}

struct Search {
    QSet<Element*> result;
    QString type;
    int tick, track, start, end;
};

void collectSymbol(void* data, Element* item)
{
    auto& target = *static_cast<Search*>(data);
    if (item->track() != target.track || item->tick().ticks() != target.tick ||
        QLatin1String(item->name()) != target.type || !item->visible() || !engravingMovableSymbol(item))
        return;
    if (item->isSpannerSegment()) {
        const auto span = toSpannerSegment(item)->spanner();
        if ((target.start >= 0 && span->tick().ticks() != target.start) ||
            (target.end >= 0 && span->tick2().ticks() != target.end))
            return;
    } else if (target.start >= 0 || target.end >= 0)
        return;
    target.result.insert(item);
}

Element* findSymbol(Score* score, const QVariantMap& row, int tick, int track)
{
    Search target{{}, row.value("type").toString(), tick, track,
                  index(row, "connectionStart"), index(row, "connectionEnd")};
    const int systemStart = target.end >= 0 ? index(row, "systemStart") : -1;
    for (const auto page : score->pages())
        for (const auto system : page->systems()) {
            const auto first = system->firstMeasure();
            const auto last = system->lastMeasure();
            if (!first || !last)
                continue;
            const int from = first->tick().ticks(), until = last->endTick().ticks();
            if (systemStart >= 0 ? from != systemStart
                : (target.end >= 0 ? (until <= tick || from >= target.end) : (tick < from || tick >= until)))
                continue;
            if (target.end < 0)
                for (const auto base : system->measures())
                    base->scanElements(&target, collectSymbol, false);
            system->scanElements(&target, collectSymbol, false);
        }
    return target.result.size() == 1 ? *target.result.cbegin() : nullptr;
}

LayoutBreak* editableLineBreak(Measure* measure, bool* protectedBreak)
{
    for (const auto element : measure->el()) {
        if (!element->isLayoutBreak())
            continue;
        const auto item = toLayoutBreak(element);
        if (!item->isLineBreak() || !item->systemLockData().isEmpty()) {
            *protectedBreak = true;
            return nullptr;
        }
        return item;
    }
    return nullptr;
}

QVariant readValue(const Edit& edit)
{
    switch (edit.kind) {
    case EditKind::Symbol: {
        const auto item = edit.element;
        return position(item->offset().x() / item->spatium(), item->offset().y() / item->spatium());
    }
    case EditKind::Column:
        return precise(toSegment(edit.element)->extraLeadingSpace().val());
    case EditKind::Stretch:
        return precise(edit.measure->userStretch());
    case EditKind::Gap: {
        const auto spacer = edit.measure->vspacerDown(edit.staff);
        return spacer ? QVariant(precise(spacer->gap() / edit.measure->score()->spatium())) : QVariant();
    }
    case EditKind::Break:
        return edit.measure->lineBreak();
    }
    return {};
}

void applyEdit(Score* score, const Edit& edit)
{
    const auto after = edit.change.value("after");
    switch (edit.kind) {
    case EditKind::Symbol: {
        const auto coordinates = after.toMap();
        edit.element->undoChangeProperty(Pid::OFFSET,
            QPointF(coordinates.value("x").toDouble(), coordinates.value("y").toDouble()) *
            edit.element->spatium());
        break;
    }
    case EditKind::Column:
        edit.element->undoChangeProperty(Pid::LEADING_SPACE, Spatium(after.toDouble()));
        break;
    case EditKind::Stretch:
        edit.measure->undoChangeProperty(Pid::USER_STRETCH, after);
        break;
    case EditKind::Gap: {
        auto spacer = edit.measure->vspacerDown(edit.staff);
        if (!after.isValid()) {
            score->undoRemoveElement(spacer);
        } else if (spacer) {
            spacer->undoChangeProperty(Pid::SPACE, after.toDouble() * score->spatium());
        } else {
            spacer = new Spacer(score);
            spacer->setParent(edit.measure);
            spacer->setTrack(edit.staff * VOICES);
            spacer->setSpacerType(SpacerType::DOWN);
            spacer->setGap(after.toDouble() * score->spatium());
            score->undoAddElement(spacer);
        }
        break;
    }
    case EditKind::Break:
        if (after.toBool()) {
            auto layoutBreak = new LayoutBreak(score);
            layoutBreak->setLayoutBreakType(LayoutBreak::LINE);
            layoutBreak->setParent(edit.measure);
            layoutBreak->setTrack(0);
            score->undoAddElement(layoutBreak);
        } else {
            score->undoRemoveElement(edit.element);
        }
        break;
    }
}
}

QVariantMap applyEngravingChanges(Score* score, const QVariantMap& scope, const QVariantList& operations,
                                 const QVariantList& expected)
{
    if (operations.isEmpty() || operations.size() > 32)
        return error(tr("请提交一至三十二处排版调整。"));
    const int start = scope.value("start").toInt(), end = scope.value("end").toInt();
    const int firstTrack = scope.value("firstTrack").toInt(), endTrack = scope.value("endTrack").toInt();
    QSet<QPair<const Element*, int>> targets;
    QList<Edit> edits;
    QVariantList changes;
    bool hasLineBreak = false, hasSpannerOffset = false;
    for (int i = 0; i < operations.size(); ++i) {
        const auto row = operations.at(i).toMap();
        const auto kind = row.value("kind").toString();
        const int tick = index(row, "tick"), track = index(row, "track"), systemTick = index(row, "systemStart");
        const bool fragment = kind == "symbolOffset" && index(row, "connectionEnd") > tick;
        const int affected = fragment ? qMax(tick, systemTick) : tick;
        if (tick < 0 || affected < start || affected >= end)
            return error(tr("第 %1 处超出了所选时间范围。").arg(i + 1));

        Edit edit;
        edit.change = {{"kind", kind}, {"tick", affected}, {"track", qMax(firstTrack, track)},
                       {"reason", row.value("reason").toString().left(300)}};
        edit.measure = score->tick2measure(Fraction::fromTicks(affected));
        if (!edit.measure)
            return error(tr("找不到第 %1 处对应的小节。").arg(i + 1));
        edit.change["measure"] = edit.measure->no() + 1;
        QVariant after;
        const Element* target = edit.measure;
        int key = 0;
        if (kind == "symbolOffset") {
            if (track < firstTrack || track >= endTrack || track >= score->ntracks())
                return error(tr("第 %1 处的符号超出了所选谱表或声部。").arg(i + 1));
            double x, y;
            if (!numeric(row, "x", -20, 20, &x) || !numeric(row, "y", -20, 20, &y))
                return error(tr("请提供第 %1 处的水平和垂直位置，单位为谱间。").arg(i + 1));
            edit.kind = EditKind::Symbol;
            edit.element = findSymbol(score, row, tick, track);
            if (!edit.element)
                return error(tr("找不到唯一的第 %1 个符号。请打开页面视图，重新读取这一行和符号位置。").arg(i + 1));
            hasSpannerOffset |= edit.element->isSpannerSegment();
            target = edit.element;
            after = position(x, y);
            edit.change["type"] = row.value("type");
            edit.change["label"] = tr("符号位置");
        } else if (kind == "timeColumnSpacing") {
            double space;
            if (!numeric(row, "space", -5, 20, &space))
                return error(tr("请为第 %1 处提供有效的拍位留白，单位为谱间。").arg(i + 1));
            const auto segment = score->tick2segment(Fraction::fromTicks(tick), false, SegmentType::ChordRest);
            if (!segment || segment->tick().ticks() != tick)
                return error(tr("第 %1 处没有对应的音符或休止拍位。").arg(i + 1));
            edit.kind = EditKind::Column;
            edit.element = segment;
            target = segment;
            after = precise(space);
            edit.change["label"] = tr("拍位前留白");
        } else {
            if (edit.measure->tick().ticks() != tick)
                return error(tr("第 %1 处需要填写目标小节开头的拍位。").arg(i + 1));
            if (kind == "measureStretch") {
                double factor;
                if (!numeric(row, "stretch", .25, 4, &factor))
                    return error(tr("第 %1 处的小节宽度需要在 25% 到 400% 之间。").arg(i + 1));
                edit.kind = EditKind::Stretch;
                after = precise(factor);
                edit.change["label"] = tr("小节宽度");
            } else if (kind == "staffSpacing") {
                const int staff = index(row, "staff");
                if (staff < 0 || staff >= score->nstaves() || staff * VOICES >= endTrack ||
                    (staff + 1) * VOICES <= firstTrack)
                    return error(tr("第 %1 处需要所选范围中的谱表编号。").arg(i + 1));
                if (!row.contains("space"))
                    return error(tr("请提供第 %1 处的谱表下方距离，或用空值恢复自动间距。").arg(i + 1));
                if (!row.value("space").isNull()) {
                    double space;
                    if (!numeric(row, "space", 0, 30, &space))
                        return error(tr("请提供第 %1 处的谱表下方距离，单位为谱间。").arg(i + 1));
                    after = precise(space);
                }
                edit.kind = EditKind::Gap;
                edit.staff = staff;
                edit.change["track"] = staff * VOICES;
                edit.change["label"] = tr("谱表下方距离");
                key = staff + 2;
            } else if (kind == "lineBreak") {
                if (row.value("enabled").userType() != QMetaType::Bool)
                    return error(tr("请指定第 %1 处是否在小节后换行。").arg(i + 1));
                bool protectedBreak = false;
                edit.element = editableLineBreak(edit.measure, &protectedBreak);
                if (protectedBreak || (edit.measure->lineBreak() && !edit.element))
                    return error(tr("第 %1 处已有锁定、分页或分节设置，请在雕版工具中调整。").arg(i + 1));
                edit.kind = EditKind::Break;
                edit.change["label"] = tr("小节后换行");
                after = row.value("enabled");
                key = 1;
                hasLineBreak = true;
            } else {
                return error(tr("第 %1 处需要一种支持的排版操作。").arg(i + 1));
            }
        }
        const auto selection = qMakePair(target, key);
        if (targets.contains(selection))
            return error(tr("第 %1 处和另一项修改指向同一个位置，请合并为一处。").arg(i + 1));
        targets.insert(selection);
        const auto before = readValue(edit);
        if (before == after)
            continue;
        edit.change["before"] = before;
        edit.change["after"] = after;
        changes.append(edit.change);
        edits.append(edit);
    }
    if (hasLineBreak && hasSpannerOffset)
        return error(tr("请先预览并采用重新分行，再调整对应谱行上的曲线或线条。"));
    if (changes.isEmpty())
        return error(tr("这些位置已经采用所指定的排版。"));
    if (!expected.isEmpty() && changes != expected)
        return error(tr("有位置已经改变，请重新生成排版预览。"));
    for (const auto& edit : edits) {
        applyEdit(score, edit);
        if (readValue(edit) != edit.change.value("after"))
            return error(tr("排版设置写入后和预览不一致，请刷新排版后重试。"));
    }
    return {{"ok", true}, {"changes", changes}};
}

QVariantMap saveEngravingCopy(Score* original, Score* candidate, const QVariantMap& request)
{
    const auto path = request.value("destination").toString();
    const QFileInfo target(path), source(*original->masterScore()->fileInfo());
    if (!target.isAbsolute() || target.suffix().compare("mscz", Qt::CaseInsensitive) != 0)
        return error(tr("请选择绝对路径，以 .mscz 保存排版副本。"));
#ifdef Q_OS_WIN
    constexpr auto samePathCase = Qt::CaseInsensitive;
#else
    constexpr auto samePathCase = Qt::CaseSensitive;
#endif
    const auto sameFile = [&](const QFileInfo& file) {
        return target.absoluteFilePath().compare(file.absoluteFilePath(), samePathCase) == 0 ||
            (target.exists() && file.exists() &&
             target.canonicalFilePath().compare(file.canonicalFilePath(), samePathCase) == 0);
    };
    if (sameFile(source))
        return error(tr("请选择一个不同于当前乐谱的文件名。"));
    if (mscore)
        for (const auto open : mscore->scores())
            if (sameFile(*open->fileInfo()))
                return error(tr("该文件仍在 MuseScore 中打开，请为排版副本另取文件名。"));
    if (target.exists() && !request.value("overwrite").toBool())
        return error(tr("这个文件已经存在，请指定其他文件名或确认覆盖。"));
    auto tags = candidate->metaTags();
    const QScopedValueRollback<QMap<QString, QString>> restore(candidate->metaTags());
    auto document = QJsonDocument::fromJson(tags.value("kumo.reference").toUtf8()).toVariant().toMap();
    if (document.value("schema").toInt() == 2) {
        document = referenceDocumentForSave(document,
            original->masterScore()->created() ? QString() : source.absoluteFilePath(), target.absoluteFilePath());
        tags["kumo.reference"] = QString::fromUtf8(QJsonDocument::fromVariant(document).toJson(QJsonDocument::Compact));
        candidate->metaTags() = tags;
    }
    // The native zip writer closes its device, so finish the ZIP before the
    // atomic write. QSaveFile must be finished exclusively with commit().
    QBuffer bytes;
    if (!bytes.open(QIODevice::WriteOnly) ||
        !candidate->saveCompressedFile(&bytes, target.completeBaseName() + ".mscx", false, false))
        return error(tr("未能生成排版副本。"));
    QSaveFile file(target.absoluteFilePath());
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes.data()) != bytes.size() || !file.commit())
        return error(tr("未能保存排版副本：%1").arg(file.errorString()));
    return {{"ok", true}, {"path", target.absoluteFilePath()}};
}
}
