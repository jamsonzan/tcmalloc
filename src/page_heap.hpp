//
// Created by jamsonzan on 2021/4/29.
//

#ifndef TCMALLOC_PAGE_HEAP_HPP
#define TCMALLOC_PAGE_HEAP_HPP

#include <stdint.h>
#include <bits/stdc++.h>
#include <mutex>

#include "system_alloc.hpp"
#include "span.hpp"
#include "page_map.hpp"

namespace tcmalloc {

class PageHeap
{
public:
    PageHeap() {
        stat.system_bytes = 0;
        stat.normal_bytes = 0;
        stat.returned_bytes = 0;
        stat.in_used_bytes = 0;

        stat.large_normal_bytes = 0;
        stat.large_returned_bytes = 0;

        release_index_ = 0;
        release_rate_ = 100;

        for (int i = 0; i <= spanSmallPages; ++i) {
            ListInit(&small_normal_[i]);
            ListInit(&small_returned_[i]);
        }
    }

    Span* New(uint64_t n) {
        std::lock_guard<std::mutex> guard(lock);
        Span* span = nullptr;
        span = SearchSmallAndLarge(n);
        if (span != nullptr) {
            return Carve(span, n);
        }

        // 没找到，但是有很多空闲内存，尝试合并碎片之后再找。
        if ( stat.normal_bytes != 0 && stat.returned_bytes != 0 &&
        (stat.normal_bytes + stat.returned_bytes) > (stat.system_bytes/4) &&
                (ReleaseNormalSpans(0x7fffffff) > 0) )
        {
            span = SearchSmallAndLarge(n);
            if (span != nullptr) {
                return Carve(span, n);
            }
        }

        if (GrowHeap(n)) {
            span = SearchSmallAndLarge(n);
            if (span != nullptr) {
                return Carve(span, n);
            }
        }
        return nullptr;
    }

    void Delete(Span* span) {
        std::lock_guard<std::mutex> guard(lock);
        MergeIntoFreeList(span);
    }

    void RegisterSizeClass(Span* span, uint64_t sc) {
        std::lock_guard<std::mutex> guard(lock);
        assert(span->location == Span::IN_USE);
        assert(page_map_.Get(span->page_id) == span);
        assert(page_map_.Get(span->page_id + span->npages - 1) == span);
        span->size_class = sc;
        for (int i = 1; i < span->npages - 1; ++i) {
            assert(page_map_.Set(span->page_id + i, span));
        }
    }

    // 如果已经RegisterSizeClass(span, sc)，id参数可以是span内的任意页的id，
    // 否则只能是span的首页或尾页
    Span* GetSpanFromPageId(uint64_t id) {
        return reinterpret_cast<Span*>(page_map_.Get(id));
    }

    bool CheckState() {
        std::lock_guard<std::mutex> guard(lock);
        assert(0 <= release_rate_);
        assert(release_index_ <= 128);
        assert((stat.normal_bytes + stat.returned_bytes + stat.in_used_bytes) == stat.system_bytes);
        assert(CheckSmallList());

        uint64_t normal_bytes = 0;
        uint64_t returned_bytes = 0;
        uint64_t large_normal_bytes = 0;
        uint64_t large_returned_bytes = 0;

        for (int i = 0; i <= spanSmallPages; ++i) {
            int count = small_normal_size_[i];
            for (Span* span = small_normal_[i].next; span != &small_normal_[i]; span = span->next) {
                count--;
                assert(count >= 0);
                assert(span->location == Span::IN_NORMAL);
                assert(page_map_.Get(span->page_id) == span);
                assert(page_map_.Get(span->page_id + span->npages - 1) == span);
                normal_bytes += span->npages * spanPageSize;
            }
            count = small_returned_size_[i];
            for (Span* span = small_returned_[i].next; span != &small_returned_[i]; span = span->next) {
                count--;
                assert(count >= 0);
                assert(span->location == Span::IN_RETURNED);
                assert(page_map_.Get(span->page_id) == span);
                assert(page_map_.Get(span->page_id + span->npages - 1) == span);
                returned_bytes += span->npages * spanPageSize;
            }
        }

        int count = large_normal_.size();
        for (auto it = large_normal_.begin(); it != large_normal_.end(); ++it) {
            count--;
            assert(count >= 0);
            assert((*it)->location == Span::IN_NORMAL);
            assert(page_map_.Get((*it)->page_id) == (*it));
            assert(page_map_.Get((*it)->page_id + (*it)->npages - 1) == (*it));
            normal_bytes += (*it)->npages * spanPageSize;
            large_normal_bytes += (*it)->npages * spanPageSize;
        }
        count = large_returned_.size();
        for (auto it = large_returned_.begin(); it != large_returned_.end(); ++it) {
            count--;
            assert(count >= 0);
            assert((*it)->location == Span::IN_RETURNED);
            assert(page_map_.Get((*it)->page_id) == (*it));
            assert(page_map_.Get((*it)->page_id + (*it)->npages - 1) == (*it));
            returned_bytes += (*it)->npages * spanPageSize;
            large_returned_bytes += (*it)->npages * spanPageSize;
        }

        assert(large_normal_bytes == stat.large_normal_bytes);
        assert(large_returned_bytes == stat.large_returned_bytes);
        assert(normal_bytes == stat.normal_bytes);
        assert(returned_bytes == stat.returned_bytes);

        return true;
    }

    static PageHeap* Instance() {
        static PageHeap page_heap;
        return &page_heap;
    }

    PageHeap(const PageHeap&) = delete;
    PageHeap& operator=(const PageHeap&) = delete;
private:

    Span* SearchSmallAndLarge(uint64_t n) {
        Span* span = nullptr;
        for (uint64_t i = n; i <= spanSmallPages; ++i) {
            if (!ListEmpty(&small_normal_[i])) {
                span = small_normal_[i].next;
                RemoveFromFreeList(span);
                assert(CheckSmallList());
                return span;
            }

            if (!ListEmpty(&small_returned_[i])) {
                span = small_returned_[i].next;
                RemoveFromFreeList(span);
                assert(CheckSmallList());
                return span;
            }
        }
        return AllocLarge(n);
    }

    Span* AllocLarge(uint64_t n) {
        Span bound;
        bound.npages = n;
        bound.page_id = 0;
        Span* best = nullptr;

        auto it_normal = large_normal_.upper_bound(&bound);
        if (it_normal != large_normal_.end()) {
            best = *it_normal;
        }

        auto it_returned = large_returned_.upper_bound(&bound);
        if (it_returned != large_returned_.end()) {
            if (best == nullptr || best->npages > (*it_returned)->npages) {
                best = *it_returned;
            }
        }

        if (best != nullptr) {
            RemoveFromFreeList(best);
        }
        assert(CheckSmallList());
        return best;
    }

    void MergeIntoFreeList(Span* span) {
        uint64_t old_npages = span->npages;
        assert(span->location == Span::IN_USE);
        stat.in_used_bytes -= span->npages * spanPageSize;
        span->location = Span::IN_NORMAL;

        // 尝试合并相邻span
        assert(CheckSmallList());
        span = MergePrevAndNextSpans(span);
        assert(CheckSmallList());
        InsertToFreeList(span);
        assert(CheckSmallList());

        // 检查是否需要释放normal状态的span
        release_rate_ -= old_npages;
        if (release_rate_ <= 0) {
            uint64_t released_pages = ReleaseNormalSpans(1);
            if (released_pages > 0) {
                release_rate_ = released_pages * 100;
            } else {
                release_rate_ = 100;
            }
        }
        assert(CheckSmallList());
    }

    Span* Carve(Span* span, uint64_t n) {
        auto old_location = span->location;
        span->location = Span::IN_USE;
        if (span->npages <= n) {
            stat.in_used_bytes += span->npages * spanPageSize;
            return span;
        }

        uint64_t extra = span->npages - n;
        Span* new_span = NewSpan(span->page_id + n, extra);

        span->npages = n;
        // oom
        assert(new_span != nullptr);
        assert(page_map_.Set(span->page_id + n - 1, span));
        assert(page_map_.Set(new_span->page_id, new_span));
        assert(page_map_.Set(new_span->page_id + extra -1, new_span));

        new_span->location = old_location;
        InsertToFreeList(new_span);
        stat.in_used_bytes += span->npages * spanPageSize;
        assert(CheckSmallList());
        return span;
    }

    bool GrowHeap(uint64_t n) {
        uint64_t alloc_size = n * spanPageSize;
        if (alloc_size < kSystemAlloc) {
            alloc_size = kSystemAlloc;
        }
        // 按页对齐
        alloc_size = ((alloc_size+spanPageSize-1)/spanPageSize)*spanPageSize;
        void* ptr = SystemAlloc(alloc_size);
        if (ptr == (void*)(-1)) {
            return false;
        }
        stat.system_bytes += alloc_size;

        Span* span = NewSpan((uint64_t)ptr/spanPageSize, alloc_size/spanPageSize);
        assert(span != nullptr);
        assert(page_map_.Set(span->page_id, span));
        assert(page_map_.Set(span->page_id + span->npages -1, span));

        span->location = Span::IN_NORMAL;
        InsertToFreeList(span);
        assert(CheckSmallList());
        return true;
    }

    uint64_t ReleaseNormalSpans(uint64_t npages) {
        uint64_t released_pages = 0;
        while ( released_pages < npages && stat.normal_bytes > 0 ) {
            release_index_++;
            assert(CheckSmallList());
            if (release_index_ >= spanSmallPages + 1) {
                release_index_ = 0;
                for (auto it = large_normal_.begin(); it != large_normal_.end(); ++it) {
                    released_pages += ReleaseSpan(*it);
                    break;
                }
                continue;
            }
            if (!ListEmpty(&small_normal_[release_index_])) {
                Span* span = small_normal_[release_index_].next;
                released_pages += ReleaseSpan(span);
                break;
            }
        }
        assert(CheckSmallList());
        return released_pages;
    }

    uint64_t ReleaseSpan(Span* span) {
        assert(span->location == Span::IN_NORMAL);
        if (!SystemRelease(
                reinterpret_cast<void *>(span->page_id * spanPageSize),
                span->npages * spanPageSize))
        {
            return 0;
        }
        uint64_t old_pages = span->npages;
        RemoveFromFreeList(span);
        span->location = Span::IN_RETURNED;
        assert(CheckSmallList());
        span = MergePrevAndNextSpans(span);
        assert(CheckSmallList());
        InsertToFreeList(span);
        assert(CheckSmallList());
        return old_pages;
    }

    Span* MergePrevAndNextSpans(Span* span) {
        Span* prev = reinterpret_cast<Span *>(page_map_.Get(span->page_id - 1));
        if (prev != nullptr && prev->location == span->location) {
            RemoveFromFreeList(prev);
            span = MergeSpanToNext(prev, span);
        }
        Span* next = reinterpret_cast<Span *>(page_map_.Get(span->page_id + span->npages));
        if (next != nullptr && next->location == span->location) {
            RemoveFromFreeList(next);
            span = MergeSpanToPrev(span, next);
        }
        assert(CheckSmallList());
        return span;
    }

    Span* MergeSpanToPrev(Span* prev, Span* next) {
        prev->npages += next->npages;
        DeleteSpan(next);
        assert(page_map_.Set(prev->page_id + prev->npages - 1, prev));
        return prev;
    }

    Span* MergeSpanToNext(Span* prev, Span* next) {
        next->page_id -= prev->npages;
        next->npages += prev->npages;
        DeleteSpan(prev);
        assert(page_map_.Set(next->page_id, next));
        return next;
    }

    void InsertToFreeList(Span* span) {
        assert((void*)span > (void*)(0x5));
        assert(span->location != Span::IN_USE);
        if (span->npages <= spanSmallPages) {
            if (span->location == Span::IN_NORMAL) {
                small_normal_size_[span->npages]++;
                stat.small_normal_bytes += span->npages * spanPageSize;
                ListInsert(&small_normal_[span->npages], span);
            } else {
                small_returned_size_[span->npages]++;
                stat.small_returned_bytes += span->npages * spanPageSize;
                ListInsert(&small_returned_[span->npages], span);
            }
        } else {
            if (span->location == Span::IN_NORMAL) {
                auto result = large_normal_.insert(span);
                // page_id不可能重复
                assert(result.second);
                stat.large_normal_bytes += span->npages * spanPageSize;
            } else {
                auto result = large_returned_.insert(span);
                assert(result.second);
                stat.large_returned_bytes += span->npages * spanPageSize;
            }
        }

        if (span->location == Span::IN_NORMAL) {
            stat.normal_bytes += span->npages * spanPageSize;
        } else {
            stat.returned_bytes += span->npages * spanPageSize;
        }

        assert(CheckSmallList());
    }

    void RemoveFromFreeList(Span* span) {
        assert(span->location != Span::IN_USE);
        if (span->npages <= spanSmallPages) {
            if (span->location == Span::IN_NORMAL) {
                small_normal_size_[span->npages]--;
                stat.small_normal_bytes -= span->npages * spanPageSize;
                ListRemove(span);
            } else {
                small_returned_size_[span->npages]--;
                stat.small_returned_bytes -= span->npages * spanPageSize;
                ListRemove(span);
            }
        } else {
            if (span->location == Span::IN_NORMAL) {
                stat.large_normal_bytes -= span->npages * spanPageSize;
                large_normal_.erase(span);
            } else {
                stat.large_returned_bytes -= span->npages * spanPageSize;
                large_returned_.erase(span);
            }
        }

        if (span->location == Span::IN_NORMAL) {
            stat.normal_bytes -= span->npages * spanPageSize;
        } else {
            stat.returned_bytes -= span->npages * spanPageSize;
        }

        assert(CheckSmallList());
    }

    bool CheckSmallList() {
        uint64_t small_normal_bytes = 0;
        uint64_t small_returned_bytes = 0;

        for (int i = 0; i <= spanSmallPages; ++i) {
            int count = small_normal_size_[i];
            for (Span* span = small_normal_[i].next; span != &small_normal_[i]; span = span->next) {
                count--;
                assert(count >= 0);
                assert(span->location == Span::IN_NORMAL);
                assert(page_map_.Get(span->page_id) == span);
                assert(page_map_.Get(span->page_id + span->npages - 1) == span);
                small_normal_bytes += span->npages * spanPageSize;
            }
            assert(count == 0);
            count = small_returned_size_[i];
            for (Span* span = small_returned_[i].next; span != &small_returned_[i]; span = span->next) {
                count--;
                assert(count >= 0);
                assert(span->location == Span::IN_RETURNED);
                assert(page_map_.Get(span->page_id) == span);
                assert(page_map_.Get(span->page_id + span->npages - 1) == span);
                small_returned_bytes += span->npages * spanPageSize;
            }
            assert(count == 0);
        }

        assert(small_normal_bytes == stat.small_normal_bytes);
        assert(small_returned_bytes == stat.small_returned_bytes);
        return true;
    }

    struct Stat {
        uint64_t     system_bytes;
        uint64_t     normal_bytes;
        uint64_t     returned_bytes;
        uint64_t     in_used_bytes;

        uint64_t     small_normal_bytes;
        uint64_t     small_returned_bytes;
        uint64_t     large_normal_bytes;
        uint64_t     large_returned_bytes;
    };

    static const uint64_t spanPageSize = Span::spanPageSize;
    static const uint64_t spanSmallPages = 127;
    static const uint64_t kSystemAlloc = 1024 * 1024 *1024;

    uint64_t release_index_;
    int64_t release_rate_;

    std::mutex lock;

    // pages [0, 127]
    Span small_normal_[spanSmallPages+1];
    Span small_returned_[spanSmallPages+1];
    uint64_t small_normal_size_[spanSmallPages+1];
    uint64_t small_returned_size_[spanSmallPages+1];

    // pages >= 128
    SpanSet large_normal_;
    SpanSet large_returned_;

    // page_id -> span
    PageMap page_map_;

    Stat stat;
};

}

#endif //TCMALLOC_PAGE_HEAP_HPP
