// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#pragma once
#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include "libmscore/score.h"
namespace Ms {
class WorkflowContext : public QObject {
      Q_OBJECT
      Q_PROPERTY(QVariantMap expression READ expression NOTIFY changed)
      Q_PROPERTY(QString feedback MEMBER _feedback NOTIFY feedbackChanged)
      Q_PROPERTY(QVariantMap audition READ audition NOTIFY auditionChanged)
      QVariantMap _audition;
      QPointer<QObject> _playbackSession;
      QPointer<Score> _score, _activeScore;
      ScoreContentState _state;
      QVariantMap _expression;
      QString _feedback;
      QPointer<Score> _referenceScore;
      QPointer<Score> _previewScore,_previewOrigin;
      const Score* _previewKey=nullptr;
      ScoreContentState _previewState;
public:
      explicit WorkflowContext(QObject* parent):QObject(parent){}
      void setActiveScore(Score* score){if(_activeScore==score)return;_activeScore=score;emit activeScoreChanged();}
      QVariantMap expression()const{return _expression;}
      QVariantMap audition()const{return _audition;}
      void finishPlaybackSession(){auto old=_playbackSession.data();_playbackSession.clear();delete old;}
      void setPlaybackSession(QObject* session){if(_playbackSession!=session){finishPlaybackSession();_playbackSession=session;}}
      void setAudition(const QVariantMap& value){_audition=value;emit auditionChanged();}
      bool current(Score* score)const{return score&&_score&&_state==_score->masterScore()->state()&&(_score==score||(_previewOrigin==_score&&_previewScore==score&&_previewState==score->masterScore()->state()));}
      bool referenceCurrent(Score* score)const{return score&&(_referenceScore==score||(_previewOrigin==_referenceScore&&_previewScore==score));}
      void setPreview(Score* origin,Score* preview){_previewOrigin=origin;_previewScore=preview;_previewKey=preview;_previewState=preview->masterScore()->state();emit changed();}
      bool clearPreview(const Score* expected=nullptr){if(expected&&expected!=_previewKey)return false;_previewOrigin.clear();_previewScore.clear();_previewKey=nullptr;emit changed();return true;}
      void setReferenceScore(Score* score){_referenceScore=score;emit changed();}
      void setExpression(Score* score,const QVariantMap& value){_score=score;_state=score?score->masterScore()->state():ScoreContentState();_expression=value;emit changed();}
      void updateExpression(const QVariantMap& state){for(auto it=state.cbegin();it!=state.cend();++it)_expression[it.key()]=it.value();emit changed();}
      Q_INVOKABLE void expressionAction(const QString& action,const QString& scoreId,const QString& midiId){emit requested(action,scoreId,midiId);}
signals:
      void changed();
      void activeScoreChanged();
      void feedbackChanged();
      void auditionChanged();
      void requested(const QString& action,const QString& scoreId,const QString& midiId);
};
WorkflowContext* workflowContext();
void notifyWorkflowCurrentScore(Score*);
}
