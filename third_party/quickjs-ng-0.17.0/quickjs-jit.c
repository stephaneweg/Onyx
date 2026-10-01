/*
 * Onyx: a baseline JIT for QuickJS-ng's bytecode on AArch64 (the Raspberry Pi 4).
 *
 * Included at the end of quickjs.c (it uses its internals). A function called
 * rt->jit_threshold times (JS_SetJIT) is compiled once, instruction by instruction, to a
 * template of machine code:
 *
 *   - the frame stays the interpreter's (JS_CallInternal sets it up: arguments, locals, the
 *     value stack in memory); the machine code keeps the stack pointer in a register and
 *     does the frequent instructions itself: constants, locals / arguments / closure
 *     variables, stack shuffles, integer arithmetic and comparisons (a comparison and the
 *     branch after it fused), branches (the interrupt poll on the backward ones), returns;
 *   - property reads and writes, calls, globals and closures go to small C helpers (the
 *     interpreter's fast paths);
 *   - any other instruction, and the slow case of a template (not two integers...), is run
 *     by the interpreter itself for one step (JS_CALL_FLAG_JIT_STEP: JS_CallInternal
 *     resumes the frame at that instruction and returns at the next dispatch), so every
 *     instruction has the interpreter's exact semantics;
 *   - the code leaves by a return, an exception (JS_CallInternal unwinds it: a catch goes
 *     on in the interpreter), or at an instruction it does not follow (a finally's `ret`,
 *     `with`): the interpreter goes on from there.
 *
 * Generators and async functions are not compiled. The code lives in executable memory
 * from JS_SetJIT's code_alloc (chunks, kept; the blocks of freed functions are reused).
 */

#ifdef CONFIG_QJS_JIT

/* the state the machine code and its helpers share (x22 points to it) */
typedef struct JitFrame {
    JSValue *sp;              /* in: the stack; out: where it was left */
    const uint8_t *pc;        /* out (JIT_EXC, JIT_EXIT): the interpreter's pc */
    JSValue ret_val;          /* out (JIT_RET) */
    int code;                 /* out: JIT_RET / JIT_EXC / JIT_EXIT (a helper's NULL) */
    int argc;
    JSContext *ctx;           /* the function's realm */
    JSContext *caller_ctx;
    JSRuntime *rt;
    JSStackFrame *sf;
    JSFunctionBytecode *b;
    JSVarRef **var_refs;
    JSValue *var_buf, *arg_buf;
    JSValueConst this_obj, new_target;
    JSValueConst *argv;
} JitFrame;

/* a block of machine code: its size class, then the instructions */
typedef struct JitCode {
    uint32_t size_class;
    uint32_t n_insn;
    uint32_t pad[2];
    uint32_t insn[];
} JitCode;

/* ---- the executable memory: chunks from code_alloc, blocks by powers of two ---------- */

#define JIT_CHUNK      (1 << 20)
#define JIT_MIN_SHIFT  8
#define JIT_CLASSES    13             /* 256 B .. 1 MB */

static void *(*jit_code_alloc)(size_t size);
static uint8_t *jit_chunk_cur, *jit_chunk_end;
static void *jit_free_list[JIT_CLASSES];
static int jit_lock_word;

static void jit_lock(void)
{
    while (__atomic_exchange_n(&jit_lock_word, 1, __ATOMIC_ACQUIRE))
        ;
}

static void jit_unlock(void)
{
    __atomic_store_n(&jit_lock_word, 0, __ATOMIC_RELEASE);
}

static JitCode *jit_block_alloc(size_t size)
{
    int cls = 0;
    JitCode *c;

    while (((size_t)1 << (JIT_MIN_SHIFT + cls)) < size)
        if (++cls >= JIT_CLASSES)
            return NULL;
    jit_lock();
    c = jit_free_list[cls];
    if (c) {
        jit_free_list[cls] = *(void **)c;
    } else {
        size_t n = (size_t)1 << (JIT_MIN_SHIFT + cls);
        if (jit_chunk_cur == NULL || jit_chunk_end - jit_chunk_cur < (ptrdiff_t)n) {
            uint8_t *m = jit_code_alloc ? jit_code_alloc(JIT_CHUNK) : NULL;
            if (!m) {
                jit_unlock();
                return NULL;
            }
            jit_chunk_cur = m;      /* (the old chunk's rest is lost) */
            jit_chunk_end = m + JIT_CHUNK;
        }
        c = (JitCode *)jit_chunk_cur;
        jit_chunk_cur += n;
    }
    jit_unlock();
    c->size_class = cls;
    return c;
}

static void jit_free_code(void *code)
{
    JitCode *c = code;
    int cls = c->size_class;

    jit_lock();
    *(void **)c = jit_free_list[cls];
    jit_free_list[cls] = c;
    jit_unlock();
}

bool JS_SetJIT(JSRuntime *rt, int threshold, void *(*code_alloc)(size_t size))
{
    if (code_alloc)
        jit_code_alloc = code_alloc;
    rt->jit_threshold = jit_code_alloc ? max_int(threshold, 0) : 0;
    return true;
}

/* ---- the helpers (x0 = the frame, x1 = the stack, w2 = the instruction's position):
   the new stack, or NULL to leave (jf->code, jf->sp, jf->pc / jf->ret_val set) ------- */

static JSValue *jit_leave_exc(JitFrame *jf, JSValue *sp, uint32_t pcn)
{
    jf->sp = sp;
    jf->pc = jf->b->byte_code_buf + pcn;
    jf->code = JIT_EXC;
    return NULL;
}

#ifdef JIT_STATS
/* (-DJIT_STATS: the instructions stepped, by opcode, on stderr at exit) */
static unsigned long jit_step_counts[256];

static __attribute__((destructor)) void jit_stats_dump(void)
{
    int i;
    for (i = 0; i < 256; i++)
        if (jit_step_counts[i] > 1000)
            fprintf(stderr, "jit step %3d: %lu\n", i, jit_step_counts[i]);
}
#endif

/* one instruction run by the interpreter: a copy of it, then OP_JIT_STEP_END (the
   interpreter returns there: its dispatch stays a constant table's) */
static JSValue *jh_step(JitFrame *jf, JSValue *sp, uint32_t pos)
{
    JSStackFrame *sf = jf->sf;
    JSRuntime *rt = jf->rt;
    const uint8_t *bc = jf->b->byte_code_buf;
    int size = short_opcode_info(bc[pos]).size;
    JitStep st;
    JSValue r;

#ifdef JIT_STATS
    jit_step_counts[bc[pos]]++;
#endif
    memcpy(st.copy, bc + pos, size);
    st.copy[size] = OP_JIT_STEP_END;
    st.real = bc + pos;
    st.prev = rt->jit_steps;
    rt->jit_steps = &st;
    sf->cur_sp = sp;
    sf->cur_pc = st.copy;
    r = JS_CallInternal(jf->caller_ctx, JS_UNDEFINED, jf->this_obj, jf->new_target,
                        jf->argc, jf->argv, JS_CALL_FLAG_JIT_STEP);
    rt->jit_steps = st.prev;
    if (likely(JS_VALUE_GET_TAG(r) == JS_TAG_CATCH_OFFSET)) {
        sf->cur_pc = (uint8_t *)st.real + size;     /* (not the copy's, gone) */
        return sf->cur_sp;
    }
    jf->sp = sf->cur_sp;
    if (JS_IsException(r)) {
        jf->pc = (const uint8_t *)sf->cur_pc >= st.copy &&
                 (const uint8_t *)sf->cur_pc <= st.copy + sizeof(st.copy) ?
                 st.real + (sf->cur_pc - st.copy) : st.real + size;
        sf->cur_pc = (uint8_t *)jf->pc;
        jf->code = JIT_EXC;
    } else {
        jf->ret_val = r;
        jf->code = JIT_RET;
    }
    return NULL;
}

/* the interrupt poll's slow case (a backward branch to pos) */
static JSValue *jh_poll(JitFrame *jf, JSValue *sp, uint32_t pos)
{
    if (__js_poll_interrupts(jf->ctx))
        return jit_leave_exc(jf, sp, pos);
    return sp;
}

/* if_true / if_false: an operand that is not a boolean / int / null / undefined */
static int jh_to_bool(JitFrame *jf, JSValue v)
{
    return JS_ToBoolFree(jf->ctx, v);
}

/* an inline cache: the shape of the objects a property read / write met last and the
   property's index in it (in the code, after a branch over it); the template checks the
   object's shape, then the index, the atom and the flags again in that shape -- a shape
   changed in place (not shared) is caught there */
typedef struct JitSite {
    JSShape *shape;
    uint32_t idx;
    uint32_t pad;
} JitSite;

/* get_field / get_field2 / get_length missed by its cache: the read, the cache filled when
   the object itself has the data property */
static JSValue *jh_get_field_ic(JitFrame *jf, JSValue *sp, uint32_t pos, JSAtom atom,
                                JitSite *site, int keep)
{
    JSValue val, obj = sp[-1];
    JSObject *p;
    JSProperty *pr;
    JSShapeProperty *prs;

    if (unlikely(JS_VALUE_GET_TAG(obj) != JS_TAG_OBJECT))
        return jh_step(jf, sp, pos);
    p = JS_VALUE_GET_OBJ(obj);
    prs = find_own_property(&pr, p, atom);
    if (prs) {
        if (unlikely(prs->flags & JS_PROP_TMASK))
            return jh_step(jf, sp, pos);
        site->shape = p->shape;     /* (its own data property: cached) */
        site->idx = prs - get_shape_prop(p->shape);
        val = js_dup(pr->u.value);
    } else {
        for (;;) {                  /* (on the prototypes: the interpreter's walk) */
            if (unlikely(p->is_exotic))
                return jh_step(jf, sp, pos);
            p = p->shape->proto;
            if (!p) {
                val = JS_UNDEFINED;
                break;
            }
            prs = find_own_property(&pr, p, atom);
            if (prs) {
                if (unlikely(prs->flags & JS_PROP_TMASK))
                    return jh_step(jf, sp, pos);
                val = js_dup(pr->u.value);
                break;
            }
        }
    }
    if (keep) {
        *sp++ = val;
    } else {
        js_free_value_inl(jf->rt, obj);
        sp[-1] = val;
    }
    return sp;
}

static JSValue *jh_put_field_ic(JitFrame *jf, JSValue *sp, uint32_t pos, JSAtom atom,
                                JitSite *site)
{
    JSValue obj = sp[-2];

    if (likely(JS_VALUE_GET_TAG(obj) == JS_TAG_OBJECT)) {
        JSObject *p = JS_VALUE_GET_OBJ(obj);
        JSProperty *pr;
        JSShapeProperty *prs = find_own_property(&pr, p, atom);
        if (prs && (prs->flags & (JS_PROP_TMASK | JS_PROP_WRITABLE | JS_PROP_LENGTH)) ==
                JS_PROP_WRITABLE) {
            site->shape = p->shape;
            site->idx = prs - get_shape_prop(p->shape);
            set_value(jf->ctx, &pr->u.value, sp[-1]);
            js_free_value_inl(jf->rt, obj);
            return sp - 2;
        }
    }
    {
        /* (the interpreter's slow path: a new property, a setter, not an object) */
        int ret;
        jf->sf->cur_pc = jf->b->byte_code_buf + pos + 5;
        ret = JS_SetPropertyInternal2(jf->ctx, obj, atom, sp[-1], obj,
                                      JS_PROP_THROW_STRICT);
        JS_FreeValue(jf->ctx, obj);
        sp -= 2;
        if (unlikely(ret < 0))
            return jit_leave_exc(jf, sp, pos + 5);
        return sp;
    }
}

/* the slow cases of the templates, the interpreter's own (no step) */

/* + - * / not on two ints: numbers as doubles, else the generic operation */
static JSValue *jh_arith(JitFrame *jf, JSValue *sp, uint32_t pos, int op)
{
    JSValue op1 = sp[-2], op2 = sp[-1];
    double d1, d2, d;
    int r;

    if (js_arith_to_float64(op1, &d1) && js_arith_to_float64(op2, &d2) &&
        !JS_VALUE_IS_BOTH_INT(op1, op2)) {
        switch (op) {
        case OP_add: sp[-2] = js_float64(d1 + d2); break;
        case OP_sub: sp[-2] = js_float64(d1 - d2); break;
        case OP_mul: sp[-2] = js_float64(d1 * d2); break;
        default: sp[-2] = js_number(d1 / d2); break;
        }
        return sp - 1;
    }
    if (JS_VALUE_IS_BOTH_INT(op1, op2) && op != OP_div)
        return jh_step(jf, sp, pos);    /* (an int overflow, -0: the interpreter's) */
    if (op == OP_div && JS_VALUE_IS_BOTH_INT(op1, op2)) {
        d = (double)JS_VALUE_GET_INT(op1) / (double)JS_VALUE_GET_INT(op2);
        sp[-2] = js_number(d);
        return sp - 1;
    }
    jf->sf->cur_pc = jf->b->byte_code_buf + pos + 1;
    r = op == OP_add ? js_add_slow(jf->ctx, sp) : js_binary_arith_slow(jf->ctx, sp, op);
    if (r)
        return jit_leave_exc(jf, sp, pos + 1);
    return sp - 1;
}

/* < <= > >= == != not on two ints */
static JSValue *jh_compare(JitFrame *jf, JSValue *sp, uint32_t pos, int op)
{
    int r;

    jf->sf->cur_pc = jf->b->byte_code_buf + pos + 1;
    if (op == OP_eq || op == OP_neq)
        r = js_eq_slow(jf->ctx, sp, op == OP_neq);
    else
        r = js_relational_slow(jf->ctx, sp, op);
    if (r)
        return jit_leave_exc(jf, sp, pos + 1);
    return sp - 1;
}

static JSValue *jh_typeof(JitFrame *jf, JSValue *sp, uint32_t pos)
{
    JSAtom atom = js_operator_typeof(jf->ctx, sp[-1]);

    (void)pos;
    js_free_value_inl(jf->rt, sp[-1]);
    sp[-1] = js_dup(JS_MKPTR(JS_TAG_STRING, jf->rt->atom_array[atom]));
    return sp;
}

/* define_field: obj value -> obj (an object literal's property) */
static JSValue *jh_define_field(JitFrame *jf, JSValue *sp, uint32_t pos, JSAtom atom)
{
    int ret = JS_DefinePropertyValue(jf->ctx, sp[-2], atom, sp[-1],
                                     JS_PROP_C_W_E | JS_PROP_THROW);
    sp--;
    if (unlikely(ret < 0))
        return jit_leave_exc(jf, sp, pos + 5);
    return sp;
}

/* get_array_el / get_array_el2 past the dense arrays: the generic read */
static JSValue *jh_get_array_el_slow(JitFrame *jf, JSValue *sp, uint32_t pos, int keep)
{
    JSValue val;

    jf->sf->cur_pc = jf->b->byte_code_buf + pos + 1;
    val = JS_GetPropertyValue(jf->ctx, sp[-2], sp[-1]);
    if (keep) {
        sp[-1] = val;
    } else {
        JS_FreeValue(jf->ctx, sp[-2]);
        sp[-2] = val;
        sp--;
    }
    if (unlikely(JS_IsException(val)))
        return jit_leave_exc(jf, sp, pos + 1);
    return sp;
}

/* ! not on a boolean / int / null / undefined */
static JSValue *jh_lnot(JitFrame *jf, JSValue *sp, uint32_t pos)
{
    (void)pos;
    sp[-1] = js_bool(!JS_ToBoolFree(jf->ctx, sp[-1]));
    return sp;
}

static JSValue *jh_get_array_el_slow(JitFrame *jf, JSValue *sp, uint32_t pos, int keep);

static JSValue *jh_get_array_el(JitFrame *jf, JSValue *sp, uint32_t pos, int keep)
{
    JSValue val;

    if (likely(JS_VALUE_GET_TAG(sp[-2]) == JS_TAG_OBJECT &&
               JS_VALUE_GET_TAG(sp[-1]) == JS_TAG_INT)) {
        JSObject *p = JS_VALUE_GET_OBJ(sp[-2]);
        uint32_t idx = JS_VALUE_GET_INT(sp[-1]);
        if (likely(p->class_id == JS_CLASS_ARRAY && idx < p->u.array.count)) {
            val = js_dup(p->u.array.u.values[idx]);
        } else if (!js_get_fast_array_element(jf->ctx, p, idx, &val)) {
            return jh_get_array_el_slow(jf, sp, pos, keep);
        }
        if (keep) {         /* get_array_el2: obj prop -> obj value */
            sp[-1] = val;
            return sp;
        }
        js_free_value_inl(jf->rt, sp[-2]);
        sp[-2] = val;
        return sp - 1;
    }
    return jh_get_array_el_slow(jf, sp, pos, keep);
}

static JSValue *jh_put_array_el(JitFrame *jf, JSValue *sp, uint32_t pos)
{
    if (likely(JS_VALUE_GET_TAG(sp[-2]) == JS_TAG_INT &&
               JS_VALUE_GET_TAG(sp[-3]) == JS_TAG_OBJECT)) {
        JSObject *p = JS_VALUE_GET_OBJ(sp[-3]);
        uint32_t idx = JS_VALUE_GET_INT(sp[-2]);
        if (likely(p->class_id == JS_CLASS_ARRAY && idx < (uint32_t)p->u.array.count)) {
            set_value(jf->ctx, &p->u.array.u.values[idx], sp[-1]);
            js_free_value_inl(jf->rt, sp[-3]);
            return sp - 3;
        }
    }
    return jh_step(jf, sp, pos);
}

/* call kinds */
enum { JC_CALL, JC_METHOD, JC_CTOR, JC_TAIL, JC_TAIL_METHOD };

static JSValue *jh_call(JitFrame *jf, JSValue *sp, uint32_t pos, int argc, int kind)
{
    JSContext *ctx = jf->ctx;
    JSValue *argv = sp - argc, ret;
    uint32_t pcn = pos + (jf->b->byte_code_buf[pos] >= OP_call0 &&
                          jf->b->byte_code_buf[pos] <= OP_call3 ? 1 : 3);
    int i, k;

    jf->sf->cur_pc = jf->b->byte_code_buf + pcn;
    switch (kind) {
    case JC_CALL:
    case JC_TAIL:
        ret = JS_CallInternal(ctx, argv[-1], JS_UNDEFINED, JS_UNDEFINED, argc,
                              vc(argv), 0);
        k = 1;
        break;
    case JC_CTOR:
        ret = JS_CallConstructorInternal(ctx, argv[-2], argv[-1], argc, vc(argv), 0);
        k = 2;
        break;
    default:
        ret = JS_CallInternal(ctx, argv[-1], argv[-2], JS_UNDEFINED, argc, vc(argv), 0);
        k = 2;
        break;
    }
    if (unlikely(JS_IsException(ret)))
        return jit_leave_exc(jf, sp, pcn);
    if (kind == JC_TAIL || kind == JC_TAIL_METHOD) {
        jf->sp = sp;
        jf->ret_val = ret;
        jf->code = JIT_RET;
        return NULL;
    }
    for (i = -k; i < argc; i++)
        js_free_value_inl(jf->rt, argv[i]);
    sp -= argc + k;
    *sp++ = ret;
    return sp;
}

static JSValue *jh_get_var(JitFrame *jf, JSValue *sp, uint32_t pos, JSAtom atom,
                           int throw_ref_error)
{
    JSValue val;

    jf->sf->cur_pc = jf->b->byte_code_buf + pos + 5;
    val = JS_GetGlobalVar(jf->ctx, atom, throw_ref_error);
    if (unlikely(JS_IsException(val)))
        return jit_leave_exc(jf, sp, pos + 5);
    *sp++ = val;
    return sp;
}

static JSValue *jh_put_var(JitFrame *jf, JSValue *sp, uint32_t pos, JSAtom atom, int flag)
{
    int ret;

    jf->sf->cur_pc = jf->b->byte_code_buf + pos + 5;
    ret = JS_SetGlobalVar(jf->ctx, atom, sp[-1], flag);
    sp--;
    if (unlikely(ret < 0))
        return jit_leave_exc(jf, sp, pos + 5);
    return sp;
}

static JSValue *jh_fclosure(JitFrame *jf, JSValue *sp, uint32_t pos, uint32_t idx)
{
    JSValue bfunc = js_dup(jf->b->cpool[idx]);
    uint32_t pcn = pos + (jf->b->byte_code_buf[pos] == OP_fclosure8 ? 2 : 5);

    *sp++ = js_closure(jf->ctx, bfunc, jf->var_refs, jf->sf);
    if (unlikely(JS_IsException(sp[-1])))
        return jit_leave_exc(jf, sp, pcn);
    return sp;
}

static JSValue *jh_object(JitFrame *jf, JSValue *sp, uint32_t pos)
{
    *sp++ = JS_NewObject(jf->ctx);
    if (unlikely(JS_IsException(sp[-1])))
        return jit_leave_exc(jf, sp, pos + 1);
    return sp;
}

static JSValue *jh_strict_eq(JitFrame *jf, JSValue *sp, uint32_t pos, int is_neq)
{
    JSValue op1 = sp[-2], op2 = sp[-1];
    int tag1 = JS_VALUE_GET_TAG(op1), tag2 = JS_VALUE_GET_TAG(op2);
    bool res;

    if (tag1 == tag2 && JS_VALUE_HAS_REF_COUNT(op1) &&
        (JS_VALUE_GET_PTR(op1) == JS_VALUE_GET_PTR(op2) ||
         tag1 == JS_TAG_OBJECT || tag1 == JS_TAG_SYMBOL)) {
        res = JS_VALUE_GET_PTR(op1) == JS_VALUE_GET_PTR(op2);
    } else if (tag1 == JS_TAG_STRING && tag2 == JS_TAG_STRING &&
               (JS_VALUE_GET_STRING(op1)->len != JS_VALUE_GET_STRING(op2)->len ||
                (JS_VALUE_GET_STRING(op1)->atom_type == JS_ATOM_TYPE_STRING &&
                 JS_VALUE_GET_STRING(op2)->atom_type == JS_ATOM_TYPE_STRING))) {
        res = false;
    } else if (tag1 == JS_TAG_UNDEFINED || tag1 == JS_TAG_NULL ||
               tag2 == JS_TAG_UNDEFINED || tag2 == JS_TAG_NULL) {
        res = tag1 == tag2;
    } else {
        return jh_step(jf, sp, pos);
    }
    js_free_value_inl(jf->rt, op1);
    js_free_value_inl(jf->rt, op2);
    sp[-2] = js_bool(res ^ is_neq);
    return sp - 1;
}

/* ---- the AArch64 assembler ------------------------------------------------------------ */

/* registers: x19 the stack, x20 the locals, x21 the arguments, x22 the JitFrame, x23 the
   realm's context, x24 the bytecode, x25 the stack frame, x26 the runtime; x0-x17 free */
enum { RSP = 19, RVAR = 20, RARG = 21, RJF = 22, RCTX = 23, RBC = 24, RSF = 25, RRT = 26,
       RZR = 31 };
enum { C_EQ = 0, C_NE, C_HS, C_LO, C_MI, C_PL, C_VS, C_VC, C_HI, C_LS, C_GE, C_LT, C_GT,
       C_LE };
enum { JT_LEAVE = -1, JT_RET = -2 };   /* branch targets besides bytecode positions */

typedef struct JitFix {
    int at;       /* the branch instruction */
    int target;   /* a bytecode position, or JT_* */
} JitFix;

typedef struct JitAsm {
    uint32_t *buf;
    int n, cap;
    int *pc2code;         /* bytecode position -> instruction index (-1: none yet) */
    JitFix *fix;
    int nfix, fixcap;
    int leave_at, ret_at; /* JT_LEAVE's / JT_RET's instruction */
    bool fail;
} JitAsm;

#define JIT_RC_OFF ((int)offsetof(JSMallocBlockHeader, ref_count) - \
                    (int)offsetof(JSMallocBlockHeader, user_data))
#define JF(f) ((int)offsetof(JitFrame, f))

static void put(JitAsm *a, uint32_t i)
{
    if (a->n >= a->cap) {
        a->fail = true;
        return;
    }
    a->buf[a->n++] = i;
}

static void fixup(JitAsm *a, int target)
{
    if (a->nfix >= a->fixcap) {
        int cap = a->fixcap * 2 + 64;
        JitFix *f = realloc(a->fix, cap * sizeof(*f));
        if (!f) {
            a->fail = true;
            return;
        }
        a->fix = f;
        a->fixcap = cap;
    }
    a->fix[a->nfix].at = a->n;
    a->fix[a->nfix].target = target;
    a->nfix++;
}

/* a branch at instruction `at` pointed to instruction `to` */
static void patch(JitAsm *a, int at, int to)
{
    uint32_t i = a->buf[at];
    int32_t rel = to - at;

    if ((i & 0xFC000000u) == 0x14000000u || (i & 0xFC000000u) == 0x94000000u)
        i = (i & 0xFC000000u) | ((uint32_t)rel & 0x3FFFFFFu);
    else if ((i & 0x7E000000u) == 0x36000000u)      /* tbz / tbnz: imm14 */
        i = (i & 0xFFF8001Fu) | (((uint32_t)rel & 0x3FFFu) << 5);
    else                  /* b.cond / cbz / cbnz: imm19 */
        i = (i & 0xFF00001Fu) | (((uint32_t)rel & 0x7FFFFu) << 5);
    if (rel >= (1 << 18) || rel < -(1 << 18))
        a->fail = true;   /* (a function too big for imm19) */
    a->buf[at] = i;
}

static void i_movz(JitAsm *a, int rd, uint32_t imm, int hw, bool x)
{
    put(a, (x ? 0xD2800000u : 0x52800000u) | (uint32_t)hw << 21 | (imm & 0xFFFF) << 5 | rd);
}

static void i_movk(JitAsm *a, int rd, uint32_t imm, int hw, bool x)
{
    put(a, (x ? 0xF2800000u : 0x72800000u) | (uint32_t)hw << 21 | (imm & 0xFFFF) << 5 | rd);
}

static void e_movx(JitAsm *a, int rd, uint64_t v)
{
    int hw;
    bool first = true;

    if ((int64_t)v < 0 && (int64_t)v >= -65536) {
        put(a, 0x92800000u | ((uint32_t)~v & 0xFFFF) << 5 | rd);   /* movn */
        return;
    }
    for (hw = 0; hw < 4; hw++) {
        uint32_t h = (uint32_t)(v >> (hw * 16)) & 0xFFFF;
        if (h == 0 && !(first && hw == 3))
            continue;
        if (first)
            i_movz(a, rd, h, hw, true);
        else
            i_movk(a, rd, h, hw, true);
        first = false;
    }
    if (first)
        i_movz(a, rd, 0, 0, true);
}

static void e_movw(JitAsm *a, int rd, uint32_t v)
{
    i_movz(a, rd, v & 0xFFFF, 0, false);
    if (v >> 16)
        i_movk(a, rd, v >> 16, 1, false);
}

static void i_movr(JitAsm *a, int rd, int rm)          /* mov xd, xm */
{
    put(a, 0xAA000000u | (uint32_t)rm << 16 | RZR << 5 | rd);
}

static void e_addi(JitAsm *a, int rd, int rn, int64_t imm)  /* x */
{
    if (imm >= 0 && imm < 4096) {
        put(a, 0x91000000u | (uint32_t)imm << 10 | (uint32_t)rn << 5 | rd);
    } else if (imm < 0 && imm > -4096) {
        put(a, 0xD1000000u | (uint32_t)(-imm) << 10 | (uint32_t)rn << 5 | rd);
    } else {
        e_movx(a, 17, (uint64_t)imm);
        put(a, 0x8B000000u | 17u << 16 | (uint32_t)rn << 5 | rd);
    }
}

/* ldp / stp of a JSValue (two x registers) at [rn + off] */
static void e_ldst_pair(JitAsm *a, bool load, int rt, int rt2, int rn, int off)
{
    if (off < -512 || off > 504 || (off & 7)) {
        e_addi(a, 17, rn, off);
        rn = 17;
        off = 0;
    }
    put(a, (load ? 0xA9400000u : 0xA9000000u) | ((uint32_t)(off / 8) & 0x7F) << 15 |
        (uint32_t)rt2 << 10 | (uint32_t)rn << 5 | rt);
}

#define e_ldp(a, rt, rt2, rn, off) e_ldst_pair(a, true, rt, rt2, rn, off)
#define e_stp(a, rt, rt2, rn, off) e_ldst_pair(a, false, rt, rt2, rn, off)

/* ldr / str x or w at [rn + off] */
static void e_ldst(JitAsm *a, bool load, bool x, int rt, int rn, int off)
{
    int sc = x ? 8 : 4;

    if (off >= 0 && !(off % sc) && off / sc < 4096) {
        put(a, (x ? (load ? 0xF9400000u : 0xF9000000u) : (load ? 0xB9400000u : 0xB9000000u)) |
            (uint32_t)(off / sc) << 10 | (uint32_t)rn << 5 | rt);
    } else if (off >= -256 && off < 256) {
        put(a, (x ? (load ? 0xF8400000u : 0xF8000000u) : (load ? 0xB8400000u : 0xB8000000u)) |
            ((uint32_t)off & 0x1FF) << 12 | (uint32_t)rn << 5 | rt);
    } else {
        e_addi(a, 17, rn, off);
        e_ldst(a, load, x, rt, 17, 0);
    }
}

#define e_ldrx(a, rt, rn, off) e_ldst(a, true, true, rt, rn, off)
#define e_strx(a, rt, rn, off) e_ldst(a, false, true, rt, rn, off)
#define e_ldrw(a, rt, rn, off) e_ldst(a, true, false, rt, rn, off)
#define e_strw(a, rt, rn, off) e_ldst(a, false, false, rt, rn, off)

static void i_cmpw_imm(JitAsm *a, int rn, uint32_t imm)   /* cmp wn, #imm */
{
    put(a, 0x71000000u | imm << 10 | (uint32_t)rn << 5 | RZR);
}

static void i_cmnw_imm(JitAsm *a, int rn, uint32_t imm)   /* cmn wn, #imm */
{
    put(a, 0x31000000u | imm << 10 | (uint32_t)rn << 5 | RZR);
}

static void i_alu(JitAsm *a, uint32_t op, int rd, int rn, int rm)
{
    put(a, op | (uint32_t)rm << 16 | (uint32_t)rn << 5 | rd);
}

#define A_ADDS_W  0x2B000000u
#define A_SUBS_W  0x6B000000u
#define A_ORR_W   0x2A000000u
#define A_AND_W   0x0A000000u
#define A_EOR_W   0x4A000000u
#define A_LSLV_W  0x1AC02000u
#define A_LSRV_W  0x1AC02400u
#define A_ASRV_W  0x1AC02800u
#define A_SMULL   0x9B207C00u

static void i_cset(JitAsm *a, int rd, int cond)
{
    put(a, 0x1A9F07E0u | (uint32_t)(cond ^ 1) << 12 | rd);
}

static void i_call(JitAsm *a, const void *fn)
{
    e_movx(a, 16, (uint64_t)(uintptr_t)fn);
    put(a, 0xD63F0000u | 16u << 5);           /* blr x16 */
}

/* forward / backward branches: to a bytecode position or a JT_* */
static void e_b(JitAsm *a, int target)
{
    fixup(a, target);
    put(a, 0x14000000u);
}

static void e_bcond(JitAsm *a, int cond, int target)
{
    fixup(a, target);
    put(a, 0x54000000u | (uint32_t)cond);
}

static void e_cbz(JitAsm *a, bool nz, bool x, int rt, int target)
{
    fixup(a, target);
    put(a, (nz ? 0x35000000u : 0x34000000u) | (x ? 0x80000000u : 0) | (uint32_t)rt);
}

/* local branches inside a template: put, then pointed at here() */
static int l_bcond(JitAsm *a, int cond)
{
    put(a, 0x54000000u | (uint32_t)cond);
    return a->n - 1;
}

static int l_b(JitAsm *a)
{
    put(a, 0x14000000u);
    return a->n - 1;
}

static int l_cbz(JitAsm *a, bool nz, int rt)   /* w */
{
    put(a, (nz ? 0x35000000u : 0x34000000u) | (uint32_t)rt);
    return a->n - 1;
}

static void l_here(JitAsm *a, int at)
{
    if (!a->fail)
        patch(a, at, a->n);
}

/* ---- values ----------------------------------------------------------------------------- */

/* the reference of (ru = pointer, rt = tag) taken, when it has one (x9) */
static void e_dup(JitAsm *a, int ru, int rt)
{
    int skip;

    i_cmnw_imm(a, rt, -JS_TAG_FIRST);       /* tag >= JS_TAG_FIRST (unsigned) */
    skip = l_bcond(a, C_LO);
    e_ldrw(a, 9, ru, JIT_RC_OFF);
    put(a, 0x11000400u | 9u << 5 | 9);       /* add w9, w9, #1 */
    e_strw(a, 9, ru, JIT_RC_OFF);
    l_here(a, skip);
}

/* the value (ru, rt) released (x0-x17 clobbered when it was the last reference) */
static void e_free(JitAsm *a, int ru, int rt)
{
    int skip, skip2;

    i_cmnw_imm(a, rt, -JS_TAG_FIRST);
    skip = l_bcond(a, C_LO);
    e_ldrw(a, 9, ru, JIT_RC_OFF);
    put(a, 0x71000400u | 9u << 5 | 9);       /* subs w9, w9, #1 */
    e_strw(a, 9, ru, JIT_RC_OFF);
    skip2 = l_bcond(a, C_GT);
    if (rt == 1) {
        i_movr(a, 2, rt);
        i_movr(a, 1, ru);
    } else {
        i_movr(a, 1, ru);
        i_movr(a, 2, rt);
    }
    i_movr(a, 0, RRT);
    i_call(a, js_free_value_rt);
    l_here(a, skip);
    l_here(a, skip2);
}

/* push (x0, x1) */
static void e_push01(JitAsm *a)
{
    put(a, 0xA8800000u | (2u & 0x7F) << 15 | 1u << 10 | (uint32_t)RSP << 5 | 0);  /* stp x0, x1, [x19], #16 */
}

/* a helper: x0 = the frame, x1 = the stack, w2 = pos, x3 / x4 = operands */
static void e_helper(JitAsm *a, const void *fn, uint32_t pos, int nops, uint64_t op3,
                     uint64_t op4)
{
    i_movr(a, 0, RJF);
    i_movr(a, 1, RSP);
    e_movw(a, 2, pos);
    if (nops > 0)
        e_movx(a, 3, op3);
    if (nops > 1)
        e_movx(a, 4, op4);
    i_call(a, fn);
    e_cbz(a, false, true, 0, JT_LEAVE);
    i_movr(a, RSP, 0);
}

#define e_step(a, pos) e_helper(a, jh_step, pos, 0, 0, 0)

/* the address of the idx-th value of a buffer register (x17 when out of reach) */
static int e_slot(JitAsm *a, int base, int idx, int *off)
{
    *off = idx * 16;
    if (*off > 504) {
        e_addi(a, 17, base, *off);
        *off = 0;
        return 17;
    }
    return base;
}

/* ---- the templates ---------------------------------------------------------------------- */

static void t_push_const(JitAsm *a, uint64_t u, int64_t tag)
{
    e_movx(a, 0, u);
    e_movx(a, 1, (uint64_t)tag);
    e_push01(a);
}

/* get: push a copy of the value at [base + idx * 16] */
static void t_get(JitAsm *a, int base, int idx)
{
    int off, r = e_slot(a, base, idx, &off);

    e_ldp(a, 0, 1, r, off);
    e_dup(a, 0, 1);
    e_push01(a);
}

/* put (keep = false: popped) / set (keep: a copy) into [r + off] */
static void t_store_at(JitAsm *a, int r, int off, bool keep)
{
    if (r == 17) {          /* (e_free's call clobbers x17: the address kept in x12) */
        i_movr(a, 12, 17);
        r = 12;
    }
    e_ldp(a, 0, 1, RSP, -16);
    if (keep)
        e_dup(a, 0, 1);
    else
        e_addi(a, RSP, RSP, -16);
    e_ldp(a, 10, 11, r, off);
    e_stp(a, 0, 1, r, off);
    e_free(a, 10, 11);
}

static void t_put(JitAsm *a, int base, int idx, bool keep)
{
    int off, r = e_slot(a, base, idx, &off);
    t_store_at(a, r, off, keep);
}

/* x9 = the var_ref's value pointer */
static void e_var_ref_ptr(JitAsm *a, int idx)
{
    e_ldrx(a, 9, RJF, JF(var_refs));
    e_ldrx(a, 9, 9, idx * 8);
    e_ldrx(a, 9, 9, (int)offsetof(JSVarRef, pvalue));
}

/* the interrupt poll of a backward branch to `target` */
static void e_poll(JitAsm *a, int target)
{
    int ok;

    e_ldrw(a, 9, RCTX, (int)offsetof(JSContext, interrupt_counter));
    put(a, 0x71000400u | 9u << 5 | 9);       /* subs w9, w9, #1 */
    e_strw(a, 9, RCTX, (int)offsetof(JSContext, interrupt_counter));
    ok = l_bcond(a, C_GT);
    e_helper(a, jh_poll, target, 0, 0, 0);
    l_here(a, ok);
}

static void e_goto(JitAsm *a, int pos, int target)
{
    if (target <= pos)
        e_poll(a, target);
    e_b(a, target);
}

/* if_true / if_false (pos: the instruction, for the poll): pop, test, branch */
static void t_if(JitAsm *a, int pos, int target, bool jump_if_true)
{
    int slow, join, nb;

    put(a, 0xA9C00000u | ((uint32_t)-2 & 0x7F) << 15 | 1u << 10 | (uint32_t)RSP << 5 | 0); /* ldp x0, x1, [x19, #-16]! */
    i_cmpw_imm(a, 1, JS_TAG_UNDEFINED);
    slow = l_bcond(a, C_HI);
    join = a->n;
    /* w0: the truth */
    if (target <= pos) {
        nb = l_cbz(a, !jump_if_true, 0);       /* not taken: past the poll */
        e_poll(a, target);
        e_b(a, target);
        l_here(a, nb);
    } else {
        e_cbz(a, jump_if_true, false, 0, target);
    }
    {
        int past = l_b(a);
        l_here(a, slow);
        i_movr(a, 2, 1);
        i_movr(a, 1, 0);
        i_movr(a, 0, RJF);
        i_call(a, jh_to_bool);
        put(a, 0x14000000u | ((uint32_t)(join - a->n) & 0x3FFFFFFu));   /* b join */
        l_here(a, past);
    }
}

/* the two operands (x0, x1) and (x2, x3), both ints? else to `slow` (a local branch) */
static int e_two_ints(JitAsm *a)
{
    e_ldp(a, 0, 1, RSP, -32);
    e_ldp(a, 2, 3, RSP, -16);
    i_alu(a, A_ORR_W, 9, 1, 3);
    return l_cbz(a, true, 9);
}

/* store the int w0 as the result replacing two operands */
static void e_int_result2(JitAsm *a)
{
    put(a, 0xA9000000u | ((uint32_t)-4 & 0x7F) << 15 | RZR << 10 | (uint32_t)RSP << 5 | 0);  /* stp x0, xzr, [x19, #-32] */
    e_addi(a, RSP, RSP, -16);
}

/* a binary int operation (op: add / sub / and / or / xor / shifts / mul), its slow case the
   interpreter's */
static void t_binary_int(JitAsm *a, int pos, int opcode)
{
    int slow = e_two_ints(a), slow2 = -1, slow3 = -1, done;

    switch (opcode) {
    case OP_add:
        i_alu(a, A_ADDS_W, 0, 0, 2);
        slow2 = l_bcond(a, C_VS);
        break;
    case OP_sub:
        i_alu(a, A_SUBS_W, 0, 0, 2);
        slow2 = l_bcond(a, C_VS);
        break;
    case OP_mul:
        i_alu(a, A_SMULL, 0, 0, 2);
        put(a, 0xEB20C000u | 0u << 16 | 0u << 5 | RZR);    /* cmp x0, w0, sxtw */
        slow2 = l_bcond(a, C_NE);
        slow3 = l_cbz(a, false, 0);                           /* 0: maybe -0 */
        break;
    case OP_and:
        i_alu(a, A_AND_W, 0, 0, 2);
        break;
    case OP_or:
        i_alu(a, A_ORR_W, 0, 0, 2);
        break;
    case OP_xor:
        i_alu(a, A_EOR_W, 0, 0, 2);
        break;
    case OP_shl:
        i_alu(a, A_LSLV_W, 0, 0, 2);
        break;
    case OP_sar:
        i_alu(a, A_ASRV_W, 0, 0, 2);
        break;
    case OP_shr:
        i_alu(a, A_LSRV_W, 0, 0, 2);
        slow2 = l_cbz(a, false, 0);                /* (placeholder, patched below) */
        a->buf[slow2] = 0x37F80000u | 0;            /* tbnz w0, #31: past INT32_MAX */
        break;
    }
    put(a, 0x2A0003E0u | 0u << 16 | 0);            /* mov w0, w0 (the upper half zero) */
    e_int_result2(a);
    done = l_b(a);
    l_here(a, slow);
    if (slow2 >= 0)
        l_here(a, slow2);
    if (slow3 >= 0)
        l_here(a, slow3);
    if (opcode == OP_add || opcode == OP_sub || opcode == OP_mul)
        e_helper(a, jh_arith, pos, 1, opcode, 0);
    else
        e_step(a, pos);
    l_here(a, done);
}

static int cmp_cond(int opcode)
{
    switch (opcode) {
    case OP_lt: return C_LT;
    case OP_lte: return C_LE;
    case OP_gt: return C_GT;
    case OP_gte: return C_GE;
    case OP_eq: case OP_strict_eq: return C_EQ;
    default: return C_NE;     /* neq, strict_neq */
    }
}

/* a comparison; fused with the if_true / if_false after it (next_op, target) when that is
   not a branch target; returns the bytes consumed past the comparison's */
static int t_compare(JitAsm *a, int pos, int opcode, int next_op, int next_pos,
                     int next_target, bool fuse)
{
    int slow = e_two_ints(a), done, cond = cmp_cond(opcode);

    put(a, A_SUBS_W | 2u << 16 | 0u << 5 | RZR);      /* cmp w0, w2 */
    if (fuse) {
        bool jt = next_op == OP_if_true || next_op == OP_if_true8;
        e_addi(a, RSP, RSP, -32);
        if (next_target <= next_pos) {
            int nb = l_bcond(a, jt ? cond ^ 1 : cond);
            e_poll(a, next_target);
            e_b(a, next_target);
            l_here(a, nb);
        } else {
            e_bcond(a, jt ? cond : cond ^ 1, next_target);
        }
        done = l_b(a);
        l_here(a, slow);
        if (opcode == OP_strict_eq || opcode == OP_strict_neq)
            e_helper(a, jh_strict_eq, pos, 1, opcode == OP_strict_neq, 0);
        else
            e_helper(a, jh_compare, pos, 1, opcode, 0);
        t_if(a, next_pos, next_target, jt);
        l_here(a, done);
        return 1;
    }
    i_cset(a, 0, cond);
    i_movz(a, 1, JS_TAG_BOOL, 0, true);
    e_stp(a, 0, 1, RSP, -32);
    e_addi(a, RSP, RSP, -16);
    done = l_b(a);
    l_here(a, slow);
    if (opcode == OP_strict_eq || opcode == OP_strict_neq)
        e_helper(a, jh_strict_eq, pos, 1, opcode == OP_strict_neq, 0);
    else
        e_helper(a, jh_compare, pos, 1, opcode, 0);
    l_here(a, done);
    return 0;
}

/* adr rd, <the instruction at index `at`> */
static void i_adr(JitAsm *a, int rd, int at)
{
    int32_t off = (at - a->n) * 4;
    put(a, 0x10000000u | ((uint32_t)off & 3) << 29 | (((uint32_t)off >> 2) & 0x7FFFF) << 5 | rd);
}

/* an inline cache's site in the code (JitSite: 4 words, branched over); its index */
static int e_site(JitAsm *a)
{
    int site;

    put(a, 0x14000005u);                  /* b over the 4 words */
    site = a->n;
    put(a, 0);
    put(a, 0);
    put(a, 0);
    put(a, 0);
    return site;
}

/* the cache's check: x0 = the object (its tag checked), the site at `site` -> x4 = the
   index, x11 = the property's JSProperty; to `miss` (a local branch list) otherwise.
   writable: the flags a write needs, else a read's */
static void e_site_check(JitAsm *a, int site, JSAtom atom, bool writable, int miss[6])
{
    i_adr(a, 9, site);
    e_ldrx(a, 2, 0, (int)offsetof(JSObject, shape));
    e_ldrx(a, 3, 9, 0);
    put(a, 0xEB000000u | 3u << 16 | 2u << 5 | RZR);        /* cmp x2, x3 */
    miss[0] = l_bcond(a, C_NE);
    e_ldrw(a, 4, 9, 8);
    e_ldrw(a, 5, 2, (int)offsetof(JSShape, prop_count));
    put(a, A_SUBS_W | 5u << 16 | 4u << 5 | RZR);           /* cmp w4, w5 */
    miss[1] = l_bcond(a, C_HS);
    e_ldrw(a, 6, 2, (int)offsetof(JSShape, prop_hash_mask));
    put(a, 0x11000400u | 6u << 5 | 6);                       /* add w6, w6, #1 */
    e_addi(a, 7, 2, (int)offsetof(JSShape, hash_table));
    put(a, 0x8B204800u | 6u << 16 | 7u << 5 | 7);           /* add x7, x7, w6, uxtw #2 */
    put(a, 0x8B204C00u | 4u << 16 | 7u << 5 | 7);           /* add x7, x7, w4, uxtw #3 */
    e_ldrw(a, 8, 7, 0);                                      /* hash_next : 26, flags : 6 */
    e_ldrw(a, 10, 7, 4);                                     /* atom */
    e_movw(a, 12, atom);
    put(a, A_SUBS_W | 12u << 16 | 10u << 5 | RZR);          /* cmp w10, w12 */
    miss[2] = l_bcond(a, C_NE);
    if (writable) {
        /* (flags & (TMASK | WRITABLE | LENGTH)) == WRITABLE */
        put(a, 0x53000000u | 26u << 16 | 31u << 10 | 8u << 5 | 8);  /* lsr w8, w8, #26 */
        e_movw(a, 12, JS_PROP_TMASK | JS_PROP_WRITABLE | JS_PROP_LENGTH);
        i_alu(a, A_AND_W, 8, 8, 12);
        i_cmpw_imm(a, 8, JS_PROP_WRITABLE);
        miss[3] = l_bcond(a, C_NE);
    } else {
        put(a, 0x7202051Fu | 8u << 5);                        /* tst w8, #0xc0000000 (TMASK) */
        miss[3] = l_bcond(a, C_NE);
    }
    e_ldrx(a, 11, 0, (int)offsetof(JSObject, prop));
    put(a, 0x8B205000u | 4u << 16 | 11u << 5 | 11);         /* add x11, x11, w4, uxtw #4 */
}

/* get_field / get_field2 / get_length through an inline cache (own data properties) */
static void t_get_field_ic(JitAsm *a, int pos, JSAtom atom, int keep)
{
    int site = e_site(a), notobj, miss[6], i, done;

    e_ldp(a, 0, 1, RSP, -16);
    i_cmnw_imm(a, 1, 1);                    /* tag == JS_TAG_OBJECT (-1)? */
    notobj = l_bcond(a, C_NE);
    e_site_check(a, site, atom, false, miss);
    e_ldp(a, 12, 13, 11, 0);
    e_dup(a, 12, 13);
    if (keep) {
        put(a, 0xA8800000u | (2u & 0x7F) << 15 | 13u << 10 | (uint32_t)RSP << 5 | 12);  /* stp x12, x13, [x19], #16 */
    } else {
        e_stp(a, 12, 13, RSP, -16);
        e_free(a, 0, 1);                    /* (the object: x0, x1) */
    }
    done = l_b(a);
    l_here(a, notobj);
    for (i = 0; i < 4; i++)
        l_here(a, miss[i]);
    i_movr(a, 0, RJF);
    i_movr(a, 1, RSP);
    e_movw(a, 2, pos);
    e_movw(a, 3, atom);
    i_adr(a, 4, site);
    i_movz(a, 5, keep, 0, true);
    i_call(a, jh_get_field_ic);
    e_cbz(a, false, true, 0, JT_LEAVE);
    i_movr(a, RSP, 0);
    l_here(a, done);
}

/* put_field through an inline cache (own writable data properties) */
static void t_put_field_ic(JitAsm *a, int pos, JSAtom atom)
{
    int site = e_site(a), notobj, miss[6], i, done;

    e_ldp(a, 0, 1, RSP, -32);
    i_cmnw_imm(a, 1, 1);
    notobj = l_bcond(a, C_NE);
    e_site_check(a, site, atom, true, miss);
    e_ldp(a, 12, 13, RSP, -16);             /* the value */
    e_ldp(a, 2, 3, 11, 0);                  /* the old one */
    e_stp(a, 12, 13, 11, 0);
    e_addi(a, RSP, RSP, -32);
    e_stp(a, 2, 3, RSP, 16);                /* (kept in the popped slot over the frees) */
    e_free(a, 0, 1);                        /* the object */
    e_ldp(a, 10, 11, RSP, 16);
    e_free(a, 10, 11);                      /* the old value */
    done = l_b(a);
    l_here(a, notobj);
    for (i = 0; i < 4; i++)
        l_here(a, miss[i]);
    i_movr(a, 0, RJF);
    i_movr(a, 1, RSP);
    e_movw(a, 2, pos);
    e_movw(a, 3, atom);
    i_adr(a, 4, site);
    i_call(a, jh_put_field_ic);
    e_cbz(a, false, true, 0, JT_LEAVE);
    i_movr(a, RSP, 0);
    l_here(a, done);
}

/* inc / dec of the top (delta +1 / -1) */
static void t_incdec(JitAsm *a, int pos, int delta)
{
    int slow, slow2, done;

    e_ldp(a, 0, 1, RSP, -16);
    slow = l_cbz(a, true, 1);
    put(a, (delta > 0 ? 0x31000400u : 0x71000400u) | 0u << 5 | 0);   /* adds / subs w0, w0, #1 */
    slow2 = l_bcond(a, C_VS);
    put(a, 0x2A0003E0u | 0);                                          /* mov w0, w0 */
    e_strx(a, 0, RSP, -16);
    done = l_b(a);
    l_here(a, slow);
    l_here(a, slow2);
    e_step(a, pos);
    l_here(a, done);
}

/* inc_loc / dec_loc / add_loc */
static void t_loc_arith(JitAsm *a, int pos, int opcode, int idx)
{
    int off, r = e_slot(a, RVAR, idx, &off), slow, slow2, done;

    if (r == 17) {
        i_movr(a, 12, 17);
        r = 12;
    }
    e_ldp(a, 0, 1, r, off);
    if (opcode == OP_add_loc) {
        e_ldp(a, 2, 3, RSP, -16);
        i_alu(a, A_ORR_W, 9, 1, 3);
        slow = l_cbz(a, true, 9);
        i_alu(a, A_ADDS_W, 0, 0, 2);
    } else {
        slow = l_cbz(a, true, 1);
        put(a, (opcode == OP_inc_loc ? 0x31000400u : 0x71000400u) | 0);
    }
    slow2 = l_bcond(a, C_VS);
    put(a, 0x2A0003E0u | 0);
    e_strx(a, 0, r, off);
    if (opcode == OP_add_loc)
        e_addi(a, RSP, RSP, -16);
    done = l_b(a);
    l_here(a, slow);
    l_here(a, slow2);
    e_step(a, pos);
    l_here(a, done);
}

/* ---- the compiler ----------------------------------------------------------------------- */

static int jit_branch_target(const uint8_t *bc, int pos, int op)
{
    switch (op) {
    case OP_goto: case OP_if_true: case OP_if_false: case OP_catch: case OP_gosub:
        return pos + 1 + (int32_t)get_u32(bc + pos + 1);
    case OP_goto16:
        return pos + 1 + (int16_t)get_u16(bc + pos + 1);
    case OP_goto8: case OP_if_true8: case OP_if_false8:
        return pos + 1 + (int8_t)bc[pos + 1];
    default:
        return -1;
    }
}

static void jit_flush(void *p, size_t n)
{
    __builtin___clear_cache((char *)p, (char *)p + n);
}

static bool jit_compile(JSContext *ctx, JSFunctionBytecode *b)
{
    JSRuntime *rt = ctx->rt;
    const uint8_t *bc = b->byte_code_buf;
    int len = b->byte_code_len, pos, i;
    uint8_t *is_target;
    JitAsm as, *a = &as;
    JitCode *code = NULL;

    memset(a, 0, sizeof(*a));
    a->cap = len * 48 + 256;
    a->buf = malloc(a->cap * sizeof(uint32_t));
    a->pc2code = malloc((len + 1) * sizeof(int));
    is_target = calloc(len + 1, 1);
    if (!a->buf || !a->pc2code || !is_target)
        goto fail;
    for (i = 0; i <= len; i++)
        a->pc2code[i] = -1;
    /* the branch targets (a comparison is not fused with a branch that is one) */
    for (pos = 0; pos < len; pos += short_opcode_info(bc[pos]).size) {
        int t = jit_branch_target(bc, pos, bc[pos]);
        if (t >= 0 && t <= len)
            is_target[t] = 1;
    }

    /* the prologue: x19-x26 saved; x0 = the JitFrame */
    put(a, 0xA9BB7BFDu);                  /* stp x29, x30, [sp, #-80]! */
    put(a, 0x910003FDu);                  /* mov x29, sp */
    put(a, 0xA90153F3u);                  /* stp x19, x20, [sp, #16] */
    put(a, 0xA9025BF5u);                  /* stp x21, x22, [sp, #32] */
    put(a, 0xA90363F7u);                  /* stp x23, x24, [sp, #48] */
    put(a, 0xA9046BF9u);                  /* stp x25, x26, [sp, #64] */
    i_movr(a, RJF, 0);
    e_ldrx(a, RSP, RJF, JF(sp));
    e_ldrx(a, RVAR, RJF, JF(var_buf));
    e_ldrx(a, RARG, RJF, JF(arg_buf));
    e_ldrx(a, RCTX, RJF, JF(ctx));
    e_ldrx(a, RSF, RJF, JF(sf));
    e_ldrx(a, RRT, RJF, JF(rt));
    e_movx(a, RBC, (uint64_t)(uintptr_t)bc);

    for (pos = 0; pos < len && !a->fail; ) {
        int op = bc[pos], size = short_opcode_info(op).size, n = pos + size;

        a->pc2code[pos] = a->n;
        switch (op) {
        case OP_push_i32:
            t_push_const(a, (uint32_t)get_u32(bc + pos + 1), JS_TAG_INT);
            break;
        case OP_push_minus1: case OP_push_0: case OP_push_1: case OP_push_2:
        case OP_push_3: case OP_push_4: case OP_push_5: case OP_push_6: case OP_push_7:
            t_push_const(a, (uint32_t)(op - OP_push_0), JS_TAG_INT);
            break;
        case OP_push_i8:
            t_push_const(a, (uint32_t)(int8_t)bc[pos + 1], JS_TAG_INT);
            break;
        case OP_push_i16:
            t_push_const(a, (uint32_t)(int16_t)get_u16(bc + pos + 1), JS_TAG_INT);
            break;
        case OP_undefined:
            t_push_const(a, 0, JS_TAG_UNDEFINED);
            break;
        case OP_null:
            t_push_const(a, 0, JS_TAG_NULL);
            break;
        case OP_push_false:
        case OP_push_true:
            t_push_const(a, op == OP_push_true, JS_TAG_BOOL);
            break;
        case OP_push_const:
        case OP_push_const8:
            {
                uint32_t idx = op == OP_push_const ? get_u32(bc + pos + 1) : bc[pos + 1];
                e_movx(a, 9, (uint64_t)(uintptr_t)&b->cpool[idx]);
                e_ldp(a, 0, 1, 9, 0);
                e_dup(a, 0, 1);
                e_push01(a);
            }
            break;
        case OP_push_atom_value:
            {
                JSAtom atom = get_u32(bc + pos + 1);
                if (!__JS_AtomIsTaggedInt(atom) &&
                    rt->atom_array[atom]->atom_type == JS_ATOM_TYPE_STRING) {
                    /* (the bytecode holds the atom: its string lives as long) */
                    e_movx(a, 0, (uint64_t)(uintptr_t)rt->atom_array[atom]);
                    e_movx(a, 1, (uint64_t)(int64_t)JS_TAG_STRING);
                    e_dup(a, 0, 1);
                    e_push01(a);
                } else {
                    e_step(a, pos);
                }
            }
            break;
        case OP_push_this:
            {
                int slow = -1, done;
                e_ldp(a, 0, 1, RJF, JF(this_obj));
                if (!b->is_strict_mode) {
                    i_cmnw_imm(a, 1, 1);        /* tag == JS_TAG_OBJECT (-1)? */
                    slow = l_bcond(a, C_NE);
                }
                e_dup(a, 0, 1);
                e_push01(a);
                if (slow >= 0) {
                    done = l_b(a);
                    l_here(a, slow);
                    e_step(a, pos);
                    l_here(a, done);
                }
            }
            break;

        case OP_get_loc: case OP_get_loc8: case OP_get_loc0: case OP_get_loc1:
        case OP_get_loc2: case OP_get_loc3:
            t_get(a, RVAR, op == OP_get_loc ? get_u16(bc + pos + 1) :
                  op == OP_get_loc8 ? bc[pos + 1] : op - OP_get_loc0);
            break;
        case OP_get_loc0_loc1:
            t_get(a, RVAR, 0);
            t_get(a, RVAR, 1);
            break;
        case OP_put_loc: case OP_put_loc8: case OP_put_loc0: case OP_put_loc1:
        case OP_put_loc2: case OP_put_loc3:
            t_put(a, RVAR, op == OP_put_loc ? get_u16(bc + pos + 1) :
                  op == OP_put_loc8 ? bc[pos + 1] : op - OP_put_loc0, false);
            break;
        case OP_set_loc: case OP_set_loc8: case OP_set_loc0: case OP_set_loc1:
        case OP_set_loc2: case OP_set_loc3:
            t_put(a, RVAR, op == OP_set_loc ? get_u16(bc + pos + 1) :
                  op == OP_set_loc8 ? bc[pos + 1] : op - OP_set_loc0, true);
            break;
        case OP_get_arg: case OP_get_arg0: case OP_get_arg1: case OP_get_arg2:
        case OP_get_arg3:
            t_get(a, RARG, op == OP_get_arg ? get_u16(bc + pos + 1) : op - OP_get_arg0);
            break;
        case OP_put_arg: case OP_put_arg0: case OP_put_arg1: case OP_put_arg2:
        case OP_put_arg3:
            t_put(a, RARG, op == OP_put_arg ? get_u16(bc + pos + 1) : op - OP_put_arg0,
                  false);
            break;
        case OP_set_arg: case OP_set_arg0: case OP_set_arg1: case OP_set_arg2:
        case OP_set_arg3:
            t_put(a, RARG, op == OP_set_arg ? get_u16(bc + pos + 1) : op - OP_set_arg0,
                  true);
            break;
        case OP_get_loc_check:
            {
                int off, r = e_slot(a, RVAR, get_u16(bc + pos + 1), &off), slow, done;
                e_ldp(a, 0, 1, r, off);
                i_cmpw_imm(a, 1, JS_TAG_UNINITIALIZED);
                slow = l_bcond(a, C_EQ);
                e_dup(a, 0, 1);
                e_push01(a);
                done = l_b(a);
                l_here(a, slow);
                e_step(a, pos);
                l_here(a, done);
            }
            break;
        case OP_put_loc_check:
            {
                int off, r = e_slot(a, RVAR, get_u16(bc + pos + 1), &off), slow, done;
                if (r == 17) {
                    i_movr(a, 12, 17);
                    r = 12;
                }
                e_ldrw(a, 9, r, off + 8);
                i_cmpw_imm(a, 9, JS_TAG_UNINITIALIZED);
                slow = l_bcond(a, C_EQ);
                t_store_at(a, r, off, false);
                done = l_b(a);
                l_here(a, slow);
                e_step(a, pos);
                l_here(a, done);
            }
            break;

        case OP_get_var_ref: case OP_get_var_ref0: case OP_get_var_ref1:
        case OP_get_var_ref2: case OP_get_var_ref3:
            e_var_ref_ptr(a, op == OP_get_var_ref ? get_u16(bc + pos + 1) :
                          op - OP_get_var_ref0);
            e_ldp(a, 0, 1, 9, 0);
            e_dup(a, 0, 1);
            e_push01(a);
            break;
        case OP_get_var_ref_check:
            {
                int slow, done;
                e_var_ref_ptr(a, get_u16(bc + pos + 1));
                e_ldp(a, 0, 1, 9, 0);
                i_cmpw_imm(a, 1, JS_TAG_UNINITIALIZED);
                slow = l_bcond(a, C_EQ);
                e_dup(a, 0, 1);
                e_push01(a);
                done = l_b(a);
                l_here(a, slow);
                e_step(a, pos);
                l_here(a, done);
            }
            break;
        case OP_put_var_ref: case OP_put_var_ref0: case OP_put_var_ref1:
        case OP_put_var_ref2: case OP_put_var_ref3:
        case OP_set_var_ref: case OP_set_var_ref0: case OP_set_var_ref1:
        case OP_set_var_ref2: case OP_set_var_ref3:
            {
                bool keep = op >= OP_set_var_ref0 ? op <= OP_set_var_ref3 :
                            op == OP_set_var_ref;
                int idx = op == OP_put_var_ref || op == OP_set_var_ref ?
                          get_u16(bc + pos + 1) :
                          op - (keep ? OP_set_var_ref0 : OP_put_var_ref0);
                e_var_ref_ptr(a, idx);
                i_movr(a, 12, 9);
                t_store_at(a, 12, 0, keep);
            }
            break;

        case OP_drop:
            put(a, 0xA9C00000u | ((uint32_t)-2 & 0x7F) << 15 | 11u << 10 | (uint32_t)RSP << 5 | 10);  /* ldp x10, x11, [x19, #-16]! */
            e_free(a, 10, 11);
            break;
        case OP_nip:                /* a b -> b */
            e_ldp(a, 0, 1, RSP, -16);
            e_ldp(a, 10, 11, RSP, -32);
            e_stp(a, 0, 1, RSP, -32);
            e_addi(a, RSP, RSP, -16);
            e_free(a, 10, 11);
            break;
        case OP_dup:                /* a -> a a */
            e_ldp(a, 0, 1, RSP, -16);
            e_dup(a, 0, 1);
            e_push01(a);
            break;
        case OP_dup2:               /* a b -> a b a b */
            e_ldp(a, 0, 1, RSP, -32);
            e_dup(a, 0, 1);
            e_ldp(a, 2, 3, RSP, -16);
            e_dup(a, 2, 3);
            e_stp(a, 0, 1, RSP, 0);
            e_stp(a, 2, 3, RSP, 16);
            e_addi(a, RSP, RSP, 32);
            break;
        case OP_swap:               /* a b -> b a */
            e_ldp(a, 0, 1, RSP, -32);
            e_ldp(a, 2, 3, RSP, -16);
            e_stp(a, 2, 3, RSP, -32);
            e_stp(a, 0, 1, RSP, -16);
            break;
        case OP_insert2:            /* obj a -> a obj a */
            e_ldp(a, 0, 1, RSP, -16);
            e_ldp(a, 2, 3, RSP, -32);
            e_dup(a, 0, 1);
            e_stp(a, 0, 1, RSP, -32);
            e_stp(a, 2, 3, RSP, -16);
            e_push01(a);
            break;

        case OP_goto: case OP_goto16: case OP_goto8:
            e_goto(a, pos, jit_branch_target(bc, pos, op));
            break;
        case OP_if_true: case OP_if_false: case OP_if_true8: case OP_if_false8:
            t_if(a, pos, jit_branch_target(bc, pos, op),
                 op == OP_if_true || op == OP_if_true8);
            break;
        case OP_catch:
            t_push_const(a, (uint32_t)jit_branch_target(bc, pos, op), JS_TAG_CATCH_OFFSET);
            break;
        case OP_gosub:
            t_push_const(a, (uint32_t)n, JS_TAG_INT);
            e_b(a, jit_branch_target(bc, pos, op));
            break;
        case OP_return:
            put(a, 0xA9C00000u | ((uint32_t)-2 & 0x7F) << 15 | 1u << 10 | (uint32_t)RSP << 5 | 0); /* ldp x0, x1, [x19, #-16]! */
            e_stp(a, 0, 1, RJF, JF(ret_val));
            e_b(a, JT_RET);
            break;
        case OP_return_undef:
            i_movz(a, 0, 0, 0, true);
            i_movz(a, 1, JS_TAG_UNDEFINED, 0, true);
            e_stp(a, 0, 1, RJF, JF(ret_val));
            e_b(a, JT_RET);
            break;

        case OP_add: case OP_sub: case OP_mul: case OP_and: case OP_or: case OP_xor:
        case OP_shl: case OP_sar: case OP_shr:
            t_binary_int(a, pos, op);
            break;
        case OP_lt: case OP_lte: case OP_gt: case OP_gte: case OP_eq: case OP_neq:
        case OP_strict_eq: case OP_strict_neq:
            {
                bool fuse = false;
                int nop = n < len ? bc[n] : -1, nt = -1;
                if ((nop == OP_if_true || nop == OP_if_false || nop == OP_if_true8 ||
                     nop == OP_if_false8) && !is_target[n]) {
                    fuse = true;
                    nt = jit_branch_target(bc, n, nop);
                }
                if (t_compare(a, pos, op, nop, n, nt, fuse)) {
                    a->pc2code[n] = -1;     /* (no entry: not a target) */
                    n += short_opcode_info(nop).size;
                }
            }
            break;
        case OP_div:
            e_helper(a, jh_arith, pos, 1, OP_div, 0);
            break;
        case OP_typeof:
            e_helper(a, jh_typeof, pos, 0, 0, 0);
            break;
        case OP_define_field:
            e_helper(a, jh_define_field, pos, 1, get_u32(bc + pos + 1), 0);
            break;
        case OP_inc:
        case OP_dec:
            t_incdec(a, pos, op == OP_inc ? 1 : -1);
            break;
        case OP_inc_loc: case OP_dec_loc: case OP_add_loc:
            t_loc_arith(a, pos, op, bc[pos + 1]);
            break;
        case OP_lnot:
            {
                int slow, done;
                e_ldp(a, 0, 1, RSP, -16);
                i_cmpw_imm(a, 1, JS_TAG_UNDEFINED);
                slow = l_bcond(a, C_HI);
                i_cmpw_imm(a, 0, 0);
                i_cset(a, 0, C_EQ);
                i_movz(a, 1, JS_TAG_BOOL, 0, true);
                e_stp(a, 0, 1, RSP, -16);
                done = l_b(a);
                l_here(a, slow);
                e_helper(a, jh_lnot, pos, 0, 0, 0);
                l_here(a, done);
            }
            break;

        case OP_get_field:
        case OP_get_field2:
            t_get_field_ic(a, pos, get_u32(bc + pos + 1), op == OP_get_field2);
            break;
        case OP_get_length:
            t_get_field_ic(a, pos, JS_ATOM_length, 0);
            break;
        case OP_put_field:
            t_put_field_ic(a, pos, get_u32(bc + pos + 1));
            break;
        case OP_get_array_el:
        case OP_get_array_el2:
            e_helper(a, jh_get_array_el, pos, 1, op == OP_get_array_el2, 0);
            break;
        case OP_put_array_el:
            e_helper(a, jh_put_array_el, pos, 0, 0, 0);
            break;
        case OP_call0: case OP_call1: case OP_call2: case OP_call3:
            e_helper(a, jh_call, pos, 2, op - OP_call0, JC_CALL);
            break;
        case OP_call:
        case OP_tail_call:
            e_helper(a, jh_call, pos, 2, get_u16(bc + pos + 1),
                     op == OP_call ? JC_CALL : JC_TAIL);
            break;
        case OP_call_method:
        case OP_tail_call_method:
            e_helper(a, jh_call, pos, 2, get_u16(bc + pos + 1),
                     op == OP_call_method ? JC_METHOD : JC_TAIL_METHOD);
            break;
        case OP_call_constructor:
            e_helper(a, jh_call, pos, 2, get_u16(bc + pos + 1), JC_CTOR);
            break;
        case OP_get_var:
        case OP_get_var_undef:
            e_helper(a, jh_get_var, pos, 2, get_u32(bc + pos + 1), op == OP_get_var);
            break;
        case OP_put_var:
        case OP_put_var_init:
            e_helper(a, jh_put_var, pos, 2, get_u32(bc + pos + 1), op == OP_put_var_init);
            break;
        case OP_fclosure:
        case OP_fclosure8:
            e_helper(a, jh_fclosure, pos, 1,
                     op == OP_fclosure ? get_u32(bc + pos + 1) : bc[pos + 1], 0);
            break;
        case OP_object:
            e_helper(a, jh_object, pos, 0, 0, 0);
            break;
        case OP_nop:
            break;

        /* the instructions whose next one is not the next in the bytecode: left to the
           interpreter, from this one on */
        case OP_ret:
        case OP_with_get_var: case OP_with_put_var: case OP_with_delete_var:
        case OP_with_make_ref: case OP_with_get_ref: case OP_with_get_ref_undef:
        case OP_initial_yield: case OP_yield: case OP_yield_star:
        case OP_async_yield_star: case OP_await: case OP_return_async:
            e_strx(a, RSP, RJF, JF(sp));
            e_addi(a, 9, RBC, pos);
            e_strx(a, 9, RJF, JF(pc));
            i_movz(a, 9, JIT_EXIT, 0, false);
            e_strw(a, 9, RJF, JF(code));
            e_b(a, JT_LEAVE);
            break;

        default:
            e_step(a, pos);
            break;
        }
        pos = n;
    }

    /* the ways out: a return (the stack in x19), a helper's NULL (jf set) */
    a->ret_at = a->n;
    e_strx(a, RSP, RJF, JF(sp));
    i_movz(a, 0, JIT_RET, 0, false);
    {
        int epi = l_b(a);
        a->leave_at = a->n;
        e_ldrw(a, 0, RJF, JF(code));
        l_here(a, epi);
    }
    put(a, 0xA94153F3u);                  /* ldp x19, x20, [sp, #16] */
    put(a, 0xA9425BF5u);                  /* ldp x21, x22, [sp, #32] */
    put(a, 0xA94363F7u);                  /* ldp x23, x24, [sp, #48] */
    put(a, 0xA9446BF9u);                  /* ldp x25, x26, [sp, #64] */
    put(a, 0xA8C57BFDu);                  /* ldp x29, x30, [sp], #80 */
    put(a, 0xD65F03C0u);                  /* ret */
    if (a->fail)
        goto fail;

    for (i = 0; i < a->nfix; i++) {
        int t = a->fix[i].target, to;
        to = t == JT_LEAVE ? a->leave_at : t == JT_RET ? a->ret_at :
             (t >= 0 && t <= len) ? a->pc2code[t] : -1;
        if (to < 0)
            goto fail;
        patch(a, a->fix[i].at, to);
    }
    if (a->fail)
        goto fail;

    code = jit_block_alloc(sizeof(JitCode) + a->n * sizeof(uint32_t));
    if (!code)
        goto fail;
    code->n_insn = a->n;
    memcpy(code->insn, a->buf, a->n * sizeof(uint32_t));
    jit_flush(code->insn, a->n * sizeof(uint32_t));
    b->jit_code = code;
 fail:
    free(a->buf);
    free(a->pc2code);
    free(a->fix);
    free(is_target);
    return code != NULL;
}

static bool jit_should_compile(JSContext *ctx, JSFunctionBytecode *b)
{
    if (b->jit_count == UINT32_MAX || b->func_kind != JS_FUNC_NORMAL)
        return false;
    if (++b->jit_count < (uint32_t)ctx->rt->jit_threshold)
        return false;
    if (b->byte_code_len > 65536 || !jit_compile(ctx, b)) {
        b->jit_count = UINT32_MAX;
        return false;
    }
    return true;
}

static no_inline int jit_enter(JSContext *caller_ctx, JSContext *ctx, JSStackFrame *sf,
                               JSFunctionBytecode *b, JSValueConst this_obj,
                               JSValueConst new_target, int argc, JSValueConst *argv,
                               JSValue *sp, JitOut *out)
{
    JitFrame jf;
    JSObject *p = JS_VALUE_GET_OBJ(sf->cur_func);
    int r;

    jf.sp = sp;
    jf.pc = NULL;
    jf.code = JIT_RET;
    jf.ctx = ctx;
    jf.caller_ctx = caller_ctx;
    jf.rt = ctx->rt;
    jf.sf = sf;
    jf.b = b;
    jf.var_refs = p->u.func.var_refs;
    jf.var_buf = sf->var_buf;
    jf.arg_buf = sf->arg_buf;
    jf.this_obj = this_obj;
    jf.new_target = new_target;
    jf.argc = argc;
    jf.argv = argv;
    r = ((int (*)(JitFrame *))(void *)((JitCode *)b->jit_code)->insn)(&jf);
    out->sp = jf.sp;
    out->pc = (uint8_t *)jf.pc;
    out->ret_val = jf.ret_val;
    return r;
}

#else /* !CONFIG_QJS_JIT */

bool JS_SetJIT(JSRuntime *rt, int threshold, void *(*code_alloc)(size_t size))
{
    (void)rt;
    (void)threshold;
    (void)code_alloc;
    return false;
}

#endif /* CONFIG_QJS_JIT */
