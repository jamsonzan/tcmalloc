//
// Created by jamsonzan on 2021/5/2.
//

#ifndef TCMALLOC_THREAD_CACHE_FREELIST_HPP
#define TCMALLOC_THREAD_CACHE_FREELIST_HPP

#include <cstdint>

#include "span.hpp"

namespace tcmalloc {

    class ThreadCacheFreeList {
    private:
        FreeList list_;
        uint32_t lowater_;
        uint32_t max_length_;
        uint32_t length_overages_;
        uint64_t obj_bytes_;

        int cl_;

    public:
        void Init(int cl, size_t size) {
            assert(cl < kMaxClass);
            lowater_ = 0;
            max_length_ = 1;
            length_overages_ = 0;
            obj_bytes_ = size;
            cl_ = cl;
        }

        size_t length() {
            return list_.FreeObjects();
        }

        int32_t object_bytes() const {
            return obj_bytes_;
        }

        int cl() const {
            return cl_;
        }

        size_t max_length() const {
            return max_length_;
        }
        void set_max_length(size_t new_max) {
            max_length_ = new_max;
        }

        size_t length_overages() const {
            return length_overages_;
        }
        void set_length_overages(size_t new_count) {
            length_overages_ = new_count;
        }

        bool empty() {
            return list_.Empty();
        }

        int lowwatermark() const { return lowater_; }
        void clear_lowwatermark() { lowater_ = list_.FreeObjects(); }

        int Push(void* ptr) {
            list_.PushFront(ptr);
            return list_.FreeObjects();
        }

        void* Pop() {
            assert(!list_.Empty());
            void* ptr = list_.PopFront();
            if (list_.FreeObjects() < lowater_) lowater_ = list_.FreeObjects();
            return ptr;
        }

        bool TryPop(void **rv) {
            if (!list_.Empty()) {
                *rv = list_.PopFront();
                if (list_.FreeObjects() < lowater_) lowater_ = list_.FreeObjects();
                return true;
            }
            return false;
        }

        void PushFreeList(int N, FreeList& fl) {
            while (!fl.Empty()) {
                list_.PushFront(fl.PopFront());
            }
        }

        void PopFreeList(int N, FreeList& fl) {
            assert(list_.FreeObjects() >= N);
            int pop = 0;
            while (pop < N) {
                fl.PushFront(list_.PopFront());
                pop++;
            }
            if (list_.FreeObjects() < lowater_) lowater_ = list_.FreeObjects();
        }
    };

}

#endif //TCMALLOC_THREAD_CACHE_FREELIST_HPP
