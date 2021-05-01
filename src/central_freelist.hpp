//
// Created by jamsonzan on 2021/4/29.
//

#ifndef TCMALLOC_CENTRAL_FREELIST_HPP
#define TCMALLOC_CENTRAL_FREELIST_HPP

#include <bits/stdc++.h>
#include <cstddef>

#include "span.hpp"
#include "size_class.hpp"
#include "page_heap.hpp"
#include "fixed_allocator.hpp"

namespace tcmalloc {

class CentralFreelist {
public:
    void Init(int cl) {
        class_ = cl;
        class_pages_ = ClassPages(cl);
        class_bytes_ = ClassSize(cl);
        num_to_move_ = ClassToMove(cl);

        cache_used_ = 0;
        cache_size_ = (1024 * 1024) / (class_bytes_ * num_to_move_);
        cache_size_ = std::max(1, cache_size_);
        cache_size_ = std::min(64, cache_size_);
        assert(1 <= cache_size_ && cache_size_ <= 64);

        span_nums_ = 0;
        free_objects_ = 0;

        ListInit(&empty_);
        ListInit(&nonempty_);
    }

    int FillFreeList(FreeList& freelist, uint64_t N) {
        assert(freelist.Empty());
        std::lock_guard<std::mutex> guard(lock_);
        if (N == num_to_move_ && cache_used_ > 0) {
            freelist = tc_slots[cache_used_-1];
            cache_used_--;
            assert(freelist.FreeObjects() == N);
            return N;
        }
        return FetchFromSpans(freelist, N);
    }

    void ReleaseFreeList(FreeList& freelist, uint64_t N) {
        assert(!freelist.Empty());
        std::lock_guard<std::mutex> guard(lock_);
        if (N == num_to_move_ && cache_used_ < cache_size_) {
            tc_slots[cache_used_] = freelist;
            cache_used_++;
            return;
        }
        return ReleaseToSpans(freelist, N);
    }

    bool CheckState() {
        assert(cache_used_ <= cache_size_);
        for (int i = 0; i < cache_used_; ++i) {
            assert(tc_slots[i].FreeObjects() == num_to_move_);
        }

        uint64_t span_nums = 0;
        uint64_t free_objects = 0;
        for (Span* it = empty_.next; it != &empty_; it = it->next) {
            assert((it)->size_class == class_);
            assert((it)->location == Span::IN_USE);
            assert((it)->refcount > 0);
            assert((it)->freelist.Empty());
            span_nums++;
        }
        for (Span* it = nonempty_.next; it != &nonempty_; it = it->next) {
            assert((it)->size_class == class_);
            assert((it)->location == Span::IN_USE);
            assert((it)->refcount >= 0);
            assert(!(it)->freelist.Empty());
            span_nums++;
            free_objects += (it)->freelist.FreeObjects();
        }
        assert(free_objects == free_objects_);
        assert(span_nums == span_nums_);
        return true;
    }

private:
    int FetchFromSpans(FreeList& freelist, int N) {
        int fetched = 0;
        fetched_nonempty:
        while (fetched < N && !ListEmpty(&nonempty_)) {
            Span* span = nonempty_.next;
            while (fetched < N && !span->freelist.Empty()) {
                fetched++;
                span->refcount++;
                free_objects_--;
                freelist.PushFront(span->freelist.PopFront());
            }
            if (span->freelist.Empty()) {
                RemoveFromList(span);
                InsertToList(&empty_, span);
            }
        }
        assert(CheckState());
        if (fetched < N) {
            Populate();
            goto fetched_nonempty;
        }
        return fetched;
    }

    void ReleaseToSpans(FreeList& freelist, int N) {
        while (!freelist.Empty()) {
            void* ptr = freelist.PopFront();
            Span* span = PageHeap::Instance()->GetSpanFromPageId(Span::PageIdFromPtr(ptr));
            assert(span->location == Span::IN_USE);
            assert(span->size_class == class_);

            if (span->freelist.Empty()) {
                RemoveFromList(span);
                InsertToList(&nonempty_, span);
            }

            span->refcount--;
            free_objects_++;
            span->freelist.PushFront(ptr);

            if (span->refcount == 0) {
                free_objects_ -= span->freelist.FreeObjects();
                RemoveFromList(span);
                PageHeap::Instance()->Delete(span);
            }
        }
        assert(CheckState());
    }

    void Populate() {
        Span* span = PageHeap::Instance()->New(class_pages_);
        assert(span->npages == class_pages_);
        PageHeap::Instance()->RegisterSizeClass(span, class_);

        free_objects_ += span->InitFreeList(class_bytes_);
        assert(span->freelist.FreeObjects() > 0);
        InsertToList(&nonempty_, span);
        assert(CheckState());
    }

    void InsertToList(Span* list, Span* span) {
        span_nums_++;
        ListInsert(list, span);
    }

    void RemoveFromList(Span* span) {
        span_nums_--;
        ListRemove(span);
    }

    int class_;
    uint64_t class_pages_;
    uint64_t class_bytes_;
    uint64_t num_to_move_;

    std::mutex lock_;
    FreeList tc_slots[64];
    int cache_size_;
    int cache_used_;

    uint64_t span_nums_;
    uint64_t free_objects_;
    Span  empty_;
    Span  nonempty_;
};

}

#endif //TCMALLOC_CENTRAL_FREELIST_HPP
