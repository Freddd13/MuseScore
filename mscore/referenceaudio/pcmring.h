// Copyright (C) 2026 Freddd13 and contributors; GPL version 2.
#pragma once
#include <atomic>
#include <cstdint>
#include <vector>
#include <algorithm>
#include <cstring>
#include <thread>
#include <chrono>
namespace KumoAudio {
// One producer and one consumer. Capacity is fixed, independent of file length.
class PcmRing {
      std::vector<float> _samples;
      const unsigned _capacity;
      std::atomic<std::uint64_t> _read{0},_write{0};
   public:
      std::atomic_bool done{false};
      std::atomic<std::uint64_t> underruns{0};
      explicit PcmRing(unsigned frames=131072):_samples(frames*2,0),_capacity(frames) {}
      unsigned available() const {return unsigned(_write.load(std::memory_order_acquire)-_read.load(std::memory_order_acquire));}
      std::uint64_t consumed() const {return _read.load(std::memory_order_acquire);}
      bool push(const float* source,unsigned count,const std::atomic_bool& canceled) {
            while(count && !canceled.load()) {
                  const auto r=_read.load(std::memory_order_acquire),w=_write.load(std::memory_order_relaxed);
                  unsigned n=std::min(count,_capacity-unsigned(w-r));
                  if(!n){std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;}
                  const unsigned offset=unsigned(w%_capacity);n=std::min(n,_capacity-offset);
                  std::memcpy(_samples.data()+offset*2,source,n*2*sizeof(float));_write.store(w+n,std::memory_order_release);source+=n*2;count-=n;
                  }
            return !canceled.load();
            }
      unsigned pop(float* destination,unsigned count,bool add=false,float gain=1) {
            const auto r=_read.load(std::memory_order_relaxed),w=_write.load(std::memory_order_acquire);const unsigned available=std::min(count,unsigned(w-r));unsigned copied=0;
            while(copied<available) {
                  const unsigned offset=unsigned((r+copied)%_capacity),n=std::min(available-copied,_capacity-offset);
                  if(add || gain!=1)for(unsigned i=0;i<n*2;++i){const float value=_samples[offset*2+i]*gain;if(add)destination[copied*2+i]+=value;else destination[copied*2+i]=value;}
                  else std::memcpy(destination+copied*2,_samples.data()+offset*2,n*2*sizeof(float));
                  copied+=n;
                  }
            if(!add && available<count)std::fill(destination+available*2,destination+count*2,0.f);
            _read.store(r+available,std::memory_order_release);if(available<count && !done.load())++underruns;return available;
            }
      };
}
