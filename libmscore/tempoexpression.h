#ifndef MS_TEMPOEXPRESSION_H
#define MS_TEMPOEXPRESSION_H
#include <QVector>
#include <QString>
#include <QtGlobal>
namespace Ms {
class Score;
class TextLine;
namespace TempoExpression {
QVector<TextLine*> curves(Score*);
QString conflict(Score*, int from, int until, const TextLine* ignore = nullptr);
qreal target(const TextLine*, qreal startBps);
qreal value(const TextLine*, qreal startBps, double tick);
// GUI/render preparation only. Replaces a derived map in one normalization.
void rebuild(Score*);
}
}
#endif
