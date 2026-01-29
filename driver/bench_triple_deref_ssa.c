#include "checked_ptr.h"
#include "heap_gen.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#if defined(__GNUC__) || defined(__clang__)
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE
#endif

NOINLINE Eval triple_deref(Heap* heap, int p, int q);

static uint64_t now_ns(void) {
#if defined(CLOCK_MONOTONIC_RAW)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

static Heap* build_good_heap(void) {
    Heap* heap = heap_create(4);
    Obj* o1;
    Obj* o2;
    Obj* o3;
    Obj* o4;

    if (!heap) {
        return NULL;
    }

    o1 = heap_get_obj(heap, 1);
    o2 = heap_get_obj(heap, 2);
    o3 = heap_get_obj(heap, 3);
    o4 = heap_get_obj(heap, 4);

    if (!o1 || !o2 || !o3 || !o4) {
        heap_free(heap);
        return NULL;
    }

    o1->has_field[FIELD_DEREF] = 1;
    o2->has_field[FIELD_DEREF] = 1;
    o3->has_field[FIELD_DEREF] = 1;
    o4->has_field[FIELD_DEREF] = 1;

    o1->value[FIELD_DEREF] = VAL_PTR(2);
    o2->value[FIELD_DEREF] = VAL_PTR(3);
    o3->value[FIELD_DEREF] = VAL_PTR(4);
    o4->value[FIELD_DEREF] = VAL_INT(7);

    return heap;
}

int main(int argc, char** argv) {
    uint64_t iters = 10000000ull;
    int i;
    Heap* heap;
    int p;
    uint64_t acc = 0;
    uint64_t start;
    uint64_t end;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--iters") == 0 && i + 1 < argc) {
            iters = (uint64_t)strtoull(argv[++i], NULL, 10);
        }
    }

    heap = build_good_heap();
    if (!heap) {
        fprintf(stderr, "failed to build heap\n");
        return 1;
    }

    p = VAL_PTR(1);

    for (i = 0; i < 1000; ++i) {
        uint64_t v = (uint64_t)triple_deref(heap, p, VAL_NULL).value;
        acc += (v + (uint64_t)i) * 2654435761u;
        acc ^= acc >> 13;
    }

    start = now_ns();
    for (uint64_t k = 0; k < iters; ++k) {
        uint64_t v = (uint64_t)triple_deref(heap, p, VAL_NULL).value;
        acc += (v + k) * 2654435761u;
        acc ^= acc >> 13;
    }
    end = now_ns();

    __asm__ volatile("" : "+r"(acc));

    printf("iters=%llu time_ns=%llu acc=%llu\n",
           (unsigned long long)iters,
           (unsigned long long)(end - start),
           (unsigned long long)acc);

    heap_free(heap);
    return 0;
}
