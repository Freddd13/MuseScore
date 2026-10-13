// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#pragma once
#include <cstdint>
// The optional module receives only audio/time values, never musical objects.
using KumoReferenceMix = void (*)(void*,float*,unsigned,int,bool);
struct KumoReferenceHost {
      unsigned version=1;
      int (*sampleRate)()=nullptr;
      bool (*attach)(KumoReferenceMix,void*)=nullptr;
      void (*detach)(void*)=nullptr;
      };
