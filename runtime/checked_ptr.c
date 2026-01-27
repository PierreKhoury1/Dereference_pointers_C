#include "checked_ptr.h"

static Eval eval_ok(int tagged) {
    Eval e;
    e.ok = 1;
    e.err = OK;
    e.value = tagged;
    return e;
}

static Eval eval_err(Err err) {
    Eval e;
    e.ok = 0;
    e.err = err;
    e.value = 0;
    return e;
}

Eval ck_input(const char* name, int tagged) {
    (void)name;
    return eval_ok(tagged);
}

Eval ck_const_int(int value) {
    return eval_ok(VAL_INT(value));
}

Eval ck_const_null(void) {
    return eval_ok(VAL_NULL);
}

Eval ck_guard_nonnull(Eval v) {
    if (!v.ok) {
        return v;
    }
    if (VAL_IS_INT(v.value)) {
        return eval_err(ERR_TYPE);
    }
    return eval_ok(VAL_INT(v.value != 0));
}

Eval ck_guard_eq(Eval a, Eval b) {
    if (!a.ok) {
        return a;
    }
    if (!b.ok) {
        return b;
    }
    return eval_ok(VAL_INT(a.value == b.value));
}

Eval ck_select(Eval cond, Eval then_v, Eval else_v) {
    if (!cond.ok) {
        return cond;
    }
    if (!VAL_IS_INT(cond.value)) {
        return eval_err(ERR_TYPE);
    }
    if (VAL_INT_VALUE(cond.value)) {
        return then_v;
    }
    return else_v;
}

Eval ck_add(Eval a, Eval b) {
    if (!a.ok) {
        return a;
    }
    if (!b.ok) {
        return b;
    }
    if (!VAL_IS_INT(a.value) || !VAL_IS_INT(b.value)) {
        return eval_err(ERR_TYPE);
    }
    return eval_ok(VAL_INT(VAL_INT_VALUE(a.value) + VAL_INT_VALUE(b.value)));
}

static Eval load_field(Heap* heap, Eval ptr, int field, int require_int) {
    Obj* obj;
    int value;

    if (!ptr.ok) {
        return ptr;
    }
    if (VAL_IS_INT(ptr.value)) {
        return eval_err(ERR_TYPE);
    }
    if (ptr.value == VAL_NULL) {
        return eval_err(ERR_NULL);
    }

    obj = heap_get_obj(heap, VAL_PTR_ADDR(ptr.value));
    if (!obj) {
        return eval_err(ERR_INVALID);
    }
    if (!heap_get_field(obj, field, &value)) {
        return eval_err(ERR_MISSING_FIELD);
    }
    if (require_int && !VAL_IS_INT(value)) {
        return eval_err(ERR_TYPE);
    }
    return eval_ok(value);
}

Eval ck_load_ptr(Heap* heap, Eval ptr) {
    return load_field(heap, ptr, FIELD_DEREF, 0);
}

Eval ck_load_int(Heap* heap, Eval ptr) {
    return load_field(heap, ptr, FIELD_DEREF, 1);
}

Eval ck_getfield(Heap* heap, Eval ptr, int field) {
    return load_field(heap, ptr, field, 0);
}

Eval ck_getfield_int(Heap* heap, Eval ptr, int field) {
    return load_field(heap, ptr, field, 1);
}