cmake_minimum_required(VERSION 2.8)

###################################################################################
project(tcmalloc)

add_subdirectory(./example)

add_library(tcmalloc src/tcmalloc.cpp src/fixed_allocator.hpp src/span.hpp src/page_map.hpp src/page_heap.hpp src/system_alloc.hpp src/central_freelist.hpp src/thread_cache.hpp src/size_class.hpp include/tcmalloc.h src/thread_cache_freelist.hpp)

include_directories(./include)

include_directories(src)
