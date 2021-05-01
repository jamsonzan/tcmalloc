//
// Created by jamsonzan on 2021/4/29.
//

#ifndef TCMALLOC_SYSTEM_ALLOC_HPP
#define TCMALLOC_SYSTEM_ALLOC_HPP

#include <cstdint>
#include <sys/mman.h>

namespace tcmalloc {

void *SystemAlloc(size_t len) {
    return mmap(nullptr, len,
                PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS,
                -1, 0);
}

bool SystemRelease(void *start, size_t n) {
    int result = madvise(start, n, MADV_FREE);
    return result != -1;
}

}

#endif //TCMALLOC_SYSTEM_ALLOC_HPP
