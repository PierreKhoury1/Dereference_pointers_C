#include "graph_eval.h"
#include "heap_gen.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Eval triple_deref(Heap* heap, int p, int q);
Eval field_chain(Heap* heap, int p, int q);
Eval guarded_chain(Heap* heap, int p, int q);
Eval alias_branch(Heap* heap, int p, int q);
Eval mixed_fields(Heap* heap, int p, int q);
Eval add_two(Heap* heap, int p, int q);

typedef Eval (*KernelFn)(Heap*, int, int);

typedef struct {
    const char* name;
    KernelFn fn;
    int fields[MAX_FIELDS];
    int num_fields;
    int use_p;
    int use_q;
} Kernel;

static void format_value(int tagged, char* buf, size_t n) {
    if (tagged == VAL_NULL) {
        snprintf(buf, n, "null");
    } else if (VAL_IS_INT(tagged)) {
        snprintf(buf, n, "%d", VAL_INT_VALUE(tagged));
    } else {
        snprintf(buf, n, "Ptr(%d)", VAL_PTR_ADDR(tagged));
    }
}

static void write_witness(const char* path, const Env* env, const Heap* heap, Eval kernel_res, Eval graph_res) {
    FILE* f = fopen(path, "w");
    if (!f) {
        return;
    }
    fprintf(f, "{\"env\":");
    env_write_json(env, f);
    fprintf(f, ",\"heap\":");
    heap_write_json(heap, f);
    fprintf(f, ",\"kernel\":{\"ok\":%d,\"err\":%d,\"value\":%d}",
            kernel_res.ok, kernel_res.err, kernel_res.value);
    fprintf(f, ",\"graph\":{\"ok\":%d,\"err\":%d,\"value\":%d}",
            graph_res.ok, graph_res.err, graph_res.value);
    fprintf(f, "}");
    fclose(f);
}

static int copy_file(const char* src, const char* dst) {
    FILE* in = fopen(src, "rb");
    FILE* out;
    char buf[4096];
    size_t n;
    if (!in) {
        return 0;
    }
    out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return 0;
    }
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        fwrite(buf, 1, n, out);
    }
    fclose(in);
    fclose(out);
    return 1;
}

static int ensure_dir(const char* path) {
#ifdef _WIN32
    const char* cmd = "mkdir";
#else
    const char* cmd = "mkdir -p";
#endif
    char buf[512];
    snprintf(buf, sizeof(buf), "%s %s", cmd, path);
    return system(buf) == 0;
}

int main(int argc, char** argv) {
    int trials = 200;
    unsigned seed = 1234;
    const char* graph_dir = "out";
    const char* out_dir = "out";
    int debug_one = 0;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--trials") == 0 && i + 1 < argc) {
            trials = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = (unsigned)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--graph_dir") == 0 && i + 1 < argc) {
            graph_dir = argv[++i];
        } else if (strcmp(argv[i], "--out_dir") == 0 && i + 1 < argc) {
            out_dir = argv[++i];
        } else if (strcmp(argv[i], "--debug_one") == 0) {
            debug_one = 1;
        }
    }

    if (debug_one) {
        trials = 1;
    }

    ensure_dir(out_dir);

    Kernel kernels[] = {
        {"triple_deref", triple_deref, {FIELD_DEREF}, 1, 1, 0},
        {"field_chain", field_chain, {FIELD_F, FIELD_G, FIELD_DEREF}, 3, 1, 0},
        {"guarded_chain", guarded_chain, {FIELD_DEREF}, 1, 1, 0},
        {"alias_branch", alias_branch, {FIELD_DEREF}, 1, 1, 1},
        {"mixed_fields", mixed_fields, {FIELD_F, FIELD_G, FIELD_DEREF}, 3, 1, 0},
        {"add_two", add_two, {FIELD_DEREF}, 1, 1, 1},
    };
    int num_kernels = (int)(sizeof(kernels) / sizeof(kernels[0]));

    for (i = 0; i < num_kernels; ++i) {
        Kernel* k = &kernels[i];
        char graph_path[512];
        Graph* graph;
        int t;
        int ok_count = 0;
        int fail_count = 0;
        int mismatch_count = 0;
        int witness_written = 0;
        Rng rng;

        snprintf(graph_path, sizeof(graph_path), "%s/%s.json", graph_dir, k->name);
        graph = graph_load_json(graph_path);
        if (!graph) {
            fprintf(stderr, "%s: missing graph %s\n", k->name, graph_path);
            continue;
        }

        rng_seed(&rng, seed);

        for (t = 0; t < trials; ++t) {
            Heap* heap = heap_create(6);
            Env env;
            Eval kernel_res;
            Eval graph_res;
            int same = 0;
            if (!heap) {
                break;
            }
            heap_randomize(heap, k->fields, k->num_fields, &rng);
            env_randomize(&env, heap->num_objs, &rng, k->use_p, k->use_q);

            kernel_res = k->fn(heap, env.p, env.q);
            graph_res = graph_eval(graph, heap, &env);

            if (debug_one) {
                printf("%s: graph=%s\n", k->name, graph_path);
                printf("  kernel: ok=%d err=%d value=%d\n", kernel_res.ok, kernel_res.err, kernel_res.value);
                printf("  graph:  ok=%d err=%d value=%d\n", graph_res.ok, graph_res.err, graph_res.value);
                printf("  env=");
                env_write_json(&env, stdout);
                printf("\n  heap=");
                heap_write_json(heap, stdout);
                printf("\n");
            }

            if (kernel_res.ok && graph_res.ok && kernel_res.value == graph_res.value) {
                ok_count++;
                same = 1;
            } else if (!kernel_res.ok && !graph_res.ok && kernel_res.err == graph_res.err) {
                fail_count++;
                same = 1;
            } else {
                mismatch_count++;
            }

            if (same && !witness_written) {
                char witness_path[512];
                snprintf(witness_path, sizeof(witness_path), "%s/%s_witness.json", out_dir, k->name);
                write_witness(witness_path, &env, heap, kernel_res, graph_res);
                witness_written = 1;
            }

            if (!same) {
                char witness_path[512];
                char graph_copy[512];
                snprintf(witness_path, sizeof(witness_path), "%s/%s_mismatch_%d.json", out_dir, k->name, t);
                write_witness(witness_path, &env, heap, kernel_res, graph_res);
                snprintf(graph_copy, sizeof(graph_copy), "%s/%s_mismatch_%d.graph.json", out_dir, k->name, t);
                copy_file(graph_path, graph_copy);
            }

            heap_free(heap);
        }

        {
            char valbuf[64];
            Heap* heap = heap_create(3);
            Env env;
            Eval witness_val;
            if (heap) {
                rng_seed(&rng, seed + 999u);
                heap_randomize(heap, k->fields, k->num_fields, &rng);
                env_randomize(&env, heap->num_objs, &rng, k->use_p, k->use_q);
                witness_val = k->fn(heap, env.p, env.q);
                if (witness_val.ok) {
                    format_value(witness_val.value, valbuf, sizeof(valbuf));
                    printf("%s: witness %s\n", k->name, valbuf);
                } else {
                    printf("%s: witness error %d\n", k->name, witness_val.err);
                }
                heap_free(heap);
            }
        }

        printf("  trials=%d ok=%d fail=%d mismatch=%d\n", trials, ok_count, fail_count, mismatch_count);
        if (mismatch_count) {
            printf("  WARNING: mismatches detected\n");
        }

        graph_free(graph);
    }

    return 0;
}
