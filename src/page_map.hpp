//
// Created by jamsonzan on 2021/4/29.
//

#ifndef TCMALLOC_PAGE_MAP_HPP
#define TCMALLOC_PAGE_MAP_HPP

#include <stdint.h>
#include <bits/stdc++.h>

#include "fixed_allocator.hpp"

namespace tcmalloc {

// 64 bits, three level radix tree
class PageMap {
public:
    PageMap() {
        memset(roots, 0, sizeof(roots));
    }

    void *Get(uint64_t key) {
        uint64_t k1 = key >> (internal_bits + leaf_bits);
        uint64_t k2 = (key >> leaf_bits) & ((1<<internal_bits) - 1);
        uint64_t k3 = key & ((1<<leaf_bits) -1);
        if (roots[k1] == nullptr || roots[k1][k2] == nullptr) {
            return nullptr;
        }
        return roots[k1][k2][k3];
    }

    bool Set(uint64_t key, void *value) {
        uint64_t k1 = key >> (internal_bits + leaf_bits);
        uint64_t k2 = (key >> leaf_bits) & ((1<<internal_bits) - 1);
        uint64_t k3 = key & ((1<<leaf_bits) -1);
        if (roots[k1] == nullptr) {
            roots[k1] = reinterpret_cast<void ***>(Alloc(INTERNAL));
            if (roots[k1] == nullptr) {
                return false;
            }
        }
        if (roots[k1][k2] == nullptr) {
            roots[k1][k2] = reinterpret_cast<void **>(Alloc(LEAF));
            if (roots[k1][k2] == nullptr) {
                return false;
            }
        }
        roots[k1][k2][k3] = value;
        return true;
    }

private:
    static const int internal_bits = 22;
    static const int internal_len = 1 << internal_bits;
    static const int leaf_bits = 20;
    static const int leaf_len = 1 << leaf_bits;

    enum Type { INTERNAL, LEAF };

    struct internal_type_help {
        void*** ptrs[internal_len];
    };
    struct leaf_type_help {
        void*** ptrs[leaf_len];
    };

    FixedAllocator<internal_type_help>  internal_allocator;
    FixedAllocator<leaf_type_help>  leaf_allocator;

    void*** roots[internal_len];

    void *Alloc(Type t) {
        void *ptr = nullptr;
        if (t == INTERNAL) {
            ptr = internal_allocator.Alloc();
            if (ptr != nullptr) {
                memset(ptr, 0, internal_len);
            }
        } else if (t == LEAF) {
            ptr = leaf_allocator.Alloc();
            if (ptr != nullptr) {
                memset(ptr, 0, leaf_len);
            }
        } else {
            assert(0);
        }
        return ptr;
    }

};

}
#endif //TCMALLOC_PAGE_MAP_HPP
