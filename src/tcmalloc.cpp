#include "page_heap.hpp"
#include "thread_cache.hpp"
#include "tcmalloc.h"

namespace tcmalloc {

    void *malloc(size_t size) {
        auto ph = PageHeap::Instance();
        ph->Delete(ph->New(1));
        return nullptr;
    }

    void free(void* ptr) {
        auto ph = PageHeap::Instance();
        ph->Delete(ph->New(150));
    }

}
