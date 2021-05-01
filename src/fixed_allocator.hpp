//
// Created by jamsonzan on 2021/4/29.
//

#ifndef TCMALLOC_FIXED_ALLOCATOR_HPP
#define TCMALLOC_FIXED_ALLOCATOR_HPP

#include <cassert>
#include <cstddef>

#include "system_alloc.hpp"

namespace tcmalloc {

template<class T>
class FixedAllocator {
public:
    FixedAllocator() {
        construct = true;
        assert(!inited);
    }
    T *Alloc() {
        if (!inited) {
            assert(construct);
            Init();
        }
        if (freelist_ != nullptr) {
            return AllocFromFreeList();
        }
        return AllocFromArea();
    }

    void Free(T *ptr) {
        assert(inited);
        FreeToFreeList(ptr);
    }

    bool FreeListEmpty() {
        return freelist_ == nullptr;
    }

    char* Area() {
        return area_;
    }
private:
    void Init() {
        assert(sizeof(void *) <= sizeof(T));
        assert(sizeof(T) <= areaSize_);
        area_ = nullptr;
        area_free_ = 0;
        freelist_ = nullptr;
        inited = true;
    }

    T *AllocFromFreeList() {
        void *result = freelist_;
        freelist_ = *(reinterpret_cast<void **>(result));
        return reinterpret_cast<T *>(result);
    }

    void FreeToFreeList(T *ptr) {
        *(reinterpret_cast<void **>(ptr)) = freelist_;
        freelist_ = ptr;
    }

    T *AllocFromArea() {
        if (area_free_ < sizeof(T)) {
            void *ptr = SystemAlloc(areaSize_);
            if (ptr == (void *) (-1)) {
                return nullptr;
            }
            area_ = (char *) ptr;
            area_free_ = areaSize_;
        }
        void *result = area_;
        area_ += sizeof(T);
        area_free_ -= sizeof(T);
        return reinterpret_cast<T *>(result);
    }

    static const int areaSize_ = 128 * 1024 * 1024;
    char *area_;
    uint64_t area_free_;
    void *freelist_;

    bool inited = false;
    bool construct = false;
};


template<typename T, int static_token>
class STLFixedAllocator {
public:
    typedef T value_type;
    typedef T *pointer;
    typedef const T *const_pointer;
    typedef T &reference;
    typedef const T &const_reference;
    typedef size_t size_type;
    typedef ptrdiff_t difference_type;
    template<class T1>
    struct rebind {
        typedef STLFixedAllocator<T1, static_token> other;
    };

    STLFixedAllocator() {}

    STLFixedAllocator(const STLFixedAllocator &) {}

    template<class T1>
    STLFixedAllocator(const STLFixedAllocator<T1, static_token> &) {}

    ~STLFixedAllocator() {}

    pointer address(reference x) const { return &x; }

    const_pointer address(const_reference x) const { return &x; }

    size_type max_size() const { return size_t(-1) / sizeof(T); }

    void construct(pointer p, const T &val) { ::new(p) T(val); }

    void construct(pointer p) { ::new(p) T(); }

    void destroy(pointer p) { p->~T(); }

    bool operator==(const STLFixedAllocator &) const { return true; }

    bool operator!=(const STLFixedAllocator &) const { return false; }

    pointer allocate(size_type n, const void * = 0) {
        assert(n == 1);
        return allocator.Alloc();
    }

    void deallocate(pointer p, size_type n) {
        assert(n == 1);
        allocator.Free(p);
    }

private:
    static FixedAllocator<T> allocator;
};

template<typename T, int static_token>
FixedAllocator<T> STLFixedAllocator<T, static_token>::allocator;

}
#endif //TCMALLOC_FIXED_ALLOCATOR_HPP
