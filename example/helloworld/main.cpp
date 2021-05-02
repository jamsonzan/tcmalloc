//
// Created by jamsonzan on 2021/5/1.
//

#include <cassert>
#include <cstring>
#include <bits/stdc++.h>
#include "tcmalloc.h"

struct Entry {
    Entry* prev;
    Entry* next;
    void*  data;
};

class List {
public:
    List() : size(0) {
        head.prev = &head;
        head.next = &head;
        head.data = nullptr;
    }

    void PushFront(void *data) {
        Entry* entry =
                (Entry*)tcmalloc::malloc(sizeof(Entry));
        memset(entry, 0, sizeof(Entry));
        entry->data = data;
        ListInsert(&head, entry);
        size++;
    }

    void* PopFront() {
        void *data = head.next->data;
        if (head.next != &head) {
            Entry* entry = head.next;
            ListRemove(entry);
            tcmalloc::free(entry);
            size--;
        }
        return data;
    }

    ~List() { while (PopFront() != nullptr); }

    bool Empty() { return size == 0; }
    int Size() { return size; }
private:
    int size;
    Entry head;
    static void ListRemove(Entry* entry) {
        entry->prev->next = entry->next;
        entry->next->prev = entry->prev;
        entry->prev = nullptr;
        entry->next = nullptr;
    }
    static void ListInsert(Entry* after, Entry* entry) {
        entry->prev = after;
        entry->next = after->next;
        entry->next->prev = entry;
        after->next = entry;
    }
};

int main() {
    tcmalloc::free(tcmalloc::malloc(60));
    tcmalloc::free(tcmalloc::malloc(1024));
    tcmalloc::free(tcmalloc::malloc(1024 * 1024 * 1024));
    assert(tcmalloc::current_used_size() > 0);
    tcmalloc::clear_current_cache();
    assert(tcmalloc::current_used_size() == 0);
    tcmalloc::set_overall_thread_cache_size(1024*1024*128);

    void *large_ptr = tcmalloc::malloc(50*1024*1024);
    memset(large_ptr, 1, 50*1024*1024);
    tcmalloc::free(large_ptr);

    List list;
    for (int size = 0; size <= 5*1024; size++) {
        list.PushFront(tcmalloc::malloc(size));
    }
    for (int size = 5*1024; size >= 0; size--) {
        void* ptr = list.PopFront();
        memset(ptr, 1, size);
        tcmalloc::free(ptr);
    }
    assert(list.Empty());

    printf("Everything is OK!\n");
}
