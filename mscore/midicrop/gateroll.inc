// Export-occurrence roll. It owns value data only; dragging changes this export.
class GateRoll final:public QAbstractScrollArea {
      const int gutter=46,header=24,pedalHeight=30;
      double rowHeight=13.,playheadMs=-1.;
      bool dragging=false,panning=false;
      int originalRelease=0;
      QPoint origin,scrollOrigin;
      int bands()const{return stacked?2:1;}
      double bandHeight()const{return qMax(1,high-low+1)*rowHeight;}
      double x(int tick)const{return gutter+clock.durationMs(0,tick)*scale-horizontalScrollBar()->value();}
      double y(int pitch)const{return header+(high-pitch)*rowHeight-verticalScrollBar()->value();}
      QRectF box(const MidiGateNote& note,bool proposed)const{return {x(note.on),y(note.pitch)+(proposed&&stacked?bandHeight()+18:0),qMax(2.,clock.durationMs(note.on,proposed?note.newOff:note.off)*scale),qMax(3.,rowHeight-2)};}
      int hit(QPoint point)const{
            if(point.x()<gutter||point.y()<header||point.y()>viewport()->height()-pedalHeight)return -1;
            if(selected>=0&&selected<notes.size()&&(box(notes[selected],false).adjusted(-3,-2,8,2).contains(point)||box(notes[selected],true).adjusted(-3,-2,8,2).contains(point)))return selected;
            const int bucket=qMax(0,clock.tickForMilliseconds((point.x()-gutter+horizontalScrollBar()->value())/scale))/4800;
            for(int i:buckets.value(bucket))if(box(notes[i],false).adjusted(-2,-1,5,1).contains(point)||box(notes[i],true).adjusted(-2,-1,5,1).contains(point))return i;
            return -1;
            }
      QPair<int,int> bounds()const{return selected>=0&&selected<notes.size()?clock.releaseBounds(notes[selected],pedals,editingOptions):QPair<int,int>(-1,-1);}
      void moveRelease(QPoint point){const auto range=bounds();if(range.first>=0)notes[selected].newOff=qBound(range.first,clock.tickForMilliseconds((point.x()-gutter+horizontalScrollBar()->value())/scale),range.second);}
      void publishHandle(){
            if(selected<0||selected>=notes.size()){setProperty("selectedNewOffHandle",QPoint());return;}
            const auto b=box(notes[selected],true);setProperty("selectedNewOffHandle",QPoint(qRound(b.right()),qRound(b.center().y())));const auto range=bounds();setProperty("selectedReleaseRange",QPoint(range.first,range.second));
            }
      void paintEvent(QPaintEvent*)override{
            QPainter p(viewport());p.setRenderHint(QPainter::Antialiasing);p.fillRect(viewport()->rect(),palette().base());
            if(notes.isEmpty()){p.setPen(palette().text().color());p.drawText(viewport()->rect(),Qt::AlignCenter,tr("此范围没有音符"));return;}
            const int bottom=viewport()->height()-pedalHeight;auto accent=QApplication::palette().color(QPalette::Active,QPalette::Highlight);auto mid=palette().mid().color();
            p.save();p.setClipRect(QRect(gutter,header,viewport()->width()-gutter,bottom-header));
            for(int band=0;band<bands();++band)for(int pitch=low;pitch<=high;++pitch){
                  const double top=y(pitch)+(band?bandHeight()+18:0);if(top+rowHeight<header||top>bottom)continue;
                  const int pc=pitch%12;if(pc==1||pc==3||pc==6||pc==8||pc==10){auto c=palette().text().color();c.setAlpha(10);p.fillRect(QRectF(gutter,top,viewport()->width()-gutter,rowHeight),c);}
                  p.setPen(QPen(mid,1));p.drawLine(QPointF(gutter,top+rowHeight),QPointF(viewport()->width(),top+rowHeight));
                  }
            const int firstTick=qMax(0,clock.tickForMilliseconds(qMax(0,horizontalScrollBar()->value())/scale));
            const int lastVisible=clock.tickForMilliseconds((horizontalScrollBar()->value()+viewport()->width()-gutter)/scale);
            const double secondsStep=scale>.12?1.:scale>.03?5.:20.;const double firstSecond=std::floor(clock.durationMs(0,firstTick)/1000./secondsStep)*secondsStep;
            for(double sec=firstSecond;sec*1000<=clock.durationMs(0,lastVisible)+1;sec+=secondsStep){const double at=gutter+sec*1000*scale-horizontalScrollBar()->value();p.setPen(QPen(mid,1,Qt::DotLine));p.drawLine(QPointF(at,header),QPointF(at,bottom));}
            QSet<int> visible;for(int b=firstTick/4800;b<=lastVisible/4800;++b)for(int i:buckets.value(b))visible.insert(i);
            for(int i:visible){const auto& note=notes[i];auto old=box(note,false),now=box(note,true);if(old.right()<gutter||old.left()>viewport()->width()||(old.bottom()<header&&now.bottom()<header)||(old.top()>bottom&&now.top()>bottom))continue;
                  const bool changed=note.newOff<note.off;const auto chosen=selected>=0&&selected<notes.size()?&notes[selected]:nullptr;
                  const bool next=chosen&&chosen->releaseFor==note.on&&chosen->hand==note.hand&&(chosen->partId>=0?chosen->partId==note.partId:chosen->port==note.port&&chosen->channel==note.channel);
                  QColor fill=changed?accent:palette().text().color();fill.setAlpha(i==selected?210:changed?155:35);p.fillRect(now,fill);
                  auto outline=i==selected||next?accent:palette().text().color();outline.setAlpha(i==selected||next?255:changed?160:65);p.setBrush(Qt::NoBrush);p.setPen(QPen(outline,i==selected?1.8:1.,Qt::DashLine));p.drawRect(old);
                  if(changed){p.setPen(QPen(accent,i==selected?2.5:1.3));p.drawLine(now.topRight(),now.bottomRight());}
                  if(next){p.setPen(QPen(accent,1.6,Qt::DotLine));p.drawRect(now.adjusted(-2,-2,2,2));}
                  if(i==selected){const auto range=bounds();if(range.first>=0){auto shade=accent;shade.setAlpha(28);p.fillRect(QRectF(x(range.first),now.top()-4,qMax(1.,x(range.second)-x(range.first)),now.height()+8),shade);}p.setPen(QPen(accent,2));p.drawRect(now);}
                  }
            p.restore();
            p.fillRect(QRect(0,0,viewport()->width(),header),palette().window());p.fillRect(QRect(0,0,gutter,viewport()->height()),palette().window());
            p.setPen(palette().text().color());for(int band=0;band<bands();++band)for(int pitch=low;pitch<=high;++pitch){const double top=y(pitch)+(band?bandHeight()+18:0);if(top>=header&&top+rowHeight<bottom&&((rowHeight>=10)||pitch%12==0))p.drawText(QRectF(2,top,gutter-5,rowHeight),Qt::AlignRight|Qt::AlignVCenter,pitchText(pitch));}
            for(double sec=firstSecond;sec*1000<=clock.durationMs(0,lastVisible)+1;sec+=secondsStep){const int at=qRound(gutter+sec*1000*scale)-horizontalScrollBar()->value();if(at>=gutter)p.drawText(at+3,16,QString::number(sec,'f',0)+" s");}
            p.fillRect(QRect(gutter,bottom,viewport()->width()-gutter,pedalHeight),palette().window());p.setPen(mid);p.drawLine(gutter,bottom,viewport()->width(),bottom);p.setPen(palette().text().color());p.drawText(3,bottom+18,tr("踏板"));
            p.save();p.setClipRect(QRect(gutter,bottom,viewport()->width()-gutter,pedalHeight));for(const auto& pedal:pedals){auto fill=accent;fill.setAlpha(pedal.supported?85:30);const QRectF r(x(pedal.down),bottom+8,qMax(1.,x(pedal.up)-x(pedal.down)),10);p.fillRect(r,fill);p.setPen(QPen(accent,1,pedal.supported?Qt::SolidLine:Qt::DashLine));p.drawRect(r);}p.restore();
            if(playheadMs>=0){const int at=qRound(gutter+playheadMs*scale)-horizontalScrollBar()->value();if(at>=gutter&&at<viewport()->width()){p.setPen(QPen(accent,1.5));p.drawLine(at,header,at,viewport()->height());}}
            if(stacked){p.setPen(palette().text().color());p.drawText(gutter+5,header+14,tr("原长度"));const int split=qRound(header+bandHeight()+18-verticalScrollBar()->value());if(split>=header&&split<bottom)p.drawText(gutter+5,split+14,tr("新长度"));}
            publishHandle();
            }
      void mousePressEvent(QMouseEvent* event)override{
            setFocus();if(event->button()==Qt::MiddleButton){panning=true;origin=event->pos();scrollOrigin={horizontalScrollBar()->value(),verticalScrollBar()->value()};setCursor(Qt::ClosedHandCursor);event->accept();return;}
            if(event->button()!=Qt::LeftButton){QAbstractScrollArea::mousePressEvent(event);return;}
            const int i=hit(event->pos());if(i<0)return;selected=i;if(selectRow)selectRow(i);const auto tail=box(notes[i],true);const auto range=bounds();
            if(range.first>=0&&std::abs(event->pos().x()-tail.right())<=9&&std::abs(event->pos().y()-tail.center().y())<rowHeight){dragging=true;originalRelease=notes[i].newOff;viewport()->grabMouse();setProperty("releaseDragStarted",true);}
            refresh();event->accept();
            }
      void mouseMoveEvent(QMouseEvent* event)override{
            if(panning){horizontalScrollBar()->setValue(scrollOrigin.x()+origin.x()-event->pos().x());verticalScrollBar()->setValue(scrollOrigin.y()+origin.y()-event->pos().y());return;}
            if(dragging){moveRelease(event->pos());viewport()->update();return;}
            const int i=hit(event->pos());if(i<0){unsetCursor();return;}const auto tail=box(notes[i],true);setCursor(std::abs(event->pos().x()-tail.right())<=9?Qt::SizeHorCursor:Qt::ArrowCursor);QToolTip::showText(event->globalPos(),pitchText(notes[i].pitch)+" · "+reasonText(notes[i].reason),this);
            }
      void mouseReleaseEvent(QMouseEvent* event)override{
            if(panning){panning=false;unsetCursor();event->accept();return;}
            if(dragging){setProperty("releaseDragFinished",true);moveRelease(event->pos());dragging=false;viewport()->releaseMouse();const auto note=notes[selected];if(note.newOff!=originalRelease&&editRelease)editRelease(note.id,note.newOff);event->accept();return;}QAbstractScrollArea::mouseReleaseEvent(event);
            }
      void keyPressEvent(QKeyEvent* event)override{if(event->key()==Qt::Key_Escape&&dragging){notes[selected].newOff=originalRelease;dragging=false;viewport()->releaseMouse();viewport()->update();event->accept();return;}QAbstractScrollArea::keyPressEvent(event);}
      void contextMenuEvent(QContextMenuEvent* event)override{const int i=hit(event->pos());if(i<0)return;selected=i;if(selectRow)selectRow(i);QMenu menu(this);auto automatic=menu.addAction(tr("恢复自动松手")),original=menu.addAction(tr("保持原长度"));const auto result=menu.exec(event->globalPos());if(editRelease&&result)editRelease(notes[i].id,result==automatic?-1:notes[i].off);Q_UNUSED(original);}
      void wheelEvent(QWheelEvent* event)override{
            if(event->modifiers()&Qt::ControlModifier){setScale(scale*std::exp(event->angleDelta().y()/600.),event->pos().x());event->accept();return;}
            if(event->modifiers()&Qt::ShiftModifier){const double pitchOffset=(event->pos().y()-header+verticalScrollBar()->value())/rowHeight;rowHeight=qBound(4.,rowHeight*std::exp(event->angleDelta().y()/900.),36.);refresh();verticalScrollBar()->setValue(qRound(pitchOffset*rowHeight-event->pos().y()+header));event->accept();return;}
            QAbstractScrollArea::wheelEvent(event);
            }
      void resizeEvent(QResizeEvent* event)override{QAbstractScrollArea::resizeEvent(event);refresh();}
      void scrollContentsBy(int,int)override{viewport()->update();publishHandle();}
public:
      MidiGateProcessor clock;MidiGateOptions editingOptions;
      std::function<void(int)> selectRow;
      std::function<void(QString,int)> editRelease;
      std::function<void(double)> scaleChanged;
      QVector<MidiGateNote> notes;QVector<MidiGatePedal> pedals;
      double scale=.15;bool stacked=false;int selected=-1,low=127,high=0,lastTick=0;
      QMap<int,QVector<int>> buckets;
      explicit GateRoll(QWidget* parent):QAbstractScrollArea(parent){setMinimumHeight(210);setMouseTracking(true);viewport()->setMouseTracking(true);setFocusPolicy(Qt::StrongFocus);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);}
      void indexNotes(){buckets.clear();low=127;high=0;lastTick=0;for(int i=0;i<notes.size();++i){const auto& n=notes[i];low=qMin(low,n.pitch);high=qMax(high,n.pitch);lastTick=qMax(lastTick,n.off);for(int b=n.on/4800;b<=n.off/4800;++b)buckets[b].append(i);}if(notes.isEmpty())low=high=60;}
      void refresh(){horizontalScrollBar()->setRange(0,qMax(0,qRound(clock.durationMs(0,lastTick)*scale)-viewport()->width()+gutter+24));horizontalScrollBar()->setPageStep(qMax(1,viewport()->width()-gutter));verticalScrollBar()->setRange(0,qMax(0,qRound(bandHeight()*bands()+(stacked?18:0))-viewport()->height()+header+pedalHeight));verticalScrollBar()->setPageStep(qMax(1,viewport()->height()-header-pedalHeight));verticalScrollBar()->setSingleStep(qRound(rowHeight));viewport()->update();publishHandle();}
      void setScale(double value,double anchor){const double at=(anchor-gutter+horizontalScrollBar()->value())/scale;scale=qBound(.00005,value,3.);refresh();horizontalScrollBar()->setValue(qRound(at*scale-anchor+gutter));if(scaleChanged)scaleChanged(scale);}
      void fitContent(){if(lastTick<=0)return;setScale(qMax(40,viewport()->width()-gutter-24)/qMax(1.,clock.durationMs(0,lastTick)),gutter);horizontalScrollBar()->setValue(0);rowHeight=qBound(10.,double(viewport()->height()-header-pedalHeight-(stacked?18:0))/qMax(1,(high-low+1)*bands()),24.);refresh();verticalScrollBar()->setValue(0);}
      void revealSelected(){if(selected<0||selected>=notes.size())return;const auto b=box(notes[selected],true);if(b.left()<gutter||b.left()>viewport()->width()-20)horizontalScrollBar()->setValue(qMax(0,int(clock.durationMs(0,notes[selected].on)*scale)-20));if(b.top()<header)verticalScrollBar()->setValue(qMax(0,verticalScrollBar()->value()+qRound(b.top()-header)));else if(b.bottom()>viewport()->height()-pedalHeight)verticalScrollBar()->setValue(verticalScrollBar()->value()+qRound(b.bottom()-viewport()->height()+pedalHeight));refresh();}
      void setPlayhead(double ms){const int previous=qRound(gutter+playheadMs*scale)-horizontalScrollBar()->value(),now=qRound(gutter+ms*scale)-horizontalScrollBar()->value();playheadMs=ms;viewport()->update(QRect(previous-2,0,5,viewport()->height()));viewport()->update(QRect(now-2,0,5,viewport()->height()));}
};
