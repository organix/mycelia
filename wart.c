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
#define MK_PAIR(p)  INT(NAT(p)|TAG_PAIR)
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
//PROC_DECL(false_beh);
//PROC_DECL(true_beh);
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
const cell_t a_unit = { .head = MK_PROC(Unit), .tail = UNDEF };
const cell_t a_false = { .head = MK_PROC(Boolean), .tail = FALSE };
const cell_t a_true = { .head = MK_PROC(Boolean), .tail = TRUE };
const cell_t a_nil = { .head = MK_PROC(Null), .tail = NIL };
const cell_t a_fail = { .head = MK_PROC(Fail), .tail = UNDEF };

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

#define CELL_MAX (1024)
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
    return (x == y)
        || (IS_PAIR(x) && IS_PAIR(y) && equal(car(x), car(y)) && equal(cdr(x), cdr(y)));
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
    proc_t p = TO_PTR(code);
    return (p)(self, arg);
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

i32 gc_mark_and_sweep(int_t root) {
    XDEBUG(debug_print("gc_mark_and_sweep root", root));
    i32 n = gc_clear();
    n = gc_mark_free();
    XDEBUG(printf("gc_mark_and_sweep: marked %d free cells\n", n));
#if GC_CALL_DEPTH
    n = gc_mark_cell(root, 0);
#else
    n = gc_mark_cell(root);
#endif
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

int_t s_quote;
int_t s_typeq;
int_t s_eval;
int_t s_apply;
int_t s_list;
int_t s_if;
int_t s_map;
int_t s_fold;
int_t s_foldr;
int_t s_bind;
int_t s_lookup;
int_t s_match;
int_t s_content;

// runtime initialization
int_t symbol_boot() {
    s_quote = symbol("quote");
    s_typeq = symbol("typeq");
    s_eval = symbol("eval");
    s_apply = symbol("apply");
    s_list = symbol("list");
    s_if = symbol("if");
    s_map = symbol("map");
    s_fold = symbol("fold");
    s_foldr = symbol("foldr");
    s_bind = symbol("bind");
    s_lookup = symbol("lookup");
    s_match = symbol("match");
    s_content = symbol("content");
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
        int freed = gc_mark_and_sweep(event_q.head);
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
        DEBUG(printf("gc_mark_and_sweep_beh: count(%"PRIdPTR") < limit(%"PRIdPTR")\n", n, m));
        effect = effect_send(effect,
            actor_send(self, MK_NUM(++n)));
        return effect;
    }

    int freed = gc_mark_and_sweep(event_q.head);
    WARN(printf("gc_mark_and_sweep_beh: gc reclaimed %d cells\n", freed));
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
    .tail = MK_NUM(3),
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
    XDEBUG(debug_print("Appl_k_args args", args));
    TAIL_ARG(opnd);
    int_t effect = NIL;
    effect = effect_send(effect,
        actor_send(oper, list_4(cust, s_apply, opnd, env)));
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

PROC_DECL(Oper_list) {  // (list . values)
    DEBUG(debug_print("Oper_list self", self));
    GET_ARGS();
    XDEBUG(debug_print("Oper_list args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(_env);
        END_ARGS();
        DEBUG(debug_print("Oper_list value", opnd));
        effect = effect_send(effect, actor_send(cust, opnd));
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}
const cell_t a_list = { .head = MK_PROC(Appl), .tail = MK_PROC(Oper_list) };

PROC_DECL(Oper_quote) {  // (quote expr)
    DEBUG(debug_print("Oper_quote self", self));
    GET_ARGS();
    XDEBUG(debug_print("Oper_quote args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(_env);
        END_ARGS();
        int_t expr = car(opnd);
        if (cdr(opnd) != NIL) {
            effect = effect_send(effect,
                actor_send(cust, error("expected 1 argument")));
        } else {
            DEBUG(debug_print("Oper_quote value", expr));
            effect = effect_send(effect, actor_send(cust, expr));
        }
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}
const cell_t a_quote = { .head = MK_PROC(Oper_quote), .tail = UNDEF };

PROC_DECL(Boolean) {
    XDEBUG(debug_print("Boolean self", self));
    GET_VARS();  // bval
    XDEBUG(debug_print("Boolean vars", vars));
    TAIL_VAR(bval); // FIXME: should be #t/#f delegate
    GET_ARGS();
    XDEBUG(debug_print("Boolean args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_if) {  // (cust 'if cnsq altn env)
        POP_ARG(cnsq);
        POP_ARG(altn);
        POP_ARG(env);
        END_ARGS();
        effect = effect_send(effect,
            actor_send(
                (bval ? cnsq : altn),
                list_3(cust, s_eval, env)));
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}

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
    return SeType(self, arg);  // delegate to SeType
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
        int_t k_apply = actor_create(MK_PROC(Pair_k_apply), list_3(cust, cdr(self), env));
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
    return Type(self, arg);  // delegate to Type (not self-evaluating)
}

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

PROC_DECL(Fixnum) {  // WARNING: behavior used directly in obj_call()
    XDEBUG(debug_print("Fixnum self", self));
    GET_ARGS();
    XDEBUG(debug_print("Fixnum args", args));
    return SeType(self, arg);  // delegate to SeType
}

PROC_DECL(Fail) {
    WARN(debug_print("Fail self", self));
    GET_ARGS();
    DEBUG(debug_print("Fail args", args));
    return error("FAILED");
}

PROC_DECL(Environment) {
    XDEBUG(debug_print("Environment self", self));
    GET_ARGS();
    XDEBUG(debug_print("Environment args", args));
    POP_ARG(cust);
    POP_ARG(req);
    int_t effect = NIL;
    if (req == s_lookup) {  // (cust 'lookup symbol)
        POP_ARG(symbol);
        END_ARGS();
        int_t value = UNDEF;
        if (symbol == s_quote) {
            value = MK_ACTOR(&a_quote);
        } else if (symbol == s_list) {
            value = MK_ACTOR(&a_list);
        } else {
            WARN(debug_print("Environment not found", symbol));
            value = error("undefined variable");
        }
        DEBUG(debug_print("Environment value", value));
        effect = effect_send(effect,
            actor_send(cust, value));
        return effect;
    }
    return SeType(self, arg);  // delegate to SeType
}
const cell_t a_ground_env = { .head = MK_PROC(Environment), .tail = NIL };

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
    DEBUG(debug_print("test_values s_match", s_match));
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

int_t test_eval() {
    WARN(fprintf(stderr, "--test_eval--\n"));
    int_t effect;
    int_t cust;
    int_t expr;
    int_t env = MK_ACTOR(&a_ground_env);
    int_t result;

    int_t s_foo = symbol("foo");
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
    DEBUG(debug_print("test_eval event_loop #1", result));

#if 1
    effect = NIL;
    //cust = actor_create(MK_PROC(assert_beh), NIL);
    //cust = actor_create(MK_PROC(assert_beh), list_3(MK_NUM(-1), MK_NUM(2), MK_NUM(3)));
    //cust = actor_create(MK_PROC(assert_beh), list_3(MK_NUM(42), s_foo, TRUE));
    cust = actor_create(MK_PROC(assert_beh),
        list_3(list_3(UNIT, UNDEF, FAIL), list_2(OK, INF), NIL));
    effect = effect_create(effect, cust);
    //expr = list_1(MK_ACTOR(&a_list));  // (<list>)
    //expr = list_4(MK_ACTOR(&a_list), MK_NUM(-1), MK_NUM(2), MK_NUM(3));  // (<list> -1 2 3)
    //expr = list_4(MK_ACTOR(&a_list), MK_NUM(42), expr, TRUE);  // (<list> 42 (<quote> foo) #t)
    //expr = list_4(s_list, MK_NUM(42), expr, TRUE);  // (list 42 (quote foo) #t)
    expr = list_4(s_list,  // (list '(#unit #undef #fail) (list 0 INF) (list))
        list_2(s_quote, list_3(UNIT, UNDEF, FAIL)),
        list_3(s_list, OK, INF),
        list_1(s_list));
    effect = effect_send(effect,
        actor_send(expr, list_3(cust, s_eval, env)));
#if CONCURRENT_GC
    effect = effect_send(effect, actor_send(MK_ACTOR(&a_concurrent_gc), MK_NUM(0)));
#endif
    // dispatch all pending events
    ASSERT(apply_effect(UNDEF, effect) == OK);
    result = event_loop();
    DEBUG(debug_print("test_eval event_loop #2", result));
#endif

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
    fprintf(stderr, "s_match = %"PRIxPTR"\n", s_match);
    ASSERT(IS_SYM(s_match));
#endif

#if 1
    result = unit_tests();
    WARN(debug_print("result", result));
#endif

#if 0
    ASSERT(IS_ACTOR(FAIL));
    cell_t *p = TO_PTR(FAIL);
    p->tail = NIL;  // FIXME: should not be able to assign to `const`
#endif

    printf("result = ");
    print(result);
    newline();
    return (result == OK ? 0 : 1);
}

int is_proc(int_t val) {
    return IS_ACTOR(val) && (TO_PTR(val) >= PTR(newline)) && (TO_PTR(val) <= PTR(main));
}
