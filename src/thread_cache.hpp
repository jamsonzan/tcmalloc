//
// Created by jamsonzan on 2021/4/29.
//

#ifndef TCMALLOC_THREAD_CACHE_HPP
#define TCMALLOC_THREAD_CACHE_HPP

#include "pthread.h"
#include "size_class.hpp"
#include "span.hpp"
#include "central_freelist.hpp"
#include "thread_cache_freelist.hpp"

namespace tcmalloc {

class ThreadCache {
public:
    void Init() {
        total_alloc_ = 0;
        total_free_ = 0;
        prev = nullptr;
        next = nullptr;

        size_ = 0;
        max_size_ = 0;
        IncreaseCacheLimitLocked();
        if (max_size_ < 0) {
            max_size_ = kMinThreadCacheSize;
            unclaimed_cache_space -= kMinThreadCacheSize;
        }
        for (int cl = 0; cl < kMaxClass; ++cl) {
            freelists_[cl].Init(cl, ClassSize(cl));
        }
    }

    void* Alloc(size_t size, uint64_t cl) {
        assert(0 < cl && cl < kMaxClass);
        void* rv;
        if (!freelists_[cl].TryPop(&rv)) {
            FetchFromCentralCache(freelists_[cl]);
            freelists_[cl].TryPop(&rv);
        }
        size = ClassSize(cl);
        size_ -= size;
        total_alloc_ += size;
        return rv;
    }

    void Free(void* ptr, uint64_t cl) {
        assert(0 < cl && cl < kMaxClass);
        size_ += freelists_[cl].object_bytes();
        total_free_ += ClassSize(cl);
        int length = freelists_[cl].Push(ptr);
        if (length > freelists_[cl].max_length()) {
            ListTooLong(freelists_[cl]);
            return;
        }
        if (size_ > max_size_){
            Scavenge();
        }
    }

    void Clear() {
        for (int cl = 0; cl < kMaxClass; ++cl) {
            if (freelists_[cl].length() > 0) {
                ReleaseToCentralCache(freelists_[cl], freelists_[cl].length());
            }
        }
    }

    void ListTooLong(ThreadCacheFreeList& fl) {
        int batch_size = ClassToMove(fl.cl());
        ReleaseToCentralCache(fl, batch_size);

        // 慢启动阶段不管分配释放都增加max_length
        // 如果这时减去1的话就很难能增长到batch_size了
        // 小的max_length对于分配释放都不利，因为会导致
        // 过多与centrallist交互
        if (fl.max_length() < batch_size) {
            fl.set_max_length(fl.max_length() + 1);
        } else {
            // 大于等于batch_size，则每次overages计数大于3时减少batch_size
            // 可以看到，即使一个线程malloc/free配对，max_length也是增长的
            // 如果增长之后ThreadCacheFreeList空闲下来了，剩下的对象是由Scavenge
            // 函数回收的。
            // Scavenge，ListToolong，FetchFromCentralCache三个函数共同产生的影响，
            // 只要分析三种情况的趋势就行了，因为除了这些函数，线程还有max_size窃取机制，
            // 得出精确的max_length值是很难的。
            // 1. 线程malloc/free配对比较均衡，max_length的趋势为增长，如果后面对应的
            // class不再活跃，由Sacenge回收多余对象
            // 2. 线程malloc比free多，比如线程为生产者的情况，max_length的趋势为增长，
            // 而且增长得比情况1的快，最大增长到8192。
            // 3. 线程free比malloc多，比如线程为消费者的情况。max_length呈现出周期性，
            // 慢启动阶段max_length+1，达到batch_size后由于增加length_overages，当
            // 这个值>kMaxOverages时直接把max_length设置为0。这种情况max_length需要
            // 增加的原因是避免过多调用ListToolong归还对象给centrallists。
            fl.set_length_overages(fl.length_overages() + 1);
            if (fl.length_overages() > kMaxOverages) {
                fl.set_max_length(fl.max_length() - batch_size);
                fl.set_length_overages(0);
            }
        }

        if (size_ > max_size_) {
            Scavenge();
        }
    }

    void FetchFromCentralCache(ThreadCacheFreeList& fl) {
        assert(fl.empty());
        int batch_size = ClassToMove(fl.cl());
        int N = batch_size > fl.max_length()? fl.max_length() : batch_size;
        FreeList central_fl;
        int fetched = central_freelists[fl.cl()].FillFreeList(central_fl, N);
        assert(fetched == N);
        fl.PushFreeList(fetched, central_fl);
        size_ += fl.object_bytes() * fetched;

        // ThreadCacheFreeList填充了新的对象，说明比较活跃，增加max_length配额
        // 控制ThreadCacheFreeList的max_length慢启动，小于batch_size时+1，
        // 大于batch_size时加batch_size，一直增长到8192为止。
        if (fl.max_length() < batch_size) {
            fl.set_max_length(fl.max_length() + 1);
        } else {
            int new_length = fl.max_length() + batch_size;
            if (new_length > kMaxDynamicFreeListLength) {
                new_length = kMaxDynamicFreeListLength;
            }
            // kMaxDynamicFreeListLength可能不是batch_size的倍数
            // 通过减去余数限制max_length为batch_size的倍数
            new_length -= new_length % batch_size;
            assert(new_length % batch_size == 0);
            fl.set_max_length(new_length);
        }
    }

    void ReleaseToCentralCache(ThreadCacheFreeList& fl, int N) {
        if (fl.length() < N) {
            N = fl.length();
        }
        if (N <= 0) {
            return;
        }
        size_ -= fl.object_bytes() * N;
        int batch_size = ClassToMove(fl.cl());
        while (N > batch_size) {
            FreeList central_fl;
            fl.PopFreeList(batch_size, central_fl);
            central_freelists[fl.cl()].ReleaseFreeList(central_fl, batch_size);
            N -= batch_size;
        }
        FreeList central_fl;
        fl.PopFreeList(N, central_fl);
        central_freelists[fl.cl()].ReleaseFreeList(central_fl, N);
    }

    void Scavenge() {
        for (int cl = 0; cl < kMaxClass; ++cl) {
            ThreadCacheFreeList& fl = freelists_[cl];
            int batch_size = ClassToMove(fl.cl());
            if (fl.lowwatermark() > 0) {
                int to_release = fl.lowwatermark()/2;
                if (to_release < 1) {
                    to_release = 1;
                }
                ReleaseToCentralCache(fl, to_release);
                if (fl.max_length() > batch_size) {
                    int new_max_length = fl.max_length() - batch_size;
                    if (new_max_length < batch_size) {
                        new_max_length = batch_size;
                    }
                    fl.set_max_length(new_max_length);
                }
            }
            fl.clear_lowwatermark();
        }

        // 因为触发Scavenge的条件是size > max_size，尝试从请他线程窃取max_size
        IncreaseCacheLimit();
    }

    void IncreaseCacheLimit() {
        global_lock.lock();
        IncreaseCacheLimitLocked();
        global_lock.unlock();
    }

    void IncreaseCacheLimitLocked() {
        if (unclaimed_cache_space > 0) {
            unclaimed_cache_space -= kStealAmount;
            max_size_ += kStealAmount;
            return;
        }
        // 从其他线程偷，只检查10个，防止加锁太久和无限循环
        int check = 10;
        while (check > 0 && next_cache_steal != nullptr) {
            if (next_cache_steal != &cache_list &&
                next_cache_steal != this && next_cache_steal->max_size_ > kMinThreadCacheSize) {
                max_size_ += kStealAmount;
                next_cache_steal->max_size_ -= kStealAmount;
                next_cache_steal = next_cache_steal->next;
                return;
            }
            check--;
            next_cache_steal = next_cache_steal->next;
        }
    }

    // 当size_>=max_size_时执行Scavenge释放部分objects到centrallist
    uint64_t size_;
    uint64_t max_size_;
    ThreadCacheFreeList freelists_[kMaxClass];

    ThreadCache* prev;
    ThreadCache* next;
    uint64_t total_alloc_;
    uint64_t total_free_;
    uint64_t UsedSize() { return size_; }
    uint64_t GetTotalAlloc() { return total_alloc_; }
    uint64_t GetTotalFree() { return total_free_; }

    static void GlobalInit() {
        assert(!global_inited);
        for (int cl = 1; cl < kMaxClass; ++cl) {
            central_freelists[cl].Init(cl);
        }
        pthread_key_create(&spec_key, DestroyThreadCache);
        CacheListInit(&cache_list);
        next_cache_steal = &cache_list;
        global_inited = true;
    }

    static ThreadCache* Current() {
        if (tls_cache != nullptr) return tls_cache;

        global_lock.lock();
        if (!global_inited) {
            ThreadCache::GlobalInit();
        }
        assert(global_inited);
        ThreadCache* cache = thread_cache_allocator.Alloc();
        cache->Init();
        // pthread_setspecific可能调用malloc递归回这个函数
        // 先设置tls_cache递归基，再调用pthread_setspecific
        tls_cache = cache;
        global_lock.unlock();
        pthread_setspecific(spec_key, cache);

        global_lock.lock();
        CacheListInsert(&cache_list, cache);
        global_lock.unlock();
        return tls_cache;
    }

    static ThreadCache* CurrentMaybe() {
        return tls_cache;
    }

    static void RecomputePerThreadCacheSizeLocked() {
        int n = cache_list_size > 0 ? cache_list_size : 1;
        size_t space = overall_thread_cache_size / n;
        if (space < kMinThreadCacheSize) space = kMinThreadCacheSize;
        if (space > kMaxThreadCacheSize) space = kMaxThreadCacheSize;
        if (per_thread_cache_size < 1) {
            per_thread_cache_size = 1;
        }
        // per_thread_cache_size为之前线程cache大小的平均值，
        // space是新计算的线程cache大小平均值，计算增长率ratio作为每个
        // 线程max_size的增长率，这样处理比直接设置绝对值更平滑。
        double ratio = space / per_thread_cache_size;
        size_t claimed = 0;
        for (ThreadCache* cache = cache_list.next; cache != &cache_list; cache = cache->next) {
            cache->max_size_ = cache->max_size_ * ratio;
            claimed += cache->max_size_;
        }
        unclaimed_cache_space = overall_thread_cache_size - claimed;
        per_thread_cache_size = space;
    }

    static void SetOverAllThreadCacheSize(size_t new_size) {
        global_lock.lock();
        if (new_size < kMinThreadCacheSize) new_size = kMinThreadCacheSize;
        if (new_size > (1<<30)) new_size = (1<<30);
        overall_thread_cache_size = new_size;
        RecomputePerThreadCacheSizeLocked();
        global_lock.unlock();
    }

    static void DestroyThreadCache(void *ptr) {
        ThreadCache* cache = static_cast<ThreadCache *>(ptr);
        DeleteCache(cache);
    }

    static void DeleteCache(ThreadCache* cache) {
        cache->Clear();
        tls_cache = nullptr;
        global_lock.lock();
        if (next_cache_steal == cache) {
            next_cache_steal = cache->next;
        }
        CacheListRemove(cache);
        unclaimed_cache_space += cache->max_size_;
        thread_cache_allocator.Free(cache);
        global_lock.unlock();
    }

private:
    static const size_t kMinThreadCacheSize = kMaxSize * 2;
    static const size_t kMaxThreadCacheSize = 4 << 20;

    // 按照kMinThreadCacheSize和kMaxThreadCacheSize的值，
    // 32MB至多可以分给64个线程，至少可以分给8个线程。
    // ThreadCache初始化时，如果32MB分完了而且
    // 没从其他线程偷到，直接配额kMinThreadCacheSize。
    static const size_t kDefaultOverallThreadCacheSize = 32 << 20;
    static const size_t kStealAmount = 1 << 16;

    static const int kMaxOverages = 3;
    static const int kMaxDynamicFreeListLength = 8192;

    static size_t overall_thread_cache_size;
    static size_t per_thread_cache_size;
    static ssize_t unclaimed_cache_space;


    static __thread ThreadCache* tls_cache;
    static pthread_key_t spec_key;
    static bool global_inited;
    static std::mutex global_lock;

    static int cache_list_size;
    static ThreadCache cache_list;
    static ThreadCache* next_cache_steal;

    static CentralFreelist central_freelists[kMaxClass];


    static FixedAllocator<ThreadCache> thread_cache_allocator;


    static void CacheListInit(ThreadCache* cache) {
        cache->prev = cache;
        cache->next = cache;
    }
    static bool CacheListEmpty(ThreadCache* cache) {
        return cache->prev == cache;
    }
    static void CacheListRemove(ThreadCache* cache) {
        cache->prev->next = cache->next;
        cache->next->prev = cache->prev;
        cache->prev = nullptr;
        cache->next = nullptr;
        cache_list_size--;
    }
    static void CacheListInsert(ThreadCache* after, ThreadCache* cache) {
        cache->prev = after;
        cache->next = after->next;
        cache->next->prev = cache;
        after->next = cache;
        cache_list_size++;
    }
};

CentralFreelist ThreadCache::central_freelists[kMaxClass];
pthread_key_t ThreadCache::spec_key;
bool ThreadCache::global_inited = false;
std::mutex ThreadCache::global_lock;
int ThreadCache::cache_list_size = 0;
ThreadCache ThreadCache::cache_list;
ThreadCache* ThreadCache::next_cache_steal = nullptr;
size_t ThreadCache::overall_thread_cache_size = kDefaultOverallThreadCacheSize;
size_t ThreadCache::per_thread_cache_size = kMaxThreadCacheSize;
ssize_t ThreadCache::unclaimed_cache_space = kDefaultOverallThreadCacheSize;
FixedAllocator<ThreadCache> ThreadCache::thread_cache_allocator;
__thread ThreadCache* ThreadCache::tls_cache = nullptr;


}

#endif //TCMALLOC_THREAD_CACHE_HPP
