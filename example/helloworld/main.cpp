//
// Created by jamsonzan on 2021/5/1.
//

#include "tcmalloc.h"

int main() {
    tcmalloc::free(tcmalloc::malloc(60));
    tcmalloc::free(tcmalloc::malloc(1024));
    tcmalloc::free(tcmalloc::malloc(1024 * 1024 * 1024));
    tcmalloc::clear_current_cache();
    tcmalloc::current_used_size();
    tcmalloc::set_overall_thread_cache_size(1024*1024*128);
}
