//
// Created by jamsonzan on 2021/5/1.
//

#include "tcmalloc.h"

int main() {
    tcmalloc::free(tcmalloc::malloc(1));
}
