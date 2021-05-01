//
// Created by jamsonzan on 2021/4/29.
//

#ifndef TCMALLOC_THREAD_CACHE_HPP
#define TCMALLOC_THREAD_CACHE_HPP

#include "pthread.h"
#include "size_class.hpp"
#include "span.hpp"
#include "central_freelist.hpp"

namespace tcmalloc {

class ThreadCache {
public:
    void Init() {
        total_alloc = 0;
        total_free = 0;
    }

    void* Alloc(size_t size, uint64_t cl) {
        assert(0 < cl && cl < kMaxClass);
        FreeList fl;
        central_freelists[cl].FillFreeList(fl, 1);
        total_alloc += size;
        return fl.PopFront();
    }

    void Free(void* ptr, uint64_t cl) {
        assert(0 < cl && cl < kMaxClass);
        FreeList fl;
        fl.PushFront(ptr);
        central_freelists[cl].ReleaseFreeList(fl, 1);
        total_free += ClassSize(cl);
    }

    void Clear() {

    }

    uint64_t GetTotalAlloc() {
        return total_alloc;
    }

    uint64_t GetTotalFree() {
        return total_free;
    }

    static ThreadCache* Current() {
        if (tls_cache_ != nullptr) return tls_cache_;

        global_inited_lock.lock();
        if (!global_inited) {
            ThreadCache::GlobalInit();
        }
        assert(global_inited);
        global_inited_lock.unlock();

        ThreadCache* cache = thread_cache_allocator.Alloc();
        cache->Init();
        // pthread_setspecific可能调用malloc递归回这个函数
        // 先设置tls_cache递归基，再调用pthread_setspecific
        tls_cache_ = cache;
        pthread_setspecific(spec_key, cache);
        return tls_cache_;
    }

    static void GlobalInit() {
        assert(!global_inited);
        for (int cl = 1; cl < kMaxClass; ++cl) {
            central_freelists[cl].Init(cl);
        }
        pthread_key_create(&spec_key, DestroyThreadCache);
        global_inited = true;
    }

    static void DestroyThreadCache(void *ptr) {
        ThreadCache* cache = static_cast<ThreadCache *>(ptr);
        DeleteCache(cache);
    }

    static void DeleteCache(ThreadCache* cache) {
        cache->Clear();
        tls_cache_ = nullptr;
        thread_cache_allocator.Free(cache);
    }

    static CentralFreelist central_freelists[kMaxClass];

private:
    static __thread ThreadCache* tls_cache_;

    static pthread_key_t spec_key;
    static bool global_inited;
    static std::mutex global_inited_lock;
    static FixedAllocator<ThreadCache> thread_cache_allocator;
    uint64_t padding_fixed_allocator;

    uint64_t total_alloc;
    uint64_t total_free;
};

CentralFreelist ThreadCache::central_freelists[kMaxClass];
pthread_key_t ThreadCache::spec_key;
bool ThreadCache::global_inited;
std::mutex ThreadCache::global_inited_lock;
FixedAllocator<ThreadCache> ThreadCache::thread_cache_allocator;
__thread ThreadCache* ThreadCache::tls_cache_;

}


#endif //TCMALLOC_THREAD_CACHE_HPP
