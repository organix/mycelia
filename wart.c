/*
 * wart.c -- WebAssembly Actor Runtime
 * Copyright 2022 Dale Schumacher
 */

/**
See further [https://github.com/organix/mycelia/blob/master/wart.md]
**/

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>     // for intptr_t, uintptr_t, uint8_t, uint16_t, etc.
#include <inttypes.h>   // for PRIiPTR, PRIuPTR, PRIXPTR, etc.
#include <time.h>       // for clock_t, clock(), etc.

#define ERROR(x)    x   // include/exclude error instrumentation
#define WARN(x)     x   // include/exclude warning instrumentation
#define DEBUG(x)        // include/exclude debug instrumentation
#define XDEBUG(x)       // include/exclude extra debugging

#define NO_CELL_FREE  0 // never release allocated cells
#define GC_CALL_DEPTH 0 // count recursion depth during garbage collection
#define GC_TRACE_FREE 1 // trace free list during mark phase
#define CONCURRENT_GC 1 // interleave garbage collection with event dispatch
#define MULTIPHASE_GC 0 // perform gc mark and sweep separately
#define TIME_DISPATCH 1 // measure execution time for message dispatch
#define META_ACTORS   1 // include meta-actors facilities

#ifndef __SIZEOF_POINTER__
#error "need __SIZEOF_POINTER__ for hexdump()"
#endif

typedef intptr_t int_t;
typedef uintptr_t nat_t;
typedef void *ptr_t;

#define INT(n) ((int_t)(n))
#define NAT(n) ((nat_t)(n))
#define PTR(n) ((ptr_t)(n))

// WASM base types
typedef int32_t i32;
typedef int64_t i64;

typedef struct cell {
    int_t       head;
    int_t       tail;
} cell_t;

#define PROC_DECL(name)  int_t name(int_t self, int_t arg)

typedef PROC_DECL((*proc_t));

#define TAG_MASK    INT(0x3)
#define TAG_FIXNUM  INT(0x0)
#define TAG_PAIR    INT(0x1)
#define TAG_SYMBOL  INT(0x2)
#define TAG_ACTOR   INT(0x3)

#define MK_NUM(n)   INT(NAT(n)<<2)
#define MK_PAIR(p)  INT(PTR(p)+TAG_PAIR)
#define MK_SYM(n)   INT((NAT(n)<<2)|TAG_SYMBOL)
#define MK_ACTOR(p) INT(PTR(p)+TAG_ACTOR)

#define MK_BOOL(b)  ((b) ? TRUE : FALSE)
#define MK_PROC(p)  MK_ACTOR(p)

#define IS_ADDR(v)  ((v)&1)

#define IS_NUM(v)   (((v)&TAG_MASK) == TAG_FIXNUM)
#define IS_PAIR(v)  (((v)&TAG_MASK) == TAG_PAIR)
#define IS_SYM(v)   (((v)&TAG_MASK) == TAG_SYMBOL)
#define IS_ACTOR(v) (((v)&TAG_MASK) == TAG_ACTOR)

#define IS_PROC(v)  is_proc(v)

#define TO_INT(v)   (INT(v)>>2)
#define TO_NAT(v)   (NAT(v)>>2)
#define TO_PTR(v)   PTR(NAT(v)&~TAG_MASK)

void newline() {  // DO NOT MOVE -- USED TO DEFINE is_proc()
    printf("\n");
    fflush(stdout);
}

#define OK          (0)
#define INF         MK_NUM(TO_INT(~(NAT(-1)>>1)))
#define UNDEF       MK_ACTOR(&a_undef)
#define UNIT        MK_ACTOR(&a_unit)
#define FALSE       MK_ACTOR(&a_false)
#define TRUE        MK_ACTOR(&a_true)
#define NIL         MK_ACTOR(&a_nil)
#define FAIL        MK_ACTOR(&a_fail)

// FORWARD DECLARATIONS
PROC_DECL(Undef);
PROC_DECL(Unit);
PROC_DECL(Boolean);
PROC_DECL(Null);
PROC_DECL(Pair);
PROC_DECL(Symbol);
PROC_DECL(Fixnum);
PROC_DECL(Fail);

int is_proc(int_t val);
void print(int_t value);
void debug_print(char *label, int_t value);
static void hexdump(char *label, int_t *addr, size_t cnt);

const cell_t a_undef = { .head = MK_PROC(Undef), .tail = UNDEF };
const cell_t a_unit = { .head = MK_PROC(Unit), .tail = UNIT };
const cell_t a_false = { .head = MK_PROC(Boolean), .tail = FALSE };
const cell_t a_true = { .head = MK_PROC(Boolean), .tail = TRUE };
const cell_t a_nil = { .head = MK_PROC(Null), .tail = NIL };
const cell_t a_fail = { .head = MK_PROC(Fail), .tail = FAIL };

/*
 * error handling
 */

int_t panic(char *reason) {
    fprintf(stderr, "\nPANIC! %s\n", reason);
    exit(-1);
    return UNDEF;  // not reached, but typed for easy swap with error()
}

int_t error(char *reason) {
    fprintf(stderr, "\nERROR! %s\n", reason);
    return UNDEF;
}

int_t failure(char *_file_, int _line_) {
    fprintf(stderr, "\nASSERT FAILED! %s:%d\n", _file_, _line_);
    return UNDEF;
}

#define ASSERT(cond)    if (!(cond)) return failure(__FILE__, __LINE__)

/*
 * heap memory management (cells)
 */

#define CELL_MAX (1 << 12)  // 4K cells
cell_t cell[CELL_MAX] = {
    { .head = 1, .tail = 1 },  // root cell (limit,free)
    { .head = 0, .tail = 0 },  // end of free-list
};
// note: free-list is linked by index, not with pointers

int in_heap(int_t val) {
    return IS_ADDR(val) && (NAT(TO_PTR(val) - PTR(cell)) < sizeof(cell));
}

PROC_DECL(FreeCell) {
    ERROR(debug_print("FreeCell self", self));
    return panic("DISPATCH TO FREE CELL!");
}
#define FREE_CELL   MK_PROC(FreeCell)

static int gc_running = 0;      // set during unsafe gc phase(s)

#if CONCURRENT_GC
static void gc_set_mark(i32 ofs);  // FORWARD DECLARATION
static void gc_clr_mark(i32 ofs);  // FORWARD DECLARATION
#endif

static cell_t *cell_new() {
    int_t head = cell[0].tail;
    int_t next = cell[head].tail;
    if (next) {
        // use cell from free-list
        cell[0].tail = next;
#if CONCURRENT_GC
        gc_set_mark(head);
#endif
        return &cell[head];
    }
    next = head + 1;
    if (next < CELL_MAX) {
        // extend top of heap
        cell[next].head = 0;
        cell[next].tail = 0;
        cell[0].head = next;
        cell[0].tail = next;
#if CONCURRENT_GC
        gc_set_mark(head);
#endif
        return &cell[head];
    }
    panic("out of cell memory");
    return PTR(0);  // NOT REACHED!
}

static void cell_reclaim(cell_t *p) {
    XDEBUG(fprintf(stderr, "cell_reclaim: p=%p\n", p));
#if !NO_CELL_FREE
    // link into free-list
    p->tail = cell[0].tail;
    int_t ofs = INT(p - cell);
    XDEBUG(fprintf(stderr, "cell_reclaim: ofs=%"PRIdPTR"\n", ofs));
    cell[0].tail = ofs;
#endif
}

int_t cell_free(int_t val) {
    XDEBUG(debug_print("cell_free val", val));
    if (!in_heap(val)) panic("free() of non-heap cell");
    cell_t *p = TO_PTR(val);
    ASSERT(p->head != FREE_CELL);  // double-free
    p->head = FREE_CELL;
    p->tail = FREE_CELL;
#if CONCURRENT_GC
    if (gc_running) {
        int_t ofs = INT(p - cell);
        XDEBUG(fprintf(stderr, "cell_free: *running* ofs=%"PRIdPTR"\n", ofs));
        gc_clr_mark(ofs);
        return NIL;
    }
#endif
    cell_reclaim(p);
    return NIL;
}

int_t cons(int_t head, int_t tail) {
    cell_t *p = cell_new();
    p->head = head;
    p->tail = tail;
    return MK_PAIR(p);
}

#define list_0  NIL
#define list_1(v1)  cons((v1), NIL)
#define list_2(v1,v2)  cons((v1), cons((v2), NIL))
#define list_3(v1,v2,v3)  cons((v1), cons((v2), cons((v3), NIL)))
#define list_4(v1,v2,v3,v4)  cons((v1), cons((v2), cons((v3), cons((v4), NIL))))
#define list_5(v1,v2,v3,v4,v5)  cons((v1), cons((v2), cons((v3), cons((v4), cons((v5), NIL)))))
#define list_6(v1,v2,v3,v4,v5,v6)  cons((v1), cons((v2), cons((v3), cons((v4), cons((v5), cons((v6), NIL))))))

int_t car(int_t val) {
    if (!IS_PAIR(val)) return error("car() of non-PAIR");
    cell_t *p = TO_PTR(val);
    return p->head;
}

int_t cdr(int_t val) {
    if (!IS_PAIR(val)) return error("cdr() of non-PAIR");
    cell_t *p = TO_PTR(val);
    return p->tail;
}

int equal(int_t x, int_t y) {
    XDEBUG(debug_print("equal x", x));
    XDEBUG(debug_print("equal y", y));
#if 0
    return (x == y)
        || (IS_PAIR(x) && IS_PAIR(y) && equal(car(x), car(y)) && equal(cdr(x), cdr(y)));
#else
    if (x == y) return 1;
    while (IS_PAIR(x) && IS_PAIR(y)) {
        if (!equal(car(x), car(y))) break;
        x = cdr(x);
        y = cdr(y);
        if (x == y) return 1;
    }
    return 0;
#endif
}

int list_len(int_t val) {
    int len = 0;
    while (IS_PAIR(val)) {
        ++len;
        val = cdr(val);
    }
    return len;
}

int_t set_car(int_t val, int_t head) {
    if (!in_heap(val)) panic("set_car() of non-heap cell");
    cell_t *p = TO_PTR(val);
    return p->head = head;
}

int_t set_cdr(int_t val, int_t tail) {
    if (!in_heap(val)) panic("set_cdr() of non-heap cell");
    cell_t *p = TO_PTR(val);
    return p->tail = tail;
}

int_t get_code(int_t val) {
    int_t code = UNDEF;  // polymorphic dispatch procedure
    if (IS_PROC(val)) {
        code = val;
    } else if (IS_PAIR(val)) {
        code = MK_PROC(Pair);
    } else if (IS_SYM(val)) {
        code = MK_PROC(Symbol);
    } else if (IS_NUM(val)) {
        code = MK_PROC(Fixnum);
    } else if (IS_ACTOR(val)) {
        cell_t *p = TO_PTR(val);
        code = p->head;
    }
    return code;
}

int_t get_data(int_t val) {
    int_t data = val;
    if (!IS_PROC(val) && IS_ACTOR(val)) {
        cell_t *p = TO_PTR(val);
        data = p->tail;
    }
    return data;
}

PROC_DECL(obj_call) {
    int_t code = get_code(self);
    if (!IS_PROC(code)) return error("obj_call() requires a procedure");
    proc_t proc = TO_PTR(code);
    return (*proc)(self, arg);
}

/*
 * garbage collection (reclaiming the heap)
 */

#define GC_LO_BITS(ofs) ((ofs) & 0x1F)
#define GC_HI_BITS(ofs) ((ofs) >> 5)

#define GC_MAX_BITS GC_HI_BITS(CELL_MAX)

i32 gc_bits[GC_MAX_BITS];

i32 gc_clear() {  // clear all GC bits
    XDEBUG(fprintf(stderr, "> gc_clear\n"));
    for (i32 i = 0; i < GC_MAX_BITS; ++i) {
        gc_bits[i] = 0;
    }
    XDEBUG(fprintf(stderr, "< gc_clear\n"));
    return 0;
}

static i32 gc_get_mark(i32 ofs) {
    return ((gc_bits[GC_HI_BITS(ofs)] & (1 << GC_LO_BITS(ofs))));
}

static void gc_set_mark(i32 ofs) {
    XDEBUG(fprintf(stderr, "  gc_set_mark(%d)\n", ofs));
    gc_bits[GC_HI_BITS(ofs)] |= (1 << GC_LO_BITS(ofs));
}

static void gc_clr_mark(i32 ofs) {
    XDEBUG(fprintf(stderr, "  gc_clr_mark(%d)\n", ofs));
    gc_bits[GC_HI_BITS(ofs)] &= ~(1 << GC_LO_BITS(ofs));
}

i32 gc_mark_free() {  // mark cells in the free-list
    XDEBUG(fprintf(stderr, "> gc_mark_free\n"));
    //gc_set_mark(0);
    i32 cnt = 0;
#if GC_TRACE_FREE
    i32 ofs = cell[0].tail;
    while (ofs) {
        gc_set_mark(ofs);
        ++cnt;
        ofs = cell[ofs].tail;
    }
#else
    i32 ofs = cell[0].head;
    DEBUG(fprintf(stderr, "  gc_mark_free: top=%d\n", ofs));
    //gc_set_mark(ofs);
    cell[0].tail = ofs;  // empty free-list
#endif
    XDEBUG(fprintf(stderr, "< gc_mark_free (cnt=%d)\n", cnt));
    return cnt;
}

#if GC_CALL_DEPTH
i32 gc_mark_cell(int_t val, i32 depth)
#else
i32 gc_mark_cell(int_t val)
#endif
{  // mark cells reachable from `val`
    //DEBUG(fprintf(stderr, "> gc_mark_cell val=16#%"PRIxPTR"\n", val));
    XDEBUG(debug_print("> gc_mark_cell", val));
    i32 cnt = 0;
    while (in_heap(val)) {
        cell_t *p = TO_PTR(val);
        i32 ofs = INT(p - cell);
        if (gc_get_mark(ofs)) {
            break;  // cell already marked
        }
        gc_set_mark(ofs);
        ++cnt;
#if GC_CALL_DEPTH
        cnt += gc_mark_cell(p->head, ++depth);  // recurse on head
#else
        cnt += gc_mark_cell(p->head);  // recurse on head
#endif
        val = p->tail;  // iterate over tail
    }
#if GC_CALL_DEPTH
    XDEBUG(fprintf(stderr, "< gc_mark_cell depth=%d (cnt=%d)\n", depth, cnt));
#else
    XDEBUG(fprintf(stderr, "< gc_mark_cell (cnt=%d)\n", cnt));
#endif
    //XDEBUG(fprintf(stderr, "< gc_mark_cell val=16#%"PRIxPTR" (cnt=%d)\n", val, cnt));
    return cnt;
}

static cell_t event_q;
static cell_t gnd_locals;
i32 gc_mark_roots() {  // mark cells reachable from the root-set
    XDEBUG(fprintf(stderr, "> gc_mark_roots\n"));
    i32 n = 0;
#if GC_CALL_DEPTH
    n += gc_mark_cell(event_q.head, 0);
    n += gc_mark_cell(gnd_locals.head, 0);
#else
    n += gc_mark_cell(event_q.head);
    n += gc_mark_cell(gnd_locals.head);
#endif
    XDEBUG(fprintf(stderr, "< gc_mark_roots (n=%d)\n", n));
    return n;
}

i32 gc_sweep() {  // free unmarked cells
    XDEBUG(fprintf(stderr, "> gc_sweep\n"));
    i32 cnt = 0;
    //if (!gc_get_mark(0)) panic("heap root not marked");
    i32 next = cell[0].head;
    //if (!gc_get_mark(next)) panic("heap next not marked");
    DEBUG(fprintf(stderr, "  gc_sweep: top=%d\n", next));
    while (--next > 0) {
        if (!gc_get_mark(next)) {
            cell_reclaim(&cell[next]);
            ++cnt;
        }
    }
    XDEBUG(fprintf(stderr, "< gc_sweep (cnt=%d)\n", cnt));
    return cnt;
}

i32 gc_mark_and_sweep() {
    i32 n = gc_clear();
    n = gc_mark_free();
    XDEBUG(printf("gc_mark_and_sweep: marked %d free cells\n", n));
    n = gc_mark_roots();
    XDEBUG(printf("gc_mark_and_sweep: marked %d used cells\n", n));
    n = gc_sweep();
    XDEBUG(printf("gc_mark_and_sweep: free'd %d dead cells\n", n));
    return n;
}

int_t cell_usage() {
    WARN(fprintf(stderr,
        "> cell_usage: limit=%"PRIdPTR" free=%"PRIdPTR" max=%"PRIdPTR"\n",
        cell[0].head, cell[0].tail, INT(CELL_MAX)));
    int_t count = 0;
    int_t prev = 0;
    int_t next = cell[0].tail;
    while (cell[next].tail) {
        XDEBUG(fprintf(stderr, "  cell_usage: trace %"PRIdPTR"\n", next));
        ++count;
        prev = next;
        next = cell[prev].tail;
    }
    WARN(fprintf(stderr,
        "< cell_usage: free=%"PRIdPTR" total=%"PRIdPTR" max=%"PRIdPTR"\n",
        count, next-1, INT(CELL_MAX)));
    return cons(MK_NUM(count), MK_NUM(next-1));  // cells (free, heap)
}

/*
 * interned strings (symbols)
 */

#define INTERN_MAX (1024)
char intern[INTERN_MAX] = {
    0,  // end of interned strings
};

int is_symbol(int_t val) {
    return IS_SYM(val) && (NAT(TO_PTR(val) - PTR(intern)) < sizeof(intern));
}

int_t symbol(char *s) {
    int_t j;
    int_t n = 0;
    while (s[n]) ++n;  // compute c-string length
    int_t i = 0;
    while (intern[i]) {
        int_t m = intern[i++];  // symbol length
        if (n == m) {
            for (j = 0; (j < n); ++j) {
                if (s[j] != intern[i+j]) {
                    goto next;
                }
            }
            // found it!
            return MK_SYM(i-1);
        }
next:   i += m;
    }
    // new symbol
    intern[i++] = n;
    for (j = 0; (j < n); ++j) {
        intern[i+j] = s[j];
    }
    intern[i+j] = 0;
    return MK_SYM(i-1);
}

int_t s_ignore;
int_t s_quote;
int_t s_typeq;
int_t s_eval;
int_t s_apply;
int_t s_map;
int_t s_list;
int_t s_cons;
int_t s_car;
int_t s_cdr;
int_t s_if;
int_t s_and;
int_t s_or;
int_t s_eqp;
int_t s_equalp;
int_t s_seq;
int_t s_lambda;
int_t s_macro;
int_t s_define;
int_t s_booleanp;
int_t s_nullp;
int_t s_pairp;
int_t s_symbolp;
int_t s_numberp;
int_t s_add;
int_t s_sub;
int_t s_mul;
int_t s_lt;
int_t s_le;
int_t s_eqn;
int_t s_ge;
int_t s_gt;
int_t s_print;
int_t s_emit;
int_t s_debug_print;
int_t s_fold;
int_t s_foldr;
int_t s_bind;
int_t s_lookup;
int_t s_content;
#if META_ACTORS
int_t s_BEH;
int_t s_SELF;
int_t s_CREATE;
int_t s_SEND;
int_t s_BECOME;
int_t s_FAIL;
int_t s_println;
#endif

// runtime initialization
int_t symbol_boot() {
    s_ignore = symbol("_");
    s_quote = symbol("quote");
    s_typeq = symbol("typeq");
    s_eval = symbol("eval");
    s_apply = symbol("apply");
    s_map = symbol("map");
    s_list = symbol("list");
    s_cons = symbol("cons");
    s_car = symbol("car");
    s_cdr = symbol("cdr");
    s_if = symbol("if");
    s_and = symbol("and");
    s_or = symbol("or");
    s_eqp = symbol("eq?");
    s_equalp = symbol("equal?");
    s_seq = symbol("seq");
    s_lambda = symbol("lambda");
    s_macro = symbol("macro");
    s_define = symbol("define");
    s_booleanp = symbol("boolean?");
    s_nullp = symbol("null?");
    s_pairp = symbol("pair?");
    s_symbolp = symbol("symbol?");
    s_numberp = symbol("number?");
    s_add = symbol("+");
    s_sub = symbol("-");
    s_mul = symbol("*");
    s_lt = symbol("<");
    s_le = symbol("<=");
    s_eqn = symbol("=");
    s_ge = symbol(">=");
    s_gt = symbol(">");
    s_print = symbol("print");
    s_emit = symbol("emit");
    s_debug_print = symbol("debug-print");
    s_fold = symbol("fold");
    s_foldr = symbol("foldr");
    s_bind = symbol("bind");
    s_lookup = symbol("lookup");
    s_content = symbol("content");
#if META_ACTORS
    s_BEH = symbol("BEH");
    s_SELF = symbol("SELF");
    s_CREATE = symbol("CREATE");
    s_SEND = symbol("SEND");
    s_BECOME = symbol("BECOME");
    s_FAIL = symbol("FAIL");
    s_println = symbol("println");
#endif
    return OK;
}

/*
 * actor primitives
 */

int_t effect_new() {
    return cons(NIL, cons(NIL, NIL));  // empty effect
}

int_t actor_create(int_t code, int_t data) {
    if (!IS_PROC(code)) return error("actor code must be a procedure");
    cell_t *p = cell_new();
    p->head = code;
    p->tail = data;
    return MK_ACTOR(p);
}

int_t effect_create(int_t effect, int_t new_actor) {
    ASSERT(IS_ACTOR(new_actor));
    ASSERT(in_heap(new_actor));
    if (effect == NIL) effect = effect_new();  // lazy init
    if (IS_PAIR(effect) && (car(effect) != FAIL)) {
        int_t created = cons(new_actor, car(effect));
        if (!IS_PAIR(created)) return UNDEF;
        set_car(effect, created);
    }
    return effect;
}

int_t actor_send(int_t target, int_t msg) {
    //ASSERT(IS_ACTOR(target)); -- obj_call() polymorphic dispatch works for _ANY_ value!
    return cons(target, msg);
}

int_t effect_send(int_t effect, int_t new_event) {
    ASSERT(IS_PAIR(new_event));
    if (effect == NIL) effect = effect_new();  // lazy init
    if (IS_PAIR(effect) && (car(effect) != FAIL)) {
        int_t rest = cdr(effect);
        int_t sent = cons(new_event, car(rest));
        set_car(rest, sent);
    }
    return effect;
}

int_t actor_become(int_t code, int_t data) {
    return cons(code, data);
}

int_t effect_become(int_t effect, int_t new_beh) {
    ASSERT(IS_PAIR(new_beh));
    if (effect == NIL) effect = effect_new();  // lazy init
    if (IS_PAIR(effect) && (car(effect) != FAIL)) {
        int_t rest = cdr(effect);
        if (cdr(rest) != NIL) return error("must only BECOME once");
        set_cdr(rest, new_beh);
    }
    return effect;
}

int_t effect_fail(int_t effect, int_t reason) {
    // FIXME: free some, or all, of effect?
    DEBUG(debug_print("effect_fail reason", reason));
    return cons(FAIL, reason);
}

/*
 * actor event dispatch
 */

static cell_t event_q = { .head = NIL, .tail = NIL };

int_t event_q_append(int_t events) {
    if (events == NIL) return OK;  // nothing to add
    ASSERT(IS_PAIR(events));
    // find the end of events
    int_t tail = events;
    while (cdr(tail) != NIL) {
        tail = cdr(tail);
    }
    // append events on event_q
    if (event_q.head == NIL) {
        event_q.head = events;
    } else {
        set_cdr(event_q.tail, events);
    }
    event_q.tail = tail;
    return OK;
}

int_t event_q_take() {
    if (event_q.head == NIL) return UNDEF; // event queue empty
    int_t head = event_q.head;
    event_q.head = cdr(head);
    if (event_q.head == NIL) {
        event_q.tail = NIL;  // empty queue
    }
    int_t event = car(head);
    head = cell_free(head);
    return event;
}

int_t apply_effect(int_t self, int_t effect) {
    XDEBUG(debug_print("apply_effect self", self));
    XDEBUG(debug_print("apply_effect effect", effect));
    if (effect == NIL) return OK;  // no effect
    if (!IS_PAIR(effect)) {
        ERROR(debug_print("apply_effect non-PAIR", effect));
        return UNDEF;
    }
    int_t actors = car(effect);
    if (actors == FAIL) {
        WARN(debug_print("apply_effect error", effect));
        return effect;  // error thrown
    }
    // unchain actors
    XDEBUG(debug_print("apply_effect actors", actors));
    int_t rest = cdr(effect);
    effect = cell_free(effect);
    while (IS_PAIR(actors)) {  // free list, but not actors
        int_t next = cdr(actors);
        cell_free(actors);
        actors = next;
    }
    int_t events = car(rest);
    int_t beh = cdr(rest);
    rest = cell_free(rest);
    // update behavior
    XDEBUG(debug_print("apply_effect beh", beh));
    if (IS_PAIR(beh) && IS_ACTOR(self)) {
        cell_t *p = TO_PTR(self);
        p->head = car(beh);  // code
        p->tail = cdr(beh);  // data
        beh = cell_free(beh);
    }
    // add events to dispatch queue
    XDEBUG(debug_print("apply_effect events", events));
    int_t ok = event_q_append(events);
    return ok;
}

i64 event_dispatch_count = 0;
i64 event_dispatch_ticks = 0;

int_t event_dispatch() {
#if TIME_DISPATCH
    clock_t t0 = clock();
#endif
    int_t event = event_q_take();
    if (!IS_PAIR(event)) return UNDEF;  // nothing to dispatch
    int_t target = car(event);
    XDEBUG(debug_print("event_dispatch target", target));
    int_t msg = cdr(event);
    XDEBUG(debug_print("event_dispatch msg", msg));
    event = cell_free(event);
    // invoke actor behavior
    int_t effect = obj_call(target, msg);
    XDEBUG(debug_print("event_dispatch effect", effect));
    int_t ok = apply_effect(target, effect);
    // gather statistics
#if TIME_DISPATCH
    clock_t t1 = clock();
    ++event_dispatch_count;
#endif
#if CONCURRENT_GC
#if TIME_DISPATCH
    event_dispatch_ticks += (t1 - t0);  // exclude gc
    DEBUG(double dt = (double)(t1 - t0) / CLOCKS_PER_SEC;
        printf("event_dispatch: t0=%ld t1=%ld dt=%.6f (%ld CPS)\n", t0, t1, dt, (long)CLOCKS_PER_SEC));
#endif
#else // !CONCURRENT_GC
    if (ok == OK) {
        int freed = gc_mark_and_sweep();
        DEBUG(printf("event_dispatch: gc reclaimed %d cells\n", freed));
    }
#if TIME_DISPATCH
    clock_t t2 = clock();
    event_dispatch_ticks += (t2 - t0);  // include gc
    DEBUG(double dt = (double)(t1 - t0) / CLOCKS_PER_SEC;
        printf("event_dispatch: t0=%ld t1=%ld dt=%.6f (%ld CPS)\n", t0, t1, dt, (long)CLOCKS_PER_SEC));
    DEBUG(double gc = (double)(t2 - t1) / CLOCKS_PER_SEC;
        printf("event_dispatch: t1=%ld t2=%ld gc=%.6f (%ld CPS)\n", t1, t2, gc, (long)CLOCKS_PER_SEC));
#endif
#endif // CONCURRENT_GC
    return ok;
}

int_t event_loop() {
#if TIME_DISPATCH
    event_dispatch_count = 0;
    event_dispatch_ticks = 0;
#endif
    int_t result = OK;
    while (result == OK) {
        result = event_dispatch();
    }
#if TIME_DISPATCH
    double average = ((double)event_dispatch_ticks / (double)event_dispatch_count);
    printf("event_loop: count=%"PRId64" ticks=%"PRId64" average=%.3f\n",
        event_dispatch_count, event_dispatch_ticks, average);
#endif
    return result;
}

/*
 * actor behaviors
 */

#define GET_VARS()     int_t vars = get_data(self)
#define POP_VAR(name)  int_t name = car(vars); vars = cdr(vars)
#define TAIL_VAR(name) int_t name = vars

#define GET_ARGS()     int_t args = arg
#define POP_ARG(name)  int_t name = car(args); args = cdr(args)
#define TAIL_ARG(name) int_t name = args
#define END_ARGS()     if (args != NIL) return error("too many args")

PROC_DECL(sink_beh) {
    XDEBUG(debug_print("sink_beh self", self));
    DEBUG(debug_print("sink_beh arg", arg));
    GET_VARS();  // effect
    XDEBUG(debug_print("sink_beh vars", vars));
    TAIL_VAR(effect);
    return effect;
}
const cell_t a_sink = { .head = MK_PROC(sink_beh), .tail = NIL };
#define SINK  MK_ACTOR(&a_sink)

PROC_DECL(println_beh) {
    DEBUG(debug_print("println_beh self", self));
    GET_ARGS();  // value
    XDEBUG(debug_print("println_beh args", args));
    TAIL_ARG(value);
    int_t effect = NIL;

    print(value);
    newline();
    fflush(stdout);

    return effect;
}
const cell_t a_println = { .head = MK_PROC(println_beh), .tail = UNDEF };

PROC_DECL(tag_beh) {
    XDEBUG(debug_print("tag_beh self", self));
    GET_VARS();  // cust
    XDEBUG(debug_print("tag_beh vars", vars));
    TAIL_VAR(cust);
    GET_ARGS();  // msg
    XDEBUG(debug_print("tag_beh args", args));
    TAIL_ARG(msg);
    int_t effect = NIL;

    effect = effect_send(effect,
        actor_send(cust, cons(self, msg)));

    return effect;
}

static PROC_DECL(join_h_beh) {
    XDEBUG(debug_print("join_h_beh self", self));
    GET_VARS();  // (cust head . k_tail)
    XDEBUG(debug_print("join_h_beh vars", vars));
    POP_VAR(cust);
    POP_VAR(head);
    TAIL_VAR(k_tail);
    GET_ARGS();  // (tag . tail)
    XDEBUG(debug_print("join_h_beh args", args));
    POP_ARG(tag);
    TAIL_ARG(tail);
    int_t effect = NIL;

    if (tag == k_tail) {
        int_t value = cons(head, tail);
        XDEBUG(debug_print("join_h_beh value", value));
        effect = effect_send(effect,
            actor_send(cust, value));
    } else {
        effect = effect_send(effect,
            actor_send(cust, error("unexpected join tag")));
    }

    return effect;
}
static PROC_DECL(join_t_beh) {
    XDEBUG(debug_print("join_t_beh self", self));
    GET_VARS();  // (cust k_head . tail)
    XDEBUG(debug_print("join_t_beh vars", vars));
    POP_VAR(cust);
    POP_VAR(k_head);
    TAIL_VAR(tail);
    GET_ARGS();  // (tag . head)
    XDEBUG(debug_print("join_t_beh args", args));
    POP_ARG(tag);
    TAIL_ARG(head);
    int_t effect = NIL;

    if (tag == k_head) {
        int_t value = cons(head, tail);
        XDEBUG(debug_print("join_t_beh value", value));
        effect = effect_send(effect,
            actor_send(cust, value));
    } else {
        effect = effect_send(effect,
            actor_send(cust, error("unexpected join tag")));
    }

    return effect;
}
PROC_DECL(join_beh) {
    XDEBUG(debug_print("join_beh self", self));
    GET_VARS();  // (cust k_head . k_tail)
    XDEBUG(debug_print("join_beh vars", vars));
    POP_VAR(cust);
    POP_VAR(k_head);
    TAIL_VAR(k_tail);
    GET_ARGS();  // (tag . value)
    XDEBUG(debug_print("join_beh args", args));
    POP_ARG(tag);
    TAIL_ARG(value);
    int_t effect = NIL;

    if (tag == k_head) {
        effect = effect_become(effect,
            actor_become(MK_PROC(join_h_beh), cons(cust, cons(value, k_tail))));
    } else if (tag == k_tail) {
        effect = effect_become(effect,
            actor_become(MK_PROC(join_t_beh), cons(cust, cons(k_head, value))));
    } else {
        effect = effect_send(effect,
            actor_send(cust, error("unexpected join tag")));
    }

    return effect;
}

PROC_DECL(fork_beh) {
    XDEBUG(debug_print("fork_beh self", self));
    GET_VARS();  // (cust head . tail)
    XDEBUG(debug_print("fork_beh vars", vars));
    POP_VAR(cust);
    POP_VAR(head);
    TAIL_VAR(tail);
    GET_ARGS();  // (h_req . t_req)
    XDEBUG(debug_print("fork_beh args", args));
    POP_ARG(h_req);
    TAIL_ARG(t_req);
    int_t effect = NIL;

    int_t k_head = actor_create(MK_PROC(tag_beh), self);
    effect = effect_create(effect, k_head);

    int_t k_tail = actor_create(MK_PROC(tag_beh), self);
    effect = effect_create(effect, k_tail);

    effect = effect_send(effect,
        actor_send(head, cons(k_head, h_req)));

    effect = effect_send(effect,
        actor_send(tail, cons(k_tail, t_req)));

    effect = effect_become(effect,
        actor_become(MK_PROC(join_beh), cons(cust, cons(k_head, k_tail))));

    return effect;
}

#if CONCURRENT_GC
PROC_DECL(gc_sweep_beh);  // FORWARD DECLARATION

PROC_DECL(gc_mark_beh) {
    XDEBUG(debug_print("gc_mark_beh self", self));
    int_t effect = NIL;

    int_t root = event_q.head;  // everything is reachable from the event queue
    if (root == NIL) {
        // if event queue is empty, stop concurrent gc
        DEBUG(printf("gc_mark_beh: STOP CONCURRENT GC\n"));
        return effect;
    }

    gc_clear();
    XDEBUG(printf("gc_mark_beh: gc marks cleared\n"));

    int n = gc_mark_free();
    XDEBUG(printf("gc_mark_beh: marked %d free cells\n", n));

#if GC_CALL_DEPTH
    int m = gc_mark_cell(root, 0);
#else
    int m = gc_mark_cell(root);
#endif
    XDEBUG(printf("gc_mark_beh: marked %d used cells\n", m));

    gc_running = 1;  // enter unsafe gc phase

    effect = effect_become(effect,
        actor_become(MK_PROC(gc_sweep_beh), UNDEF));
    effect = effect_send(effect,
        actor_send(self, arg));

    XDEBUG(debug_print("gc_mark_beh effect", effect));
    return effect;
}
PROC_DECL(gc_sweep_beh) {
    XDEBUG(debug_print("gc_sweep_beh self", self));
    int_t effect = NIL;

    int n = gc_sweep();
    XDEBUG(printf("gc_sweep_beh: free'd %d dead cells\n", n));

    gc_running = 0;  // leave unsafe gc phase

    effect = effect_become(effect,
        actor_become(MK_PROC(gc_mark_beh), UNDEF));
    effect = effect_send(effect,
        actor_send(self, arg));

    XDEBUG(debug_print("gc_sweep_beh effect", effect));
    return effect;
}

PROC_DECL(gc_mark_and_sweep_beh) {  // perform all GC steps together
    XDEBUG(debug_print("gc_mark_and_sweep_beh self", self));
    GET_VARS();  // limit
    XDEBUG(debug_print("gc_mark_and_sweep_beh vars", vars));
    TAIL_VAR(limit);
    GET_ARGS();  // count
    XDEBUG(debug_print("gc_mark_and_sweep_beh args", args));
    TAIL_ARG(count);
    int_t effect = NIL;

    if (event_q.head == NIL) {
        // if event queue is empty, stop concurrent gc
        DEBUG(printf("gc_mark_and_sweep_beh: STOP CONCURRENT GC\n"));
        return effect;
    }

    int_t n = TO_INT(count);
    int_t m = TO_INT(limit);
    if (n < m) {
        // skip `limit` gc cycles
        XDEBUG(printf("gc_mark_and_sweep_beh: count(%"PRIdPTR") < limit(%"PRIdPTR")\n", n, m));
        effect = effect_send(effect,
            actor_send(self, MK_NUM(++n)));
        return effect;
    }

    int freed = gc_mark_and_sweep();
    DEBUG(printf("gc_mark_and_sweep_beh: gc reclaimed %d cells\n", freed));
    effect = effect_send(effect,
        actor_send(self, MK_NUM(0)));

    XDEBUG(debug_print("gc_mark_and_sweep_beh effect", effect));
    return effect;
}

// Note: `a_concurrent_gc` can not be `const` because it mutates during gc!
// WARNING! gc does not traverse static actors, so they can't point to the heap.
/*const*/ cell_t a_concurrent_gc = {
#if MULTIPHASE_GC
    .head = MK_PROC(gc_mark_beh),
#else
    .head = MK_PROC(gc_mark_and_sweep_beh),
#endif
    .tail = MK_NUM(5),
};
//    effect = effect_send(effect, actor_send(MK_ACTOR(&a_concurrent_gc), MK_NUM(0)));  // start gc
#endif // CONCURRENT_GC

PROC_DECL(assert_beh) {
    DEBUG(debug_print("assert_beh self", self));
    GET_VARS();  // expect
    XDEBUG(debug_print("assert_beh vars", vars));
    TAIL_VAR(expect);
    GET_ARGS();  // actual
    XDEBUG(debug_print("assert_beh args", args));
    TAIL_ARG(actual);
    int_t effect = NIL;
    if (!equal(expect, actual)) {
        ERROR(debug_print("assert_beh expect", expect));
        ERROR(debug_print("assert_beh actual", actual));
        effect = effect_fail(effect,
            panic("assert_beh !equal(expect, actual)"));
    }
    XDEBUG(debug_print("assert_beh effect", effect));
    return effect;
}

/*
 * ground environment
 */

static PROC_DECL(Type) {
    XDEBUG(debug_print("Type self", self));
    int_t T = get_code(self);  // behavior proc serves as a "type" identifier
    XDEBUG(debug_print("Type T", T));
    GET_ARGS();
    XDEBUG(debug_print("Type args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_typeq) {  // (cust 'typeq T)
        POP_ARG(Tq);
        DEBUG(debug_print("Type T?", Tq));
        END_ARGS();
        int_t value = MK_BOOL(T == Tq);
        DEBUG(debug_print("Type value", value));
        effect = effect_send(effect, actor_send(cust, value));
        return effect;
    }
    WARN(debug_print("Type NOT UNDERSTOOD", arg));
    return effect_send(effect,
        actor_send(cust, error("NOT UNDERSTOOD")));
}

static PROC_DECL(SeType) {
    XDEBUG(debug_print("SeType self", self));
    GET_ARGS();
    XDEBUG(debug_print("SeType args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_eval) {  // (cust 'eval env)
        POP_ARG(_env);
        END_ARGS();
        DEBUG(debug_print("SeType value", self));
        effect = effect_send(effect, actor_send(cust, self));
        return effect;
    }
    return Type(self, arg);  // delegate to Type
}

PROC_DECL(Undef) {
    DEBUG(debug_print("Undef self", self));
    GET_ARGS();
    DEBUG(debug_print("Undef args", args));
    return SeType(self, arg);  // delegate to SeType
}

PROC_DECL(Unit) {
    XDEBUG(debug_print("Unit self", self));
    GET_ARGS();
    XDEBUG(debug_print("Unit args", args));
    return SeType(self, arg);  // delegate to SeType
}

static PROC_DECL(Appl_k_args) {
    XDEBUG(debug_print("Appl_k_args self", self));
    GET_VARS();  // (cust oper env)
    XDEBUG(debug_print("Appl_k_args vars", vars));
    POP_VAR(cust);
    POP_VAR(oper);
    POP_VAR(env);
    GET_ARGS();  // opnd
    DEBUG(debug_print("Appl_k_args args", args));
    TAIL_ARG(opnd);
    int_t effect = NIL;
    if (IS_PROC(oper)) {
        proc_t prim = TO_PTR(oper);
        int_t value = (*prim)(opnd, env);  // delegate to primitive proc
        WARN(debug_print("Appl_k_args --DEPRECATED-- value", value));
        effect = effect_send(effect, actor_send(cust, value));
    } else {
        effect = effect_send(effect,
            actor_send(oper, list_4(cust, s_apply, opnd, env)));
    }
    return effect;
}
PROC_DECL(Appl) {
    DEBUG(debug_print("Appl self", self));
    GET_VARS();  // oper
    XDEBUG(debug_print("Appl vars", vars));
    TAIL_VAR(oper);
    GET_ARGS();
    XDEBUG(debug_print("Appl args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(env);
        END_ARGS();
        int_t k_args = actor_create(MK_PROC(Appl_k_args), list_3(cust, oper, env));
        effect = effect_create(effect, k_args);
        effect = effect_send(effect,
            actor_send(opnd, list_4(k_args, s_map, s_eval, env)));
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}

PROC_DECL(Macro);  // FORWARD DECLARATION
int_t unwrap(int_t appl) {
    // FIXME: figure out how to use `apply` with macros...
    //if (get_code(appl) != MK_PROC(Macro)) return appl;  // operative macro
    if (get_code(appl) != MK_PROC(Appl)) return error("applicative required");
    return get_data(appl);  // return underlying operative
}

PROC_DECL(Oper_prim) {  // call primitive procedure
    XDEBUG(debug_print("Oper_prim self", self));
    GET_VARS();  // proc
    XDEBUG(debug_print("Oper_prim vars", vars));
    TAIL_VAR(proc);
    ASSERT(IS_PROC(proc));
    GET_ARGS();
    DEBUG(debug_print("Oper_prim args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(env);
        END_ARGS();
        proc_t prim = TO_PTR(proc);
        int_t value = (*prim)(opnd, env);  // delegate to primitive proc
        XDEBUG(debug_print("Oper_prim value", value));
        effect = effect_send(effect, actor_send(cust, value));
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}

int_t match_pattern(int_t ptrn, int_t opnd, int_t assoc) {
    XDEBUG(debug_print("match_pattern ptrn", ptrn));
    XDEBUG(debug_print("match_pattern opnd", opnd));
    while (IS_PAIR(ptrn)) {
        if (car(ptrn) == s_quote) {
            ptrn = car(cdr(ptrn));
            if (!equal(ptrn, opnd)) return UNDEF;  // wrong literal
            XDEBUG(debug_print("match_pattern quote", assoc));
            return assoc;  // success
        }
        if (!IS_PAIR(opnd)) return UNDEF;  // wrong structure
        assoc = match_pattern(car(ptrn), car(opnd), assoc);
        if (assoc == UNDEF) return UNDEF;  // nested failure
        ptrn = cdr(ptrn);
        opnd = cdr(opnd);
    }
    if (IS_SYM(ptrn)) {
        if (ptrn != s_ignore) {
            assoc = cons(cons(ptrn, opnd), assoc);
        }
    } else {
        if (!equal(ptrn, opnd)) return UNDEF;  // wrong constant
    }
    XDEBUG(debug_print("match_pattern assoc", assoc));
    return assoc;  // success
}
int_t assoc_find(int_t assoc, int_t key) {
    while (IS_PAIR(assoc)) {
        int_t entry = car(assoc);
        if (!IS_PAIR(entry)) return UNDEF;  // invalid assoc list
        if (car(entry) == key) return entry;  // found!
        assoc = cdr(assoc);
    }
    return UNDEF;  // not found
}
PROC_DECL(Scope) {
    XDEBUG(debug_print("Scope self", self));
    GET_VARS();  // (locals penv)
    DEBUG(debug_print("Scope vars", vars));
    POP_VAR(locals);  // local bindings
    POP_VAR(penv);  // parent environment
    GET_ARGS();
    DEBUG(debug_print("Scope args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_lookup) {  // (cust 'lookup symbol)
        POP_ARG(symbol);
        END_ARGS();
        int_t binding = assoc_find(locals, symbol);
        if (IS_PAIR(binding)) {
            int_t value = cdr(binding);
            DEBUG(debug_print("Scope value", value));
            effect = effect_send(effect,
                actor_send(cust, value)); // send value to cust
        } else {
            effect = effect_send(effect,
                actor_send(penv, arg)); // delegate to parent
        }
        return effect;
    }
    if (req == s_bind) {  // (cust 'bind assoc)
        POP_ARG(assoc);
        XDEBUG(debug_print("Scope assoc", assoc));
        END_ARGS();
        while (IS_PAIR(assoc)) {
            int_t new_binding = car(assoc);
            if (IS_PAIR(new_binding)) {
                int_t key = car(new_binding);
                int_t value = cdr(new_binding);
                int_t old_binding = assoc_find(locals, key);
                // FIXME: consider the dangers of making this impure!
                if (IS_PAIR(old_binding)) {
                    set_cdr(old_binding, value);  // update in-place
                } else {
                    locals = cons(new_binding, locals);  // new binding
                }
            }
            assoc = cdr(assoc);
        }
        effect = effect_send(effect,
            actor_send(cust, UNIT));
        // FIXME: surgically replace `locals` (WARNING! this is a non-transactional BECOME)
        XDEBUG(debug_print("Scope locals", locals));
        cell_t *p = TO_PTR(get_data(self));
        p->head = locals;
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}
static PROC_DECL(fold_last) {
    int_t zero = self;
    XDEBUG(debug_print("fold_last zero", zero));
    int_t one = arg;
    XDEBUG(debug_print("fold_last one", one));
    return one;
}
PROC_DECL(Closure) {
    XDEBUG(debug_print("Closure self", self));
    GET_VARS();  // (eptrn body lenv)
    XDEBUG(debug_print("Closure vars", vars));
    POP_VAR(eptrn);
    POP_VAR(body);
    POP_VAR(lenv);  // lexical (captured) environment
    GET_ARGS();
    DEBUG(debug_print("Closure args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_apply) {  // (cust 'apply opnd denv)
        POP_ARG(opnd);
        POP_ARG(denv);  // dynamic (caller) environment
        END_ARGS();
        int_t assoc = match_pattern(eptrn, cons(denv, opnd), NIL);
        if (assoc == UNDEF) {
            effect = effect_send(effect,
                actor_send(cust, error("argument pattern mismatch")));
            return effect;
        }
        int_t xenv = actor_create(MK_PROC(Scope), list_2(assoc, lenv));
        effect = effect_create(effect, xenv);
        effect = effect_send(effect,
            actor_send(body, list_6(cust, s_fold, UNIT, MK_PROC(fold_last), s_eval, xenv)));
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}
PROC_DECL(Oper_lambda) {  // (lambda pattern . objects)
    XDEBUG(debug_print("Oper_lambda self", self));
    GET_ARGS();
    DEBUG(debug_print("Oper_lambda args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(lenv);
        END_ARGS();
        if (IS_PAIR(opnd)) {
            int_t ptrn = car(opnd);
            int_t body = cdr(opnd);
            int_t closure = actor_create(
                MK_PROC(Closure), list_3(cons(s_ignore, ptrn), body, lenv));
            effect = effect_create(effect, closure);
            int_t appl = actor_create(MK_PROC(Appl), closure);
            effect = effect_create(effect, appl);
            effect = effect_send(effect, actor_send(cust, appl));
            return effect;
        }
        effect = effect_send(effect,
            actor_send(cust, error("lambda expected pattern . body")));
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}
const cell_t a_lambda = { .head = MK_PROC(Oper_lambda), .tail = UNDEF };

PROC_DECL(Oper_seq) {
    XDEBUG(debug_print("Oper_seq self", self));
    GET_ARGS();
    XDEBUG(debug_print("Oper_seq args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(env);
        END_ARGS();
        effect = effect_send(effect,
            actor_send(opnd, list_6(cust, s_fold, UNIT, MK_PROC(fold_last), s_eval, env)));
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}
const cell_t a_seq = { .head = MK_PROC(Oper_seq), .tail = UNDEF };

PROC_DECL(Oper_eval) {  // (eval expr [env])
    XDEBUG(debug_print("Oper_eval self", self));
    GET_ARGS();
    DEBUG(debug_print("Oper_eval args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(env);
        END_ARGS();
        if (IS_PAIR(opnd)) {
            int_t expr = car(opnd);
            opnd = cdr(opnd);
            if (IS_PAIR(opnd)) {  // optional env
                env = car(opnd);
                opnd = cdr(opnd);
            }
            if (opnd == NIL) {
                effect = effect_send(effect,
                    actor_send(expr, list_3(cust, s_eval, env)));
                return effect;
            }
        }
        effect = effect_send(effect,
            actor_send(cust, error("eval expected 1 or 2 arguments")));
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}
const cell_t oper_eval = { .head = MK_PROC(Oper_eval), .tail = UNDEF };
const cell_t a_eval = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_eval) };

PROC_DECL(Oper_apply) {  // (apply oper args [env])
    XDEBUG(debug_print("Oper_apply self", self));
    GET_ARGS();
    DEBUG(debug_print("Oper_apply args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(env);
        END_ARGS();
        if (IS_PAIR(opnd)) {
            int_t oper = car(opnd);
            oper = unwrap(oper);  // get underlying operative
            if (oper == UNDEF) {
                effect = effect_send(effect, actor_send(cust, UNDEF));
                return effect;
            }
            opnd = cdr(opnd);
            if (IS_PAIR(opnd)) {
                int_t args = car(opnd);
                opnd = cdr(opnd);
                if (IS_PAIR(opnd)) {  // optional env
                    env = car(opnd);
                    opnd = cdr(opnd);
                }
                if (opnd == NIL) {
                    effect = effect_send(effect,
                        actor_send(oper, list_4(cust, s_apply, args, env)));
                    return effect;
                }
            }
        }
        effect = effect_send(effect,
            actor_send(cust, error("apply expected 2 or 3 arguments")));
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}
const cell_t oper_apply = { .head = MK_PROC(Oper_apply), .tail = UNDEF };
const cell_t a_apply = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_apply) };

PROC_DECL(Oper_map) {  // (map oper . lists)
    XDEBUG(debug_print("Oper_map self", self));
    GET_ARGS();
    DEBUG(debug_print("Oper_map args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(env);
        END_ARGS();
        if (IS_PAIR(opnd)) {
            int_t oper = car(opnd);
            oper = unwrap(oper);  // get underlying operative
            if (oper == UNDEF) {
                effect = effect_send(effect, actor_send(cust, UNDEF));
                return effect;
            }
            opnd = cdr(opnd);
            if (IS_PAIR(opnd)) {
                int_t x = car(opnd);
                int n = list_len(x);
                int_t xs = cdr(opnd);
                int_t ys = cons(x, NIL); // list heads
                int_t y = ys;
                // copy heads and check list lengths
                while (IS_PAIR(xs)) {
                    x = car(xs);
                    if (list_len(x) != n) {
                        effect = effect_send(effect,
                            actor_send(cust, error("map requires equal list lengths")));
                        return effect;
                    }
                    set_cdr(y, cons(x, NIL));
                    y = cdr(y);
                    xs = cdr(xs);
                }
                XDEBUG(debug_print("Oper_map ys", ys));
                // construct applications
                int_t zs = cons(UNDEF, NIL);  // anchor cell
                int_t z = zs;
                while (IS_PAIR(car(ys))) {
                    xs = cons(oper, NIL);
                    set_cdr(z, cons(xs, NIL));
                    z = cdr(z);
                    x = xs;
                    y = ys;
                    while (IS_PAIR(y)) {
                        set_cdr(x, cons(car(car(y)), NIL));  // copy head
                        x = cdr(x);
                        set_car(y, cdr(car(y)));  // advance head
                        y = cdr(y);
                    }
                    XDEBUG(debug_print("Oper_map ys'", ys));
                }
                y = ys;
                while (IS_PAIR(y)) {  // release head iters
                    ys = cdr(y);
                    cell_free(y);
                    y = ys;
                }
                z = cdr(zs);
                zs = cell_free(zs);  // release anchor
                // map eval applications
                DEBUG(debug_print("Oper_map z", z));
                effect = effect_send(effect,
                    actor_send(z, list_4(cust, s_map, s_eval, env)));
                return effect;
            }
        }
        effect = effect_send(effect,
            actor_send(cust, error("map expected 2 or more arguments")));
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}
const cell_t oper_map = { .head = MK_PROC(Oper_map), .tail = UNDEF };
const cell_t a_map = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_map) };

static PROC_DECL(Macro_k_form) {
    XDEBUG(debug_print("Macro_k_form self", self));
    GET_VARS();  // (cust denv)
    XDEBUG(debug_print("Macro_k_form vars", vars));
    POP_VAR(cust);
    POP_VAR(denv);
    GET_ARGS();  // form
    DEBUG(debug_print("Macro_k_form args", args));
    TAIL_ARG(form);
    int_t effect = NIL;
    effect = effect_send(effect,
        actor_send(form, list_3(cust, s_eval, denv)));
    return effect;
}
PROC_DECL(Macro) {
    DEBUG(debug_print("Macro self", self));
    GET_VARS();  // oper
    XDEBUG(debug_print("Macro vars", vars));
    TAIL_VAR(oper);
    GET_ARGS();
    DEBUG(debug_print("Macro args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(denv);
        END_ARGS();
        int_t k_form = actor_create(MK_PROC(Macro_k_form), list_2(cust, denv));
        effect = effect_create(effect, k_form);
        effect = effect_send(effect,
            actor_send(oper, list_4(k_form, s_apply, opnd, denv)));
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}
PROC_DECL(Oper_macro) {  // (macro pattern evar . objects)
    XDEBUG(debug_print("Oper_macro self", self));
    GET_ARGS();
    DEBUG(debug_print("Oper_macro args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(lenv);
        END_ARGS();
        if (IS_PAIR(opnd)) {
            int_t ptrn = car(opnd);
            opnd = cdr(opnd);
            if (IS_PAIR(opnd)) {
                int_t evar = car(opnd);
                int_t body = cdr(opnd);
                int_t closure = actor_create(
                    MK_PROC(Closure), list_3(cons(evar, ptrn), body, lenv));
                effect = effect_create(effect, closure);
                int_t oper = actor_create(MK_PROC(Macro), closure);
                effect = effect_create(effect, oper);
                effect = effect_send(effect, actor_send(cust, oper));
                return effect;
            }
        }
        effect = effect_send(effect,
            actor_send(cust, error("macro expected pattern evar . body")));
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}
const cell_t a_macro = { .head = MK_PROC(Oper_macro), .tail = UNDEF };

static PROC_DECL(Define_k_bind) {
    XDEBUG(debug_print("Define_k_bind self", self));
    GET_VARS();  // (cust ptrn env)
    XDEBUG(debug_print("Define_k_bind vars", vars));
    POP_VAR(cust);
    POP_VAR(ptrn);
    POP_VAR(env);
    GET_ARGS();  // value
    XDEBUG(debug_print("Define_k_bind args", args));
    TAIL_ARG(value);
    int_t effect = NIL;
    int_t assoc = match_pattern(ptrn, value, NIL);
    if (assoc == UNDEF) {
        effect = effect_send(effect,
            actor_send(cust, error("define pattern mismatch")));
    } else {
        effect = effect_send(effect,
            actor_send(env, list_3(cust, s_bind, assoc)));
    }
    return effect;
}
PROC_DECL(Oper_define) {  // (define pattern expression)
    XDEBUG(debug_print("Oper_define self", self));
    GET_ARGS();
    XDEBUG(debug_print("Oper_define args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(env);
        END_ARGS();
        if (IS_PAIR(opnd)) {
            int_t ptrn = car(opnd);
            opnd = cdr(opnd);
            if (IS_PAIR(opnd)) {
                int_t expr = car(opnd);
                int_t k_bind = actor_create(MK_PROC(Define_k_bind), list_3(cust, ptrn, env));
                effect = effect_create(effect, k_bind);
                effect = effect_send(effect,
                    actor_send(expr, list_3(k_bind, s_eval, env)));
                return effect;
            }
            return effect;
        }
        effect = effect_send(effect,
            actor_send(cust, error("define expected 2 operands")));
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}
const cell_t a_define = { .head = MK_PROC(Oper_define), .tail = UNDEF };

static PROC_DECL(prim_list) {  // (list . objects)
    int_t opnd = self;
    //int_t env = arg;
    return opnd;
}
const cell_t oper_list = { .head = MK_PROC(Oper_prim), .tail = MK_PROC(prim_list) };
const cell_t a_list = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_list) };

static PROC_DECL(prim_cons) {  // (cons head tail)
    int_t opnd = self;
    //int_t env = arg;
    if (IS_PAIR(opnd)) {
        int_t x = car(opnd);
        opnd = cdr(opnd);
        if (IS_PAIR(opnd)) {
            int_t y = car(opnd);
            if (cdr(opnd) == NIL) {
                return cons(x, y);
            }
        }
    }
    return error("cons expected 2 arguments");
}
const cell_t oper_cons = { .head = MK_PROC(Oper_prim), .tail = MK_PROC(prim_cons) };
const cell_t a_cons = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_cons) };

static PROC_DECL(prim_car) {  // (car pair)
    int_t opnd = self;
    //int_t env = arg;
    if (IS_PAIR(opnd)) {
        int_t x = car(opnd);
        if (cdr(opnd) == NIL) {
            return car(x);
        }
    }
    return error("car expected 1 argument");
}
const cell_t oper_car = { .head = MK_PROC(Oper_prim), .tail = MK_PROC(prim_car) };
const cell_t a_car = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_car) };

static PROC_DECL(prim_cdr) {  // (cdr pair)
    int_t opnd = self;
    //int_t env = arg;
    if (IS_PAIR(opnd)) {
        int_t x = car(opnd);
        if (cdr(opnd) == NIL) {
            return cdr(x);
        }
    }
    return error("cdr expected 1 argument");
}
const cell_t oper_cdr = { .head = MK_PROC(Oper_prim), .tail = MK_PROC(prim_cdr) };
const cell_t a_cdr = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_cdr) };

PROC_DECL(Oper_quote) {  // (quote expr)
    XDEBUG(debug_print("Oper_quote self", self));
    GET_ARGS();
    XDEBUG(debug_print("Oper_quote args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(_env);
        END_ARGS();
        if (IS_PAIR(opnd)) {
            int_t expr = car(opnd);
            if (cdr(opnd) == NIL) {
                DEBUG(debug_print("Oper_quote value", expr));
                effect = effect_send(effect, actor_send(cust, expr));
                return effect;
            }
        }
        effect = effect_send(effect,
            actor_send(cust, error("quote expected 1 operand")));
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}
const cell_t a_quote = { .head = MK_PROC(Oper_quote), .tail = UNDEF };

static PROC_DECL(If_k_pred) {
    XDEBUG(debug_print("If_k_pred self", self));
    GET_VARS();  // (cust cnsq altn env)
    XDEBUG(debug_print("If_k_pred vars", vars));
    POP_VAR(cust);
    POP_VAR(cnsq);
    POP_VAR(altn);
    POP_VAR(env);
    GET_ARGS();  // pred
    XDEBUG(debug_print("If_k_pred args", args));
    TAIL_ARG(pred);
    int_t effect = NIL;
    if (pred == UNDEF) {  // short-circuit for #undef
        effect = effect_send(effect, actor_send(cust, UNDEF));
    } else {
        effect = effect_send(effect,
            actor_send(
                ((pred == FALSE) ? altn : cnsq),
                list_3(cust, s_eval, env)));
    }
    return effect;
}
PROC_DECL(Oper_if) {  // (if pred cnsq altn)
    XDEBUG(debug_print("Oper_if self", self));
    GET_ARGS();
    XDEBUG(debug_print("Oper_if args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(env);
        END_ARGS();
        if (IS_PAIR(opnd)) {
            int_t pred = car(opnd);
            opnd = cdr(opnd);
            if (IS_PAIR(opnd)) {
                int_t cnsq = car(opnd);
                opnd = cdr(opnd);
                if (IS_PAIR(opnd)) {
                    int_t altn = car(opnd);
                    if (cdr(opnd) == NIL) {
                        int_t k_pred = actor_create(MK_PROC(If_k_pred),
                            list_4(cust, cnsq, altn, env));
                        effect = effect_create(effect, k_pred);
                        effect = effect_send(effect,
                            actor_send(pred, list_3(k_pred, s_eval, env)));
                        return effect;
                    }
                }
            }
        }
        effect = effect_send(effect,
            actor_send(cust, error("if expected 3 operands")));
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}
const cell_t a_if = { .head = MK_PROC(Oper_if), .tail = UNDEF };

static PROC_DECL(prim_eqp) {  // (eq? . objects)
    int_t opnd = self;
    //int_t env = arg;
    if (IS_PAIR(opnd)) {
        int_t x = car(opnd);
        opnd = cdr(opnd);
        while (IS_PAIR(opnd)) {
            int_t y = car(opnd);
            if (x != y) {
                return FALSE;
            }
            opnd = cdr(opnd);
        }
    }
    if (opnd != NIL) {
        return error("proper list required");
    }
    return TRUE;
}
const cell_t oper_eqp = { .head = MK_PROC(Oper_prim), .tail = MK_PROC(prim_eqp) };
const cell_t a_eqp = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_eqp) };

static PROC_DECL(prim_equalp) {  // (equal? . objects)
    int_t opnd = self;
    //int_t env = arg;
    if (IS_PAIR(opnd)) {
        int_t x = car(opnd);
        opnd = cdr(opnd);
        while (IS_PAIR(opnd)) {
            int_t y = car(opnd);
            if (!equal(x, y)) {
                return FALSE;
            }
            opnd = cdr(opnd);
        }
    }
    if (opnd != NIL) {
        return error("proper list required");
    }
    return TRUE;
}
const cell_t oper_equalp = { .head = MK_PROC(Oper_prim), .tail = MK_PROC(prim_equalp) };
const cell_t a_equalp = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_equalp) };

static PROC_DECL(fold_and) {
    int_t zero = self;
    XDEBUG(debug_print("fold_and zero", zero));
    int_t one = arg;
    XDEBUG(debug_print("fold_and one", one));
    if (one == FALSE) return FALSE;
    return one;
}
PROC_DECL(Oper_typep) {
    XDEBUG(debug_print("Oper_typep self", self));
    GET_VARS();  // type
    XDEBUG(debug_print("Oper_typep vars", vars));
    TAIL_VAR(type);
    GET_ARGS();
    XDEBUG(debug_print("Oper_typep args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(_env);
        END_ARGS();
        effect = effect_send(effect,
            actor_send(opnd, list_6(cust, s_fold, TRUE, MK_PROC(fold_and), s_typeq, type)));
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}

PROC_DECL(Boolean) {
    XDEBUG(debug_print("Boolean self", self));
    GET_VARS();  // bval
    XDEBUG(debug_print("Boolean vars", vars));
    TAIL_VAR(bval);
    GET_ARGS();
    XDEBUG(debug_print("Boolean args", args));
    return SeType(self, arg);  // delegate to SeType
}
const cell_t oper_booleanp = { .head = MK_PROC(Oper_typep), .tail = MK_PROC(Boolean) };
const cell_t a_booleanp = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_booleanp) };

static PROC_DECL(And_k_rest) {
    XDEBUG(debug_print("And_k_rest self", self));
    GET_VARS();  // (cust rest env)
    XDEBUG(debug_print("And_k_rest vars", vars));
    POP_VAR(cust);
    POP_VAR(rest);
    POP_VAR(env);
    GET_ARGS();  // value
    XDEBUG(debug_print("And_k_rest args", args));
    TAIL_ARG(value);
    int_t effect = NIL;
    if ((value == UNDEF) || (value == FALSE) || (rest == NIL)) {
        effect = effect_send(effect,
            actor_send(cust, value));
    } else if (IS_PAIR(rest)) {
        int_t expr = car(rest);
        rest = cdr(rest);
        effect = effect_become(effect,
            actor_become(MK_PROC(And_k_rest), list_3(cust, rest, env)));
        effect = effect_send(effect,
            actor_send(expr, list_3(self, s_eval, env)));
    } else if (rest == NIL) {
        effect = effect_send(effect,
            actor_send(cust, TRUE));
    } else {
        effect = effect_send(effect,
            actor_send(cust, error("proper list required")));
    }
    return effect;
}
static PROC_DECL(Or_k_rest) {
    XDEBUG(debug_print("Or_k_rest self", self));
    GET_VARS();  // (cust rest env)
    XDEBUG(debug_print("Or_k_rest vars", vars));
    POP_VAR(cust);
    POP_VAR(rest);
    POP_VAR(env);
    GET_ARGS();  // value
    XDEBUG(debug_print("Or_k_rest args", args));
    TAIL_ARG(value);
    int_t effect = NIL;
    if ((value == UNDEF) || (value != FALSE) || (rest == NIL)) {
        effect = effect_send(effect,
            actor_send(cust, value));
    } else if (IS_PAIR(rest)) {
        int_t expr = car(rest);
        rest = cdr(rest);
        effect = effect_become(effect,
            actor_become(MK_PROC(Or_k_rest), list_3(cust, rest, env)));
        effect = effect_send(effect,
            actor_send(expr, list_3(self, s_eval, env)));
    } else {
        effect = effect_send(effect,
            actor_send(cust, error("proper list required")));
    }
    return effect;
}
// progressive application (short-circuit)
PROC_DECL(Oper_prog) {  // (op . exprs)
    XDEBUG(debug_print("Oper_prog self", self));
    GET_VARS();  // (proc . dflt)
    XDEBUG(debug_print("Oper_prog vars", vars));
    POP_VAR(proc);
    TAIL_VAR(dflt);
    GET_ARGS();
    XDEBUG(debug_print("Oper_prog args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(env);
        END_ARGS();
        if (IS_PAIR(opnd)) {
            int_t expr = car(opnd);
            int_t rest = cdr(opnd);
            int_t k_rest = actor_create(proc, list_3(cust, rest, env));
            effect = effect_create(effect, k_rest);
            effect = effect_send(effect,
                actor_send(expr, list_3(k_rest, s_eval, env)));
            return effect;
        }
        effect = effect_send(effect,
            actor_send(cust, dflt));
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}
const cell_t and_rest = { .head = MK_PROC(And_k_rest), .tail = TRUE };
const cell_t a_and = { .head = MK_PROC(Oper_prog), .tail = MK_PAIR(&and_rest) };
const cell_t or_rest = { .head = MK_PROC(Or_k_rest), .tail = FALSE };
const cell_t a_or = { .head = MK_PROC(Oper_prog), .tail = MK_PAIR(&or_rest) };

PROC_DECL(Null) {
    XDEBUG(debug_print("Null self", self));
    GET_ARGS();
    XDEBUG(debug_print("Null args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_map) {  // (cust 'map . h_req)
        TAIL_ARG(h_req);
        effect = effect_send(effect,
            actor_send(self, cons(cust, h_req)));
        return effect;
    }
    if (req == s_fold) {  // (cust 'fold zero oplus . req)
        POP_ARG(zero);
        //POP_ARG(oplus);
        //TAIL_ARG(req);
        effect = effect_send(effect,
            actor_send(cust, zero));
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}
const cell_t oper_nullp = { .head = MK_PROC(Oper_typep), .tail = MK_PROC(Null) };
const cell_t a_nullp = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_nullp) };

static PROC_DECL(Pair_k_fold) {
    XDEBUG(debug_print("Pair_k_fold self", self));
    GET_VARS();  // (cust tail zero oplus . req)
    XDEBUG(debug_print("Pair_k_fold vars", vars));
    POP_VAR(cust);
    POP_VAR(tail);
    POP_VAR(zero);
    POP_VAR(oplus);
    TAIL_VAR(req);
    GET_ARGS();  // one
    XDEBUG(debug_print("Pair_k_fold args", args));
    TAIL_ARG(one);
    int_t effect = NIL;
    ASSERT(IS_PROC(oplus));
    proc_t proc = TO_PTR(oplus);
    zero = (*proc)(zero, one);  // update accumulator
    DEBUG(debug_print("Pair_k_fold accum", zero));
    effect = effect_send(effect,
        actor_send(tail, cons(cust, cons(s_fold, cons(zero, cons(oplus, req))))));
    return effect;
}
static PROC_DECL(Pair_k_apply) {
    XDEBUG(debug_print("Pair_k_apply self", self));
    GET_VARS();  // (cust opnd env)
    XDEBUG(debug_print("Pair_k_apply vars", vars));
    POP_VAR(cust);
    POP_VAR(opnd);
    POP_VAR(env);
    GET_ARGS();  // oper
    XDEBUG(debug_print("Pair_k_apply args", args));
    TAIL_ARG(oper);
    int_t effect = NIL;
    effect = effect_send(effect,
        actor_send(oper, list_4(cust, s_apply, opnd, env)));
    return effect;
}
PROC_DECL(Pair) {  // WARNING: behavior used directly in obj_call()
    XDEBUG(debug_print("Pair self", self));
    GET_ARGS();
    XDEBUG(debug_print("Pair args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_eval) {  // (cust 'eval env)
        POP_ARG(env);
        END_ARGS();
        int_t k_apply = actor_create(MK_PROC(Pair_k_apply),
            list_3(cust, cdr(self), env));
        effect = effect_create(effect, k_apply);
        effect = effect_send(effect,
            actor_send(car(self), list_3(k_apply, s_eval, env)));
        return effect;
    }
    if (req == s_map) {  // (cust 'map . h_req)
        TAIL_ARG(h_req);
        int_t t_req = cdr(arg);  // re-use original arg
        int_t fork = actor_create(MK_PROC(fork_beh), cons(cust, self));
        effect = effect_create(effect, fork);
        effect = effect_send(effect,
            actor_send(fork, cons(h_req, t_req)));
        return effect;
    }
    if (req == s_fold) {  // (cust 'fold zero oplus . req)
        int_t rest = args;  // re-use args
        POP_ARG(zero);
        POP_ARG(oplus);
        TAIL_ARG(req);
        int_t head = car(self);
        int_t tail = cdr(self);
        int_t k_fold = actor_create(MK_PROC(Pair_k_fold),
            cons(cust, cons(tail, rest)));
        effect = effect_create(effect, k_fold);
        effect = effect_send(effect,
            actor_send(head, cons(k_fold, req)));
        return effect;
    }
    return Type(self, arg);  // delegate to Type (not self-evaluating)
}
const cell_t oper_pairp = { .head = MK_PROC(Oper_typep), .tail = MK_PROC(Pair) };
const cell_t a_pairp = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_pairp) };

PROC_DECL(Symbol) {  // WARNING: behavior used directly in obj_call()
    DEBUG(debug_print("Symbol self", self));
    GET_ARGS();
    XDEBUG(debug_print("Symbol args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_eval) {  // (cust 'eval env)
        POP_ARG(env);
        END_ARGS();
        effect = effect_send(effect,
            actor_send(env, list_3(cust, s_lookup, self)));
        return effect;
    }
    return Type(self, arg);  // delegate to Type (not self-evaluating)
}
const cell_t oper_symbolp = { .head = MK_PROC(Oper_typep), .tail = MK_PROC(Symbol) };
const cell_t a_symbolp = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_symbolp) };

PROC_DECL(Fixnum) {  // WARNING: behavior used directly in obj_call()
    XDEBUG(debug_print("Fixnum self", self));
    GET_ARGS();
    XDEBUG(debug_print("Fixnum args", args));
    return SeType(self, arg);  // delegate to SeType
}
const cell_t oper_numberp = { .head = MK_PROC(Oper_typep), .tail = MK_PROC(Fixnum) };
const cell_t a_numberp = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_numberp) };
static PROC_DECL(prim_add) {  // (+ . numbers)
    int_t n = 0;
    int_t opnd = self;
    //int_t env = arg;
    while (IS_PAIR(opnd)) {
        int_t x = car(opnd);
        if (!IS_NUM(x)) return UNDEF;
        n += TO_INT(x);
        opnd = cdr(opnd);
    }
    if (opnd != NIL) {
        return error("proper list required");
    }
    return MK_NUM(n);
}
const cell_t oper_add = { .head = MK_PROC(Oper_prim), .tail = MK_PROC(prim_add) };
const cell_t a_add = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_add) };
static PROC_DECL(prim_mul) {  // (* . numbers)
    int_t n = 1;
    int_t opnd = self;
    //int_t env = arg;
    while (IS_PAIR(opnd)) {
        int_t x = car(opnd);
        if (!IS_NUM(x)) return UNDEF;
        n *= TO_INT(x);
        opnd = cdr(opnd);
    }
    if (opnd != NIL) {
        return error("proper list required");
    }
    return MK_NUM(n);
}
const cell_t oper_mul = { .head = MK_PROC(Oper_prim), .tail = MK_PROC(prim_mul) };
const cell_t a_mul = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_mul) };
static PROC_DECL(prim_sub) {  // (- . numbers)
    int_t n = 0;
    int_t opnd = self;
    //int_t env = arg;
    if (IS_PAIR(opnd)) {
        int_t x = car(opnd);
        if (!IS_NUM(x)) return UNDEF;
        n = TO_INT(x);
        opnd = cdr(opnd);
        if (opnd == NIL) return MK_NUM(-n);
        while (IS_PAIR(opnd)) {
            int_t y = car(opnd);
            if (!IS_NUM(y)) return UNDEF;
            n -= TO_INT(y);
            opnd = cdr(opnd);
        }
    }
    if (opnd != NIL) {
        return error("proper list required");
    }
    return MK_NUM(n);
}
const cell_t oper_sub = { .head = MK_PROC(Oper_prim), .tail = MK_PROC(prim_sub) };
const cell_t a_sub = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_sub) };
static PROC_DECL(prim_lt) {  // (< . numbers)
    int_t opnd = self;
    //int_t env = arg;
    if (IS_PAIR(opnd)) {
        int_t x = car(opnd);
        if (!IS_NUM(x)) return UNDEF;
        int_t n = TO_INT(x);
        opnd = cdr(opnd);
        while (IS_PAIR(opnd)) {
            int_t y = car(opnd);
            if (!IS_NUM(y)) return UNDEF;
            int_t m = TO_INT(y);
            if (!(n < m)) return FALSE;
            n = m;
            opnd = cdr(opnd);
        }
    }
    if (opnd != NIL) {
        return error("proper list required");
    }
    return TRUE;
}
const cell_t oper_lt = { .head = MK_PROC(Oper_prim), .tail = MK_PROC(prim_lt) };
const cell_t a_lt = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_lt) };
static PROC_DECL(prim_le) {  // (<= . numbers)
    int_t opnd = self;
    //int_t env = arg;
    if (IS_PAIR(opnd)) {
        int_t x = car(opnd);
        if (!IS_NUM(x)) return UNDEF;
        int_t n = TO_INT(x);
        opnd = cdr(opnd);
        while (IS_PAIR(opnd)) {
            int_t y = car(opnd);
            if (!IS_NUM(y)) return UNDEF;
            int_t m = TO_INT(y);
            if (!(n <= m)) return FALSE;
            n = m;
            opnd = cdr(opnd);
        }
    }
    if (opnd != NIL) {
        return error("proper list required");
    }
    return TRUE;
}
const cell_t oper_le = { .head = MK_PROC(Oper_prim), .tail = MK_PROC(prim_le) };
const cell_t a_le = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_le) };
static PROC_DECL(prim_eqn) {  // (= . numbers)
    int_t opnd = self;
    //int_t env = arg;
    if (IS_PAIR(opnd)) {
        int_t x = car(opnd);
        if (!IS_NUM(x)) return UNDEF;
        int_t n = TO_INT(x);
        opnd = cdr(opnd);
        while (IS_PAIR(opnd)) {
            int_t y = car(opnd);
            if (!IS_NUM(y)) return UNDEF;
            int_t m = TO_INT(y);
            if (!(n == m)) return FALSE;
            n = m;
            opnd = cdr(opnd);
        }
    }
    if (opnd != NIL) {
        return error("proper list required");
    }
    return TRUE;
}
const cell_t oper_eqn = { .head = MK_PROC(Oper_prim), .tail = MK_PROC(prim_eqn) };
const cell_t a_eqn = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_eqn) };
static PROC_DECL(prim_ge) {  // (>= . numbers)
    int_t opnd = self;
    //int_t env = arg;
    if (IS_PAIR(opnd)) {
        int_t x = car(opnd);
        if (!IS_NUM(x)) return UNDEF;
        int_t n = TO_INT(x);
        opnd = cdr(opnd);
        while (IS_PAIR(opnd)) {
            int_t y = car(opnd);
            if (!IS_NUM(y)) return UNDEF;
            int_t m = TO_INT(y);
            if (!(n >= m)) return FALSE;
            n = m;
            opnd = cdr(opnd);
        }
    }
    if (opnd != NIL) {
        return error("proper list required");
    }
    return TRUE;
}
const cell_t oper_ge = { .head = MK_PROC(Oper_prim), .tail = MK_PROC(prim_ge) };
const cell_t a_ge = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_ge) };
static PROC_DECL(prim_gt) {  // (> . numbers)
    int_t opnd = self;
    //int_t env = arg;
    if (IS_PAIR(opnd)) {
        int_t x = car(opnd);
        if (!IS_NUM(x)) return UNDEF;
        int_t n = TO_INT(x);
        opnd = cdr(opnd);
        while (IS_PAIR(opnd)) {
            int_t y = car(opnd);
            if (!IS_NUM(y)) return UNDEF;
            int_t m = TO_INT(y);
            if (!(n > m)) return FALSE;
            n = m;
            opnd = cdr(opnd);
        }
    }
    if (opnd != NIL) {
        return error("proper list required");
    }
    return TRUE;
}
const cell_t oper_gt = { .head = MK_PROC(Oper_prim), .tail = MK_PROC(prim_gt) };
const cell_t a_gt = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_gt) };

static PROC_DECL(prim_print) {  // (print object)
    int_t opnd = self;
    //int_t env = arg;
    if (IS_PAIR(opnd)) {
        int_t obj = car(opnd);
        opnd = cdr(opnd);
        if (opnd == NIL) {
            print(obj);
            fflush(stdout);
            return obj;
        }
    }
    return error("print expected 1 argument");
}
const cell_t oper_print = { .head = MK_PROC(Oper_prim), .tail = MK_PROC(prim_print) };
const cell_t a_print = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_print) };

static PROC_DECL(prim_emit) {  // (emit . codepoints)
    int_t opnd = self;
    //int_t env = arg;
    while (IS_PAIR(opnd)) {
        int_t code = car(opnd);
        opnd = cdr(opnd);
        if (!IS_NUM(code)) continue;
        int_t ch = TO_INT(code);
        if (NAT(ch) > 0xFF) continue;
        putchar(ch);  // FIXME: need unicode-capable output...
    }
    if (opnd != NIL) {
        return error("proper list required");
    }
    return UNIT;
}
const cell_t oper_emit = { .head = MK_PROC(Oper_prim), .tail = MK_PROC(prim_emit) };
const cell_t a_emit = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_emit) };

static PROC_DECL(prim_debug_print) {  // (debug-print object)
    int_t opnd = self;
    //int_t env = arg;
    if (IS_PAIR(opnd)) {
        int_t obj = car(opnd);
        opnd = cdr(opnd);
        if (opnd == NIL) {
            debug_print("", obj);
            return obj;
        }
    }
    return error("print expected 1 argument");
}
const cell_t oper_debug_print = { .head = MK_PROC(Oper_prim), .tail = MK_PROC(prim_debug_print) };
const cell_t a_debug_print = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_debug_print) };

PROC_DECL(Fail) {
    WARN(debug_print("Fail self", self));
    GET_ARGS();
    DEBUG(debug_print("Fail args", args));
    return error("FAILED");
}

const cell_t a_ground_env;  // FORWARD DECLARATION
#define GROUND_ENV  MK_ACTOR(&a_ground_env)

#if META_ACTORS
static PROC_DECL(fold_effect) {
    int_t zero = self;
    WARN(debug_print("fold_effect zero", zero));
    int_t one = arg;
    WARN(debug_print("fold_effect one", one));
    // merge effect `one` into effects `zero`
    if (IS_PAIR(zero) && (car(zero) != UNDEF)) {
        int_t events = car(zero);
        int_t beh = cdr(zero);
        if (!IS_PAIR(one)) return UNDEF;  // invalid effect
        int_t event = car(one);
        if (event == UNDEF) return one;  // failure takes precedence
        if (event == NIL) {
            if (beh != NIL) return error("only one BECOME allowed");
            beh = cdr(one);  // new behavior
        } else {
            events = cons(event, events);  // new message event
        }
        set_car(zero, events);
        set_cdr(zero, beh);
    }
    return zero;
}
PROC_DECL(Behavior) {
    DEBUG(debug_print("Behavior self", self));
    GET_VARS();  // (ptrn stmts env)
    XDEBUG(debug_print("Behavior vars", vars));
    POP_VAR(ptrn);
    POP_VAR(stmts);
    POP_VAR(lenv);
    GET_ARGS();
    WARN(debug_print("Behavior args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);  // (target . msg)
        POP_ARG(_env);
        END_ARGS();
        if (IS_PAIR(opnd)) {
            int_t target = car(opnd);
            int_t msg = cdr(opnd);
            int_t assoc = list_1(cons(s_SELF, target));  // bind SELF
            assoc = match_pattern(ptrn, msg, assoc);
            if (assoc == UNDEF) {
                effect = effect_send(effect,
                    actor_send(cust, error("message pattern mismatch")));
                return effect;
            }
            WARN(debug_print("Behavior assoc", assoc));
            int_t aenv = actor_create(MK_PROC(Scope), list_2(assoc, lenv));
            effect = effect_create(effect, aenv);
            int_t empty = cons(NIL, NIL);  // empty effect
            effect = effect_send(effect,
                actor_send(stmts, list_6(cust, s_fold, empty, MK_PROC(fold_effect), s_eval, aenv)));
        }
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}
PROC_DECL(Oper_BEH) {  // (BEH pattern . statements)
    XDEBUG(debug_print("Oper_BEH self", self));
    GET_ARGS();
    WARN(debug_print("Oper_BEH args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(env);
        END_ARGS();
        if (IS_PAIR(opnd)) {
            int_t ptrn = car(opnd);
            int_t stmts = cdr(opnd);
            int_t beh = actor_create(
                MK_PROC(Behavior), list_3(ptrn, stmts, env));
            effect = effect_create(effect, beh);
            effect = effect_send(effect, actor_send(cust, beh));
            return effect;
        }
        effect = effect_send(effect,
            actor_send(cust, error("BEH expected pattern . statements")));
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}
const cell_t a_BEH = { .head = MK_PROC(Oper_BEH), .tail = UNDEF };

PROC_DECL(Actor);  // FORWARD DECLARATION
static PROC_DECL(Actor_k_done) {
    XDEBUG(debug_print("Actor_k_done self", self));
    GET_VARS();  // (cust actor)
    XDEBUG(debug_print("Actor_k_done vars", vars));
    POP_VAR(cust);
    POP_VAR(actor);
    GET_ARGS();  // effects
    DEBUG(debug_print("Actor_k_done args", args));
    TAIL_ARG(effects);
    // apply effects for this actor
    int_t effect = NIL;
    if (IS_PAIR(effects)) {
        // commit transaction
        WARN(debug_print("Actor_k_done commit", effects));
        if (car(effects) == UNDEF) {
            int_t reason = cdr(effects);
            ERROR(debug_print("Actor_k_done FAIL", cdr(reason)));
        } else {
            int_t events = car(effects);
            while (IS_PAIR(events)) {
                int_t event = car(events);
                ASSERT(IS_PAIR(event));
                WARN(debug_print("Actor_k_done meta-event", event));
                int_t target = car(event);
                int_t msg = cdr(event);
                effect = effect_send(effect,  // enqueue meta-event
                    actor_send(target, list_4(SINK, s_apply, msg, GROUND_ENV)));
                events = cdr(events);
            }
            int_t beh = cdr(effects);
            WARN(debug_print("Actor_k_done meta-actor", actor));
            cell_t *p = TO_PTR(actor);
            if (beh == NIL) {
                p->tail = cdr(p->tail);  // restore beh, end event transaction
            } else {
                WARN(debug_print("Actor_k_done meta-become", beh));
                ASSERT(car(beh) == MK_PROC(Actor));
                p->tail = cdr(beh);  // install new beh, end event transaction
            }
        }
    } else {
        // rollback transaction
        WARN(debug_print("Actor_k_done rollback", effects));
        cell_t *p = TO_PTR(actor);
        p->tail = cdr(p->tail);  // restore beh, end event transaction
    }
    effect = effect_send(effect,
        actor_send(cust, UNIT));
    return effect;
}
PROC_DECL(Actor) {
    WARN(debug_print("Actor self", self));
    GET_VARS();  // IS_ACTOR(beh) -or- IS_PAIR(effect)
    XDEBUG(debug_print("Actor vars", vars));
    TAIL_VAR(beh);
    if (IS_PAIR(beh)) {  // actor is busy handling an event
        WARN(debug_print("Actor BUSY", self));
        return effect_send(NIL,
            actor_send(self, arg));  // re-queue current message (serializer)
    }
    ASSERT(IS_ACTOR(beh));  // actor is ready to handle an event
    cell_t *p = TO_PTR(self);
    p->tail = cons(NIL, beh);  // save beh, begin event transaction
    GET_ARGS();
    DEBUG(debug_print("Actor args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);  // msg
        POP_ARG(env);
        END_ARGS();
        int_t k_done = actor_create(MK_PROC(Actor_k_done), list_2(cust, self));
        effect = effect_create(effect, k_done);
        effect = effect_send(effect,
            actor_send(beh, list_4(k_done, s_apply, cons(self, opnd), env)));
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}

static PROC_DECL(prim_CREATE) {  // (CREATE beh)
    int_t opnd = self;
    //int_t env = arg;
    if (IS_PAIR(opnd)) {
        int_t beh = car(opnd);
        //if (!IS_ACTOR(beh)) return UNDEF;  // FIXME: type-check here?
        opnd = cdr(opnd);
        if (opnd == NIL) {
            return actor_create(MK_PROC(Actor), beh);
        }
    }
    return error("CREATE expected 1 argument");
}
const cell_t oper_CREATE = { .head = MK_PROC(Oper_prim), .tail = MK_PROC(prim_CREATE) };
const cell_t a_CREATE = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_CREATE) };

static PROC_DECL(prim_SEND) {  // (SEND target msg)
    int_t opnd = self;
    //int_t env = arg;
    if (IS_PAIR(opnd)) {
        int_t target = car(opnd);
        //if (!IS_ACTOR(target)) return UNDEF;  // FIXME: type-check here?
        opnd = cdr(opnd);
        if (IS_PAIR(opnd)) {
            int_t msg = car(opnd);
            opnd = cdr(opnd);
            if (opnd == NIL) {
                return list_1(actor_send(target, msg));
            }
        }
    }
    return error("SEND expected 2 arguments");
}
const cell_t oper_SEND = { .head = MK_PROC(Oper_prim), .tail = MK_PROC(prim_SEND) };
const cell_t a_SEND = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_SEND) };

static PROC_DECL(prim_BECOME) {  // (BECOME beh)
    int_t opnd = self;
    //int_t env = arg;
    if (IS_PAIR(opnd)) {
        int_t beh = car(opnd);
        //if (!IS_ACTOR(beh)) return UNDEF;  // FIXME: type-check here?
        opnd = cdr(opnd);
        if (opnd == NIL) {
            return cons(NIL, actor_become(MK_PROC(Actor), beh));
        }
    }
    return error("BECOME expected 1 argument");
}
const cell_t oper_BECOME = { .head = MK_PROC(Oper_prim), .tail = MK_PROC(prim_BECOME) };
const cell_t a_BECOME = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_BECOME) };

static PROC_DECL(prim_FAIL) {  // (FAIL reason)
    int_t opnd = self;
    //int_t env = arg;
    if (IS_PAIR(opnd)) {
        int_t reason = car(opnd);
        opnd = cdr(opnd);
        if (opnd == NIL) {
            return cons(UNDEF, reason);
        }
    }
    return error("FAIL expected 1 argument");
}
const cell_t oper_FAIL = { .head = MK_PROC(Oper_prim), .tail = MK_PROC(prim_FAIL) };
const cell_t a_FAIL = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_FAIL) };

PROC_DECL(Beh_println) {
    XDEBUG(debug_print("Beh_println self", self));
    GET_VARS();  // meta-effect
    XDEBUG(debug_print("Beh_println vars", vars));
    TAIL_VAR(meta_effect);
    GET_ARGS();
    WARN(debug_print("Beh_println args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);  // (target . msg)
        POP_ARG(_env);
        END_ARGS();
        if (IS_PAIR(opnd)) {
            //int_t target = car(opnd);
            int_t msg = cdr(opnd);
            print(msg);
            newline();
            fflush(stdout);
            effect = effect_send(effect,
                actor_send(cust, meta_effect));
            return effect;
        }
    }
    return SeType(self, arg);  // delegate to SeType
}
const cell_t empty_effect = { .head = NIL, .tail = NIL };
const cell_t beh_println = { .head = MK_PROC(Beh_println), .tail = MK_PAIR(&empty_effect) };
static cell_t a_meta_println = { .head = MK_PROC(Actor), .tail = MK_ACTOR(&beh_println) };
#endif // META_ACTORS

PROC_DECL(Global) {
    XDEBUG(debug_print("Global self", self));
    //GET_VARS();  // locals
    //XDEBUG(debug_print("Global vars", vars));
    //TAIL_VAR(locals);
    GET_ARGS();
    XDEBUG(debug_print("Global args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_lookup) {  // (cust 'lookup symbol)
        POP_ARG(symbol);
        END_ARGS();
        int_t value = UNDEF;
        int_t binding = UNDEF;//assoc_find(locals, symbol);
        if (IS_PAIR(binding)) {
            value = cdr(binding);
        } else if (symbol == s_quote) {
            value = MK_ACTOR(&a_quote);
        } else if (symbol == s_list) {
            value = MK_ACTOR(&a_list);
        } else if (symbol == s_cons) {
            value = MK_ACTOR(&a_cons);
        } else if (symbol == s_car) {
            value = MK_ACTOR(&a_car);
        } else if (symbol == s_cdr) {
            value = MK_ACTOR(&a_cdr);
        } else if (symbol == s_if) {
            value = MK_ACTOR(&a_if);
        } else if (symbol == s_and) {
            value = MK_ACTOR(&a_and);
        } else if (symbol == s_or) {
            value = MK_ACTOR(&a_or);
        } else if (symbol == s_eqp) {
            value = MK_ACTOR(&a_eqp);
        } else if (symbol == s_equalp) {
            value = MK_ACTOR(&a_equalp);
        } else if (symbol == s_seq) {
            value = MK_ACTOR(&a_seq);
        } else if (symbol == s_lambda) {
            value = MK_ACTOR(&a_lambda);
        } else if (symbol == s_eval) {
            value = MK_ACTOR(&a_eval);
        } else if (symbol == s_apply) {
            value = MK_ACTOR(&a_apply);
        } else if (symbol == s_map) {
            value = MK_ACTOR(&a_map);
        } else if (symbol == s_macro) {
            value = MK_ACTOR(&a_macro);
        } else if (symbol == s_define) {
            value = MK_ACTOR(&a_define);
        } else if (symbol == s_booleanp) {
            value = MK_ACTOR(&a_booleanp);
        } else if (symbol == s_nullp) {
            value = MK_ACTOR(&a_nullp);
        } else if (symbol == s_pairp) {
            value = MK_ACTOR(&a_pairp);
        } else if (symbol == s_symbolp) {
            value = MK_ACTOR(&a_symbolp);
        } else if (symbol == s_numberp) {
            value = MK_ACTOR(&a_numberp);
        } else if (symbol == s_add) {
            value = MK_ACTOR(&a_add);
        } else if (symbol == s_sub) {
            value = MK_ACTOR(&a_sub);
        } else if (symbol == s_mul) {
            value = MK_ACTOR(&a_mul);
        } else if (symbol == s_lt) {
            value = MK_ACTOR(&a_lt);
        } else if (symbol == s_le) {
            value = MK_ACTOR(&a_le);
        } else if (symbol == s_eqn) {
            value = MK_ACTOR(&a_eqn);
        } else if (symbol == s_ge) {
            value = MK_ACTOR(&a_ge);
        } else if (symbol == s_gt) {
            value = MK_ACTOR(&a_gt);
        } else if (symbol == s_print) {
            value = MK_ACTOR(&a_print);
        } else if (symbol == s_emit) {
            value = MK_ACTOR(&a_emit);
        } else if (symbol == s_debug_print) {
            value = MK_ACTOR(&a_debug_print);
#if META_ACTORS
        } else if (symbol == s_BEH) {
            value = MK_ACTOR(&a_BEH);
        } else if (symbol == s_CREATE) {
            value = MK_ACTOR(&a_CREATE);
        } else if (symbol == s_SEND) {
            value = MK_ACTOR(&a_SEND);
        } else if (symbol == s_BECOME) {
            value = MK_ACTOR(&a_BECOME);
        } else if (symbol == s_FAIL) {
            value = MK_ACTOR(&a_FAIL);
        } else if (symbol == s_println) {
            value = MK_ACTOR(&a_meta_println);
#endif
        } else {
            WARN(debug_print("Global lookup failed", symbol));
            value = error("undefined variable");
        }
        DEBUG(debug_print("Global value", value));
        effect = effect_send(effect,
            actor_send(cust, value));
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}
// Note: `gnd_locals` can not be `const` because `locals` is mutable
// WARNING! gnd_locals.head must be part of the gc root set!
const cell_t a_global_env = { .head = MK_PROC(Global), .tail = NIL };
const cell_t gnd_parent = { .head = MK_ACTOR(&a_global_env), .tail = NIL };
static cell_t gnd_locals = { .head = NIL, .tail = MK_PAIR(&gnd_parent) };
const cell_t a_ground_env = { .head = MK_PROC(Scope), .tail = MK_PAIR(&gnd_locals) };

/*
 * display procedures
 */

void print(int_t value) {
    if (value == FREE_CELL) {
        printf("#FREE-CELL");
    } else if (IS_PROC(value)) {
        proc_t p = TO_PTR(value);
        printf("#proc-%"PRIxPTR"", NAT(p));
    } else if (IS_NUM(value)) {
        printf("%+"PRIdPTR"", TO_INT(value));
    } else if (IS_SYM(value)) {
        char *s = &intern[TO_NAT(value)];
        printf("%.*s", (int)(*s), (s + 1));
    } else if (value == UNDEF) {
        printf("#undefined");
    } else if (value == UNIT) {
        printf("#unit");
    } else if (value == FALSE) {
        printf("#f");
    } else if (value == TRUE) {
        printf("#t");
    } else if (value == NIL) {
        printf("()");
    } else if (value == INF) {
        printf("#inf");
    } else if (value == FAIL) {
        printf("#fail");
    } else if (IS_ACTOR(value)) {
        cell_t *p = TO_PTR(value);
        printf("#actor-%"PRIxPTR"", NAT(p));
    } else if (IS_PAIR(value) && (value > 1024)) {  // FIXME: protect against bad data
        char *s = "(";
        while (IS_PAIR(value) && (value > 1024)) {  // FIXME: protect against bad data
            printf("%s", s);
            DEBUG(fflush(stdout));
            print(car(value));
            s = " ";
            value = cdr(value);
        };
        if (value != NIL) {
            printf(" . ");
            DEBUG(fflush(stdout));
            print(value);
        }
        printf(")");
    } else {
        printf("#UNKNOWN?-%"PRIxPTR"", value);
    }
    DEBUG(fflush(stdout));
}

void debug_print(char *label, int_t value) {
    fprintf(stderr, "%s:", label);
    fprintf(stderr, " 16#%"PRIxPTR"", value);
    //fprintf(stderr, " %"PRIdPTR"", value);
    if (in_heap(value)) {
        cell_t *p = TO_PTR(value);
        nat_t n = NAT(p - cell);
        char mark = (gc_get_mark(n) ? '+' : '-');
        fprintf(stderr, " HEAP[%c%"PRIuPTR"]", mark, n);
    }
    //if (IS_ADDR(value)) fprintf(stderr, " ADDR");
    if (IS_PROC(value)) fprintf(stderr, " PROC");
    if (IS_NUM(value)) fprintf(stderr, " NUM");
    if (IS_PAIR(value)) fprintf(stderr, " PAIR");
    if (IS_SYM(value)) {
        nat_t n = TO_NAT(value);
        fprintf(stderr, " SYM[%"PRIuPTR"]", n);
    }
    if (IS_ACTOR(value)) fprintf(stderr, " ACTOR");
    if (IS_ADDR(value) && !IS_PROC(value)) {
        cell_t *p = TO_PTR(value);
        fprintf(stderr, " <%"PRIxPTR",%"PRIxPTR">", p->head, p->tail);
    }
    //fprintf(stderr, "\n");
    fprintf(stderr, " ");
    fflush(stderr);
    print(value);
    newline();
}

#if (__SIZEOF_POINTER__ == 4)
static void hexdump(char *label, int_t *addr, size_t cnt) {
    fprintf(stderr, "%s: %04"PRIxPTR"..", label, (NAT(addr) >> 16));
    for (nat_t n = 0; n < cnt; ++n) {
        if ((n & 0x7) == 0x0) {
            fprintf(stderr, "\n..%04"PRIxPTR":", (NAT(addr) & 0xFFFF));
        }
        fprintf(stderr, " %08"PRIXPTR"", NAT(*addr++) & 0xFFFFFFFF);
    }
    fprintf(stderr, "\n");
}
#endif

#if (__SIZEOF_POINTER__ == 8)
static void hexdump(char *label, int_t *addr, size_t cnt) {
    fprintf(stderr, "%s: %08"PRIxPTR"..", label, (NAT(addr) >> 32));
    for (nat_t n = 0; n < cnt; ++n) {
        if ((n & 0x3) == 0x0) {
            fprintf(stderr, "\n..%08"PRIxPTR":", (NAT(addr) & 0xFFFFFFFF));
        }
        fprintf(stderr, " %016"PRIXPTR"", NAT(*addr++));
    }
    fprintf(stderr, "\n");
}
#endif

/*
 * read-eval-print-loop (REPL)
 */

#define NSTR_MAX (256)

i32 cstr_to_nstr(char *cstr, char *nstr, i32 max) {
    char ch;
    i32 len = 0;
    while ((ch = cstr[len]) != 0) {
        ++len;
        if (len >= max) return -1;  // overflow
        nstr[len] = ch;
    }
    nstr[0] = len;
    return len;
}

typedef struct input input_t;
struct input {
    i32       (*get)(input_t *self);
    i32       (*next)(input_t *self, i32 ofs);
};

typedef struct nstr_in {
    input_t     input;
    char       *nstr;
    i32         ofs;
} nstr_in_t;
i32 nstr_get(input_t *self) {
    nstr_in_t *in = (nstr_in_t *)self;
    XDEBUG(fprintf(stderr, "  nstr_get ofs=%d\n", in->ofs));
    if (in->ofs < 0) return -1;  // illegal offset
    char *s = in->nstr;
    i32 len = *s++;
    if (in->ofs < len) {
        i32 ch = s[in->ofs];
        XDEBUG(fprintf(stderr, "  nstr_get (ch=%d)\n", ch));
        return ch;  // return char at ofs
    }
    return -1;  // out of bounds
}
i32 nstr_next(input_t *self, i32 ofs) {
    nstr_in_t *in = (nstr_in_t *)self;
    in->ofs += ofs;
    return in->ofs;
}
i32 nstr_init(nstr_in_t *in, char *nstr) {
    in->input.get = nstr_get;
    in->input.next = nstr_next;
    in->nstr = nstr;
    in->ofs = 0;
    return 0;  // success
}

typedef struct file_in {
    input_t     input;
    FILE       *file;
    i32         ofs;
    nstr_in_t   nstr_in;
    char        buffer[NSTR_MAX];
} file_in_t;
i32 file_get(input_t *self) {
    i32 ch;
    file_in_t *in = (file_in_t *)self;
    input_t *nstr_in = &in->nstr_in.input;
retry:
    ch = (nstr_in->get)(nstr_in);  // get from nstr
    XDEBUG(fprintf(stderr, "  file_get (ch=%d)\n", ch));
    if (ch >= 0) return ch;  // return char at ofs

    // refill line buffer
    in->ofs += in->buffer[0];  // advance base buffer offset
    XDEBUG(fprintf(stderr, "  file_get refill ofs=%d\n", in->ofs));
    if (!fgets(&in->buffer[1], (sizeof(in->buffer) - 1), in->file)) {
        // error or EOF
        in->buffer[0] = 0;  // empty nstr
        return -1;
    }
    i32 len = cstr_to_nstr(&in->buffer[1], &in->buffer[0], sizeof(in->buffer) - 1);
    XDEBUG(fprintf(stderr, "  file_get len=%d\n", len));
    if (len < 0) return -1;  // error
    in->buffer[len+1] = '\0';  // belt-and-suspenders
    XDEBUG(fprintf(stderr, "  file_get str=\"%s\"\n", &in->buffer[1]));
    if (nstr_init(&in->nstr_in, &in->buffer[0]) != 0) return -1;  // refill failed
    goto retry;
}
i32 file_next(input_t *self, i32 ofs) {
    file_in_t *in = (file_in_t *)self;
    input_t *nstr_in = &in->nstr_in.input;
    i32 pos = in->ofs + (nstr_in->next)(nstr_in, ofs);  // pass to nstr
    return pos;
}
i32 file_init(file_in_t *in, FILE *file) {
    in->input.get = file_get;
    in->input.next = file_next;
    in->file = file;
    in->ofs = 0;
    in->buffer[0] = 0;  // zero-length nstr
    return nstr_init(&in->nstr_in, &in->buffer[0]);
}

static int is_delim(i32 ch) {
    switch (ch) {
        case '(':   return 1;
        case ')':   return 1;
        case ';':   return 1;
        case '\'':  return 1;
        case '"':   return 1;
        case '`':   return 1;
        case ',':   return 1;
        case '[':   return 1;
        case ']':   return 1;
        case '{':   return 1;
        case '}':   return 1;
        case '|':   return 1;
    }
    return 0;
}
static int is_blank(i32 ch) {
    return (ch <= ' ');
}
static int cstr_eq(char *s, char *t) {
    if (s == t) return 1;
    if (!s) return 0;
    while (*s != '\0') {
        if (*s++ != *t++) return 0;
    }
    return (*t == '\0');
}

static int_t skip_space(input_t *in) {  // skip whitespace and comments
    i32 ch;
    while ((ch = (in->get)(in)) >= 0) {
        if (ch == ';') {
            // comment to end-of-line
            while (ch != '\n') {
                (in->next)(in, 1);
                ch = (in->get)(in);
            }
        }
        if (!is_blank(ch)) break;
        (in->next)(in, 1);
    }
    return ch;
}
static char token[256];
int_t read_sexpr(input_t *in) {
    XDEBUG(fprintf(stderr, "> read_sexpr\n"));
    int_t sexpr = FAIL;
    i32 ch;
    i32 n = 0;
    i32 i = 0;
    char *p = token;

    ch = skip_space(in);
    if (ch < 0) return FAIL;  // error or EOF
    if (ch == '(') {
        (in->next)(in, 1);
        ch = skip_space(in);
        if (ch == ')') {
            (in->next)(in, 1);
            ch = (in->get)(in);
            sexpr = NIL;  // empty sexpr
            XDEBUG(debug_print("< read_sexpr NIL", sexpr));
            return sexpr;
        }
        int_t x = read_sexpr(in);
        if (x == FAIL) return FAIL;  // error or EOF
        x = cons(x, UNDEF);
        sexpr = x;
        while (1) {
            ch = skip_space(in);
            if (ch == ')') {
                (in->next)(in, 1);
                set_cdr(x, NIL);
                XDEBUG(debug_print("< read_sexpr list", sexpr));
                return sexpr;  // NIL tail
            } else if (ch == '.') {
                (in->next)(in, 1);
                int_t y = read_sexpr(in);
                if (y == FAIL) return FAIL;  // error or EOF
                ch = skip_space(in);
                if (ch != ')') return FAIL;  // missing ')'
                (in->next)(in, 1);
                set_cdr(x, y);
                XDEBUG(debug_print("< read_sexpr dotted", sexpr));
                return sexpr;  // dotted tail
            } else {
                int_t y = read_sexpr(in);
                if (y == FAIL) return FAIL;  // error or EOF
                y = cons(y, UNDEF);
                set_cdr(x, y);
                x = y;
            }
        }
    }
    if (ch == '\'') {
        (in->next)(in, 1);
        int_t x = read_sexpr(in);
        XDEBUG(debug_print("  read_sexpr quoted", x));
        if (x == FAIL) return FAIL;  // error or EOF
        sexpr = list_2(s_quote, x);
        XDEBUG(debug_print("< read_sexpr quote", sexpr));
        return sexpr;
    }
    if (is_delim(ch)) {
        return FAIL;  // illegal character
    }
    if ((ch == '-') || (ch == '+') || ((ch >= '0') && (ch <= '9'))) {
        i32 neg = 0;
        if ((ch == '-') || (ch == '+')) {  // leading sign
            neg = (ch == '-');
            if (i >= sizeof(token)) return FAIL;  // overflow
            token[i++] = ch;
            (in->next)(in, 1);
            ch = (in->get)(in);
        }
        while ((ch >= '0') && (ch <= '9')) {
            n = (n * 10) + (ch - '0');
            if (i >= sizeof(token)) return FAIL;  // overflow
            token[i++] = ch;
            (in->next)(in, 1);
            ch = (in->get)(in);
            if ((ch < 0) || is_blank(ch) || is_delim(ch)) {
                // parsed a number
                sexpr = MK_NUM(neg ? -n : n);
                XDEBUG(debug_print("< read_sexpr num", sexpr));
                return sexpr;
            }
        }
    }
    while (!is_blank(ch) && !is_delim(ch)) {
        if (i >= sizeof(token)) return FAIL;  // overflow
        token[i++] = ch;
        (in->next)(in, 1);
        ch = (in->get)(in);
    }
    // parsed a symbol
    if (i >= sizeof(token)) return FAIL;  // overflow
    token[i] = '\0';  // terminate token cstr
    if (token[0] == '#') {
        // check for special values
        if (cstr_eq(token, "#t")) return TRUE;
        if (cstr_eq(token, "#f")) return FALSE;
        if (cstr_eq(token, "#unit")) return UNIT;
        if (cstr_eq(token, "#undefined")) return UNDEF;
        if (cstr_eq(token, "#inf")) return INF;
    }
    sexpr = symbol(token);
    XDEBUG(debug_print("< read_sexpr num", sexpr));
    return sexpr;
}

int_t read_eval_print_loop(input_t *in) {
    WARN(fprintf(stderr, "--REPL--\n"));
    while (1) {
        int_t effect = NIL;

        fprintf(stdout, "wart> ");  // REPL prompt
        fflush(stdout);

        // read expr to evaluate
        int_t expr = read_sexpr(in);
        if (expr == FAIL) break;
        WARN(debug_print("  REPL expr", expr));

        // evalute expr and print result
        int_t cust = MK_ACTOR(&a_println);
        int_t env = GROUND_ENV;
        effect = effect_send(effect,
            actor_send(expr, list_3(cust, s_eval, env)));

#if CONCURRENT_GC
        effect = effect_send(effect,
            actor_send(MK_ACTOR(&a_concurrent_gc), MK_NUM(0)));
#endif

        // dispatch all pending events
        ASSERT(apply_effect(UNDEF, effect) == OK);
        event_loop();
    }
    newline();
    return OK;
}

int_t load_file(input_t *in) {
    while (1) {
        int_t effect = NIL;

        int_t expr = read_sexpr(in);
        if (expr == FAIL) break;

        int_t cust = SINK;
        int_t env = GROUND_ENV;
        effect = effect_send(effect,
            actor_send(expr, list_3(cust, s_eval, env)));

#if CONCURRENT_GC
        effect = effect_send(effect,
            actor_send(MK_ACTOR(&a_concurrent_gc), MK_NUM(0)));
#endif

        // dispatch all pending events
        ASSERT(apply_effect(UNDEF, effect) == OK);
        event_loop();
    }
    return OK;
}

/*
 * unit tests
 */

int_t test_values() {
    WARN(fprintf(stderr, "--test_values--\n"));
    DEBUG(debug_print("test_values OK", OK));
    DEBUG(debug_print("test_values INF", INF));
    DEBUG(debug_print("test_values FALSE", FALSE));
    DEBUG(debug_print("test_values TRUE", TRUE));
    DEBUG(debug_print("test_values NIL", NIL));
    DEBUG(debug_print("test_values UNIT", UNIT));
    DEBUG(debug_print("test_values FAIL", FAIL));
    DEBUG(debug_print("test_values UNDEF", UNDEF));
    DEBUG(debug_print("test_values Undef", MK_PROC(Undef)));
    DEBUG(debug_print("test_values s_quote", s_quote));
    DEBUG(debug_print("test_values s_content", s_content));
    DEBUG(debug_print("test_values SINK", SINK));
    return OK;
}

int_t test_cells() {
    WARN(fprintf(stderr, "--test_cells--\n"));
    int_t v, v0, v1, v2;

    v = cons(TRUE, FALSE);
    ASSERT(IS_PAIR(v));
    DEBUG(debug_print("test_cells cons v", v));
    DEBUG(debug_print("test_cells cons car(v)", car(v)));
    DEBUG(debug_print("test_cells cons cdr(v)", cdr(v)));
    ASSERT(car(v) == TRUE);
    ASSERT(cdr(v) == FALSE);

    v0 = cons(v, NIL);
    DEBUG(debug_print("test_cells cons v0", v0));
    ASSERT(IS_PAIR(v0));

    v1 = list_3(MK_NUM(-1), MK_NUM(2), MK_NUM(3));
    //v1 = list_3(s_quote, s_eval, s_apply);
    DEBUG(debug_print("test_cells cons v1", v1));
    ASSERT(IS_PAIR(v1));

    v2 = cell_free(v0);
    DEBUG(debug_print("test_cells free v0", v2));
    ASSERT(v2 == NIL);

    v2 = actor_create(MK_PROC(sink_beh), v1);
    DEBUG(debug_print("test_cells cons v2", v2));
    ASSERT(in_heap(v2));
#if !NO_CELL_FREE
    ASSERT(TO_PTR(v2) == TO_PTR(v0));  // re-used cell?
#endif
    v1 = obj_call(v2, v);

    v = cell_free(v);
    v2 = cell_free(v2);
    ASSERT(v2 == NIL);

    DEBUG(hexdump("cell", PTR(cell), 16));
    int_t usage = cell_usage();

    return OK;
}

int_t test_actors() {
    WARN(fprintf(stderr, "--test_actors--\n"));
    int_t effect = NIL;
    XDEBUG(debug_print("test_actors effect", effect));
    int_t a = actor_create(MK_PROC(sink_beh), NIL);
    XDEBUG(debug_print("test_actors actor_create", a));
    effect = effect_create(effect, a);
    XDEBUG(debug_print("test_actors effect_create", effect));
    int_t m = list_3(SINK, s_eval, NIL);
    XDEBUG(debug_print("test_actors message", m));
    int_t e = actor_send(a, m);
    XDEBUG(debug_print("test_actors actor_send", e));
    effect = effect_send(effect, e);
    XDEBUG(debug_print("test_actors effect_send", effect));
    int_t x = apply_effect(UNDEF, effect);
    XDEBUG(debug_print("test_actors apply_effect", x));
    int_t r = event_dispatch();
    DEBUG(debug_print("test_actors event_dispatch", r));
    if (r != OK) return r;  // early exit on failure...

#if 1
    effect = NIL;
    // UNIT is self-evaluating
    a = actor_create(MK_PROC(assert_beh), UNIT);
    effect = effect_create(effect, a);
    m = list_3(a, s_eval, NIL);
    DEBUG(debug_print("test_actors m_1", m));
    e = actor_send(UNIT, m);
    effect = effect_send(effect, e);
    // UNIT has Unit type
    a = actor_create(MK_PROC(assert_beh), TRUE);
    effect = effect_create(effect, a);
    m = list_3(a, s_typeq, MK_PROC(Unit));
    DEBUG(debug_print("test_actors m_2", m));
    e = actor_send(UNIT, m);
    effect = effect_send(effect, e);
    // dispatch all pending events
    ASSERT(apply_effect(UNDEF, effect) == OK);
    r = event_loop();
    DEBUG(debug_print("test_actors event_loop", r));
#endif

    int_t usage = cell_usage();
    return OK;
}

int_t eval_test_expr(int_t expr, int_t expect) {
    int_t effect = NIL;

    int_t cust = actor_create(MK_PROC(assert_beh), expect);
    effect = effect_create(effect, cust);

    int_t env = GROUND_ENV;
    effect = effect_send(effect,
        actor_send(expr, list_3(cust, s_eval, env)));

#if CONCURRENT_GC
    effect = effect_send(effect,
        actor_send(MK_ACTOR(&a_concurrent_gc), MK_NUM(0)));
#endif

    // dispatch all pending events
    ASSERT(apply_effect(UNDEF, effect) == OK);
    int_t result = event_loop();
    DEBUG(debug_print("eval_test_expr event_loop", result));

    return OK;
}

static char nstr_buf[256];
int_t eval_test_cstr(char *cstr, char *result) {
    nstr_in_t str_in;

    cstr_to_nstr(cstr, nstr_buf, sizeof(nstr_buf));
    ASSERT(nstr_init(&str_in, nstr_buf) == 0);
    int_t expr = read_sexpr(&str_in.input);
    if (expr == FAIL) error("eval_test_cstr: read expr failed");
    DEBUG(debug_print("  expr", expr));

    cstr_to_nstr(result, nstr_buf, sizeof(nstr_buf));
    ASSERT(nstr_init(&str_in, nstr_buf) == 0);
    int_t expect = read_sexpr(&str_in.input);
    if (expect == FAIL) error("eval_test_cstr: read expect failed");
    DEBUG(debug_print("  expect", expect));

    eval_test_expr(expr, expect);
    return OK;
}

int top_level_eval(char *expr) {
    // Note: this will panic() if the result is not UNIT
    return eval_test_cstr(expr, "#unit");
}

int_t test_eval() {
    WARN(fprintf(stderr, "--test_eval--\n"));
    int_t effect;
    int_t cust;
    int_t expr;
    int_t env = GROUND_ENV;
    int_t result;
    int_t s_foo = symbol("foo");
    int_t s_bar = symbol("bar");
    int_t s_baz = symbol("baz");

    effect = NIL;
    cust = actor_create(MK_PROC(assert_beh), s_foo);
    effect = effect_create(effect, cust);
    //expr = list_2(MK_ACTOR(&a_quote), s_foo);  // (<quote> foo)
    expr = list_2(s_quote, s_foo);  // (quote foo)
    effect = effect_send(effect,
        actor_send(expr, list_3(cust, s_eval, env)));
#if CONCURRENT_GC
    effect = effect_send(effect, actor_send(MK_ACTOR(&a_concurrent_gc), MK_NUM(0)));
#endif
    // dispatch all pending events
    ASSERT(apply_effect(UNDEF, effect) == OK);
    result = event_loop();
    DEBUG(debug_print("test_eval event_loop", result));

    ASSERT(list_3(UNIT, s_foo, s_bar) != list_3(UNIT, s_foo, s_bar));
    ASSERT(equal(list_3(UNIT, s_foo, s_bar), list_3(UNIT, s_foo, s_bar)));
    ASSERT(!equal(TRUE, FALSE));

    eval_test_expr(
        // (<list>)
        list_1(MK_ACTOR(&a_list)),
        // ==> ()
        NIL);

    eval_test_expr(
        // (list)
        list_1(s_list),
        // ==> ()
        NIL);

    eval_test_expr(
        // (list -1 2 3)
        list_4(s_list, MK_NUM(-1), MK_NUM(2), MK_NUM(3)),
        // ==> (-1 2 3)
        list_3(MK_NUM(-1), MK_NUM(2), MK_NUM(3)));

    eval_test_expr(
        // (list '(#unit foo bar baz) (list #t #f) 'list)
        list_4(s_list,
            list_2(s_quote, list_4(UNIT, s_foo, s_bar, s_baz)),
            list_3(s_list, TRUE, FALSE),
            list_2(s_quote, s_list)),
        // ==> ((#unit foo bar baz) (#t #f) list)
        list_3(
            list_4(UNIT, s_foo, s_bar, s_baz),
            list_2(TRUE, FALSE),
            s_list));

    eval_test_cstr(
        "(list '(foo bar baz #undefined) (list 13 -8) 'quote)",
        "((foo bar baz #undefined) (13 -8) quote)"
    );

    eval_test_cstr(
        "(list (car (cons 1 2)) (cdr (cons 3 4)) (cdr (list 5 6)))",
        "(1 4 (6))"
    );

    eval_test_cstr(
        "(if #t #unit -unevaluated-)",
        "#unit"
    );

    eval_test_cstr(
        "(if #f -unevaluated- #unit)",
        "#unit"
    );

    eval_test_cstr(
        "(eq? 'foo 'foo)",
        "#t"
    );

    eval_test_cstr(
        "(eq? 1 1 1)",
        "#t"
    );

    eval_test_cstr(
        "(eq? '(1 2) '(1 2))",
        "#f"
    );

    eval_test_cstr(
        "(equal? '(1 2) (list 1 2) (cons 1 (cons 2 '())))",
        "#t"
    );

    eval_test_cstr(
        "(equal? #t #f #unit)",
        "#f"
    );

    eval_test_cstr(
        "((lambda (x) x) 42)",
        "42"
    );

    eval_test_cstr(
        "((lambda ('x #t y 2 . z) (list z y)) 'x #t 1 2 3 4)",
        "((3 4) 1)"
    );

    eval_test_cstr(
        "(eval '(* 1 2 3 4 5))",
        "120"
    );

    eval_test_cstr(
        "(apply - '(1 2 3 4 5))",
        "-13"
    );

    eval_test_cstr(
        "(map + '(1 2) '(3 4) '(5 6)))",
        "(9 12)"
    );

    int_t assoc = list_3(cons(s_foo, MK_NUM(1)), cons(s_bar, MK_NUM(2)), cons(s_baz, MK_NUM(3)));
    DEBUG(debug_print("test_eval assoc", assoc));
    ASSERT(cdr(assoc_find(assoc, s_foo)) == MK_NUM(1));
    ASSERT(cdr(assoc_find(assoc, s_bar)) == MK_NUM(2));
    ASSERT(cdr(assoc_find(assoc, s_baz)) == MK_NUM(3));
    ASSERT(assoc_find(assoc, NIL) == UNDEF);

    result = match_pattern(NIL, NIL, assoc);
    DEBUG(debug_print("test_eval match 1", result));
    ASSERT(result == assoc);

    int_t opnd = list_3(MK_NUM(3), MK_NUM(2), MK_NUM(1));
    result = match_pattern(s_foo, opnd, NIL);
    ASSERT(equal(result, list_1(cons(s_foo, opnd))));
    DEBUG(debug_print("test_eval match 2", result));

    int_t ptrn = list_3(s_baz, s_bar, s_foo);
    result = match_pattern(ptrn, opnd, NIL);
    DEBUG(debug_print("test_eval match 3", result));
    ASSERT(equal(result, assoc));

    ptrn = list_2(s_foo, s_ignore);
    opnd = list_2(MK_NUM(1), MK_NUM(3));
    result = match_pattern(ptrn, opnd, list_1(cons(s_bar, MK_NUM(2))));
    assoc = list_2(cons(s_foo, MK_NUM(1)), cons(s_bar, MK_NUM(2)));
    DEBUG(debug_print("test_eval match 4", result));
    ASSERT(equal(result, assoc));

    ptrn = list_2(list_2(s_quote, s_foo), s_bar);
    opnd = list_2(s_foo, MK_NUM(2));
    result = match_pattern(ptrn, opnd, NIL);
    assoc = list_1(cons(s_bar, MK_NUM(2)));
    DEBUG(debug_print("test_eval match 5", result));
    ASSERT(equal(result, assoc));

    int freed = gc_mark_and_sweep();
    WARN(printf("test_eval: gc reclaimed %d cells\n", freed));
    int_t usage = cell_usage();
    return OK;
}

int_t unit_tests() {
    if (test_values() != OK) return UNDEF;
    if (test_cells() != OK) return UNDEF;
    if (test_actors() != OK) return UNDEF;
    if (test_eval() != OK) return UNDEF;
    return OK;
}

/*
 * bootstrap
 */

int_t actor_boot() {
    if (symbol_boot() != OK) return UNDEF;
    return OK;
}

int_t load_library() {
    WARN(fprintf(stderr, "--load_library--\n"));
    top_level_eval("(define cadr (lambda ((_ x . _)) x))");
    top_level_eval("(define caddr (lambda ((_ _ x . _)) x))");
    top_level_eval("(define not (lambda (b) (eq? b #f)))");
    top_level_eval("(define unit? (macro objs _ (cons eq? (cons #unit objs))))");
    top_level_eval("(define zero? (lambda (n) (= n 0)))");
    top_level_eval("(define list? (lambda (p) (if (pair? p) (list? (cdr p)) (null? p))))");
    top_level_eval("(define length (lambda (p) (if (pair? p) (+ (length (cdr p)) 1) 0)))");
    top_level_eval("(define list* (lambda (h . t) (if (pair? t) (cons h (apply list* t)) h)))");
    top_level_eval("(define par (lambda _))");
    //top_level_eval("");
    return OK;
}

int main(int argc, char const *argv[])
{
    clock_t t0 = clock();
    clock_t t1 = clock();
    double dt = (double)(t1 - t0) / CLOCKS_PER_SEC;
    printf("t0=%ld t1=%ld dt=%.9f (%ld CPS)\n", t0, t1, dt, (long)CLOCKS_PER_SEC);

    int_t result = actor_boot();
    if (result != OK) panic("actor_boot() failed");

#if 1
    fprintf(stderr, "newline = %"PRIxPTR"\n", INT(newline));
    fprintf(stderr, "  Undef = %"PRIxPTR"\n", INT(Undef));
    fprintf(stderr, "   Unit = %"PRIxPTR"\n", INT(Unit));
    fprintf(stderr, "   main = %"PRIxPTR"\n", INT(main));
    fprintf(stderr, "is_proc = %"PRIxPTR"\n", INT(is_proc));
    fprintf(stderr, "  UNDEF = %"PRIxPTR"\n", UNDEF);
    fprintf(stderr, "   UNIT = %"PRIxPTR"\n", UNIT);
    ASSERT(INT(newline) < INT(main));

    DEBUG(hexdump("UNDEF", TO_PTR(UNDEF), 12));
    ASSERT(IS_ACTOR(UNDEF));

    ASSERT(UNIT != UNDEF);
    ASSERT(IS_ACTOR(UNIT));
    ASSERT(IS_PROC(get_code(UNIT)));

    fprintf(stderr, "   cell = %"PRIxPTR"x%"PRIxPTR"\n",
        INT(cell), NAT(sizeof(cell)));
    fprintf(stderr, " intern = %"PRIxPTR"x%"PRIxPTR"\n",
        INT(intern), NAT(sizeof(intern)));
    //ASSERT((NAT(cell) & 0x7) == 0x0);
    ASSERT((NAT(cell) & TAG_MASK) == 0x0);

    fprintf(stderr, "s_quote = %"PRIxPTR"\n", s_quote);
    fprintf(stderr, "s_content = %"PRIxPTR"\n", s_content);
    ASSERT(IS_SYM(s_content));
#endif

#if 1
    result = unit_tests();
    WARN(debug_print("result", result));
#endif

    printf("result = ");
    print(result);
    newline();

#if 0
    ASSERT(IS_ACTOR(FAIL));
    cell_t *p = TO_PTR(FAIL);
    p->tail = NIL;  // FIXME: should not be able to assign to `const`
#endif

    ASSERT(load_library() == OK);

    WARN(fprintf(stderr, "--load_file--\n"));
    printf("argc = %d\n", argc);
    for (int i = 1; i < argc; ++i) {
        printf("argv[%d] = %s\n", i, argv[i]);
        FILE *f = fopen(argv[i], "r");
        if (f) {
            file_in_t file_in;
            ASSERT(file_init(&file_in, f) == 0);
            result = load_file(&file_in.input);
        }
        fclose(f);
    }

    int_t usage = cell_usage();
    int freed = gc_mark_and_sweep();
    WARN(printf("main: gc reclaimed %d cells\n", freed));

    file_in_t std_in;
    ASSERT(file_init(&std_in, stdin) == 0);
    result = read_eval_print_loop(&std_in.input);

    return (result == OK ? 0 : 1);
}

int is_proc(int_t val) {
    return IS_ACTOR(val) && (TO_PTR(val) >= PTR(newline)) && (TO_PTR(val) <= PTR(main));
}
