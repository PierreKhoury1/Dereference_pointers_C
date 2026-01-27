#include "graph_eval.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    TK_EOF,
    TK_LBRACE,
    TK_RBRACE,
    TK_LBRACKET,
    TK_RBRACKET,
    TK_COLON,
    TK_COMMA,
    TK_STRING,
    TK_NUMBER,
    TK_TRUE,
    TK_FALSE,
    TK_NULL
} TokenKind;

typedef struct {
    TokenKind kind;
    const char* start;
    int len;
    long num;
} Token;

typedef struct {
    int id;
    char kind[24];
    char name[32];
    int x;
    int y;
    int field;
    int value;
    int cond;
    int then_id;
    int else_id;
} Node;

struct Graph {
    int num_nodes;
    Node* nodes; /* 1-based index */
    int output;
};

static void skip_ws(const char** p) {
    while (**p && isspace((unsigned char)**p)) {
        (*p)++;
    }
}

static Token next_token(const char** p) {
    Token t;
    const char* s;
    skip_ws(p);
    s = *p;
    t.start = s;
    t.len = 1;
    t.num = 0;
    if (*s == '\0') {
        t.kind = TK_EOF;
        return t;
    }
    switch (*s) {
        case '{': t.kind = TK_LBRACE; (*p)++; return t;
        case '}': t.kind = TK_RBRACE; (*p)++; return t;
        case '[': t.kind = TK_LBRACKET; (*p)++; return t;
        case ']': t.kind = TK_RBRACKET; (*p)++; return t;
        case ':': t.kind = TK_COLON; (*p)++; return t;
        case ',': t.kind = TK_COMMA; (*p)++; return t;
        case '"':
            (*p)++;
            t.start = *p;
            while (**p && **p != '"') {
                (*p)++;
            }
            t.len = (int)(*p - t.start);
            t.kind = TK_STRING;
            if (**p == '"') {
                (*p)++;
            }
            return t;
        default:
            break;
    }

    if (*s == '-' || isdigit((unsigned char)*s)) {
        char* endp;
        t.kind = TK_NUMBER;
        t.num = strtol(s, &endp, 10);
        t.len = (int)(endp - s);
        *p = endp;
        return t;
    }

    if (strncmp(s, "true", 4) == 0) {
        t.kind = TK_TRUE;
        t.len = 4;
        *p += 4;
        return t;
    }
    if (strncmp(s, "false", 5) == 0) {
        t.kind = TK_FALSE;
        t.len = 5;
        *p += 5;
        return t;
    }
    if (strncmp(s, "null", 4) == 0) {
        t.kind = TK_NULL;
        t.len = 4;
        *p += 4;
        return t;
    }

    t.kind = TK_EOF;
    return t;
}

static int token_is(const Token* t, TokenKind kind) {
    return t->kind == kind;
}

static int token_equals(const Token* t, const char* s) {
    size_t n = strlen(s);
    if (t->kind != TK_STRING) {
        return 0;
    }
    if ((size_t)t->len != n) {
        return 0;
    }
    return strncmp(t->start, s, n) == 0;
}

static void parse_expect(const char** p, TokenKind kind) {
    Token t = next_token(p);
    if (t.kind != kind) {
        /* minimal error handling: stop parsing */
    }
}

static void parse_skip_value(const char** p);

static void parse_skip_object(const char** p) {
    Token t = next_token(p);
    if (!token_is(&t, TK_LBRACE)) {
        return;
    }
    for (;;) {
        t = next_token(p);
        if (token_is(&t, TK_RBRACE) || token_is(&t, TK_EOF)) {
            break;
        }
        parse_expect(p, TK_COLON);
        parse_skip_value(p);
        t = next_token(p);
        if (token_is(&t, TK_RBRACE) || token_is(&t, TK_EOF)) {
            break;
        }
    }
}

static void parse_skip_array(const char** p) {
    Token t = next_token(p);
    if (!token_is(&t, TK_LBRACKET)) {
        return;
    }
    for (;;) {
        skip_ws(p);
        t = next_token(p);
        if (token_is(&t, TK_RBRACKET) || token_is(&t, TK_EOF)) {
            break;
        }
        *p = t.start;
        parse_skip_value(p);
        t = next_token(p);
        if (token_is(&t, TK_RBRACKET) || token_is(&t, TK_EOF)) {
            break;
        }
    }
}

static void parse_skip_value(const char** p) {
    Token t = next_token(p);
    if (token_is(&t, TK_LBRACE)) {
        *p = t.start;
        parse_skip_object(p);
        return;
    }
    if (token_is(&t, TK_LBRACKET)) {
        *p = t.start;
        parse_skip_array(p);
        return;
    }
}

static int ensure_capacity(Node** nodes, int* cap, int id) {
    int new_cap;
    Node* new_nodes;
    int i;
    if (id < *cap) {
        return 1;
    }
    new_cap = *cap ? *cap : 8;
    while (new_cap <= id) {
        new_cap *= 2;
    }
    new_nodes = (Node*)realloc(*nodes, (size_t)new_cap * sizeof(Node));
    if (!new_nodes) {
        return 0;
    }
    for (i = *cap; i < new_cap; ++i) {
        memset(&new_nodes[i], 0, sizeof(Node));
    }
    *nodes = new_nodes;
    *cap = new_cap;
    return 1;
}

static void parse_nodes_array(const char** p, Graph* graph) {
    Token t = next_token(p);
    Node* nodes = graph->nodes;
    int cap = graph->num_nodes;
    if (!token_is(&t, TK_LBRACKET)) {
        return;
    }
    for (;;) {
        Node node;
        int id = 0;
        memset(&node, 0, sizeof(node));
        t = next_token(p);
        if (token_is(&t, TK_RBRACKET) || token_is(&t, TK_EOF)) {
            break;
        }
        if (!token_is(&t, TK_LBRACE)) {
            break;
        }
        for (;;) {
            Token key = next_token(p);
            if (token_is(&key, TK_RBRACE) || token_is(&key, TK_EOF)) {
                break;
            }
            parse_expect(p, TK_COLON);
            if (token_equals(&key, "id")) {
                Token v = next_token(p);
                if (v.kind == TK_NUMBER) {
                    id = (int)v.num;
                    node.id = id;
                }
            } else if (token_equals(&key, "kind")) {
                Token v = next_token(p);
                if (v.kind == TK_STRING) {
                    int n = v.len < (int)sizeof(node.kind) - 1 ? v.len : (int)sizeof(node.kind) - 1;
                    memcpy(node.kind, v.start, (size_t)n);
                    node.kind[n] = '\0';
                }
            } else if (token_equals(&key, "name")) {
                Token v = next_token(p);
                if (v.kind == TK_STRING) {
                    int n = v.len < (int)sizeof(node.name) - 1 ? v.len : (int)sizeof(node.name) - 1;
                    memcpy(node.name, v.start, (size_t)n);
                    node.name[n] = '\0';
                }
            } else if (token_equals(&key, "x")) {
                Token v = next_token(p);
                if (v.kind == TK_NUMBER) {
                    node.x = (int)v.num;
                }
            } else if (token_equals(&key, "y")) {
                Token v = next_token(p);
                if (v.kind == TK_NUMBER) {
                    node.y = (int)v.num;
                }
            } else if (token_equals(&key, "field")) {
                Token v = next_token(p);
                if (v.kind == TK_NUMBER) {
                    node.field = (int)v.num;
                }
            } else if (token_equals(&key, "value")) {
                Token v = next_token(p);
                if (v.kind == TK_NUMBER) {
                    node.value = (int)v.num;
                }
            } else if (token_equals(&key, "cond")) {
                Token v = next_token(p);
                if (v.kind == TK_NUMBER) {
                    node.cond = (int)v.num;
                }
            } else if (token_equals(&key, "then")) {
                Token v = next_token(p);
                if (v.kind == TK_NUMBER) {
                    node.then_id = (int)v.num;
                }
            } else if (token_equals(&key, "else")) {
                Token v = next_token(p);
                if (v.kind == TK_NUMBER) {
                    node.else_id = (int)v.num;
                }
            } else {
                parse_skip_value(p);
            }

            t = next_token(p);
            if (token_is(&t, TK_RBRACE) || token_is(&t, TK_EOF)) {
                break;
            }
        }

        if (id > 0 && ensure_capacity(&nodes, &cap, id + 1)) {
            nodes[id] = node;
            if (id > graph->num_nodes) {
                graph->num_nodes = id;
            }
        }

        t = next_token(p);
        if (token_is(&t, TK_RBRACKET) || token_is(&t, TK_EOF)) {
            break;
        }
    }
    graph->nodes = nodes;
}

Graph* graph_load_json(const char* path) {
    FILE* f = fopen(path, "rb");
    long size;
    char* buf;
    const char* p;
    Graph* graph;
    Token t;
    if (!f) {
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = (char*)malloc((size_t)size + 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    fread(buf, 1, (size_t)size, f);
    buf[size] = '\0';
    fclose(f);

    graph = (Graph*)calloc(1, sizeof(Graph));
    if (!graph) {
        free(buf);
        return NULL;
    }
    graph->nodes = NULL;
    graph->num_nodes = 0;

    p = buf;
    t = next_token(&p);
    if (!token_is(&t, TK_LBRACE)) {
        free(buf);
        graph_free(graph);
        return NULL;
    }

    for (;;) {
        Token key = next_token(&p);
        if (token_is(&key, TK_RBRACE) || token_is(&key, TK_EOF)) {
            break;
        }
        parse_expect(&p, TK_COLON);
        if (token_equals(&key, "nodes")) {
            parse_nodes_array(&p, graph);
        } else if (token_equals(&key, "output")) {
            Token v = next_token(&p);
            if (v.kind == TK_NUMBER) {
                graph->output = (int)v.num;
            }
        } else {
            parse_skip_value(&p);
        }
        t = next_token(&p);
        if (token_is(&t, TK_RBRACE) || token_is(&t, TK_EOF)) {
            break;
        }
    }

    free(buf);
    return graph;
}

void graph_free(Graph* graph) {
    if (!graph) {
        return;
    }
    free(graph->nodes);
    free(graph);
}

static int env_lookup(const Env* env, const char* name) {
    if (strcmp(name, "p") == 0) {
        return env->p;
    }
    if (strcmp(name, "q") == 0) {
        return env->q;
    }
    return VAL_NULL;
}

static Eval eval_node(const Graph* graph, const Heap* heap, const Env* env, int id, Eval* memo, unsigned char* seen) {
    Node* node;
    if (id <= 0 || id > graph->num_nodes) {
        return (Eval){0, ERR_INVALID, 0};
    }
    if (seen[id]) {
        return memo[id];
    }
    seen[id] = 1;
    node = &graph->nodes[id];

    if (strcmp(node->kind, "input") == 0) {
        memo[id] = ck_input(node->name, env_lookup(env, node->name));
    } else if (strcmp(node->kind, "const_int") == 0) {
        memo[id] = ck_const_int(node->value);
    } else if (strcmp(node->kind, "const_null") == 0) {
        memo[id] = ck_const_null();
    } else if (strcmp(node->kind, "is_nonnull") == 0) {
        memo[id] = ck_guard_nonnull(eval_node(graph, heap, env, node->x, memo, seen));
    } else if (strcmp(node->kind, "guard_ptr") == 0) {
        Eval v = eval_node(graph, heap, env, node->x, memo, seen);
        if (!v.ok) {
            memo[id] = v;
        } else if (VAL_IS_INT(v.value)) {
            memo[id] = (Eval){0, ERR_TYPE, 0};
        } else {
            memo[id] = v;
        }
    } else if (strcmp(node->kind, "guard_nonnull") == 0) {
        Eval v = eval_node(graph, heap, env, node->x, memo, seen);
        if (!v.ok) {
            memo[id] = v;
        } else if (VAL_IS_INT(v.value)) {
            memo[id] = (Eval){0, ERR_TYPE, 0};
        } else if (v.value == VAL_NULL) {
            memo[id] = (Eval){0, ERR_NULL, 0};
        } else {
            memo[id] = v;
        }
    } else if (strcmp(node->kind, "guard_eq") == 0) {
        memo[id] = ck_guard_eq(
            eval_node(graph, heap, env, node->x, memo, seen),
            eval_node(graph, heap, env, node->y, memo, seen));
    } else if (strcmp(node->kind, "load_ptr") == 0) {
        memo[id] = ck_load_ptr((Heap*)heap, eval_node(graph, heap, env, node->x, memo, seen));
    } else if (strcmp(node->kind, "load_int") == 0) {
        memo[id] = ck_load_int((Heap*)heap, eval_node(graph, heap, env, node->x, memo, seen));
    } else if (strcmp(node->kind, "getfield") == 0) {
        memo[id] = ck_getfield((Heap*)heap, eval_node(graph, heap, env, node->x, memo, seen), node->field);
    } else if (strcmp(node->kind, "getfield_int") == 0) {
        memo[id] = ck_getfield_int((Heap*)heap, eval_node(graph, heap, env, node->x, memo, seen), node->field);
    } else if (strcmp(node->kind, "select") == 0) {
        memo[id] = ck_select(
            eval_node(graph, heap, env, node->cond, memo, seen),
            eval_node(graph, heap, env, node->then_id, memo, seen),
            eval_node(graph, heap, env, node->else_id, memo, seen));
    } else if (strcmp(node->kind, "add") == 0) {
        memo[id] = ck_add(
            eval_node(graph, heap, env, node->x, memo, seen),
            eval_node(graph, heap, env, node->y, memo, seen));
    } else {
        memo[id] = (Eval){0, ERR_INVALID, 0};
    }

    return memo[id];
}

Eval graph_eval(const Graph* graph, const Heap* heap, const Env* env) {
    Eval* memo;
    unsigned char* seen;
    Eval out;
    if (!graph || graph->output <= 0) {
        return (Eval){0, ERR_INVALID, 0};
    }
    memo = (Eval*)calloc((size_t)graph->num_nodes + 1, sizeof(Eval));
    seen = (unsigned char*)calloc((size_t)graph->num_nodes + 1, 1);
    if (!memo || !seen) {
        free(memo);
        free(seen);
        return (Eval){0, ERR_INVALID, 0};
    }
    out = eval_node(graph, heap, env, graph->output, memo, seen);
    free(memo);
    free(seen);
    return out;
}
