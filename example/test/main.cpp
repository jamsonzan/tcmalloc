#include <new>
#include <cstdlib>
#include <bits/stdc++.h>
#include "fixed_allocator.hpp"
#include "page_map.hpp"
#include "span.hpp"
#include "page_heap.hpp"
#include "size_class.hpp"
#include "central_freelist.hpp"
#include "thread_cache.hpp"

/*
tcmalloc unit test.
*/

void TestFixAllocator() {
    printf("===================== TestFixAllocator BEGIN ===================\n");
    struct People {
        People(int age) : age_(age) {}
        int     age_;
        int     padding;
    };

    tcmalloc::FixedAllocator<People> allocator;
    People* p1 = allocator.Alloc();
    People* p2 = allocator.Alloc();
    assert(p1 != nullptr);
    assert(p2 != nullptr);
    char *old_area = allocator.Area();
    assert(old_area != nullptr);

    p1 = new (p1) People(18);
    p1->~People();

    assert(allocator.FreeListEmpty());
    allocator.Free(p1);
    assert(!allocator.FreeListEmpty());
    allocator.Free(p2);
    assert(!allocator.FreeListEmpty());

    assert(allocator.Alloc() != nullptr);
    assert(!allocator.FreeListEmpty());
    assert(allocator.Alloc() != nullptr);
    assert(allocator.FreeListEmpty());
    assert(old_area = allocator.Area());

    printf("===================== TestFixAllocator PASS =====================\n");
}

void TestFreeList() {
    printf("===================== TestFreeList BEGIN =====================\n");
    struct People {
        People(int age) : age_(age) {}
        int     age_;
        int     padding;
    };
    tcmalloc::FixedAllocator<People> allocator;

    std::list<People*> check_list;
    tcmalloc::FreeList freelist;
    assert(freelist.Empty());
    for (int i = 0; i < 10; ++i) {
        People* ptr = allocator.Alloc();
        check_list.push_front(ptr);
        freelist.PushFront(ptr);
        assert(freelist.FreeObjects() == (i+1));
    }
    for (int i = 10; i > 0; --i) {
        assert(freelist.FreeObjects() == i);
        assert((People*)freelist.PopFront() == check_list.front());
        allocator.Free(check_list.front());
        check_list.pop_front();
    }
    assert(freelist.Empty());
    printf("===================== TestFreeList Finish =====================\n");
}

void TestSpanSet() {
    printf("===================== TestSpanSet BEGIN =====================\n");
    tcmalloc::SpanSet set;
    tcmalloc::Span* s1 = tcmalloc::NewSpan(4, 2);
    tcmalloc::Span* s2 = tcmalloc::NewSpan(1, 3);
    tcmalloc::Span* s3 = tcmalloc::NewSpan(10, 3);
    tcmalloc::Span* s4 = tcmalloc::NewSpan(12, 5);

    set.insert(s1);
    set.insert(s2);
    set.insert(s3);
    set.insert(s4);
    assert(set.size() == 4);
    set.insert(s1);
    assert(set.size() == 4);

    tcmalloc::Span bound;
    bound.npages = 3;
    bound.page_id = 0;
    assert((*(set.upper_bound(&bound))) == s2);
    bound.npages = 4;
    bound.page_id = 0;
    assert((*(set.upper_bound(&bound))) == s4);

    tcmalloc::DeleteSpan(s1);
    tcmalloc::DeleteSpan(s2);
    tcmalloc::DeleteSpan(s3);
    tcmalloc::DeleteSpan(s4);
    printf("===================== TestSpanSet PASS =====================\n");
}

void TestPageMap() {
    printf("===================== TestPageMap BEGIN =====================\n");
    auto* pm = new tcmalloc::PageMap();
    assert(pm->Set(0x1, (void*)2));
    assert(pm->Set(0x2, (void*)2));
    assert(pm->Set(0x2, (void*)3));
    assert(pm->Get(0x1) == (void*)2);
    assert(pm->Get(0x3) == nullptr);

    assert(pm->Set(0x0f00fff0, (void*)2));
    assert(pm->Set(0x0f00fff1, (void*)2));
    assert(pm->Set(0xff00fff0, (void*)3));
    assert(pm->Get(0x0f00fff1) == (void*)2);
    assert(pm->Get(0xff00fff0) == (void*)3);
    assert(pm->Get(0x8f00fff0) == nullptr);

    assert(pm->Set(0xfff100000f00fff0, (void*)2));
    assert(pm->Set(0xfff200000f00fff1, (void*)2));
    assert(pm->Set(0xfff30000ff00fff0, (void*)3));
    assert(pm->Get(0xfff200000f00fff1) == (void*)2);
    assert(pm->Get(0xfff30000ff00fff0) == (void*)3);
    assert(pm->Get(0xffff00008f00fff0) == nullptr);
    delete pm;
    printf("===================== TestPageMap PASS =====================\n");
}

void TestPageHeap() {
    printf("===================== TestPageHeap BEGIN =====================\n");
    auto* page_heap = new tcmalloc::PageHeap();

    tcmalloc::Span* s1 = page_heap->New(10);
    assert(page_heap->CheckState());
    tcmalloc::Span* s2 = page_heap->New(30);
    assert(page_heap->CheckState());
    tcmalloc::Span* s3 = page_heap->New(300);
    assert(page_heap->CheckState());
    assert(s1 != nullptr);
    assert(s2 != nullptr);
    assert(s3 != nullptr);

    page_heap->Delete(s1);
    assert(page_heap->CheckState());
    page_heap->Delete(s2);
    assert(page_heap->CheckState());
    page_heap->Delete(s3);
    assert(page_heap->CheckState());

    for (int i = 0; i < 1000; ++i) {
        int alloc = (rand() % 100) + 1;
        std::vector<tcmalloc::Span*> spans;
        for (int j = 0; j < alloc; ++j) {
            int n = (rand() % 150) + 1;
            spans.push_back(page_heap->New(n));
            assert(page_heap->CheckState());
        }
        // shuffle
        for (int j = 0; j < alloc; ++j) {
            int pos = (rand() % (alloc-j));
            tcmalloc::Span *tmp = spans[j];
            spans[j] = spans[pos];
            spans[pos] = tmp;
        }
        for (int j = 0; j < alloc; ++j) {
            page_heap->Delete(spans[j]);
            assert(page_heap->CheckState());
        }
    }

    //  > 1GB
    tcmalloc::Span* very_large1 = page_heap->New(1024 * 150);
    assert(page_heap->CheckState());
    page_heap->Delete(very_large1);
    assert(page_heap->CheckState());

    tcmalloc::Span* very_large2 = page_heap->New(1024 * 150);
    assert(page_heap->CheckState());
    page_heap->Delete(very_large2);
    assert(page_heap->CheckState());

    delete page_heap;
    printf("===================== TestPageHeap Finish =====================\n");
}

void TestCentralFreeList() {
    printf("===================== TestCentralFreeList BEGIN =====================\n");
    for (int cl = 1; cl < tcmalloc::kMaxClass; ++cl) {
        tcmalloc::CentralFreelist central_freelist;
        central_freelist.Init(cl);

        for (int i = 0; i < 100; ++i) {
            int alloc = (rand() % 10) + 1;
            std::vector<tcmalloc::FreeList> freelists;
            freelists.resize(alloc);
            for (int j = 0; j < alloc; ++j) {
                int N = (rand() % 50) + 1;
                if (j < alloc/3 || j > 2*alloc/3) {
                    N = tcmalloc::ClassToMove(cl);
                }
                central_freelist.FillFreeList(freelists[j], N);
                assert(freelists[j].FreeObjects() == N);
                assert(central_freelist.CheckState());
            }
            for (int j = 0; j < alloc; ++j) {
                int pos = (rand() % (alloc-j));
                std::swap(freelists[j], freelists[pos]);
            }
            for (int j = 0; j < alloc; ++j) {
                central_freelist.ReleaseFreeList(freelists[j], freelists[j].FreeObjects());
                assert(central_freelist.CheckState());
            }
        }

    }
    printf("===================== TestCentralFreeList Finish =====================\n");
}

void TestThreadCache() {
    printf("===================== TestThreadCache BEGIN =====================\n");
    std::vector<std::thread> threads;
    for (int i = 0; i < 10; ++i) {
        threads.emplace_back([](){
            struct People {
                People(int age) : age_(age) {}
                int     age_;
                int     padding;
            };
            int cl;
            assert(sizeof(People) == 8);
            assert(tcmalloc::SizeToClass(8, &cl) && cl == 1 && tcmalloc::ClassSize(cl) == 8);

            tcmalloc::ThreadCache* curr = tcmalloc::ThreadCache::Current();
            assert(curr->GetTotalAlloc() == 0 && curr->GetTotalFree() == 0);
            auto p1 = (People*)curr->Alloc(8, 1);
            assert(p1 != nullptr);
            assert(curr->GetTotalAlloc() == 8 && curr->GetTotalFree() == 0);
            p1->age_ = 18; p1->padding = 1;
            curr->Free(p1, 1);

            // wait for other threads
            std::this_thread::sleep_for(std::chrono::seconds(1));
            assert(curr->GetTotalAlloc() == 8 && curr->GetTotalFree() == 8);

            auto void0 = curr->Alloc(16, 2);
            auto void1 = curr->Alloc(16, 2);
            assert(curr->GetTotalAlloc() == 40 && curr->GetTotalFree() == 8);
            curr->Free(void0, 2);
            curr->Free(void1, 2);
            assert(curr->GetTotalAlloc() == 40 && curr->GetTotalFree() == 40);

            for (int i = 0; i < 10; ++i) {
                int alloc = (rand() % (1024*100)) + 1;
                std::vector<void*> ptrs;
                std::vector<int> cls;
                ptrs.resize(alloc);
                cls.resize(alloc);
                for (int j = 0; j < alloc; ++j) {
                    int size = (rand() % 50) + 1;
                    int cl;
                    assert(tcmalloc::SizeToClass(size, &cl));
                    cls[j] = cl;
                    ptrs[j] = curr->Alloc(size, cl);
                    // check mem
                    memset(ptrs[j], 1, size);
                    assert(curr->GetTotalAlloc() > curr->GetTotalFree());
                    int extra = (rand() % 10);
                    if (extra < 2) {
                        void *ptr = curr->Alloc(size, cl);
                        curr->Free(ptr, cl);
                    }
                }
                for (int j = 0; j < alloc; ++j) {
                    curr->Free(ptrs[j], cls[j]);
                }
                assert(curr->GetTotalAlloc() == curr->GetTotalFree());
                std::this_thread::sleep_for(std::chrono::milliseconds (100));
            }

        });
    }
    for (auto it = threads.begin(); it != threads.end(); ++it) {
        it->join();
    }
    printf("===================== TestThreadCache Finish =====================\n");
}

int main()
{
    TestFixAllocator();
    TestFreeList();
    TestSpanSet();
    TestPageMap();
    TestPageHeap();
    TestCentralFreeList();
    TestThreadCache();
}
