#ifndef GRAPH_EVAL_H
#define GRAPH_EVAL_H

#include "checked_ptr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Graph Graph;

Graph* graph_load_json(const char* path);
void graph_free(Graph* graph);
Eval graph_eval(const Graph* graph, const Heap* heap, const Env* env);

#ifdef __cplusplus
}
#endif

#endif