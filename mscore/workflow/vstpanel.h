// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QVariantMap>
class QMenu;
namespace Ms {
class Score;
class Channel;
class MasterSynthesizer;
void routeVstChannels(MasterSynthesizer *, Score *);
void prepareVstForScore(MasterSynthesizer *, Score *);
void addVstPanelAction(QMenu *);
void showVstPanel(Channel *channel = nullptr);
void restoreVstForScore(Score *);
void saveVstEdits(Score *,bool fileSave=false);
QVariantMap copyVstInstrument(Score *, Channel *, int);
QVariantMap applyVstConfiguration(Score *, const QVariantMap &);
QVariantMap assignVstInstrument(Score *, Channel *,
                                const QVariantMap &descriptor,
                                bool isolated = false,
                                const QVariantMap &initialEffect = {},
                                const QVariantMap &copiedInstance = {});
} // namespace Ms
