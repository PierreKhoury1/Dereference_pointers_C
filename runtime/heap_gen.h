#ifndef HEAP_GEN_H
#define HEAP_GEN_H

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FIELD_DEREF 0
#define FIELD_F 1
#define FIELD_G 2
#define MAX_FIELDS 3

typedef struct {
    int has_field[MAX_FIELDS];
    int value[MAX_FIELDS]; /* tagged values */
} Obj;

typedef struct {
    int num_objs;
    Obj* objs;
} Heap;

typedef struct {
    int p; /* tagged value */
    int q; /* tagged value */
} Env;

typedef struct {
    unsigned state;
} Rng;

void rng_seed(Rng* rng, unsigned seed);
unsigned rng_next(Rng* rng);
int rng_range(Rng* rng, int lo, int hi);
int rng_chance(Rng* rng, int percent);

Heap* heap_create(int num_objs);
void heap_free(Heap* heap);
Obj* heap_get_obj(Heap* heap, int addr);
int heap_get_field(const Obj* obj, int field, int* out_value);

void heap_randomize(Heap* heap, const int* fields, int num_fields, Rng* rng);
void env_randomize(Env* env, int num_objs, Rng* rng, int use_p, int use_q);

void heap_write_json(const Heap* heap, FILE* f);
void env_write_json(const Env* env, FILE* f);

#ifdef __cplusplus
}
#endif

#endif