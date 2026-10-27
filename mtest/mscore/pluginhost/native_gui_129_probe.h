// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QAbstractEventDispatcher>
#include <QAbstractNativeEventFilter>

// Test-only timing of this GUI thread's native window work. Never changes a message.
class PianoNativeProbe129 final : public QObject, public QAbstractNativeEventFilter {
#ifdef Q_OS_WIN
    struct Frame { HWND window=nullptr; UINT message=0; LPARAM data=0; qint64 start=0; };
    struct Finished { Frame frame; double milliseconds=0; bool paired=false; };
    std::array<Frame,128> frames{};
    std::array<Finished,256> slow{};
    int depth=0,count=0;
    Frame raw;
    bool rawPending=false;
    HHOOK enterHook=nullptr,leaveHook=nullptr;
    QElapsedTimer clock;
    inline static thread_local PianoNativeProbe129* current=nullptr;
    void record(const Frame& frame,qint64 until,bool paired) {
        const double duration=(until-frame.start)/1.e6;
        if(duration>=20 && count<int(slow.size()))slow[count++]={frame,duration,paired};
    }
    static LRESULT CALLBACK entering(int code,WPARAM argument,LPARAM data) {
        if(code>=0 && current) {
            const auto message=reinterpret_cast<const CWPSTRUCT*>(data);
            const int at=current->depth++;
            if(at<int(current->frames.size()))current->frames[at]={message->hwnd,message->message,message->lParam,current->clock.nsecsElapsed()};
        }
        return CallNextHookEx(nullptr,code,argument,data);
    }
    static LRESULT CALLBACK leaving(int code,WPARAM argument,LPARAM data) {
        if(code>=0 && current && current->depth>0) {
            const int at=--current->depth;
            if(at<int(current->frames.size()))current->record(current->frames[at],current->clock.nsecsElapsed(),true);
        }
        return CallNextHookEx(nullptr,code,argument,data);
    }
    void finishRaw() {
        if(rawPending)record(raw,clock.nsecsElapsed(),false);
        rawPending=false;
    }
#endif
public:
    explicit PianoNativeProbe129(bool enabled) {
#ifdef Q_OS_WIN
        if(!enabled)return;
        clock.start();current=this;
        enterHook=SetWindowsHookExW(WH_CALLWNDPROC,entering,nullptr,GetCurrentThreadId());
        leaveHook=SetWindowsHookExW(WH_CALLWNDPROCRET,leaving,nullptr,GetCurrentThreadId());
        qApp->installNativeEventFilter(this);
        connect(QAbstractEventDispatcher::instance(),&QAbstractEventDispatcher::aboutToBlock,this,[this]{finishRaw();});
#else
        Q_UNUSED(enabled)
#endif
    }
    ~PianoNativeProbe129() override {
#ifdef Q_OS_WIN
        if(current!=this)return;
        qApp->removeNativeEventFilter(this);current=nullptr;
        if(enterHook)UnhookWindowsHookEx(enterHook);
        if(leaveHook)UnhookWindowsHookEx(leaveHook);
#endif
    }
    bool nativeEventFilter(const QByteArray&,void* message,long*) override {
#ifdef Q_OS_WIN
        finishRaw();const auto msg=static_cast<const MSG*>(message);
        raw={msg->hwnd,msg->message,msg->lParam,clock.nsecsElapsed()};rawPending=true;
#else
        Q_UNUSED(message)
#endif
        return false;
    }
    QVariantList report() {
        QVariantList rows;
#ifdef Q_OS_WIN
        if(current!=this)return rows;
        finishRaw();
        for(int index=0;index<count;++index) {
            const auto item=slow[index];wchar_t className[256]{},path[1024]{};
            if(item.frame.window)GetClassNameW(item.frame.window,className,256);
            HMODULE module=nullptr;
            if(item.frame.message==WM_TIMER && item.frame.data)
                GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(item.frame.data),&module);
            else if(item.frame.window)module=reinterpret_cast<HMODULE>(GetWindowLongPtrW(item.frame.window,GWLP_HINSTANCE));
            if(module)GetModuleFileNameW(module,path,1024);
            rows.append(QVariantMap{{"kind",item.paired?"windowProcedure":"dispatchInterval"},
                {"message",int(item.frame.message)},{"windowClass",QString::fromWCharArray(className)},
                {"module",QString::fromWCharArray(path)},{"milliseconds",item.milliseconds},
                {"atMilliseconds",item.frame.start/1.e6}});
        }
#endif
        return rows;
    }
};
