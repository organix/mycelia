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
#define ATRACE(x)       // include/exclude meta-actor tracing
#define PTRACE(x)       // include/exclude PEG-actor tracing

#define NO_CELL_FREE  0 // never release allocated cells
#define GC_CALL_DEPTH 0 // count recursion depth during garbage collection
#define GC_TRACE_FREE 1 // trace free list during mark phase
#define CONCURRENT_GC 0 // interleave garbage collection with event dispatch
#define MULTIPHASE_GC 0 // perform gc mark and sweep separately
#define TIME_DISPATCH 1 // measure execution time for message dispatch
#define META_ACTORS   1 // include meta-actors facilities
#define PEG_ACTORS    1 // include PEG parsing facilities
#define RUN_SELF_TEST 1 // include and run unit-test suite
#define RUN_FILE_REPL 1 // evalutate code from files and interactive REPL

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
#define TAG_ENUM    INT(0x2)
#define TAG_ACTOR   INT(0x3)

#define ENUM_MASK   INT(0xF)
#define ENUM_PROC   INT(0x2)
#define ENUM_SYMBOL INT(0x6)
#define ENUM_RSVD1  INT(0xA)
#define ENUM_RSVD2  INT(0xE)

#define IS_ADDR(v)  ((v)&1)

#define MK_NUM(n)   INT(NAT(n)<<2)
#define MK_PAIR(p)  INT(PTR(p)+TAG_PAIR)
#define MK_ENUM(n)  INT((NAT(n)<<4)|TAG_ENUM)
#define MK_ACTOR(p) INT(PTR(p)+TAG_ACTOR)

#define MK_SYM(n)   (MK_ENUM(n)|ENUM_SYMBOL)
#define MK_PROC(f)  INT(PTR(f)+TAG_ENUM)
#define MK_BOOL(b)  ((b) ? TRUE : FALSE)

#define IS_NUM(v)   (((v)&TAG_MASK) == TAG_FIXNUM)
#define IS_PAIR(v)  (((v)&TAG_MASK) == TAG_PAIR)
#define IS_ENUM(v)  (((v)&TAG_MASK) == TAG_ENUM)
#define IS_ACTOR(v) (((v)&TAG_MASK) == TAG_ACTOR)

//#define IS_SYM(v)   (((v)&ENUM_MASK) == ENUM_SYMBOL)
#define IS_SYM(v)   (IS_ENUM(v)&&((v)<0xFFFF))
#define IS_PROC(v)  (IS_ENUM(v)&&((v)>0xFFFF))

#define TO_INT(v)   (INT(v)>>2)
#define TO_NAT(v)   (NAT(v)>>2)
#define TO_PTR(v)   PTR(NAT(v)&~TAG_MASK)
#define TO_ENUM(v)  (INT(v)>>4)
#define TO_PROC(v)  ((proc_t)(PTR(v)-TAG_ENUM))

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
#else
static int gc_free_cnt = 0;
#endif

static cell_t *cell_new() {
    int_t head = cell[0].tail;
    int_t next = cell[head].tail;
    if (next) {
        // use cell from free-list
        cell[0].tail = next;
#if CONCURRENT_GC
        gc_set_mark(head);
#else
        --gc_free_cnt;
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
#if !CONCURRENT_GC
    ++gc_free_cnt;
#endif
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
    if (IS_ACTOR(val)) {
        cell_t *p = TO_PTR(val);
        code = p->head;
    } else if (IS_PAIR(val)) {
        code = MK_PROC(Pair);
    } else if (IS_NUM(val)) {
        code = MK_PROC(Fixnum);
    } else if (IS_SYM(val)) {
        code = MK_PROC(Symbol);
    } else if (IS_PROC(val)) {
        code = val;
    } else {
        code = error("undefined dispatch procedure");
    }
    return code;
}

int_t get_data(int_t val) {
    int_t data = val;
    if (IS_ACTOR(val)) {
        cell_t *p = TO_PTR(val);
        data = p->tail;
    }
    return data;
}

PROC_DECL(obj_call) {
    int_t code = get_code(self);
    if (!IS_PROC(code)) return error("obj_call() requires a procedure");
    proc_t proc = TO_PROC(code);
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

static cell_t event_q;  // FORWARD DECLARATION
static cell_t gnd_locals;  // FORWARD DECLARATION
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
#if !CONCURRENT_GC
    WARN(fprintf(stderr, "  cell_usage: gc_free_cnt %d\n", gc_free_cnt));
#endif
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

#define INTERN_MAX (4096)
char intern[INTERN_MAX] = {
    0,  // end of interned strings
};

int is_symbol(int_t val) {
    return IS_SYM(val) && (NAT(PTR(&intern[TO_ENUM(val)]) - PTR(intern)) < sizeof(intern));
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
    if ((i + n + 1) >= INTERN_MAX) return panic("out of symbol memory");
    intern[i++] = n;
    for (j = 0; (j < n); ++j) {
        intern[i+j] = s[j];
    }
    intern[i+n] = 0;
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
#endif
    return OK;
}

/*
 * actor event dispatch
 */

static cell_t event_q = { .head = NIL, .tail = NIL };

int_t event_q_put(int_t event) {
    if (!IS_PAIR(event)) return FAIL;
    int_t tail = cons(event, NIL);
    if (event_q.head == NIL) {
        event_q.head = tail;
    } else {
        set_cdr(event_q.tail, tail);
    }
    event_q.tail = tail;
    return OK;
}

int_t event_q_pop() {
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

static cell_t saved_q = { .head = NIL, .tail = NIL };
static cell_t saved_a = { .head = UNDEF, .tail = UNDEF };

int_t event_begin(int_t event) {
    XDEBUG(debug_print("event_begin event", event));
    saved_q = event_q;  // snapshot event queue
    saved_a.head = UNDEF;  // prepare for BECOME
    saved_a.tail = UNDEF;
    XDEBUG(hexdump("event_begin saved", PTR(&saved_q), 4));
    return event;
}

int_t event_commit(int_t event) {
    XDEBUG(debug_print("event_commit event", event));
    int_t target = car(event);
    XDEBUG(debug_print("event_commit target", target));
    if ((saved_a.head != UNDEF) && IS_ACTOR(target)) {
        cell_t *p = TO_PTR(target);
        *p = saved_a;  // update target actor
        XDEBUG(hexdump("event_commit become", PTR(p), 2));
    }
    return cell_free(event);
}

int_t event_rollback(int_t event) {
    XDEBUG(debug_print("event_rollback event", event));
    event_q = saved_q;  // restore event queue (un-SEND)
    return cell_free(event);
}

i64 event_dispatch_count = 0;
i64 event_dispatch_ticks = 0;

int_t event_dispatch() {
#if TIME_DISPATCH
    clock_t t0 = clock();
#endif
    int_t event = event_q_pop();
    if (!IS_PAIR(event)) return UNDEF;  // nothing to dispatch
    int_t target = car(event);
    XDEBUG(debug_print("event_dispatch target", target));
    int_t code = get_code(target);
    XDEBUG(debug_print("event_dispatch code", code));
    if (!IS_PROC(code)) return error("event_dispatch requires a procedure");
    int_t msg = cdr(event);
    XDEBUG(debug_print("event_dispatch msg", msg));
    event = event_begin(event);

    // invoke actor behavior
    proc_t proc = TO_PROC(code);
    int_t ok = (*proc)(target, msg);
    XDEBUG(debug_print("event_dispatch ok", ok));

    // apply effect(s)
    if (ok == OK) {
        event = event_commit(event);
    } else {
        event = event_rollback(event);
    }
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
    if ((gc_free_cnt < 128) && (cell[0].head > (CELL_MAX - 256))) {
        int freed = gc_mark_and_sweep();
        clock_t t2 = clock();
        DEBUG(printf("event_dispatch: gc reclaimed %d cells\n", freed));
#if TIME_DISPATCH
        event_dispatch_ticks += (t2 - t0);  // include gc
        DEBUG(double gc = (double)(t2 - t1) / CLOCKS_PER_SEC;
            printf("event_dispatch: t1=%ld t2=%ld gc=%.6f (%ld CPS)\n", t1, t2, gc, (long)CLOCKS_PER_SEC));
#endif
    } else {
#if TIME_DISPATCH
        event_dispatch_ticks += (t1 - t0);  // exclude gc
#endif
    }
#if TIME_DISPATCH
    DEBUG(double dt = (double)(t1 - t0) / CLOCKS_PER_SEC;
        printf("event_dispatch: t0=%ld t1=%ld dt=%.6f (%ld CPS)\n", t0, t1, dt, (long)CLOCKS_PER_SEC));
#endif
#endif // CONCURRENT_GC
    return OK;
}

static cell_t a_concurrent_gc;  // FORWARD DECLARATION
int_t event_loop() {
#if CONCURRENT_GC
    SEND(MK_ACTOR(&a_concurrent_gc), MK_NUM(0));  // start GC actor
#endif
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
 * actor primitives
 */

int_t actor_create(int_t code, int_t data) {
    XDEBUG(debug_print("actor_create code", code));
    XDEBUG(debug_print("actor_create data", data));
    if (!IS_PROC(code)) return error("CREATE code must be a procedure");
    cell_t *p = cell_new();
    p->head = code;
    p->tail = data;
    return MK_ACTOR(p);
}

int_t actor_send(int_t target, int_t msg) {
    XDEBUG(debug_print("actor_send target", target));
    XDEBUG(debug_print("actor_send msg", msg));
    int_t event = cons(target, msg);
    return event_q_put(event);
}

int_t actor_become(int_t code, int_t data) {
    XDEBUG(debug_print("actor_become code", code));
    XDEBUG(debug_print("actor_become data", data));
    if (!IS_PROC(code)) return error("BECOME code must be a procedure");
    if (IS_PROC(saved_a.head)) return error("only one BECOME allowed");
    saved_a.head = code;
    saved_a.tail = data;
    return OK;
}

/*
 * actor behaviors
 */

#define CREATE(c,d)     actor_create((c),(d))
#define SEND(t,m)       if (actor_send((t),(m)) != OK) return FAIL
#define BECOME(c,d)     if (actor_become((c),(d)) != OK) return FAIL

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
    GET_VARS();  // ok
    XDEBUG(debug_print("sink_beh vars", vars));
    TAIL_VAR(ok);
    return ok;
}
const cell_t a_sink = { .head = MK_PROC(sink_beh), .tail = OK };
#define SINK  MK_ACTOR(&a_sink)

PROC_DECL(println_beh) {
    XDEBUG(debug_print("println_beh self", self));
    GET_ARGS();  // value
    DEBUG(debug_print("println_beh args", args));
    TAIL_ARG(value);

    print(value);
    newline();
    fflush(stdout);

    return OK;
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

    SEND(cust, cons(self, msg));

    return OK;
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

    if (tag == k_tail) {
        int_t value = cons(head, tail);
        XDEBUG(debug_print("join_h_beh value", value));
        SEND(cust, value);
    } else {
        SEND(cust, error("unexpected join tag"));
    }

    return OK;
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

    if (tag == k_head) {
        int_t value = cons(head, tail);
        XDEBUG(debug_print("join_t_beh value", value));
        SEND(cust, value);
    } else {
        SEND(cust, error("unexpected join tag"));
    }

    return OK;
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

    if (tag == k_head) {
        BECOME(MK_PROC(join_h_beh), cons(cust, cons(value, k_tail)));
    } else if (tag == k_tail) {
        BECOME(MK_PROC(join_t_beh), cons(cust, cons(k_head, value)));
    } else {
        SEND(cust, error("unexpected join tag"));
    }

    return OK;
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

    int_t k_head = CREATE(MK_PROC(tag_beh), self);
    int_t k_tail = CREATE(MK_PROC(tag_beh), self);
    SEND(head, cons(k_head, h_req));
    SEND(tail, cons(k_tail, t_req));
    BECOME(MK_PROC(join_beh), cons(cust, cons(k_head, k_tail)));

    return OK;
}

#if CONCURRENT_GC
PROC_DECL(gc_sweep_beh);  // FORWARD DECLARATION

PROC_DECL(gc_mark_beh) {
    XDEBUG(debug_print("gc_mark_beh self", self));

    int_t root = event_q.head;  // everything is reachable from the event queue
    if (root == NIL) {
        // if event queue is empty, stop concurrent gc
        DEBUG(printf("gc_mark_beh: STOP CONCURRENT GC\n"));
        return OK;
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

    BECOME(MK_PROC(gc_sweep_beh), UNDEF);
    SEND(self, arg);

    return OK;
}
PROC_DECL(gc_sweep_beh) {
    XDEBUG(debug_print("gc_sweep_beh self", self));

    int n = gc_sweep();
    XDEBUG(printf("gc_sweep_beh: free'd %d dead cells\n", n));

    gc_running = 0;  // leave unsafe gc phase

    BECOME(MK_PROC(gc_mark_beh), UNDEF);
    SEND(self, arg);

    return OK;
}

PROC_DECL(gc_mark_and_sweep_beh) {  // perform all GC steps together
    XDEBUG(debug_print("gc_mark_and_sweep_beh self", self));
    GET_VARS();  // limit
    XDEBUG(debug_print("gc_mark_and_sweep_beh vars", vars));
    TAIL_VAR(limit);
    GET_ARGS();  // count
    XDEBUG(debug_print("gc_mark_and_sweep_beh args", args));
    TAIL_ARG(count);

    if (event_q.head == NIL) {
        // if event queue is empty, stop concurrent gc
        DEBUG(printf("gc_mark_and_sweep_beh: STOP CONCURRENT GC\n"));
        return OK;
    }

    int_t n = TO_INT(count);
    int_t m = TO_INT(limit);
    if (n < m) {
        // skip `limit` gc cycles
        XDEBUG(printf("gc_mark_and_sweep_beh: count(%"PRIdPTR") < limit(%"PRIdPTR")\n", n, m));
        SEND(self, MK_NUM(++n));
        return OK;
    }

    int freed = gc_mark_and_sweep();
    DEBUG(printf("gc_mark_and_sweep_beh: gc reclaimed %d cells\n", freed));
    SEND(self, MK_NUM(0));

    return OK;
}

// WARNING! gc does not traverse static actors, so they can't point to the heap.
static cell_t a_concurrent_gc = {
#if MULTIPHASE_GC
    .head = MK_PROC(gc_mark_beh),
#else
    .head = MK_PROC(gc_mark_and_sweep_beh),
#endif
    .tail = MK_NUM(16),
};
//  SEND(MK_ACTOR(&a_concurrent_gc), MK_NUM(0));  // start gc -- done in event_loop()
#endif // CONCURRENT_GC

PROC_DECL(assert_beh) {
    DEBUG(debug_print("assert_beh self", self));
    GET_VARS();  // expect
    XDEBUG(debug_print("assert_beh vars", vars));
    TAIL_VAR(expect);
    GET_ARGS();  // actual
    XDEBUG(debug_print("assert_beh args", args));
    TAIL_ARG(actual);
    if (!equal(expect, actual)) {
        ERROR(debug_print("assert_beh expect", expect));
        ERROR(debug_print("assert_beh actual", actual));
        return panic("assert_beh !equal(expect, actual)");
    }
    return OK;
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
    if (req == s_typeq) {  // (cust 'typeq T)
        POP_ARG(Tq);
        DEBUG(debug_print("Type T?", Tq));
        END_ARGS();
        int_t value = MK_BOOL(T == Tq);
        DEBUG(debug_print("Type value", value));
        SEND(cust, value);
        return OK;
    }
    WARN(debug_print("Type NOT UNDERSTOOD", arg));
    SEND(cust, error("NOT UNDERSTOOD"));
    return OK;
}

static PROC_DECL(SeType) {
    XDEBUG(debug_print("SeType self", self));
    GET_ARGS();
    XDEBUG(debug_print("SeType args", args));
    POP_ARG(cust);
    POP_ARG(req);
    if (req == s_eval) {  // (cust 'eval env)
        POP_ARG(_env);
        END_ARGS();
        DEBUG(debug_print("SeType value", self));
        SEND(cust, self);
        return OK;
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
#if 0
    if (IS_PROC(oper)) {
        proc_t prim = TO_PROC(oper);
        int_t value = (*prim)(opnd, env);  // delegate to primitive proc
        WARN(debug_print("Appl_k_args --DEPRECATED-- value", value));
        SEND(cust, value);
        return panic("Appl must wrap an Actor, not a Proc");
    }
#endif
    SEND(oper, list_4(cust, s_apply, opnd, env));
    return OK;
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
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(env);
        END_ARGS();
        int_t k_args = CREATE(MK_PROC(Appl_k_args), list_3(cust, oper, env));
        SEND(opnd, list_4(k_args, s_map, s_eval, env));
        return OK;
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
    DEBUG(debug_print("Oper_prim vars", vars));
    TAIL_VAR(proc);
    ASSERT(IS_PROC(proc));
    GET_ARGS();
    DEBUG(debug_print("Oper_prim args", args));
    POP_ARG(cust);
    POP_ARG(req);
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(env);
        END_ARGS();
        proc_t prim = TO_PROC(proc);
        int_t value = (*prim)(opnd, env);  // delegate to primitive proc
        XDEBUG(debug_print("Oper_prim value", value));
        SEND(cust, value);
        return OK;
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
    GET_VARS();  // (locals . penv)
    DEBUG(debug_print("Scope vars", vars));
    POP_VAR(locals);  // local bindings
    TAIL_VAR(penv);  // parent environment
    GET_ARGS();
    DEBUG(debug_print("Scope args", args));
    POP_ARG(cust);
    POP_ARG(req);
    if (req == s_lookup) {  // (cust 'lookup symbol)
        POP_ARG(symbol);
        END_ARGS();
        int_t binding = assoc_find(locals, symbol);
        if (IS_PAIR(binding)) {
            int_t value = cdr(binding);
            DEBUG(debug_print("Scope value", value));
            SEND(cust, value); // send value to cust
        } else if ((penv != NIL) && (penv != UNDEF)) {
            SEND(penv, arg);  // delegate to parent
        } else {
            SEND(cust, UNDEF);  // no parent, no binding
        }
        return OK;
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
        SEND(cust, UNIT);
        // FIXME: surgically replace `locals` (WARNING! this is a non-transactional BECOME)
        XDEBUG(debug_print("Scope locals", locals));
        cell_t *p = TO_PTR(get_data(self));
        p->head = locals;
        return OK;
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
    if (req == s_apply) {  // (cust 'apply opnd denv)
        POP_ARG(opnd);
        POP_ARG(denv);  // dynamic (caller) environment
        END_ARGS();
        int_t assoc = match_pattern(eptrn, cons(denv, opnd), NIL);
        if (assoc == UNDEF) {
            SEND(cust, error("argument pattern mismatch"));
            return OK;
        }
        int_t xenv = CREATE(MK_PROC(Scope), cons(assoc, lenv));
        SEND(body, list_6(cust, s_fold, UNIT, MK_PROC(fold_last), s_eval, xenv));
        return OK;
    }
    return SeType(self, arg);  // delegate to SeType
}
PROC_DECL(Oper_lambda) {  // (lambda pattern . objects)
    XDEBUG(debug_print("Oper_lambda self", self));
    GET_ARGS();
    DEBUG(debug_print("Oper_lambda args", args));
    POP_ARG(cust);
    POP_ARG(req);
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(lenv);
        END_ARGS();
        if (IS_PAIR(opnd)) {
            int_t ptrn = car(opnd);
            int_t body = cdr(opnd);
            int_t closure = CREATE(MK_PROC(Closure),
                list_3(cons(s_ignore, ptrn), body, lenv));
            int_t appl = CREATE(MK_PROC(Appl), closure);
            SEND(cust, appl);
            return OK;
        }
        SEND(cust, error("lambda expected pattern . body"));
        return OK;
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
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(env);
        END_ARGS();
        SEND(opnd, list_6(cust, s_fold, UNIT, MK_PROC(fold_last), s_eval, env));
        return OK;
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
                SEND(expr, list_3(cust, s_eval, env));
                return OK;
            }
        }
        SEND(cust, error("eval expected 1 or 2 arguments"));
        return OK;
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
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(env);
        END_ARGS();
        if (IS_PAIR(opnd)) {
            int_t oper = car(opnd);
            oper = unwrap(oper);  // get underlying operative
            if (oper == UNDEF) {
                SEND(cust, UNDEF);
                return OK;
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
                    SEND(oper, list_4(cust, s_apply, args, env));
                    return OK;
                }
            }
        }
        SEND(cust, error("apply expected 2 or 3 arguments"));
        return OK;
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
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(env);
        END_ARGS();
        if (IS_PAIR(opnd)) {
            int_t oper = car(opnd);
            oper = unwrap(oper);  // get underlying operative
            if (oper == UNDEF) {
                SEND(cust, UNDEF);
                return OK;
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
                        SEND(cust, error("map requires equal list lengths"));
                        return OK;
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
                SEND(z, list_4(cust, s_map, s_eval, env));
                return OK;
            }
        }
        SEND(cust, error("map expected 2 or more arguments"));
        return OK;
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
    SEND(form, list_3(cust, s_eval, denv));
    return OK;
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
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(denv);
        END_ARGS();
        int_t k_form = CREATE(MK_PROC(Macro_k_form), list_2(cust, denv));
        SEND(oper, list_4(k_form, s_apply, opnd, denv));
        return OK;
    }
    return SeType(self, arg);  // delegate to SeType
}
PROC_DECL(Oper_macro) {  // (macro pattern evar . objects)
    XDEBUG(debug_print("Oper_macro self", self));
    GET_ARGS();
    DEBUG(debug_print("Oper_macro args", args));
    POP_ARG(cust);
    POP_ARG(req);
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
                int_t closure = CREATE(MK_PROC(Closure),
                    list_3(cons(evar, ptrn), body, lenv));
                int_t oper = CREATE(MK_PROC(Macro), closure);
                SEND(cust, oper);
                return OK;
            }
        }
        SEND(cust, error("macro expected pattern evar . body"));
        return OK;
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
    int_t assoc = match_pattern(ptrn, value, NIL);
    if (assoc == UNDEF) {
        SEND(cust, error("define pattern mismatch"));
    } else {
        SEND(env, list_3(cust, s_bind, assoc));
    }
    return OK;
}
PROC_DECL(Oper_define) {  // (define pattern expression)
    XDEBUG(debug_print("Oper_define self", self));
    GET_ARGS();
    XDEBUG(debug_print("Oper_define args", args));
    POP_ARG(cust);
    POP_ARG(req);
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(env);
        END_ARGS();
        if (IS_PAIR(opnd)) {
            int_t ptrn = car(opnd);
            opnd = cdr(opnd);
            if (IS_PAIR(opnd)) {
                int_t expr = car(opnd);
                int_t k_bind = CREATE(MK_PROC(Define_k_bind), list_3(cust, ptrn, env));
                SEND(expr, list_3(k_bind, s_eval, env));
                return OK;
            }
        }
        SEND(cust, error("define expected 2 operands"));
        return OK;
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
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(_env);
        END_ARGS();
        if (IS_PAIR(opnd)) {
            int_t expr = car(opnd);
            if (cdr(opnd) == NIL) {
                DEBUG(debug_print("Oper_quote value", expr));
                SEND(cust, expr);
                return OK;
            }
        }
        SEND(cust, error("quote expected 1 operand"));
        return OK;
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
    if (pred == UNDEF) {  // short-circuit for #undef
        SEND(cust, UNDEF);
    } else {
        int_t expr = ((pred == FALSE) ? altn : cnsq);
        SEND(expr, list_3(cust, s_eval, env));
    }
    return OK;
}
PROC_DECL(Oper_if) {  // (if pred cnsq altn)
    XDEBUG(debug_print("Oper_if self", self));
    GET_ARGS();
    XDEBUG(debug_print("Oper_if args", args));
    POP_ARG(cust);
    POP_ARG(req);
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
                        int_t k_pred = CREATE(MK_PROC(If_k_pred),
                            list_4(cust, cnsq, altn, env));
                        SEND(pred, list_3(k_pred, s_eval, env));
                        return OK;
                    }
                }
            }
        }
        SEND(cust, error("if expected 3 operands"));
        return OK;
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
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(_env);
        END_ARGS();
        SEND(opnd, list_6(cust, s_fold, TRUE, MK_PROC(fold_and), s_typeq, type));
        return OK;
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
static PROC_DECL(prim_booleanp) {  // (boolean? . objects)
    int_t opnd = self;
    //int_t env = arg;
    while (IS_PAIR(opnd)) {
        int_t x = car(opnd);
        if ((x != FALSE) && (x != TRUE)) return FALSE;
        opnd = cdr(opnd);
    }
    if (opnd != NIL) {
        return error("proper list required");
    }
    return TRUE;
}
const cell_t oper_booleanp = { .head = MK_PROC(Oper_prim), .tail = MK_PROC(prim_booleanp) };
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
    if ((value == UNDEF) || (value == FALSE) || (rest == NIL)) {
        SEND(cust, value);
    } else if (IS_PAIR(rest)) {
        int_t expr = car(rest);
        rest = cdr(rest);
        BECOME(MK_PROC(And_k_rest), list_3(cust, rest, env));
        SEND(expr, list_3(self, s_eval, env));
    } else if (rest == NIL) {
        SEND(cust, TRUE);
    } else {
        SEND(cust, error("proper list required"));
    }
    return OK;
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
    if ((value == UNDEF) || (value != FALSE) || (rest == NIL)) {
        SEND(cust, value);
    } else if (IS_PAIR(rest)) {
        int_t expr = car(rest);
        rest = cdr(rest);
        BECOME(MK_PROC(Or_k_rest), list_3(cust, rest, env));
        SEND(expr, list_3(self, s_eval, env));
    } else {
        SEND(cust, error("proper list required"));
    }
    return OK;
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
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(env);
        END_ARGS();
        if (IS_PAIR(opnd)) {
            int_t expr = car(opnd);
            int_t rest = cdr(opnd);
            int_t k_rest = CREATE(proc, list_3(cust, rest, env));
            SEND(expr, list_3(k_rest, s_eval, env));
            return OK;
        }
        SEND(cust, dflt);
        return OK;
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
    if (req == s_map) {  // (cust 'map . h_req)
        TAIL_ARG(h_req);
        SEND(self, cons(cust, h_req));
        return OK;
    }
    if (req == s_fold) {  // (cust 'fold zero oplus . req)
        // WARNING! this behavior is also inlined in Pair_k_fold
        POP_ARG(zero);
        //POP_ARG(oplus);
        //TAIL_ARG(req);
        SEND(cust, zero);
        return OK;
    }
    return SeType(self, arg);  // delegate to SeType
}
static PROC_DECL(prim_nullp) {  // (null? . objects)
    int_t opnd = self;
    //int_t env = arg;
    while (IS_PAIR(opnd)) {
        int_t x = car(opnd);
        if (x != NIL) return FALSE;
        opnd = cdr(opnd);
    }
    if (opnd != NIL) {
        return error("proper list required");
    }
    return TRUE;
}
const cell_t oper_nullp = { .head = MK_PROC(Oper_prim), .tail = MK_PROC(prim_nullp) };
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
    ASSERT(IS_PROC(oplus));
    proc_t proc = TO_PROC(oplus);
    zero = (*proc)(zero, one);  // update accumulator
    DEBUG(debug_print("Pair_k_fold accum", zero));
    if (tail == NIL) {
        // inlined behavior from Null...
        SEND(cust, zero);
    } else {
        SEND(tail, cons(cust, cons(s_fold, cons(zero, cons(oplus, req)))));
    }
    return OK;
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
    SEND(oper, list_4(cust, s_apply, opnd, env));
    return OK;
}
static PROC_DECL(Pair_k_eval_tail) {
    XDEBUG(debug_print("Pair_k_eval_tail self", self));
    GET_VARS();  // cust
    XDEBUG(debug_print("Pair_k_eval_tail vars", vars));
    TAIL_VAR(cust);
    GET_ARGS();  // head
    XDEBUG(debug_print("Pair_k_eval_tail args", args));
    TAIL_ARG(head);
    SEND(cust, cons(head, NIL));
    return OK;
}
PROC_DECL(Pair) {  // WARNING: behavior used directly in obj_call()
    XDEBUG(debug_print("Pair self", self));
    GET_ARGS();
    XDEBUG(debug_print("Pair args", args));
    POP_ARG(cust);
    POP_ARG(req);
    if (req == s_eval) {  // (cust 'eval env)
        POP_ARG(env);
        END_ARGS();
        int_t k_apply = CREATE(MK_PROC(Pair_k_apply),
            list_3(cust, cdr(self), env));
        SEND(car(self), list_3(k_apply, s_eval, env));
        return OK;
    }
    if (req == s_map) {  // (cust 'map . h_req)
        TAIL_ARG(h_req);
        if ((cdr(self) == NIL) && (car(h_req) == s_eval)) {
            // optimization to eval NIL tail
            int_t k_eval_tail = CREATE(MK_PROC(Pair_k_eval_tail), cust);
            SEND(car(self), cons(k_eval_tail, h_req));
        } else {
            int_t t_req = cdr(arg);  // re-use original arg
            int_t fork = CREATE(MK_PROC(fork_beh), cons(cust, self));
            SEND(fork, cons(h_req, t_req));
        }
        return OK;
    }
    if (req == s_fold) {  // (cust 'fold zero oplus . req)
        int_t rest = args;  // re-use args
        POP_ARG(zero);
        POP_ARG(oplus);
        TAIL_ARG(req);
        int_t head = car(self);
        int_t tail = cdr(self);
        int_t k_fold = CREATE(MK_PROC(Pair_k_fold),
            cons(cust, cons(tail, rest)));
        SEND(head, cons(k_fold, req));
        return OK;
    }
    return Type(self, arg);  // delegate to Type (not self-evaluating)
}
static PROC_DECL(prim_pairp) {  // (pair? . objects)
    int_t opnd = self;
    //int_t env = arg;
    while (IS_PAIR(opnd)) {
        int_t x = car(opnd);
        if (!IS_PAIR(x)) return FALSE;
        opnd = cdr(opnd);
    }
    if (opnd != NIL) {
        return error("proper list required");
    }
    return TRUE;
}
const cell_t oper_pairp = { .head = MK_PROC(Oper_prim), .tail = MK_PROC(prim_pairp) };
const cell_t a_pairp = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_pairp) };

PROC_DECL(Symbol) {  // WARNING: behavior used directly in obj_call()
    DEBUG(debug_print("Symbol self", self));
    GET_ARGS();
    XDEBUG(debug_print("Symbol args", args));
    POP_ARG(cust);
    POP_ARG(req);
    if (req == s_eval) {  // (cust 'eval env)
        POP_ARG(env);
        END_ARGS();
        SEND(env, list_3(cust, s_lookup, self));
        return OK;
    }
    return Type(self, arg);  // delegate to Type (not self-evaluating)
}
#if 0
const cell_t oper_symbolp = { .head = MK_PROC(Oper_typep), .tail = MK_PROC(Symbol) };
const cell_t a_symbolp = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_symbolp) };
#else
static PROC_DECL(prim_symbolp) {  // (symbol? . objects)
    int_t opnd = self;
    //int_t env = arg;
    while (IS_PAIR(opnd)) {
        int_t x = car(opnd);
        //if (get_code(x) != MK_PROC(Symbol)) return FALSE;
        if (!IS_SYM(x)) return FALSE;
        opnd = cdr(opnd);
    }
    if (opnd != NIL) {
        return error("proper list required");
    }
    return TRUE;
}
const cell_t oper_symbolp = { .head = MK_PROC(Oper_prim), .tail = MK_PROC(prim_symbolp) };
const cell_t a_symbolp = { .head = MK_PROC(Appl), .tail = MK_ACTOR(&oper_symbolp) };
#endif

PROC_DECL(Fixnum) {  // WARNING: behavior used directly in obj_call()
    XDEBUG(debug_print("Fixnum self", self));
    GET_ARGS();
    XDEBUG(debug_print("Fixnum args", args));
    return SeType(self, arg);  // delegate to SeType
}
static PROC_DECL(prim_numberp) {  // (number? . objects)
    int_t opnd = self;
    //int_t env = arg;
    while (IS_PAIR(opnd)) {
        int_t x = car(opnd);
        if (!IS_NUM(x)) return FALSE;
        opnd = cdr(opnd);
    }
    if (opnd != NIL) {
        return error("proper list required");
    }
    return TRUE;
}
const cell_t oper_numberp = { .head = MK_PROC(Oper_prim), .tail = MK_PROC(prim_numberp) };
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
    XDEBUG(debug_print("fold_effect zero", zero));
    int_t one = arg;
    XDEBUG(debug_print("fold_effect one", one));
    // merge effect `one` into effects `zero`
    if (IS_PAIR(one) && IS_PAIR(zero) && (car(zero) != UNDEF)) {
        int_t events = car(zero);
        int_t beh = cdr(zero);
        //if (!IS_PAIR(one)) return zero;  // nothing to add
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
    DEBUG(debug_print("Behavior args", args));
    POP_ARG(cust);
    POP_ARG(req);
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
                SEND(cust, error("message pattern mismatch"));
                return OK;
            }
            XDEBUG(debug_print("Behavior assoc", assoc));
            int_t aenv = CREATE(MK_PROC(Scope), cons(assoc, lenv));
            int_t empty = cons(NIL, NIL);  // empty meta-effect
            SEND(stmts, list_6(cust, s_fold, empty, MK_PROC(fold_effect), s_eval, aenv));
        }
        return OK;
    }
    return SeType(self, arg);  // delegate to SeType
}
PROC_DECL(Oper_BEH) {  // (BEH pattern . statements)
    XDEBUG(debug_print("Oper_BEH self", self));
    GET_ARGS();
    DEBUG(debug_print("Oper_BEH args", args));
    POP_ARG(cust);
    POP_ARG(req);
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);
        POP_ARG(env);
        END_ARGS();
        if (IS_PAIR(opnd)) {
            int_t ptrn = car(opnd);
            int_t stmts = cdr(opnd);
            int_t beh = CREATE(MK_PROC(Behavior),
                list_3(ptrn, stmts, env));
            SEND(cust, beh);
            return OK;
        }
        SEND(cust, error("BEH expected pattern . statements"));
        return OK;
    }
    return SeType(self, arg);  // delegate to SeType
}
const cell_t a_BEH = { .head = MK_PROC(Oper_BEH), .tail = UNDEF };

int is_meta_beh(int_t val) {
    if (IS_ACTOR(val)) {
        cell_t *p = TO_PTR(val);
        return (p->head == MK_PROC(Behavior));
    }
    return 0;
}

PROC_DECL(Actor);  // FORWARD DECLARATION
static PROC_DECL(Actor_k_done) {
    XDEBUG(debug_print("Actor_k_done self", self));
    GET_VARS();  // (cust actor beh)
    XDEBUG(debug_print("Actor_k_done vars", vars));
    POP_VAR(cust);
    POP_VAR(actor);
    POP_VAR(beh);
    GET_ARGS();  // effects
    XDEBUG(debug_print("Actor_k_done args", args));
    TAIL_ARG(effects);
    // apply effects for this actor
    if (IS_PAIR(effects)) {
        // commit transaction
        ATRACE(debug_print("Actor_k_done commit", effects));
        if (car(effects) == UNDEF) {
            int_t reason = cdr(effects);
            WARN(debug_print("Actor_k_done FAIL", reason));
        } else {
            int_t events = car(effects);
            while (IS_PAIR(events)) {
                int_t event = car(events);
                ATRACE(debug_print("Actor_k_done meta-event", event));
                ASSERT(IS_PAIR(event));
                int_t target = car(event);
                int_t msg = cdr(event);
                SEND(target, list_4(SINK, s_apply, msg, GROUND_ENV));
                events = cdr(events);
            }
            int_t new_beh = cdr(effects);
            if (is_meta_beh(new_beh)) {
                beh = new_beh;  // install new beh
                ATRACE(debug_print("Actor_k_done meta-become", beh));
            }
        }
    } else {
        // rollback transaction
        ATRACE(debug_print("Actor_k_done rollback", effects));
    }
    XDEBUG(debug_print("Actor_k_done meta-actor", actor));
    cell_t *p = TO_PTR(actor);
    p->tail = beh;  // end event transaction
    SEND(cust, UNIT);
    return OK;
}
PROC_DECL(Actor) {
    XDEBUG(debug_print("Actor self", self));
    GET_VARS();  // beh  ; UNDEF if busy
    XDEBUG(debug_print("Actor vars", vars));
    TAIL_VAR(beh);
    if (!is_meta_beh(beh)) {  // actor is busy handling an event
        ATRACE(debug_print("Actor BUSY", self));
        SEND(self, arg);  // re-queue current message (serializer)
        return OK;
    }
    GET_ARGS();
    ATRACE(debug_print("Actor args", args));
    POP_ARG(cust);
    POP_ARG(req);
    if (req == s_apply) {  // (cust 'apply opnd env)
        POP_ARG(opnd);  // msg
        POP_ARG(env);
        END_ARGS();
        int_t k_done = CREATE(MK_PROC(Actor_k_done), list_3(cust, self, beh));
        SEND(beh, list_4(k_done, s_apply, cons(self, opnd), env));
        cell_t *p = TO_PTR(self);
        p->tail = UNDEF;  // begin event transaction
        return OK;
    }
    return SeType(self, arg);  // delegate to SeType
}

int is_meta_actor(int_t val) {
    if (IS_ACTOR(val)) {
        cell_t *p = TO_PTR(val);
        return (p->head == MK_PROC(Actor));
    }
    return 0;
}

static PROC_DECL(prim_CREATE) {  // (CREATE beh)
    int_t opnd = self;
    //int_t env = arg;
    if (IS_PAIR(opnd)) {
        int_t beh = car(opnd);
        if (!is_meta_beh(beh)) return error("CREATE requires Behavior");
        opnd = cdr(opnd);
        if (opnd == NIL) {
            return CREATE(MK_PROC(Actor), beh);
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
        if (!is_meta_actor(target)) return error("SEND requires Actor target");
        opnd = cdr(opnd);
        if (IS_PAIR(opnd)) {
            int_t msg = car(opnd);
            opnd = cdr(opnd);
            if (opnd == NIL) {
                return cons(cons(target, msg), NIL);
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
        if (!is_meta_beh(beh)) return error("BECOME requires Behavior");
        opnd = cdr(opnd);
        if (opnd == NIL) {
            return cons(NIL, beh);
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
#endif // META_ACTORS

#if PEG_ACTORS

PROC_DECL(peg_empty_beh) {
    PTRACE(debug_print("peg_empty_beh self", self));
    GET_ARGS();  // (custs value . in) = ((ok . fail) value . (token . next))
    PTRACE(debug_print("peg_empty_beh args", args));
    POP_ARG(custs);  // (ok . fail)
    POP_ARG(_value);
    TAIL_ARG(in);  // (token . next) -or- NIL
    int_t ok = car(custs);
    //int_t fail = cdr(custs);
    SEND(ok, cons(NIL, in));
    return OK;
}
const cell_t peg_empty = { .head = MK_PROC(peg_empty_beh), .tail = NIL };

PROC_DECL(peg_fail_beh) {
    PTRACE(debug_print("peg_fail_beh self", self));
    GET_ARGS();  // (custs value . in) = ((ok . fail) value . (token . next))
    PTRACE(debug_print("peg_fail_beh args", args));
    POP_ARG(custs);  // (ok . fail)
    int_t resume = args;  // (value . in)
    //POP_ARG(value);
    //TAIL_ARG(in);  // (token . next) -or- NIL
    //int_t ok = car(custs);
    int_t fail = cdr(custs);
    SEND(fail, resume);
    return OK;
}
const cell_t peg_fail = { .head = MK_PROC(peg_fail_beh), .tail = UNDEF };

static PROC_DECL(peg_k_next) {
    PTRACE(debug_print("peg_k_next self", self));
    GET_VARS();  // (cust . value)
    PTRACE(debug_print("peg_k_next vars", vars));
    POP_VAR(cust);
    TAIL_VAR(value);
    GET_ARGS();  // in = (token . next) -or- NIL
    PTRACE(debug_print("peg_k_next args", args));
    TAIL_ARG(in);
    SEND(cust, cons(value, in));
    return OK;
}
PROC_DECL(peg_any_beh) {
    PTRACE(debug_print("peg_any_beh self", self));
    GET_ARGS();  // (custs value . in) = ((ok . fail) value . (token . next))
    PTRACE(debug_print("peg_any_beh args", args));
    POP_ARG(custs);  // (ok . fail)
    int_t resume = args;  // (value . in)
    POP_ARG(_value);
    TAIL_ARG(in);  // (token . next) -or- NIL
    int_t ok = car(custs);
    int_t fail = cdr(custs);
    if (in == NIL) {
        SEND(fail, resume);  // fail
    } else {
        int_t token = car(in);
        int_t next = cdr(in);
        int_t k_next = CREATE(MK_PROC(peg_k_next), cons(ok, token));
        SEND(next, k_next);  // advance
    }
    return OK;
}
const cell_t peg_any = { .head = MK_PROC(peg_any_beh), .tail = UNIT };

PROC_DECL(peg_eq_beh) {
    PTRACE(debug_print("peg_eq_beh self", self));
    GET_VARS();  // match
    PTRACE(debug_print("peg_eq_beh vars", vars));
    TAIL_VAR(match);
    GET_ARGS();  // (custs value . in) = ((ok . fail) value . (token . next))
    PTRACE(debug_print("peg_eq_beh args", args));
    POP_ARG(custs);  // (ok . fail)
    int_t resume = args;  // (value . in)
    POP_ARG(_value);
    TAIL_ARG(in);  // (token . next) -or- NIL
    int_t ok = car(custs);
    int_t fail = cdr(custs);
    if (in != NIL) {
        int_t token = car(in);
        int_t next = cdr(in);
        if (equal(match, token)) {
            int_t k_next = CREATE(MK_PROC(peg_k_next), cons(ok, token));
            SEND(next, k_next);  // advance
            return OK;
        }
    }
    SEND(fail, resume);  // fail
    return OK;
}

// character classes
#define CTL (1<<0)  /* control */
#define DGT (1<<1)  /* digit */
#define UPR (1<<2)  /* uppercase */
#define LWR (1<<3)  /* lowercase */
#define DLM (1<<4)  /* "'(),;[]`{|} */
#define SYM (1<<5)  /* symbol (non-DLM) */
#define HEX (1<<6)  /* hexadecimal */
#define WSP (1<<7)  /* whitespace */

static unsigned char char_class[128] = {
/*      _0       _1       _2       _3       _4       _5       _6       _7    */
/*0_*/  CTL,     CTL,     CTL,     CTL,     CTL,     CTL,     CTL,     CTL,
/*      _8       _9       _A       _B       _C       _D       _E       _F    */
/*0_*/  CTL,     CTL|WSP, CTL|WSP, CTL|WSP, CTL|WSP, CTL|WSP, CTL,     CTL,
/*      _0       _1       _2       _3       _4       _5       _6       _7    */
/*1_*/  CTL,     CTL,     CTL,     CTL,     CTL,     CTL,     CTL,     CTL,
/*      _8       _9       _A       _B       _C       _D       _E       _F    */
/*1_*/  CTL,     CTL,     CTL,     CTL,     CTL,     CTL,     CTL,     CTL,
/*      _0       _1       _2       _3       _4       _5       _6       _7    */
/*2_*/  WSP,     SYM,     DLM,     SYM,     SYM,     SYM,     SYM,     DLM,
/*      _8       _9       _A       _B       _C       _D       _E       _F    */
/*2_*/  DLM,     DLM,     SYM,     SYM,     DLM,     SYM,     SYM,     SYM,
/*      _0       _1       _2       _3       _4       _5       _6       _7    */
/*3_*/  DGT|HEX, DGT|HEX, DGT|HEX, DGT|HEX, DGT|HEX, DGT|HEX, DGT|HEX, DGT|HEX,
/*      _8       _9       _A       _B       _C       _D       _E       _F    */
/*3_*/  DGT|HEX, DGT|HEX, SYM,     DLM,     SYM,     SYM,     SYM,     SYM,
/*      _0       _1       _2       _3       _4       _5       _6       _7    */
/*4_*/  SYM,     UPR|HEX, UPR|HEX, UPR|HEX, UPR|HEX, UPR|HEX, UPR|HEX, UPR,
/*      _8       _9       _A       _B       _C       _D       _E       _F    */
/*4_*/  UPR,     UPR,     UPR,     UPR,     UPR,     UPR,     UPR,     UPR,
/*      _0       _1       _2       _3       _4       _5       _6       _7    */
/*5_*/  UPR,     UPR,     UPR,     UPR,     UPR,     UPR,     UPR,     UPR,
/*      _8       _9       _A       _B       _C       _D       _E       _F    */
/*5_*/  UPR,     UPR,     UPR,     DLM,     SYM,     DLM,     SYM,     SYM,
/*      _0       _1       _2       _3       _4       _5       _6       _7    */
/*6_*/  DLM,     LWR|HEX, LWR|HEX, LWR|HEX, LWR|HEX, LWR|HEX, LWR|HEX, LWR,
/*      _8       _9       _A       _B       _C       _D       _E       _F    */
/*6_*/  LWR,     LWR,     LWR,     LWR,     LWR,     LWR,     LWR,     LWR,
/*      _0       _1       _2       _3       _4       _5       _6       _7    */
/*7_*/  LWR,     LWR,     LWR,     LWR,     LWR,     LWR,     LWR,     LWR,
/*      _8       _9       _A       _B       _C       _D       _E       _F    */
/*7_*/  LWR,     LWR,     LWR,     DLM,     DLM,     DLM,     SYM,     CTL,
};

PROC_DECL(peg_class_beh) {
    PTRACE(debug_print("peg_class_beh self", self));
    GET_VARS();  // class
    PTRACE(debug_print("peg_class_beh vars", vars));
    TAIL_VAR(class);
    GET_ARGS();  // (custs value . in) = ((ok . fail) value . (token . next))
    PTRACE(debug_print("peg_class_beh args", args));
    POP_ARG(custs);  // (ok . fail)
    int_t resume = args;  // (value . in)
    POP_ARG(_value);
    TAIL_ARG(in);  // (token . next) -or- NIL
    int_t ok = car(custs);
    int_t fail = cdr(custs);
    if (in != NIL) {
        int_t token = car(in);
        XDEBUG(debug_print("peg_class_beh token", token));
        int_t next = cdr(in);
        if (IS_NUM(token)) {
            nat_t codepoint = TO_NAT(token);
            XDEBUG(fprintf(stderr, "peg_class_beh codepoint=16#%02"PRIxPTR" (%"PRIdPTR")\n",
                codepoint, codepoint));
            if (codepoint < 0x80) {
                nat_t mask = TO_NAT(class);
                XDEBUG(fprintf(stderr, "peg_class_beh mask=16#%02"PRIxPTR" class=16#%02"PRIxPTR"\n",
                    mask, NAT(char_class[codepoint])));
                if (NAT(char_class[codepoint]) & mask) {
                    int_t k_next = CREATE(MK_PROC(peg_k_next), cons(ok, token));
                    SEND(next, k_next);  // advance
                    return OK;
                }
            }
        }
    }
    SEND(fail, resume);  // fail
    return OK;
}

static int in_set(int_t set, int_t val) {
    XDEBUG(debug_print("in_set set", set));
    XDEBUG(debug_print("in_set val", val));
    if (!IS_NUM(val)) return 0;  // number token required
    int_t n = TO_INT(val);
    while (IS_PAIR(set)) {
        int_t item = car(set);
        XDEBUG(debug_print("in_set item", item));
        if (IS_PAIR(item)) {
            int_t lo = car(item);
            if (!IS_NUM(lo)) return 0;  // number pattern required
            int_t hi = cdr(item);
            if (!IS_NUM(hi)) return 0;  // number pattern required
            lo = TO_INT(lo);
            hi = TO_INT(hi);
            if ((lo <= n) && (n <= hi)) return 1;  // match!
        } else {
            if (!IS_NUM(item)) return 0;  // number pattern required
            int_t m = TO_INT(item);
            if (m == n) return 1;  // match!
        }
        set = cdr(set);
    }
    XDEBUG(debug_print("in_set tail", set));
    if (set != NIL) {
        if (!IS_NUM(set)) return 0;  // number pattern required
        int_t m = TO_INT(set);
        if (m == n) return 1;  // match!
    }
    return 0;  // fail
}
PROC_DECL(peg_in_set_beh) {
    PTRACE(debug_print("peg_in_set_beh self", self));
    GET_VARS();  // set
    PTRACE(debug_print("peg_in_set_beh vars", vars));
    TAIL_VAR(set);
    GET_ARGS();  // (custs value . in) = ((ok . fail) value . (token . next))
    PTRACE(debug_print("peg_in_set_beh args", args));
    POP_ARG(custs);  // (ok . fail)
    int_t resume = args;  // (value . in)
    POP_ARG(_value);
    TAIL_ARG(in);  // (token . next) -or- NIL
    int_t ok = car(custs);
    int_t fail = cdr(custs);
    if (in != NIL) {
        int_t token = car(in);
        int_t next = cdr(in);
        if (in_set(set, token)) {
            int_t k_next = CREATE(MK_PROC(peg_k_next), cons(ok, token));
            SEND(next, k_next);  // advance
            return OK;
        }
    }
    SEND(fail, resume);  // fail
    return OK;
}

static PROC_DECL(peg_or_fail_beh) {
    PTRACE(debug_print("peg_or_fail_beh self", self));
    GET_VARS();  // (rest . restart)
    PTRACE(debug_print("peg_or_fail_beh vars", vars));
    POP_VAR(rest);
    TAIL_VAR(restart);  // (custs value . in)
    SEND(rest, restart);
    return OK;
}
PROC_DECL(peg_or_beh) {
    PTRACE(debug_print("peg_or_beh self", self));
    GET_VARS();  // (first . rest)
    PTRACE(debug_print("peg_or_beh vars", vars));
    POP_VAR(first);
    TAIL_VAR(rest);
    GET_ARGS();  // (custs value . in) = ((ok . fail) value . (token . next))
    PTRACE(debug_print("peg_or_beh args", args));
    int_t restart = args;  // (custs value . in)
    POP_ARG(custs);  // (ok . fail)
    int_t resume = args;  // (value . in)
    POP_ARG(value);
    TAIL_ARG(in);  // (token . next) -or- NIL
    int_t ok = car(custs);
    //int_t fail = cdr(custs);
    int_t or_fail = CREATE(MK_PROC(peg_or_fail_beh), cons(rest, restart));
    SEND(first, cons(cons(ok, or_fail), resume));
    return OK;
}

static PROC_DECL(peg_and_pair_beh) {
    PTRACE(debug_print("peg_and_pair_beh self", self));
    GET_VARS();  // (cust . head)
    PTRACE(debug_print("peg_and_pair_beh vars", vars));
    POP_VAR(cust);
    TAIL_VAR(head);
    GET_ARGS();  // (value . in)
    PTRACE(debug_print("peg_and_pair_beh args", args));
    POP_ARG(tail);
    TAIL_ARG(in);  // (token . next) -or- NIL
    SEND(cust, cons(cons(head, tail), in));
    return OK;
}
static PROC_DECL(peg_and_ok_beh) {
    PTRACE(debug_print("peg_and_ok_beh self", self));
    GET_VARS();  // (rest . (ok . and_fail))
    PTRACE(debug_print("peg_and_ok_beh vars", vars));
    POP_VAR(rest);
    POP_VAR(ok);
    TAIL_VAR(and_fail);
    GET_ARGS();  // (value . in)
    PTRACE(debug_print("peg_and_ok_beh args", args));
    int_t resume = args;  // (value . in)
    POP_ARG(value);
    TAIL_ARG(_in);  // (token . next) -or- NIL
    int_t and_pair = CREATE(MK_PROC(peg_and_pair_beh), cons(ok, value));
    SEND(rest, cons(cons(and_pair, and_fail), resume));
    return OK;
}
static PROC_DECL(peg_and_fail_beh) {
    PTRACE(debug_print("peg_and_fail_beh self", self));
    GET_VARS();  // (fail . resume)
    PTRACE(debug_print("peg_and_fail_beh vars", vars));
    POP_VAR(fail);
    TAIL_VAR(resume);  // (value . in)
    SEND(fail, resume);
    return OK;
}
PROC_DECL(peg_and_beh) {
    PTRACE(debug_print("peg_and_beh self", self));
    GET_VARS();  // (first . rest)
    PTRACE(debug_print("peg_and_beh vars", vars));
    POP_VAR(first);
    TAIL_VAR(rest);
    GET_ARGS();  // (custs value . in) = ((ok . fail) value . (token . next))
    PTRACE(debug_print("peg_and_beh args", args));
    POP_ARG(custs);  // (ok . fail)
    int_t resume = args;  // (value . in)
    POP_ARG(value);
    TAIL_ARG(in);  // (token . next) -or- NIL
    int_t ok = car(custs);
    int_t fail = cdr(custs);
    int_t and_fail = CREATE(MK_PROC(peg_and_fail_beh), cons(fail, resume));
    int_t and_ok = CREATE(MK_PROC(peg_and_ok_beh), cons(rest, cons(ok, and_fail)));
    SEND(first, cons(cons(and_ok, fail), resume));
    return OK;
}

PROC_DECL(peg_alt_beh) {
    PTRACE(debug_print("peg_alt_beh self", self));
    GET_VARS();  // ptrns
    PTRACE(debug_print("peg_alt_beh vars", vars));
    TAIL_VAR(ptrns);
    if (IS_PAIR(ptrns)) {
        int_t head = car(ptrns);
        int_t tail = cdr(ptrns);
        int_t rest = (IS_PAIR(tail)
            ? CREATE(MK_PROC(peg_alt_beh), tail)
            : MK_ACTOR(&peg_fail));
        BECOME(MK_PROC(peg_or_beh), cons(head, rest));
    } else if (ptrns == NIL) {
        BECOME(MK_PROC(peg_fail_beh), UNDEF);
    } else {
        return error("proper list required");
    }
    SEND(self, arg);  // re-send original message
    return OK;
}

PROC_DECL(peg_seq_beh) {
    PTRACE(debug_print("peg_seq_beh self", self));
    GET_VARS();  // ptrns
    PTRACE(debug_print("peg_seq_beh vars", vars));
    TAIL_VAR(ptrns);
    if (IS_PAIR(ptrns)) {
        int_t head = car(ptrns);
        int_t tail = cdr(ptrns);
        int_t rest = (IS_PAIR(tail)
            ? CREATE(MK_PROC(peg_seq_beh), tail)
            : MK_ACTOR(&peg_empty));
        BECOME(MK_PROC(peg_and_beh), cons(head, rest));
    } else if (ptrns == NIL) {
        BECOME(MK_PROC(peg_empty_beh), UNDEF);
    } else {
        return error("proper list required");
    }
    SEND(self, arg);  // re-send original message
    return OK;
}

PROC_DECL(peg_opt_beh) {
    PTRACE(debug_print("peg_opt_beh self", self));
    GET_VARS();  // ptrn
    PTRACE(debug_print("peg_opt_beh vars", vars));
    TAIL_VAR(ptrn);
    // Note: we want either a single-element list or an empty list
    ptrn = CREATE(MK_PROC(peg_and_beh), cons(ptrn, MK_ACTOR(&peg_empty)));
    BECOME(MK_PROC(peg_or_beh), cons(ptrn, MK_ACTOR(&peg_empty)));
    SEND(self, arg);  // re-send original message
    return OK;
}

PROC_DECL(peg_star_beh) {
    PTRACE(debug_print("peg_star_beh self", self));
    GET_VARS();  // ptrn
    PTRACE(debug_print("peg_star_beh vars", vars));
    TAIL_VAR(ptrn);
    ptrn = CREATE(MK_PROC(peg_and_beh), cons(ptrn, self));
    BECOME(MK_PROC(peg_or_beh), cons(ptrn, MK_ACTOR(&peg_empty)));
    SEND(self, arg);  // re-send original message
    return OK;
}

PROC_DECL(peg_plus_beh) {
    PTRACE(debug_print("peg_plus_beh self", self));
    GET_VARS();  // ptrn
    PTRACE(debug_print("peg_plus_beh vars", vars));
    TAIL_VAR(ptrn);
    int_t reps = CREATE(MK_PROC(peg_star_beh), ptrn);
    BECOME(MK_PROC(peg_and_beh), cons(ptrn, reps));
    SEND(self, arg);  // re-send original message
    return OK;
}

static PROC_DECL(peg_k_not_beh) {
    PTRACE(debug_print("peg_k_not_beh self", self));
    GET_VARS();  // (cust . in)
    PTRACE(debug_print("peg_k_not_beh vars", vars));
    POP_VAR(cust);
    TAIL_VAR(in);
    SEND(cust, cons(UNIT, in));
    return OK;
}
PROC_DECL(peg_not_beh) {
    PTRACE(debug_print("peg_not_beh self", self));
    GET_VARS();  // ptrn
    PTRACE(debug_print("peg_not_beh vars", vars));
    TAIL_VAR(ptrn);
    GET_ARGS();  // (custs value . in) = ((ok . fail) value . (token . next))
    PTRACE(debug_print("peg_not_beh args", args));
    POP_ARG(custs);  // (ok . fail)
    TAIL_ARG(resume);  // (value . in)
    int_t ok = car(custs);
    int_t fail = cdr(custs);
    int_t in = cdr(resume);
    int_t not_ok = CREATE(MK_PROC(peg_k_not_beh), cons(fail, in));
    int_t not_fail = CREATE(MK_PROC(peg_k_not_beh), cons(ok, in));
    SEND(ptrn, cons(cons(not_ok, not_fail), resume));
    return OK;
}

PROC_DECL(peg_peek_beh) {
    PTRACE(debug_print("peg_peek_beh self", self));
    GET_VARS();  // ptrn
    PTRACE(debug_print("peg_peek_beh vars", vars));
    TAIL_VAR(ptrn);
    int_t not = CREATE(MK_PROC(peg_not_beh), ptrn);
    BECOME(MK_PROC(peg_not_beh), not);
    SEND(self, arg);  // re-send original message
    return OK;
}

static PROC_DECL(peg_k_call_beh) {
    PTRACE(debug_print("peg_k_call_beh self", self));
    GET_VARS();  // req
    PTRACE(debug_print("peg_k_call_beh vars", vars));
    TAIL_VAR(req);
    GET_ARGS();  // ptrn
    PTRACE(debug_print("peg_k_call_beh args", args));
    TAIL_ARG(ptrn);
    if (ptrn == UNDEF) {
        WARN(debug_print("peg_k_call_beh rule not found", ptrn));
        ptrn = MK_ACTOR(&peg_fail);  // if rule not found, fail
    }
    SEND(ptrn, req);  // fwd original req to ptrn
    return OK;
}
// FIXME: consider BECOMEing fwd to ptrn on first lookup (lazy cache)
PROC_DECL(peg_call_beh) {
    PTRACE(debug_print("peg_call_beh self", self));
    GET_VARS();  // (name . scope)
    PTRACE(debug_print("peg_call_beh vars", vars));
    POP_VAR(name);
    TAIL_VAR(scope);
    GET_ARGS();  // (custs value . in) = ((ok . fail) value . (token . next))
    PTRACE(debug_print("peg_call_beh args", args));
    TAIL_ARG(req);
    int_t k_call = CREATE(MK_PROC(peg_k_call_beh), req);
    SEND(scope, list_3(k_call, s_lookup, name));
    return OK;
}

#endif // PEG_ACTORS

PROC_DECL(Global) {
    XDEBUG(debug_print("Global self", self));
    GET_ARGS();
    XDEBUG(debug_print("Global args", args));
    POP_ARG(cust);
    POP_ARG(req);
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
#endif
        } else {
            WARN(debug_print("Global lookup failed", symbol));
            value = error("undefined variable");
        }
        DEBUG(debug_print("Global value", value));
        SEND(cust, value);
        return OK;
    }
    return SeType(self, arg);  // delegate to SeType
}
// WARNING! gnd_locals.head must be part of the gc root set!
const cell_t a_global_env = { .head = MK_PROC(Global), .tail = UNDEF };
static cell_t gnd_locals = { .head = NIL, .tail = MK_ACTOR(&a_global_env) };
const cell_t a_ground_env = { .head = MK_PROC(Scope), .tail = MK_PAIR(&gnd_locals) };

/*
 * display procedures
 */

void print(int_t value) {
    if (value == FREE_CELL) {
        printf("#FREE-CELL");
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
    } else if (IS_PROC(value)) {
        proc_t f = TO_PROC(value);
        printf("#proc-%"PRIxPTR"", NAT(f));
    } else if (IS_NUM(value)) {
        printf("%+"PRIdPTR"", TO_INT(value));
    } else if (IS_SYM(value)) {
        char *s = &intern[TO_ENUM(value)];
        printf("%.*s", (int)(*s), (s + 1));
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
    fflush(stdout);
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
        nat_t n = NAT(TO_ENUM(value));
        fprintf(stderr, " SYM[%"PRIuPTR"]", n);
    }
    if (IS_ACTOR(value)) fprintf(stderr, " ACTOR");
    if (IS_ADDR(value)) {
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
        fprintf(stdout, "wart> ");  // REPL prompt
        fflush(stdout);

        // read expr to evaluate
        int_t expr = read_sexpr(in);
        if (expr == FAIL) break;
        DEBUG(debug_print("  REPL expr", expr));

        // evalute expr and print result
        int_t cust = MK_ACTOR(&a_println);
        int_t env = GROUND_ENV;
        SEND(expr, list_3(cust, s_eval, env));

        // dispatch all pending events
        event_loop();
    }
    newline();
    return OK;
}

int_t load_file(input_t *in) {
    while (1) {
        int_t expr = read_sexpr(in);
        if (expr == FAIL) break;

        // evalute each expr in file
        int_t cust = SINK;
        int_t env = GROUND_ENV;
        SEND(expr, list_3(cust, s_eval, env));

        // dispatch all pending events
        event_loop();
    }
    return OK;
}

#if PEG_ACTORS
PROC_DECL(input_resolved_beh) {
    PTRACE(debug_print("input_resolved_beh self", self));
    GET_VARS();  // (token . next) -or- NIL
    PTRACE(debug_print("input_resolved_beh vars", vars));
    TAIL_VAR(in);
    GET_ARGS();  // cust
    PTRACE(debug_print("input_resolved_beh args", args));
    TAIL_ARG(cust);
    SEND(cust, in);
    return OK;
}
PROC_DECL(input_promise_beh) {
    PTRACE(debug_print("input_promise_beh self", self));
    GET_VARS();  // input
    PTRACE(debug_print("input_promise_beh vars", vars));
    TAIL_VAR(input);
    GET_ARGS();  // cust
    PTRACE(debug_print("input_promise_beh args", args));
    TAIL_ARG(cust);
    input_t *in = TO_PTR(input);
    i32 ch = (in->get)(in);  // read
    if (ch < 0) {
        BECOME(MK_PROC(&input_resolved_beh), NIL);
    } else {
        (in->next)(in, 1);  // advance
        int_t next = CREATE(MK_PROC(&input_promise_beh), input);
        BECOME(MK_PROC(&input_resolved_beh), cons(MK_NUM(ch), next));
    }
    SEND(self, arg);  // re-send original request to resolved promise
    return OK;
}

PROC_DECL(peg_result_beh) {
    XDEBUG(debug_print("peg_result_beh self", self));
    GET_VARS();  // label
    XDEBUG(debug_print("peg_result_beh vars", vars));
    TAIL_VAR(label);
    GET_ARGS();  // result
    XDEBUG(debug_print("peg_result_beh args", args));
    TAIL_ARG(result);
    print(label);
    fflush(stdout);
    debug_print("", result);
    return OK;
}

PROC_DECL(peg_start_beh) {
    PTRACE(debug_print("peg_start_beh self", self));
    GET_VARS();  // ((ok . fail) . ptrn)
    PTRACE(debug_print("peg_start_beh vars", vars));
    POP_VAR(custs);
    TAIL_VAR(ptrn);
    GET_ARGS();  // (token . next) -or- NIL
    PTRACE(debug_print("peg_start_beh args", args));
    TAIL_ARG(in);
    SEND(ptrn, cons(custs, cons(UNDEF, in)));
    return OK;
}

int_t test_parsing() {
    WARN(fprintf(stderr, "--test_parsing--\n"));
    //char nstr_buf[] = { 0 };  // ""
    //char nstr_buf[] = { 1, 48 };  // "0"
    //char nstr_buf[] = { 2, 48, 49 };  // "01"
    //char nstr_buf[] = { 3, 48, 49, 10 };  // "01\n"
    char nstr_buf[] = { 4, 48, 49, 48, 10 };  // "010\n"
    nstr_in_t str_in;
    int_t src;
    int_t ptrn;
    int_t ptrn_0;
    int_t ptrn_1;
    int_t start;
    int_t env;
    int_t scope;

    int_t ok = CREATE(MK_PROC(&peg_result_beh), symbol("ok"));
    int_t fail = CREATE(MK_PROC(&peg_result_beh), symbol("fail"));

    ASSERT(nstr_init(&str_in, nstr_buf) == 0);
    src = CREATE(MK_PROC(&input_promise_beh), MK_ACTOR(&str_in));
    //ptrn = MK_ACTOR(&peg_empty);
    //ptrn = MK_ACTOR(&peg_fail);
    //ptrn = MK_ACTOR(&peg_any);
    ptrn = CREATE(MK_PROC(&peg_eq_beh), MK_NUM(48));
    //ptrn = CREATE(MK_PROC(&peg_eq_beh), MK_NUM(49));
    start = CREATE(MK_PROC(&peg_start_beh), cons(cons(ok, fail), ptrn));
    SEND(src, start);
    event_loop();

    ASSERT(nstr_init(&str_in, nstr_buf) == 0);
    src = CREATE(MK_PROC(&input_promise_beh), MK_ACTOR(&str_in));
    ptrn_0 = CREATE(MK_PROC(&peg_eq_beh), MK_NUM(48));
    ptrn_1 = CREATE(MK_PROC(&peg_eq_beh), MK_NUM(49));
    //ptrn = CREATE(MK_PROC(&peg_or_beh), cons(ptrn_0, ptrn_1));
    ptrn = CREATE(MK_PROC(&peg_or_beh), cons(ptrn_1, ptrn_0));
    start = CREATE(MK_PROC(&peg_start_beh), cons(cons(ok, fail), ptrn));
    SEND(src, start);
    event_loop();

    ASSERT(nstr_init(&str_in, nstr_buf) == 0);
    src = CREATE(MK_PROC(&input_promise_beh), MK_ACTOR(&str_in));
    ptrn_0 = CREATE(MK_PROC(&peg_eq_beh), MK_NUM(48));
    ptrn_1 = CREATE(MK_PROC(&peg_eq_beh), MK_NUM(49));
    ptrn = CREATE(MK_PROC(&peg_and_beh), cons(ptrn_0, ptrn_1));
    //ptrn = CREATE(MK_PROC(&peg_and_beh), cons(ptrn_1, ptrn_0));
    //ptrn = CREATE(MK_PROC(&peg_and_beh), cons(ptrn_0, ptrn_0));
    //ptrn = CREATE(MK_PROC(&peg_and_beh), cons(ptrn_0, MK_ACTOR(&peg_empty)));
    start = CREATE(MK_PROC(&peg_start_beh), cons(cons(ok, fail), ptrn));
    SEND(src, start);
    event_loop();

    ASSERT(nstr_init(&str_in, nstr_buf) == 0);
    src = CREATE(MK_PROC(&input_promise_beh), MK_ACTOR(&str_in));
    ptrn_0 = CREATE(MK_PROC(&peg_eq_beh), MK_NUM(48));
    ptrn_1 = CREATE(MK_PROC(&peg_eq_beh), MK_NUM(49));
    ptrn = CREATE(MK_PROC(&peg_or_beh), cons(ptrn_0, ptrn_1));
    int_t loop = cons(ptrn, UNDEF);
    ptrn_0 = CREATE(MK_PROC(&peg_and_beh), loop);
    ptrn_1 = CREATE(MK_PROC(&peg_or_beh), cons(ptrn_0, MK_ACTOR(&peg_empty)));
    set_cdr(loop, ptrn_1);
    start = CREATE(MK_PROC(&peg_start_beh), cons(cons(ok, fail), ptrn_1));
    SEND(src, start);
    event_loop();

    ASSERT(nstr_init(&str_in, nstr_buf) == 0);
    src = CREATE(MK_PROC(&input_promise_beh), MK_ACTOR(&str_in));
    ptrn = CREATE(MK_PROC(&peg_alt_beh), list_5(
        CREATE(MK_PROC(&peg_eq_beh), MK_NUM(52)),
        CREATE(MK_PROC(&peg_eq_beh), MK_NUM(51)),
        CREATE(MK_PROC(&peg_eq_beh), MK_NUM(50)),
        CREATE(MK_PROC(&peg_eq_beh), MK_NUM(49)),
        CREATE(MK_PROC(&peg_eq_beh), MK_NUM(48))
    ));
    start = CREATE(MK_PROC(&peg_start_beh), cons(cons(ok, fail), ptrn));
    SEND(src, start);
    event_loop();

    ASSERT(nstr_init(&str_in, nstr_buf) == 0);
    src = CREATE(MK_PROC(&input_promise_beh), MK_ACTOR(&str_in));
    ptrn = CREATE(MK_PROC(&peg_seq_beh), list_5(
        CREATE(MK_PROC(&peg_eq_beh), MK_NUM(48)),
        CREATE(MK_PROC(&peg_opt_beh),
            CREATE(MK_PROC(&peg_eq_beh), MK_NUM(49))),
        CREATE(MK_PROC(&peg_opt_beh),
            CREATE(MK_PROC(&peg_eq_beh), MK_NUM(49))),
        CREATE(MK_PROC(&peg_eq_beh), MK_NUM(48)),
        CREATE(MK_PROC(&peg_eq_beh), MK_NUM(10))
    ));
    start = CREATE(MK_PROC(&peg_start_beh), cons(cons(ok, fail), ptrn));
    SEND(src, start);
    event_loop();

    ASSERT(nstr_init(&str_in, nstr_buf) == 0);
    src = CREATE(MK_PROC(&input_promise_beh), MK_ACTOR(&str_in));
    ptrn_0 = CREATE(MK_PROC(&peg_eq_beh), MK_NUM(48));
    ptrn_1 = CREATE(MK_PROC(&peg_eq_beh), MK_NUM(49));
    ptrn = CREATE(MK_PROC(&peg_or_beh), cons(ptrn_0, ptrn_1));
    ptrn = CREATE(MK_PROC(&peg_star_beh), ptrn);
    ptrn = CREATE(MK_PROC(&peg_seq_beh), list_2(ptrn, ptrn));
    start = CREATE(MK_PROC(&peg_start_beh), cons(cons(ok, fail), ptrn));
    SEND(src, start);
    event_loop();

    ASSERT(nstr_init(&str_in, nstr_buf) == 0);
    src = CREATE(MK_PROC(&input_promise_beh), MK_ACTOR(&str_in));
    ptrn_0 = CREATE(MK_PROC(&peg_eq_beh), MK_NUM(48));
    ptrn_0 = CREATE(MK_PROC(&peg_plus_beh), ptrn_0);
    ptrn_1 = CREATE(MK_PROC(&peg_alt_beh), list_2(
        CREATE(MK_PROC(&peg_eq_beh), MK_NUM(49)),
        CREATE(MK_PROC(&peg_eq_beh), MK_NUM(48))
    ));
    ptrn_1 = CREATE(MK_PROC(&peg_plus_beh), ptrn_1);
    ptrn = CREATE(MK_PROC(&peg_seq_beh), list_2(ptrn_0, ptrn_1));
    start = CREATE(MK_PROC(&peg_start_beh), cons(cons(ok, fail), ptrn));
    SEND(src, start);
    event_loop();

    ASSERT(nstr_init(&str_in, nstr_buf) == 0);
    src = CREATE(MK_PROC(&input_promise_beh), MK_ACTOR(&str_in));
    ptrn_0 = CREATE(MK_PROC(&peg_in_set_beh), list_1(cons(MK_NUM(48), MK_NUM(57))));  // [0-9]
    ptrn_1 = CREATE(MK_PROC(&peg_eq_beh), MK_NUM(10));
    ptrn_1 = CREATE(MK_PROC(&peg_not_beh), ptrn_1);
    ptrn_1 = CREATE(MK_PROC(&peg_and_beh), cons(ptrn_0, ptrn_1));
    ptrn = CREATE(MK_PROC(&peg_star_beh), ptrn_1);
    start = CREATE(MK_PROC(&peg_start_beh), cons(cons(ok, fail), ptrn));
    SEND(src, start);
    event_loop();

    ASSERT(nstr_init(&str_in, nstr_buf) == 0);
    src = CREATE(MK_PROC(&input_promise_beh), MK_ACTOR(&str_in));
    ptrn_0 = CREATE(MK_PROC(&peg_star_beh), MK_ACTOR(&peg_any));
    ptrn_1 = CREATE(MK_PROC(&peg_peek_beh), MK_ACTOR(&peg_empty));
    ptrn = CREATE(MK_PROC(&peg_seq_beh), list_2(ptrn_0, ptrn_1));
    start = CREATE(MK_PROC(&peg_start_beh), cons(cons(ok, fail), ptrn));
    SEND(src, start);
    event_loop();

    /*
     * test-case "0\t-12\r +369\n"
     */
    char nstr_buf2[] = { 12, 48, 9, 45, 49, 50, 13, 32, 43, 51, 54, 57, 10 };
    ASSERT(nstr_init(&str_in, nstr_buf2) == 0);
    src = CREATE(MK_PROC(&input_promise_beh), MK_ACTOR(&str_in));
    ptrn_0 = CREATE(MK_PROC(&peg_in_set_beh), cons(MK_NUM(43), MK_NUM(45)));  // [+-]
    ptrn_0 = CREATE(MK_PROC(&peg_opt_beh), ptrn_0);
    ptrn_1 = CREATE(MK_PROC(&peg_class_beh), MK_NUM(DGT));  // digit
    ptrn_1 = CREATE(MK_PROC(&peg_plus_beh), ptrn_1);
    ptrn = CREATE(MK_PROC(&peg_in_set_beh), list_2(cons(MK_NUM(9), MK_NUM(13)), MK_NUM(32)));  // [\t-\r ]
    ptrn = CREATE(MK_PROC(&peg_star_beh), ptrn);
    ptrn = CREATE(MK_PROC(&peg_seq_beh), list_3(ptrn, ptrn_0, ptrn_1));
    ptrn = CREATE(MK_PROC(&peg_star_beh), ptrn);  // ([\t-\r ]*[+-]?[0-9]+)*
    start = CREATE(MK_PROC(&peg_start_beh), cons(cons(ok, fail), ptrn));
    SEND(src, start);
    event_loop();

    // opt_wsp: (star (class WSP))
    //ptrn = CREATE(MK_PROC(&peg_in_set_beh), list_2(cons(MK_NUM(9), MK_NUM(13)), MK_NUM(32)));  // [\t-\r ]
    ptrn = CREATE(MK_PROC(&peg_class_beh), MK_NUM(WSP));
    int_t opt_wsp = CREATE(MK_PROC(&peg_star_beh), ptrn);

    // digits: (plus (class DGT))
    ptrn = CREATE(MK_PROC(&peg_class_beh), MK_NUM(DGT));
    int_t digits = CREATE(MK_PROC(&peg_plus_beh), ptrn);

    ASSERT(nstr_init(&str_in, nstr_buf2) == 0);
    src = CREATE(MK_PROC(&input_promise_beh), MK_ACTOR(&str_in));
    ptrn_0 = CREATE(MK_PROC(&peg_in_set_beh), cons(MK_NUM(43), MK_NUM(45)));  // [+-]
    ptrn_0 = CREATE(MK_PROC(&peg_opt_beh), ptrn_0);
    ptrn = CREATE(MK_PROC(&peg_seq_beh), list_3(opt_wsp, ptrn_0, digits));
    ptrn = CREATE(MK_PROC(&peg_star_beh), ptrn);  // (star (seq opt_wsp opt_sign digits))
    start = CREATE(MK_PROC(&peg_start_beh), cons(cons(ok, fail), ptrn));
    SEND(src, start);
    event_loop();

    /*
     * test-case "(CAR ( LIST 0 1)\t)"
     */
    char nstr_buf3[] = { 18, 40, 67, 65, 82, 32, 40, 32, 76, 73, 83, 84, 32, 48, 32, 49, 41, 9, 41 };
    ASSERT(nstr_init(&str_in, nstr_buf3) == 0);
    src = CREATE(MK_PROC(&input_promise_beh), MK_ACTOR(&str_in));
    env = cons(NIL, NIL);  // empty env, no parent
    scope = CREATE(MK_PROC(Scope), env);  // rule scope
    // list: (seq (eq 40) (star sexpr) _ (eq 41))
    ptrn_0 = CREATE(MK_PROC(&peg_call_beh), cons(symbol("sexpr"), scope));
    ptrn = CREATE(MK_PROC(&peg_seq_beh), list_4(
        CREATE(MK_PROC(&peg_eq_beh), MK_NUM(40)),
        CREATE(MK_PROC(&peg_star_beh), ptrn_0),
        CREATE(MK_PROC(&peg_call_beh), cons(symbol("_"), scope)),
        CREATE(MK_PROC(&peg_eq_beh), MK_NUM(41))));
    set_car(env, cons(cons(symbol("list"), ptrn), car(env)));
    // number: (plus (class DGT))
    ptrn = CREATE(MK_PROC(&peg_class_beh), MK_NUM(DGT));
    ptrn = CREATE(MK_PROC(&peg_plus_beh), ptrn);
    set_car(env, cons(cons(symbol("number"), ptrn), car(env)));
    // symbol: (plus (class DGT LWR UPR SYM))
    ptrn = CREATE(MK_PROC(&peg_class_beh), MK_NUM(DGT|LWR|UPR|SYM));
    ptrn = CREATE(MK_PROC(&peg_plus_beh), ptrn);
    set_car(env, cons(cons(symbol("symbol"), ptrn), car(env)));
    // _: (star (class WSP))
    ptrn = CREATE(MK_PROC(&peg_class_beh), MK_NUM(WSP));
    ptrn = CREATE(MK_PROC(&peg_star_beh), ptrn);
    set_car(env, cons(cons(symbol("_"), ptrn), car(env)));
    // sexpr: (seq _ (alt list number symbol))
    ptrn = CREATE(MK_PROC(&peg_call_beh), cons(symbol("list"), scope));
    ptrn_0 = CREATE(MK_PROC(&peg_call_beh), cons(symbol("number"), scope));
    ptrn_1 = CREATE(MK_PROC(&peg_call_beh), cons(symbol("symbol"), scope));
    ptrn = CREATE(MK_PROC(&peg_alt_beh), list_3(ptrn, ptrn_0, ptrn_1));
    //ptrn_0 = CREATE(MK_PROC(&peg_call_beh), cons(symbol("_"), scope));
    //ptrn_0 = CREATE(MK_PROC(&peg_call_beh), cons(symbol("opt_wsp"), scope));
    ptrn_0 = opt_wsp;
    ptrn = CREATE(MK_PROC(&peg_seq_beh), list_2(ptrn_0, ptrn));
    set_car(env, cons(cons(symbol("sexpr"), ptrn), car(env)));
    DEBUG(debug_print("test_parsing sepxr env", env));
    // parse test-case
    start = CREATE(MK_PROC(&peg_start_beh), cons(cons(ok, fail), ptrn));
    SEND(src, start);
    event_loop();

    return OK;
}
#endif // PEG_ACTORS

/*
 * unit tests
 */

int_t eval_test_expr(int_t expr, int_t expect) {
    int_t cust = CREATE(MK_PROC(assert_beh), expect);
    SEND(expr, list_3(cust, s_eval, GROUND_ENV));
    // dispatch all pending events
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

#if RUN_SELF_TEST
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

    v2 = CREATE(MK_PROC(sink_beh), v1);
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
    int_t a = CREATE(MK_PROC(sink_beh), NIL);
    XDEBUG(debug_print("test_actors CREATE", a));
    int_t m = list_3(SINK, s_eval, NIL);
    XDEBUG(debug_print("test_actors message", m));
    SEND(a, m);
    // dispatch one event
    int_t r = event_dispatch();
    DEBUG(debug_print("test_actors event_dispatch", r));
    if (r != OK) return r;  // early exit on failure...

#if 1
    // UNIT is self-evaluating
    a = CREATE(MK_PROC(assert_beh), UNIT);
    m = list_3(a, s_eval, NIL);
    DEBUG(debug_print("test_actors m_1", m));
    SEND(UNIT, m);
    // UNIT has Unit type
    a = CREATE(MK_PROC(assert_beh), TRUE);
    m = list_3(a, s_typeq, MK_PROC(Unit));
    DEBUG(debug_print("test_actors m_2", m));
    SEND(UNIT, m);
    // dispatch all pending events
    r = event_loop();
    DEBUG(debug_print("test_actors event_loop", r));
#endif

    int_t usage = cell_usage();
    return OK;
}

int_t test_eval() {
    WARN(fprintf(stderr, "--test_eval--\n"));
    int_t cust;
    int_t expr;
    int_t env = GROUND_ENV;
    int_t result;
    int_t s_foo = symbol("foo");
    int_t s_bar = symbol("bar");
    int_t s_baz = symbol("baz");

    cust = CREATE(MK_PROC(assert_beh), s_foo);
    //expr = list_2(MK_ACTOR(&a_quote), s_foo);  // (<quote> foo)
    expr = list_2(s_quote, s_foo);  // (quote foo)
    SEND(expr, list_3(cust, s_eval, env));
    // dispatch all pending events
    result = event_loop();
    DEBUG(debug_print("test_eval event_loop", result));

    ASSERT(list_3(UNIT, s_foo, s_bar) != list_3(UNIT, s_foo, s_bar));
    ASSERT(equal(list_3(UNIT, s_foo, s_bar), list_3(UNIT, s_foo, s_bar)));
    ASSERT(!equal(TRUE, FALSE));

#if 0
    DEBUG(debug_print("test_eval a_list", MK_ACTOR(&a_list)));
    DEBUG(debug_print("test_eval oper_list", MK_ACTOR(&oper_list)));
    DEBUG(debug_print("test_eval Oper_prim", MK_PROC(Oper_prim)));
    DEBUG(debug_print("test_eval prim_list", MK_PROC(prim_list)));
    DEBUG(debug_print("test_eval prim_cons", MK_PROC(prim_cons)));
#endif
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
        "(eq? () '() (quote ()))",
        "#t"
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
    ASSERT(test_values() == OK);
    ASSERT(test_cells() == OK);
    ASSERT(test_actors() == OK);
    ASSERT(test_eval() == OK);
#if PEG_ACTORS
    ASSERT(test_parsing() == OK);
#endif // PEG_ACTORS
    return OK;
}
#endif // RUN_SELF_TEST

/*
 * bootstrap
 */

int_t actor_boot() {
    ASSERT(symbol_boot() == OK);
    return OK;
}

int top_level_eval(char *expr) {
    // Note: this will panic() if the result is not UNIT
    return eval_test_cstr(expr, "#unit");
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
    int_t result = OK;

    ASSERT(actor_boot() == OK);

#if RUN_SELF_TEST
    clock_t t0 = clock();
    clock_t t1 = clock();
    double dt = (double)(t1 - t0) / CLOCKS_PER_SEC;
    printf("t0=%ld t1=%ld dt=%.9f (%ld CPS)\n", t0, t1, dt, (long)CLOCKS_PER_SEC);

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

    // run unit tests
    ASSERT(unit_tests() == OK);
#endif // RUN_SELF_TEST

#if RUN_FILE_REPL
    // load internal library
    ASSERT(load_library() == OK);

    WARN(fprintf(stderr, "--load_file--\n"));
    printf("argc = %d\n", argc);
    for (int i = 1; i < argc; ++i) {
        printf("argv[%d] = %s\n", i, argv[i]);
        FILE *f = fopen(argv[i], "r");
        if (f) {
            file_in_t file_in;
            ASSERT(file_init(&file_in, f) == 0);
            ASSERT(load_file(&file_in.input) == OK);
        }
        fclose(f);
    }

    int_t usage = cell_usage();
    int freed = gc_mark_and_sweep();
    WARN(printf("main: gc reclaimed %d cells\n", freed));

    file_in_t std_in;
    ASSERT(file_init(&std_in, stdin) == 0);
    result = read_eval_print_loop(&std_in.input);
#endif // RUN_FILE_REPL

    return (result == OK ? 0 : 1);
}

int is_proc(int_t val) {
    return IS_ACTOR(val) && (TO_PTR(val) >= PTR(newline)) && (TO_PTR(val) <= PTR(main));
}
