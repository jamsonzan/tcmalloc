//
// Created by jamsonzan on 2021/4/29.
//

#ifndef TCMALLOC_THREAD_CACHE_HPP
#define TCMALLOC_THREAD_CACHE_HPP

#include "pthread.h"
#include "size_class.hpp"
#include "central_freelist.hpp"

namespace tcmalloc {

class ThreadCache {
public:
    void Init() {

    }

    void* Alloc(size_t size, int cl) {

    }

    void Free(void* ptr, int cl) {

    }

    void Clear() {

    }

    static ThreadCache* Current() {
        if (tls_cache_ != nullptr) return tls_cache_;
        if (!global_inited) {
            ThreadCache::GlobalInit();
        }
        assert(global_inited);
        ThreadCache* cache = thread_cache_allocator.Alloc();
        cache->Init();
        tls_cache_ = cache;
    }

    static void GlobalInit() {
        assert(!global_inited);
        for (int cl = 0; cl < kMaxClass; ++cl) {
            central_freelists->Init(cl);
        }
        global_inited = true;
    }

    static CentralFreelist central_freelists[kMaxClass];

private:
    static __thread ThreadCache* tls_cache_;

    static pthread_key_t key;
    static bool global_inited;
    static FixedAllocator<ThreadCache> thread_cache_allocator;
};

}


#endif //TCMALLOC_THREAD_CACHE_HPP
