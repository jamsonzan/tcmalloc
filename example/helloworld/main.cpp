//
// Created by jamsonzan on 2021/5/1.
//

#include "tcmalloc.h"

int main() {
    tcmalloc::free(tcmalloc::malloc(60));
    tcmalloc::free(tcmalloc::malloc(1024));
    tcmalloc::free(tcmalloc::malloc(1024 * 1024 * 1024));
}
