#include "checked_ptr.h"
#include "heap_gen.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

Eval graph_walk(Heap* heap, int p, int q);

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

static Heap* build_chain_heap(int len) {
    Heap* heap = heap_create(len);
    if (!heap) {
        return NULL;
    }

    for (int i = 1; i <= len; ++i) {
        Obj* obj = heap_get_obj(heap, i);
        if (!obj) {
            heap_free(heap);
            return NULL;
        }
        obj->has_field[FIELD_DEREF] = 1;
    }

    for (int i = 1; i < len; ++i) {
        Obj* obj = heap_get_obj(heap, i);
        obj->value[FIELD_DEREF] = VAL_PTR(i + 1);
    }

    Obj* last = heap_get_obj(heap, len);
    last->value[FIELD_DEREF] = VAL_PTR(len);

    return heap;
}

int main(int argc, char** argv) {
    uint64_t iters = 10000000ull;
    int len = 6;
    int i;
    Heap* heap;
    int p;
    volatile uint64_t sink = 0;
    uint64_t start;
    uint64_t end;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--iters") == 0 && i + 1 < argc) {
            iters = (uint64_t)strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--len") == 0 && i + 1 < argc) {
            len = atoi(argv[++i]);
        }
    }

    if (len < 5) {
        fprintf(stderr, "len must be >= 5\n");
        return 1;
    }

    heap = build_chain_heap(len);
    if (!heap) {
        fprintf(stderr, "failed to build heap\n");
        return 1;
    }

    p = VAL_PTR(1);

    for (i = 0; i < 1000; ++i) {
        Eval e = graph_walk(heap, p, VAL_NULL);
        sink += (uint64_t)e.value;
    }

    start = now_ns();
    for (uint64_t k = 0; k < iters; ++k) {
        Eval e = graph_walk(heap, p, VAL_NULL);
        sink += (uint64_t)e.value;
    }
    end = now_ns();

    printf("iters=%llu time_ns=%llu sink=%llu\n",
           (unsigned long long)iters,
           (unsigned long long)(end - start),
           (unsigned long long)sink);

    heap_free(heap);
    return 0;
}
