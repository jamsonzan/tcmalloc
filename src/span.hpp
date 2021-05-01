//
// Created by jamsonzan on 2021/4/29.
//

#ifndef TCMALLOC_SPAN_HPP
#define TCMALLOC_SPAN_HPP

#include <bits/stdc++.h>
#include <cstdint>

#include "fixed_allocator.hpp"

namespace tcmalloc {

class FreeList {
public:
    FreeList() :head_(nullptr), free_count_(0) {}

    void PushFront(void* ptr) {
        *(reinterpret_cast<void **>(ptr)) = head_;
        head_ = ptr;
        free_count_++;
    }

    void* PopFront() {
        if (Empty()) return nullptr;
        void *result = head_;
        head_ = *(reinterpret_cast<void **>(result));
        free_count_--;
        return result;
    }

    int FreeObjects() {
        return free_count_;
    }

    void Clear() {
        head_ = nullptr;
        free_count_ = 0;
    }

    bool Empty() {
        return free_count_ == 0;
    }

private:
    uint64_t free_count_;
    void* head_;
};

struct Span {
    Span* prev;
    Span* next;

    uint64_t     page_id;
    uint64_t     npages;
    uint64_t     size_class;
    uint64_t     refcount;
    FreeList     freelist;

    static const uint64_t spanPageSize = 8 * 1024;
    enum Location { IN_USE, IN_NORMAL, IN_RETURNED };
    uint64_t     location;

    uint64_t InitFreeList(uint64_t obj_bytes) {
        assert(location == IN_USE);
        freelist.Clear();
        assert(freelist.Empty());
        uint64_t ptr = reinterpret_cast<uint64_t>(reinterpret_cast<char *>(page_id * spanPageSize));
        uint64_t limit = ptr + (npages * spanPageSize);
        while (ptr + obj_bytes <= limit) {
            freelist.PushFront((void*)ptr);
            ptr += obj_bytes;
        }
        return freelist.FreeObjects();
    }

    static uint64_t PageIdFromPtr(void* ptr) {
        return ((uint64_t)ptr)/spanPageSize;
    }
};

static tcmalloc::FixedAllocator<Span> span_allocator;

Span* NewSpan(uint64_t page_id, uint64_t npages) {
    Span* span = span_allocator.Alloc();
    memset(span, 0, sizeof(span));
    span->page_id = page_id;
    span->npages = npages;
    span->refcount = 0;
    return span;
}

void DeleteSpan(Span* span) {
    span_allocator.Free(span);
}

struct SpanLessCompare {
    bool operator() (const Span* lhs, const Span* rhs) const {
        if (lhs->npages < rhs->npages) return true;
        if (rhs->npages < lhs->npages) return false;
        return lhs->page_id < rhs->page_id;
    }
};

typedef std::set<Span*, SpanLessCompare, tcmalloc::STLFixedAllocator<Span*, 0>> SpanSet;

void ListInit(Span* span) {
    span->prev = span;
    span->next = span;
}

bool ListEmpty(Span* span) {
    return span->prev == span;
}

void ListRemove(Span* span) {
    span->prev->next = span->next;
    span->next->prev = span->prev;
    span->prev = nullptr;
    span->next = nullptr;
}

void ListInsert(Span* after, Span* span) {
    span->prev = after;
    span->next = after->next;
    span->next->prev = span;
    after->next = span;
}

}

#endif //TCMALLOC_SPAN_HPP
