#include "checked_ptr.h"

Eval triple_deref(Heap* heap, int p, int q) {
    (void)q;
    Eval vp = ck_input("p", p);
    Eval v1 = ck_load_ptr(heap, vp);
    Eval v2 = ck_load_ptr(heap, v1);
    Eval v3 = ck_load_ptr(heap, v2);
    return v3;
}

Eval field_chain(Heap* heap, int p, int q) {
    (void)q;
    Eval vp = ck_input("p", p);
    Eval v1 = ck_getfield(heap, vp, FIELD_F);
    Eval v2 = ck_getfield(heap, v1, FIELD_G);
    return v2;
}

Eval guarded_chain(Heap* heap, int p, int q) {
    (void)q;
    Eval vp = ck_input("p", p);
    Eval cond = ck_guard_nonnull(vp);
    Eval then_v = ck_load_ptr(heap, ck_load_ptr(heap, vp));
    Eval else_v = ck_const_int(0);
    return ck_select(cond, then_v, else_v);
}

Eval alias_branch(Heap* heap, int p, int q) {
    Eval vp = ck_input("p", p);
    Eval vq = ck_input("q", q);
    Eval cond = ck_guard_eq(vp, vq);
    Eval then_v = ck_load_ptr(heap, vp);
    Eval else_v = ck_load_ptr(heap, vq);
    return ck_select(cond, then_v, else_v);
}

Eval mixed_fields(Heap* heap, int p, int q) {
    (void)q;
    Eval vp = ck_input("p", p);
    Eval pf = ck_getfield(heap, vp, FIELD_F);
    Eval cond = ck_guard_nonnull(pf);
    Eval then_v = ck_getfield(heap, pf, FIELD_G);
    Eval else_v = ck_const_int(0);
    return ck_select(cond, then_v, else_v);
}

Eval add_two(Heap* heap, int p, int q) {
    Eval vp = ck_input("p", p);
    Eval vq = ck_input("q", q);
    Eval lp = ck_load_ptr(heap, vp);
    Eval lq = ck_load_ptr(heap, vq);
    return ck_add(lp, lq);
}