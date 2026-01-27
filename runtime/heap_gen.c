#include "heap_gen.h"
#include "checked_ptr.h"
#include <stdlib.h>

void rng_seed(Rng* rng, unsigned seed) {
    rng->state = seed ? seed : 1u;
}

unsigned rng_next(Rng* rng) {
    /* xorshift32 */
    unsigned x = rng->state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng->state = x;
    return x;
}

int rng_range(Rng* rng, int lo, int hi) {
    unsigned v = rng_next(rng);
    int span = hi - lo + 1;
    return lo + (int)(v % (unsigned)span);
}

int rng_chance(Rng* rng, int percent) {
    return (int)(rng_next(rng) % 100u) < percent;
}

Heap* heap_create(int num_objs) {
    Heap* heap = (Heap*)calloc(1, sizeof(Heap));
    int i, f;
    if (!heap) {
        return NULL;
    }
    heap->num_objs = num_objs;
    heap->objs = (Obj*)calloc((size_t)num_objs, sizeof(Obj));
    if (!heap->objs) {
        free(heap);
        return NULL;
    }
    for (i = 0; i < num_objs; ++i) {
        for (f = 0; f < MAX_FIELDS; ++f) {
            heap->objs[i].has_field[f] = 0;
            heap->objs[i].value[f] = 0;
        }
    }
    return heap;
}

void heap_free(Heap* heap) {
    if (!heap) {
        return;
    }
    free(heap->objs);
    free(heap);
}

Obj* heap_get_obj(Heap* heap, int addr) {
    if (!heap || addr <= 0 || addr > heap->num_objs) {
        return NULL;
    }
    return &heap->objs[addr - 1];
}

int heap_get_field(const Obj* obj, int field, int* out_value) {
    if (!obj || field < 0 || field >= MAX_FIELDS) {
        return 0;
    }
    if (!obj->has_field[field]) {
        return 0;
    }
    if (out_value) {
        *out_value = obj->value[field];
    }
    return 1;
}

void heap_randomize(Heap* heap, const int* fields, int num_fields, Rng* rng) {
    int i, j;
    if (!heap || !rng) {
        return;
    }
    for (i = 0; i < heap->num_objs; ++i) {
        Obj* obj = &heap->objs[i];
        for (j = 0; j < num_fields; ++j) {
            int field = fields[j];
            int make_ptr = 0;
            int make_null = 0;
            int value;
            obj->has_field[field] = 1;
            if (field == FIELD_DEREF) {
                make_ptr = rng_chance(rng, 70);
            } else {
                make_ptr = rng_chance(rng, 50);
            }
            if (make_ptr) {
                make_null = rng_chance(rng, 10);
                if (make_null) {
                    value = VAL_NULL;
                } else {
                    int addr = rng_range(rng, 1, heap->num_objs);
                    value = VAL_PTR(addr);
                }
            } else {
                value = VAL_INT(rng_range(rng, 0, 9));
            }
            obj->value[field] = value;
        }
    }
}

void env_randomize(Env* env, int num_objs, Rng* rng, int use_p, int use_q) {
    if (!env || !rng) {
        return;
    }
    if (use_p) {
        if (rng_chance(rng, 10)) {
            env->p = VAL_NULL;
        } else {
            env->p = VAL_PTR(rng_range(rng, 1, num_objs));
        }
    } else {
        env->p = VAL_NULL;
    }
    if (use_q) {
        if (rng_chance(rng, 10)) {
            env->q = VAL_NULL;
        } else {
            env->q = VAL_PTR(rng_range(rng, 1, num_objs));
        }
    } else {
        env->q = VAL_NULL;
    }
}

static void write_obj_json(const Obj* obj, FILE* f) {
    int field;
    int first = 1;
    fprintf(f, "{");
    for (field = 0; field < MAX_FIELDS; ++field) {
        if (!obj->has_field[field]) {
            continue;
        }
        if (!first) {
            fprintf(f, ",");
        }
        first = 0;
        fprintf(f, "\"%d\":%d", field, obj->value[field]);
    }
    fprintf(f, "}");
}

void heap_write_json(const Heap* heap, FILE* f) {
    int i;
    fprintf(f, "{");
    fprintf(f, "\"num_objs\":%d,\"objs\":[", heap->num_objs);
    for (i = 0; i < heap->num_objs; ++i) {
        if (i) {
            fprintf(f, ",");
        }
        write_obj_json(&heap->objs[i], f);
    }
    fprintf(f, "]}");
}

void env_write_json(const Env* env, FILE* f) {
    fprintf(f, "{\"p\":%d,\"q\":%d}", env->p, env->q);
}