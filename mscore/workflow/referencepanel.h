// GPL-2.0-or-later
#pragma once
#include <QString>
class QMenu;
namespace Ms {
class Score;
class MasterScore;
void showReferencePanel(bool visible=true);
void addReferencePanelAction(QMenu*);
void flushReferencePanelForScore(Score*);

class ReferenceScoreSave {
public:
    ReferenceScoreSave(Score*, const QString& targetPath);
    ~ReferenceScoreSave();
    ReferenceScoreSave(const ReferenceScoreSave&) = delete;
    ReferenceScoreSave& operator=(const ReferenceScoreSave&) = delete;
    void accept();

private:
    MasterScore* _master = nullptr;
    QString _previous;
    bool _hadReference = false;
    bool _staged = false;
};
}
