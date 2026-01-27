#ifndef CHECKED_PTR_H
#define CHECKED_PTR_H

#include "heap_gen.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { OK=0, ERR_NULL=1, ERR_INVALID=2, ERR_TYPE=3, ERR_MISSING_FIELD=4 } Err;

typedef struct {
    int ok;   /* 1 for ok, 0 for error */
    Err err;  /* error code if ok==0 */
    int value; /* tagged value */
} Eval;

/* tagged value helpers: low bit 1 = int, low bit 0 = pointer (0 = null) */
#define VAL_INT(x) (((x) << 1) | 1)
#define VAL_PTR(addr) ((addr) << 1)
#define VAL_NULL 0
#define VAL_IS_INT(v) (((v) & 1) != 0)
#define VAL_IS_PTR(v) (((v) != 0) && (((v) & 1) == 0))
#define VAL_INT_VALUE(v) ((v) >> 1)
#define VAL_PTR_ADDR(v) ((v) >> 1)

Eval ck_input(const char* name, int tagged);
Eval ck_const_int(int value);
Eval ck_const_null(void);

Eval ck_guard_nonnull(Eval v);
Eval ck_guard_eq(Eval a, Eval b);
Eval ck_select(Eval cond, Eval then_v, Eval else_v);
Eval ck_add(Eval a, Eval b);

Eval ck_load_ptr(Heap* heap, Eval ptr); /* field 0 */
Eval ck_load_int(Heap* heap, Eval ptr);
Eval ck_getfield(Heap* heap, Eval ptr, int field);
Eval ck_getfield_int(Heap* heap, Eval ptr, int field);

#ifdef __cplusplus
}
#endif

#endif