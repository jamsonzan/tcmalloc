#include "page_heap.hpp"
#include "size_class.hpp"
#include "span.hpp"
#include "thread_cache.hpp"
#include "tcmalloc.h"

namespace tcmalloc {

    void *malloc(size_t size) {
        int cl;
        if (SizeToClass(size, &cl)) {
            ThreadCache* curr = ThreadCache::Current();
            size_t alloc_size = ClassSize(cl);
            return curr->Alloc(alloc_size, cl);
        }
        uint64_t npages = size / Span::spanPageSize;
        Span* span = PageHeap::Instance()->New(npages);
        return reinterpret_cast<void *>(span->page_id * Span::spanPageSize);
    }

    void free(void* ptr) {
        uint64_t page_id = (uint64_t)ptr / Span::spanPageSize;
        Span* span = PageHeap::Instance()->GetSpanFromPageId(page_id);
        assert(span != nullptr && span->location == Span::IN_USE);
        if (span->size_class != 0 && span->size_class < kMaxClass) {
            ThreadCache* curr = ThreadCache::Current();
            curr->Free(ptr, span->size_class);
            return;
        }
        assert(reinterpret_cast<void *>(span->page_id*Span::spanPageSize) == ptr);
        PageHeap::Instance()->Delete(span);
    }

}
