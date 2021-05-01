add_executable(helloworld helloworld/main.cpp)

add_executable(print_index print_index/main.cpp)

add_executable(test test/main.cpp)

include_directories(../include)

include_directories(../src)

target_link_libraries(helloworld tcmalloc pthread)

target_link_libraries(test pthread)


