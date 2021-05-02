//
// Created by jamsonzan on 2021/5/1.
//

#ifndef TCMALLOC_TCMALLOC_H
#define TCMALLOC_TCMALLOC_H

#include <cstddef>

namespace tcmalloc {

    void *malloc(size_t size);

    void free(void* ptr);

    void clear_current_cache();

    size_t current_used_size();

    void set_overall_thread_cache_size(size_t new_size);

}

#endif //TCMALLOC_TCMALLOC_H
