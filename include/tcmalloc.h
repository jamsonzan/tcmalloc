//
// Created by jamsonzan on 2021/5/1.
//

#ifndef TCMALLOC_TCMALLOC_H
#define TCMALLOC_TCMALLOC_H

#include <cstddef>

namespace tcmalloc {

    void *malloc(size_t size);

    void free(void* ptr);

}

#endif //TCMALLOC_TCMALLOC_H
