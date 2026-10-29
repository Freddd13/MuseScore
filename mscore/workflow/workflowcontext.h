// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#pragma once
#include <QObject>
#include <QPointer>
#include <QVariantMap>
#include <QHash>
#include "libmscore/score.h"
namespace Ms {
class MidiCaptureSession;
class WorkflowContext : public QObject {
      Q_OBJECT
      Q_PROPERTY(QVariantMap expression READ expression NOTIFY changed)
      Q_PROPERTY(QString feedback MEMBER _feedback NOTIFY feedbackChanged)
      Q_PROPERTY(QVariantMap audition READ audition NOTIFY auditionChanged)
      Q_PROPERTY(QVariantMap capture READ capture NOTIFY captureChanged)
      Q_PROPERTY(QVariantMap lastRecording READ lastRecording NOTIFY lastRecordingChanged)
      QVariantMap _capture;
      QPointer<QObject> _captureOwner;
      QPointer<Score> _captureScore;
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
      struct ExpressionSessionData {ScoreContentState content;QVariantMap values;};
      QHash<Score*,ExpressionSessionData> _expressionSessions;
      QHash<Score*,QPointer<MidiCaptureSession>> _expressionCaptures;
public:
      explicit WorkflowContext(QObject* parent):QObject(parent){}
      Q_INVOKABLE QVariantMap expressionSession(QObject* score) const;
      Q_INVOKABLE bool rememberExpressionSession(QObject* score,const QVariantMap& value);
      Q_INVOKABLE QObject* expressionCaptureSession(QObject* score);
      QVariantMap lastRecording() const;
      void notifyRecordingSaved(){emit lastRecordingChanged();}
      void setActiveScore(Score* score){if(_activeScore==score)return;_activeScore=score;emit activeScoreChanged();}
      QVariantMap expression()const{return _expression;}
      QVariantMap audition()const{return _audition;}
      QVariantMap capture()const{return _capture;}
      bool captureCurrent(Score* score)const{return score&&_captureScore==score;}
      void setCapture(QObject* owner,Score* score,const QVariantMap& value){_captureOwner=owner;_captureScore=score;if(_capture==value)return;_capture=value;emit captureChanged();}
      void clearCapture(QObject* owner){if(_captureOwner!=owner)return;_captureOwner.clear();_captureScore.clear();_capture.clear();emit captureChanged();}
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
      void scoreFileChanged(Score* score);
      void feedbackChanged();
      void auditionChanged();
      void captureChanged();
      void lastRecordingChanged();
      void requested(const QString& action,const QString& scoreId,const QString& midiId);
};
WorkflowContext* workflowContext();
WorkflowContext* workflowContextIfCreated();
void notifyWorkflowCurrentScore(Score*);
void notifyWorkflowScoreFileChanged(Score*);
QString workflowScoreSession(Score*);
}
