// Opt-in local test crash report. Production code has no dependency on this helper.
#ifdef Q_OS_WIN
#include <windows.h>
#include <dbghelp.h>
#include <cstdio>
#pragma comment(lib,"dbghelp.lib")
static LONG CALLBACK reportTestCrash(EXCEPTION_POINTERS* exception)
      {
      if(exception->ExceptionRecord->ExceptionCode!=EXCEPTION_ACCESS_VIOLATION)return EXCEPTION_CONTINUE_SEARCH;
      const auto path=qgetenv("PERFORMANCE_TRACE_CRASH");FILE* output=nullptr;fopen_s(&output,path.constData(),"a");if(!output)return EXCEPTION_CONTINUE_SEARCH;
      auto process=GetCurrentProcess();SymSetOptions(SYMOPT_LOAD_LINES|SYMOPT_UNDNAME|SYMOPT_DEFERRED_LOADS);SymInitialize(process,nullptr,TRUE);
      CONTEXT context=*exception->ContextRecord;STACKFRAME64 frame{};frame.AddrPC.Offset=context.Rip;frame.AddrStack.Offset=context.Rsp;frame.AddrFrame.Offset=context.Rsp;frame.AddrPC.Mode=frame.AddrStack.Mode=frame.AddrFrame.Mode=AddrModeFlat;
      fprintf(output,"Access violation %p, address %llx\n",exception->ExceptionRecord->ExceptionAddress,exception->ExceptionRecord->ExceptionInformation[1]);
      for(int i=0;i<60;++i){
            char storage[sizeof(SYMBOL_INFO)+2048]{};auto symbol=reinterpret_cast<SYMBOL_INFO*>(storage);symbol->SizeOfStruct=sizeof(SYMBOL_INFO);symbol->MaxNameLen=2047;DWORD64 displacement=0;
            HMODULE module=nullptr;wchar_t name[MAX_PATH]{};GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCWSTR>(frame.AddrPC.Offset),&module);GetModuleFileNameW(module,name,MAX_PATH);
            fprintf(output,"%ls+%llx ",name,frame.AddrPC.Offset-reinterpret_cast<DWORD64>(module));
            if(SymFromAddr(process,frame.AddrPC.Offset,&displacement,symbol))fprintf(output,"%s + %llx",symbol->Name,displacement);
            IMAGEHLP_LINE64 line{};line.SizeOfStruct=sizeof(line);DWORD offset=0;if(SymGetLineFromAddr64(process,frame.AddrPC.Offset,&offset,&line))fprintf(output," %s:%lu",line.FileName,line.LineNumber);fprintf(output,"\n");
            if(!StackWalk64(IMAGE_FILE_MACHINE_AMD64,process,GetCurrentThread(),&frame,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr))break;
            }
      fclose(output);return EXCEPTION_CONTINUE_SEARCH;
      }
static void installTestCrashReport(){if(!qgetenv("PERFORMANCE_TRACE_CRASH").isEmpty())AddVectoredExceptionHandler(1,reportTestCrash);}
#else
static void installTestCrashReport(){}
#endif
