/*
 * ufork.c -- Actor Virtual Machine
 * Copyright 2022 Dale Schumacher
 */

/**
See further [https://github.com/organix/mycelia/blob/master/ufork.md]
**/

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>     // for intptr_t, uintptr_t, uint8_t, uint16_t, etc.
#include <inttypes.h>   // for PRIiPTR, PRIuPTR, PRIxPTR, etc.
#include <time.h>       // for clock_t, clock(), etc.

#define INCLUDE_DEBUG 1 // include debugging facilities
#define RUN_DEBUGGER  1 // run program under interactive debugger
#define EXPLICIT_FREE 1 // explicitly free leaked memory

#if INCLUDE_DEBUG
#define DEBUG(x)    x   // include/exclude debug instrumentation
#define ITRACE(x)       // include/exclude instruction trace
#define XTRACE(x)       // include/exclude execution trace
#else
#define DEBUG(x)        // exclude debug instrumentation
#define ITRACE(x)       // exclude instruction trace
#define XTRACE(x)       // exclude execution trace
#endif

#if EXPLICIT_FREE
#define XFREE(x)    cell_free(x)
#else
#define XFREE(x)    // free removed
#endif

// choose a definition of "machine word" from the following:
#define USE_INT16_T   1 // int16_t from <stdint.h>
#define USE_INT32_T   0 // int32_t from <stdint.h>
#define USE_INT64_T   0 // int64_t from <stdint.h>
#define USE_INTPTR_T  0 // intptr_t from <stdint.h>

#if USE_INT16_T
typedef int16_t int_t;
typedef uint16_t nat_t;
typedef void *ptr_t;
#define PdI PRId16
#define PuI PRIu16
#define PxI PRIx16
#endif
#if USE_INT32_T
typedef int32_t int_t;
typedef uint32_t nat_t;
typedef void *ptr_t;
#define PdI PRId32
#define PuI PRIu32
#define PxI PRIx32
#endif
#if USE_INT64_T
typedef int64_t int_t;
typedef uint64_t nat_t;
typedef void *ptr_t;
#define PdI PRId64
#define PuI PRIu64
#define PxI PRIx64
#endif
#if USE_INTPTR_T
typedef intptr_t int_t;
typedef uintptr_t nat_t;
typedef void *ptr_t;
#define PdI PRIdPTR
#define PuI PRIuPTR
#define PxI PRIxPTR
#endif

#define INT(n) ((int_t)(n))
#define NAT(n) ((nat_t)(n))
#define PTR(n) ((ptr_t)(n))

typedef struct cell {
    int_t       t;      // proc/type (code offset from proc_zero)
    int_t       x;      // head/car  (data offset from cell_zero)
    int_t       y;      // tail/cdr  (data offset from cell_zero)
    int_t       z;      // link/next (data offset from cell_zero)
} cell_t;

#define PROC_DECL(name)  int_t name(int_t self, int_t arg)

typedef PROC_DECL((*proc_t));

// FORWARD DECLARATIONS
int_t panic(char *reason);
int_t error(char *reason);
int_t failure(char *_file_, int _line_);
#if INCLUDE_DEBUG
void hexdump(char *label, int_t *addr, size_t cnt);
void debug_print(char *label, int_t addr);
void continuation_trace();
int_t debugger();
#endif // INCLUDE_DEBUG

#define ASSERT(cond)    if (!(cond)) return failure(__FILE__, __LINE__)

/*
 * native code procedures
 */

// FORWARD DECLARATIONS
PROC_DECL(Undef);
PROC_DECL(Null);
PROC_DECL(Pair);
PROC_DECL(Symbol);
PROC_DECL(Boolean);
PROC_DECL(Unit);
PROC_DECL(Actor);
PROC_DECL(Event);
PROC_DECL(Free);
PROC_DECL(vm_cell);
PROC_DECL(vm_get);
PROC_DECL(vm_set);
PROC_DECL(vm_pair);
PROC_DECL(vm_part);
PROC_DECL(vm_push);
PROC_DECL(vm_depth);
PROC_DECL(vm_drop);
PROC_DECL(vm_pick);
PROC_DECL(vm_dup);
PROC_DECL(vm_alu);
PROC_DECL(vm_eq);
PROC_DECL(vm_cmp);
PROC_DECL(vm_if);
PROC_DECL(vm_msg);
PROC_DECL(vm_act);
PROC_DECL(vm_putc);
PROC_DECL(vm_getc);

#define Undef_T     (-1)
#define Null_T      (-2)
#define Pair_T      (-3)
#define Symbol_T    (-4)
#define Boolean_T   (-5)
#define Unit_T      (-6)
#define Actor_T     (-7)
#define Event_T     (-8)
#define Free_T      (-9)
#define VM_cell     (-10)
#define VM_get      (-11)
#define VM_set      (-12)
#define VM_pair     (-13)
#define VM_part     (-14)
#define VM_push     (-15)
#define VM_depth    (-16)
#define VM_drop     (-17)
#define VM_pick     (-18)
#define VM_dup      (-19)
#define VM_alu      (-20)
#define VM_eq       (-21)
#define VM_cmp      (-22)
#define VM_if       (-23)
#define VM_msg      (-24)
#define VM_act      (-25)
#define VM_putc     (-26)
#define VM_getc     (-27)

#define PROC_MAX    NAT(sizeof(proc_table) / sizeof(proc_t))
proc_t proc_table[] = {
    vm_getc,
    vm_putc,
    vm_act,
    vm_msg,
    vm_if,
    vm_cmp,
    vm_eq,
    vm_alu,
    vm_dup,
    vm_pick,
    vm_drop,
    vm_depth,
    vm_push,
    vm_part,
    vm_pair,
    vm_set,
    vm_get,
    vm_cell,
    Free,  // free-cell marker
    Event,
    Actor,
    Unit,
    Boolean,
    Symbol,
    Pair,
    Null,
    Undef,
};
proc_t *proc_zero = &proc_table[PROC_MAX];  // base for proc offsets

#if INCLUDE_DEBUG
static char *proc_label(int_t proc) {
    static char *label[] = {
        "Undef_T",
        "Null_T",
        "Pair_T",
        "Symbol_T",
        "Boolean_T",
        "Unit_T",
        "Actor_T",
        "Event_T",
        "Free_T",
        "VM_cell",
        "VM_get",
        "VM_set",
        "VM_pair",
        "VM_part",
        "VM_push",
        "VM_depth",
        "VM_drop",
        "VM_pick",
        "VM_dup",
        "VM_alu",
        "VM_eq",
        "VM_cmp",
        "VM_if",
        "VM_msg",
        "VM_act",
        "VM_putc",
        "VM_getc",
    };
    nat_t ofs = NAT(-1 - proc);
    if (ofs < PROC_MAX) return label[ofs];
    return "<unknown>";
}
#endif

int_t call_proc(int_t proc, int_t self, int_t arg) {
    nat_t ofs = NAT(-1 - proc);
    ASSERT(ofs < PROC_MAX);
    XTRACE(debug_print("call_proc self", self));
    int_t result = (proc_zero[proc])(self, arg);
    return result;
}

// VM_get/VM_set fields
#define FLD_T       (0)
#define FLD_X       (1)
#define FLD_Y       (2)
#define FLD_Z       (3)

// VM_alu operations
#define ALU_ADD     (0)
#define ALU_SUB     (1)
#define ALU_MUL     (2)

// VM_cmp relations
#define CMP_EQ      (0)
#define CMP_GE      (1)
#define CMP_GT      (2)
#define CMP_LT      (3)
#define CMP_LE      (4)
#define CMP_NE      (5)

// VM_act effects
#define ACT_SELF    (0)
#define ACT_SEND    (1)
#define ACT_CREATE  (2)
#define ACT_BECOME  (3)
#define ACT_ABORT   (4)
#define ACT_COMMIT  (5)

/*
 * heap memory management (cells)
 */

// constants
#define FALSE       (0)
#define TRUE        (1)
#define NIL         (2)
#define UNDEF       (3)
#define UNIT        (4)
#define START       (5)
#define A_BOOT      (6)  // START+1
#define EMPTY_ENV   (28) // START+23

#if INCLUDE_DEBUG
static char *cell_label(int_t cell) {
    static char *label[] = {
        "FALSE",
        "TRUE",
        "NIL",
        "UNDEF",
        "UNIT",
    };
    if (cell < 0) return proc_label(cell);
    if (cell < START) return label[cell];
    return "cell";
}
#endif

/* (cust, index) -> lookup variable by De Bruijn index {x=value, y=next} */
#define bound_beh(BASE, _value_, _next_)                                    \
    { .t=VM_msg,        .x=2,           .y=BASE+1,      .z=UNDEF        },  \
    { .t=VM_push,       .x=1,           .y=BASE+2,      .z=UNDEF        },  \
    { .t=VM_alu,        .x=ALU_SUB,     .y=BASE+3,      .z=UNDEF        },  \
    { .t=VM_pick,       .x=1,           .y=BASE+4,      .z=UNDEF        },  \
    { .t=VM_eq,         .x=0,           .y=BASE+5,      .z=UNDEF        },  \
    { .t=VM_if,         .x=BASE+14,     .y=BASE+6,      .z=UNDEF        },  \
    { .t=VM_push,       .x=NIL,         .y=BASE+7,      .z=UNDEF        },  \
    { .t=VM_pick,       .x=2,           .y=BASE+8,      .z=UNDEF        },  \
    { .t=VM_pair,       .x=UNDEF,       .y=BASE+9,      .z=UNDEF        },  \
    { .t=VM_msg,        .x=1,           .y=BASE+10,     .z=UNDEF        },  \
    { .t=VM_pair,       .x=UNDEF,       .y=BASE+11,     .z=UNDEF        },  \
    { .t=VM_pick,       .x=3,           .y=BASE+12,     .z=UNDEF        },  \
    { .t=VM_act,        .x=ACT_SEND,    .y=BASE+13,     .z=UNDEF        },  \
    { .t=VM_act,        .x=ACT_COMMIT,  .y=UNDEF,       .z=UNDEF        },  \
    { .t=VM_pick,       .x=3,           .y=BASE+16,     .z=UNDEF        },  \
    { .t=VM_msg,        .x=1,           .y=BASE+12,     .z=UNDEF        },  // bound_beh #16

#define CELL_MAX NAT(1<<10)  // 1K cells
cell_t cell_table[CELL_MAX] = {
    { .t=Boolean_T,     .x=FALSE,       .y=FALSE,       .z=UNDEF        },
    { .t=Boolean_T,     .x=TRUE,        .y=TRUE,        .z=UNDEF        },
    { .t=Null_T,        .x=NIL,         .y=NIL,         .z=UNDEF        },
    { .t=Undef_T,       .x=UNDEF,       .y=UNDEF,       .z=UNDEF        },
    { .t=Unit_T,        .x=UNIT,        .y=UNIT,        .z=UNDEF        },
    { .t=Event_T,       .x=A_BOOT,      .y=NIL,         .z=NIL          },  // <--- START
    { .t=Actor_T,       .x=A_BOOT+1,    .y=UNDEF,       .z=UNDEF        },  // <--- A_BOOT
    { .t=VM_push,       .x='>',         .y=A_BOOT+2,    .z=UNDEF        },
    { .t=VM_putc,       .x=UNDEF,       .y=A_BOOT+3,    .z=UNDEF        },
    { .t=VM_push,       .x=' ',         .y=A_BOOT+4,    .z=UNDEF        },
    { .t=VM_putc,       .x=UNDEF,       .y=A_BOOT+5,    .z=UNDEF        },
    { .t=VM_push,       .x=NIL,         .y=A_BOOT+6,    .z=UNDEF        },
    { .t=VM_act,        .x=ACT_SELF,    .y=A_BOOT+7,    .z=UNDEF        },
    { .t=VM_act,        .x=ACT_SEND,    .y=A_BOOT+8,    .z=UNDEF        },
    { .t=VM_push,       .x=A_BOOT+11,   .y=A_BOOT+9,    .z=UNDEF        },
    { .t=VM_act,        .x=ACT_BECOME,  .y=A_BOOT+10,   .z=UNDEF        },
    { .t=VM_act,        .x=ACT_COMMIT,  .y=UNDEF,       .z=UNDEF        },
    { .t=VM_getc,       .x=UNDEF,       .y=A_BOOT+12,   .z=UNDEF        },  // +11
    { .t=VM_pick,       .x=1,           .y=A_BOOT+13,   .z=UNDEF        },
    { .t=VM_push,       .x='\0',        .y=A_BOOT+14,   .z=UNDEF        },
    { .t=VM_cmp,        .x=CMP_LT,      .y=A_BOOT+15,   .z=UNDEF        },
    { .t=VM_if,         .x=A_BOOT+21,   .y=A_BOOT+16,   .z=UNDEF        },
    { .t=VM_putc,       .x=UNDEF,       .y=A_BOOT+17,   .z=UNDEF        },
    { .t=VM_push,       .x=NIL,         .y=A_BOOT+18,   .z=UNDEF        },
    { .t=VM_act,        .x=ACT_SELF,    .y=A_BOOT+19,   .z=UNDEF        },
    { .t=VM_act,        .x=ACT_SEND,    .y=A_BOOT+20,   .z=UNDEF        },
    { .t=VM_act,        .x=ACT_COMMIT,  .y=UNDEF,       .z=UNDEF        },  // +21
    { .t=VM_drop,       .x=1,           .y=A_BOOT+20,   .z=UNDEF        },
/**/
    { .t=Actor_T,       .x=EMPTY_ENV+1, .y=UNDEF,       .z=UNDEF        },  // <--- EMPTY_ENV
    { .t=VM_push,       .x=UNDEF,       .y=EMPTY_ENV+2, .z=UNDEF        },
    { .t=VM_msg,        .x=1,           .y=EMPTY_ENV+3, .z=UNDEF        },
    { .t=VM_act,        .x=ACT_SEND,    .y=EMPTY_ENV+4, .z=UNDEF        },
    { .t=VM_act,        .x=ACT_COMMIT,  .y=UNDEF,       .z=UNDEF        },
/**/
#define BOUND_42 (33)
    { .t=Actor_T,       .x=BOUND_42+1,  .y=UNDEF,       .z=UNDEF        },  // <--- BOUND_42
    bound_beh(BOUND_42+1, 42, EMPTY_ENV)
};
cell_t *cell_zero = &cell_table[0];  // base for cell offsets
int_t cell_next = NIL;  // head of cell free-list (or NIL if empty)
int_t cell_top = START+23; // limit of allocated cell memory

#define get_t(n) (cell_zero[(n)].t)
#define get_x(n) (cell_zero[(n)].x)
#define get_y(n) (cell_zero[(n)].y)
#define get_z(n) (cell_zero[(n)].z)

#define set_t(n,v) (cell_zero[(n)].t = (v))
#define set_x(n,v) (cell_zero[(n)].x = (v))
#define set_y(n,v) (cell_zero[(n)].y = (v))
#define set_z(n,v) (cell_zero[(n)].z = (v))

#define IS_PROC(n)  ((n) < 0)
#define IS_BOOL(n)  (((n) == FALSE) || ((n) == TRUE))

#define TYPEQ(t,n)  (!IS_PROC(n) && (get_t(n) == (t)))
#define IS_PAIR(n)  TYPEQ(Pair_T,(n))
#define IS_ACTOR(n) TYPEQ(Actor_T,(n))

PROC_DECL(Free) {
    return panic("DISPATCH TO FREE CELL!");
}

static int_t gc_free_cnt = 0;  // number of cells in free-list

static int_t cell_new(int_t t, int_t x, int_t y, int_t z) {
    int_t next = cell_top;
    if (cell_next != NIL) {
        // use cell from free-list
        next = cell_next;
        cell_next = get_z(next);
        --gc_free_cnt;
    } else if (next < CELL_MAX) {
        // extend top of heap
        ++cell_top;
    } else {
        return panic("out of cell memory");
    }
    set_t(next, t);
    set_x(next, x);
    set_y(next, y);
    set_z(next, z);
    return next;
}

static void cell_reclaim(int_t addr) {
    // link into free-list
    set_z(addr, cell_next);
    set_y(addr, UNDEF);
    set_x(addr, UNDEF);
    set_t(addr, Free_T);
    cell_next = addr;
    ++gc_free_cnt;
}

int_t cell_free(int_t addr) {
    ASSERT(cell_zero[addr].t != Free_T);  // prevent double-free
    cell_reclaim(addr);
    return UNDEF;
}

int_t cons(int_t head, int_t tail) {
    return cell_new(Pair_T, head, tail, UNDEF);
}

#define list_0  NIL
#define list_1(v1)  cons((v1), NIL)
#define list_2(v1,v2)  cons((v1), cons((v2), NIL))
#define list_3(v1,v2,v3)  cons((v1), cons((v2), cons((v3), NIL)))
#define list_4(v1,v2,v3,v4)  cons((v1), cons((v2), cons((v3), cons((v4), NIL))))
#define list_5(v1,v2,v3,v4,v5)  cons((v1), cons((v2), cons((v3), cons((v4), cons((v5), NIL)))))
#define list_6(v1,v2,v3,v4,v5,v6)  cons((v1), cons((v2), cons((v3), cons((v4), cons((v5), cons((v6), NIL))))))

#define car(v) get_x(v)
#define cdr(v) get_y(v)

#define set_car(v,x) set_x((v),(x))
#define set_cdr(v,y) set_y((v),(y))

int_t equal(int_t x, int_t y) {
    if (x == y) return TRUE;
    while (IS_PAIR(x) && IS_PAIR(y)) {
        if (!equal(car(x), car(y))) break;
        x = cdr(x);
        y = cdr(y);
        if (x == y) return TRUE;
    }
    return FALSE;
}

int_t list_len(int_t val) {
    int_t len = 0;
    while (IS_PAIR(val)) {
        ++len;
        val = cdr(val);
    }
    return len;
}

// WARNING! destuctive reverse in-place and append
int_t append_reverse(int_t head, int_t tail) {
    while (IS_PAIR(head)) {
        int_t rest = cdr(head);
        set_cdr(head, tail);
        tail = head;
        head = rest;
    }
    return tail;
}

/*
 * actor event-queue
 */

int_t e_queue_head = START;
int_t e_queue_tail = START;

#define event_q_empty() (e_queue_head == NIL)

int_t event_q_put(int_t event) {
    set_z(event, NIL);
    if (event_q_empty()) {
        e_queue_head = event;
    } else {
        set_z(e_queue_tail, event);
    }
    e_queue_tail = event;
    return event;
}

int_t event_q_pop() {
    if (event_q_empty()) return UNDEF; // event queue empty
    int_t event = e_queue_head;
    e_queue_head = get_z(event);
    set_z(event, NIL);
    if (event_q_empty()) {
        e_queue_tail = NIL;  // empty queue
    }
    return event;
}

/*
 * VM continuation-queue
 */

int_t k_queue_head = NIL;
int_t k_queue_tail = NIL;

// FIXME: e_queue and k_queue management procedures are the same...

#define cont_q_empty() (k_queue_head == NIL)

int_t cont_q_put(int_t cont) {
    set_z(cont, NIL);
    if (cont_q_empty()) {
        k_queue_head = cont;
    } else {
        set_z(k_queue_tail, cont);
    }
    k_queue_tail = cont;
    return cont;
}

int_t cont_q_pop() {
    if (cont_q_empty()) return UNDEF; // cont queue empty
    int_t cont = k_queue_head;
    k_queue_head = get_z(cont);
    set_z(cont, NIL);
    if (cont_q_empty()) {
        k_queue_tail = NIL;  // empty queue
    }
    return cont;
}

/*
 * runtime (virtual machine engine)
 */

#define GET_IP() get_t(k_queue_head)
#define GET_SP() get_x(k_queue_head)
#define GET_EP() get_y(k_queue_head)

#define SET_IP(v) set_t(k_queue_head,(v))
#define SET_SP(v) set_x(k_queue_head,(v))
#define SET_EP(v) set_y(k_queue_head,(v))

int_t stack_push(int_t value) {
    XTRACE(debug_print("stack push", value));
    int_t sp = GET_SP();
    sp = cons(value, sp);
    SET_SP(sp);
    return value;
}

int_t stack_pop() {
    int_t item = UNDEF;
    int_t sp = GET_SP();
    if (IS_PAIR(sp)) {
        item = car(sp);
        int_t rest = cdr(sp);
        SET_SP(rest);
        XFREE(sp);
    }
    XTRACE(debug_print("stack pop", item));
    return item;
}

int_t stack_clear() {
    int_t sp = GET_SP();
    while (IS_PAIR(sp)) {
        int_t rest = cdr(sp);
        XFREE(sp);
    }
    return SET_SP(NIL);
}

static int_t dispatch() {
    int_t event = event_q_pop();
    XTRACE(debug_print("runtime event", event));
    if (event == UNDEF) {  // event queue empty
        return UNDEF;
    }
    int_t actor = get_x(event);
    ASSERT(IS_ACTOR(actor));
    if (get_y(actor) != UNDEF) {  // actor busy
        event_q_put(event);  // re-queue event
        return FALSE;
    }
    // spawn new "thread" to handle event
    set_y(actor, NIL);  // begin actor transaction
    set_z(actor, UNDEF);  // no BECOME
    int_t cont = cell_new(get_x(actor), get_y(event), event, NIL);
    ITRACE(debug_print("runtime spawn", cont));
    cont_q_put(cont);  // enqueue continuation
    return event;
}
static int_t execute() {
    XTRACE(debug_print("runtime cont", k_queue_head));
    if (cont_q_empty()) {
        return UNDEF;  // no more instructions to execute...
    }
    // execute next continuation
    int_t ip = GET_IP();
    int_t proc = get_t(ip);
#if INCLUDE_DEBUG
    if (!debugger()) {
        return FALSE;  // debugger quit
    }
#endif
    ip = call_proc(proc, ip, GET_EP());  // FIXME: EP is already available
    SET_IP(ip);  // update IP
    int_t cont = cont_q_pop();
    XTRACE(debug_print("runtime done", cont));
    if (ip >= START) {
        cont_q_put(cont);  // enqueue continuation
    } else {
        // if "thread" is dead, free cont and event
        XFREE(get_y(cont));
        XFREE(cont);
    }
    return UNIT;
}
int_t runtime() {
    int_t rv = UNIT;
    while (rv == UNIT) {
        rv = dispatch();    // dispatch a pending event (if any)
        rv = execute();     // execute next VM instruction
    }
    return rv;
}

/*
 * native procedures
 */

PROC_DECL(Undef) {
    return error("Undef message not understood");
}

PROC_DECL(Null) {
    return error("Null message not understood");
}

PROC_DECL(Pair) {
    return error("Pair message not understood");
}

PROC_DECL(Symbol) {
    return error("Symbol message not understood");
}

PROC_DECL(Boolean) {
    return error("Boolean message not understood");
}

PROC_DECL(Unit) {
    return error("Unit message not understood");
}

PROC_DECL(Actor) {
    return error("Actor message not understood");
}

PROC_DECL(Event) {
    return error("Event message not understood");
}

PROC_DECL(vm_cell) {
    int_t n = get_x(self);
    int_t z = UNDEF;
    int_t y = UNDEF;
    int_t x = UNDEF;
    ASSERT(n > 0);
    if (n > 3) { z = stack_pop(); }
    if (n > 2) { y = stack_pop(); }
    if (n > 1) { x = stack_pop(); }
    int_t t = stack_pop();
    int_t v = cell_new(t, x, y, z);
    stack_push(v);
    return get_y(self);
}

PROC_DECL(vm_get) {
    int_t f = get_x(self);
    int_t cell = stack_pop();
    int_t v = UNDEF;
    switch (f) {
        case FLD_T:     v = get_t(cell);    break;
        case FLD_X:     v = get_x(cell);    break;
        case FLD_Y:     v = get_y(cell);    break;
        case FLD_Z:     v = get_z(cell);    break;
        default:        return error("unknown field");
    }
    stack_push(v);
    return get_y(self);
}

PROC_DECL(vm_set) {
    int_t f = get_x(self);
    int_t v = stack_pop();
    int_t sp = GET_SP();
    if (!IS_PAIR(sp)) return error("set requires a cell");
    int_t cell = car(sp);
    switch (f) {
        case FLD_T:     set_t(cell, v);     break;
        case FLD_X:     set_x(cell, v);     break;
        case FLD_Y:     set_y(cell, v);     break;
        case FLD_Z:     set_z(cell, v);     break;
        default:        return error("unknown field");
    }
    return get_y(self);
}

PROC_DECL(vm_pair) {
    int_t h = stack_pop();
    int_t t = stack_pop();
    stack_push(cons(h, t));
    return get_y(self);
}

PROC_DECL(vm_part) {
    int_t c = stack_pop();
    int_t h = car(c);
    int_t t = cdr(c);
    stack_push(t);
    stack_push(h);
    return get_y(self);
}

PROC_DECL(vm_push) {
    int_t v = get_x(self);
    stack_push(v);
    return get_y(self);
}

PROC_DECL(vm_depth) {
    int_t v = 0;
    int_t sp = GET_SP();
    while (IS_PAIR(sp)) {  // count items on stack
        ++v;
        sp = cdr(sp);
    }
    stack_push(v);
    return get_y(self);
}

PROC_DECL(vm_drop) {
    int_t n = get_x(self);
    while (n-- > 0) {
        stack_pop();
    }
    return get_y(self);
}

PROC_DECL(vm_pick) {
    int_t n = get_x(self);
    int_t v = UNDEF;
    int_t sp = GET_SP();
    while (n-- > 0) {  // copy n-th item to top of stack
        v = car(sp);
        sp = cdr(sp);
    }
    stack_push(v);
    return get_y(self);
}

PROC_DECL(vm_dup) {
    int_t n = get_x(self);
    int_t dup = NIL;
    int_t sp = GET_SP();
    while (n-- > 0) {  // copy n items from stack
        dup = cons(car(sp), dup);
        sp = cdr(sp);
    }
    SET_SP(append_reverse(dup, GET_SP()));
    return get_y(self);
}

PROC_DECL(vm_alu) {
    int_t op = get_x(self);
    int_t m = stack_pop();
    int_t n = stack_pop();
    switch (op) {
        case ALU_ADD:   stack_push(n + m);      break;
        case ALU_SUB:   stack_push(n - m);      break;
        case ALU_MUL:   stack_push(n * m);      break;
        default:        return error("unknown operation");
    }
    return get_y(self);
}

PROC_DECL(vm_eq) {
    int_t n = get_x(self);
    int_t m = stack_pop();
    stack_push((n == m) ? TRUE : FALSE);
    return get_y(self);
}

PROC_DECL(vm_cmp) {
    int_t r = get_x(self);
    int_t m = stack_pop();
    int_t n = stack_pop();
    switch (r) {
        case CMP_EQ:    stack_push((n == m) ? TRUE : FALSE);    break;
        case CMP_GE:    stack_push((n >= m) ? TRUE : FALSE);    break;
        case CMP_GT:    stack_push((n > m) ? TRUE : FALSE);     break;
        case CMP_LT:    stack_push((n < m) ? TRUE : FALSE);     break;
        case CMP_LE:    stack_push((n <= m) ? TRUE : FALSE);    break;
        case CMP_NE:    stack_push((n != m) ? TRUE : FALSE);    break;
        default:        return error("unknown relation");
    }
    return get_y(self);
}

PROC_DECL(vm_if) {
    int_t b = stack_pop();
    // FIXME: check for UNDEF? ...if so, then what?
    return ((b == FALSE) ? get_y(self) : get_x(self));
}

PROC_DECL(vm_msg) {
    int_t i = get_x(self);
    int_t ep = GET_EP();
    int_t m = get_y(ep);
    int_t v = UNDEF;
    if (i == 0) {  // entire message
        v = m;
    } else if (i > 0) {  // message item at index
        while (IS_PAIR(m)) {
            if (--i == 0) {
                v = car(m);
                break;
            }
            m = cdr(m);
        }
    } /* FIXME: consider using -i to select the i-th tail? */
    stack_push(v);
    return get_y(self);
}

PROC_DECL(vm_act) {
    int_t e = get_x(self);
    int_t ep = GET_EP();
    int_t me = get_x(ep);
    switch (e) {
        case ACT_SELF: {
            stack_push(me);
            break;
        }
        case ACT_SEND: {
            int_t a = stack_pop();  // target
            if (!IS_ACTOR(a)) {
                set_y(me, UNDEF);  // abort actor transaction
                return error("SEND requires an Actor");  // terminate thread
            }
            int_t m = stack_pop();  // message
            int_t ev = cell_new(Event_T, a, m, get_y(me));
            set_y(me, ev);
            break;
        }
        case ACT_CREATE: {
            int_t b = stack_pop();  // behavior
            int_t a = cell_new(Actor_T, b, UNDEF, UNDEF);
            stack_push(a);
            break;
        }
        case ACT_BECOME: {
            int_t b = stack_pop();  // behavior
            ASSERT(get_z(me) == UNDEF);  // BECOME only allowed once
            set_z(me, b);
            break;
        }
        case ACT_ABORT: {
            int_t r = stack_pop();  // reason
            DEBUG(debug_print("ABORT!", r));
            stack_clear();
            set_y(me, UNDEF);  // abort actor transaction
            return FALSE;  // terminate thread
        }
        case ACT_COMMIT: {
            stack_clear();
            int_t b = get_z(me);
            if (b != UNDEF) {
                set_x(me, b);  // BECOME new behavior
            }
            int_t e = get_y(me);
            while (e != NIL) {
                int_t es = get_z(e);
                event_q_put(e);
                e = es;
            }
            set_y(me, UNDEF);  // commit actor transaction
            return TRUE;  // terminate thread
        }
        default: {
            return error("unknown effect");
        }
    }
    return get_y(self);
}

PROC_DECL(vm_putc) {
    int_t c = stack_pop();
    putchar(c);
    return get_y(self);
}

PROC_DECL(vm_getc) {
    int_t c = getchar();
    stack_push(c);
    return get_y(self);
}

/*
 * debugging tools
 */
#if INCLUDE_DEBUG

#if USE_INT16_T || (USE_INTPTR_T && (__SIZEOF_POINTER__ == 2))
void hexdump(char *label, int_t *addr, size_t cnt) {
    fprintf(stderr, "%s:", label);
    for (nat_t n = 0; n < cnt; ++n) {
        if ((n & 0x7) == 0x0) {
            fprintf(stderr, "\n%04"PxI":", NAT(addr));
        }
        if ((n & 0x3) == 0x0) {
            fprintf(stderr, " ");
        }
        fprintf(stderr, " %04"PxI"", NAT(*addr++));
    }
    fprintf(stderr, "\n");
}
#endif
#if USE_INT32_T || (USE_INTPTR_T && (__SIZEOF_POINTER__ == 4))
void hexdump(char *label, int_t *addr, size_t cnt) {
    fprintf(stderr, "%s: %04"PxI"..", label, (NAT(addr) >> 16));
    for (nat_t n = 0; n < cnt; ++n) {
        if ((n & 0x7) == 0x0) {
            fprintf(stderr, "\n..%04"PxI":", (NAT(addr) & 0xFFFF));
        }
        fprintf(stderr, " %08"PxI"", NAT(*addr++) & 0xFFFFFFFF);
    }
    fprintf(stderr, "\n");
}
#endif
#if USE_INT64_T || (USE_INTPTR_T && (__SIZEOF_POINTER__ == 8))
void hexdump(char *label, int_t *addr, size_t cnt) {
    fprintf(stderr, "%s: %08"PxI"..", label, (NAT(addr) >> 32));
    for (nat_t n = 0; n < cnt; ++n) {
        if ((n & 0x3) == 0x0) {
            fprintf(stderr, "\n..%08"PxI":", (NAT(addr) & 0xFFFFFFFF));
        }
        fprintf(stderr, " %016"PxI"", NAT(*addr++));
    }
    fprintf(stderr, "\n");
}
#endif

void debug_print(char *label, int_t addr) {
    fprintf(stderr, "%s: ", label);
    fprintf(stderr, "%s[%"PdI"]", cell_label(addr), addr);
    if (addr >= 0) {
        fprintf(stderr, " = ");
        fprintf(stderr, "{t:%s(%"PdI"),", cell_label(get_t(addr)), get_t(addr));
        fprintf(stderr, " x:%s(%"PdI"),", cell_label(get_x(addr)), get_x(addr));
        fprintf(stderr, " y:%s(%"PdI"),", cell_label(get_y(addr)), get_y(addr));
        fprintf(stderr, " z:%s(%"PdI")}", cell_label(get_z(addr)), get_z(addr));
    }
    fprintf(stderr, "\n");
}

static void print_event(int_t ep) {
    fprintf(stderr, "(%"PdI"", get_x(ep));  // target actor
    int_t msg = get_y(ep);  // actor message
    while (IS_PAIR(msg)) {
        fprintf(stderr, " %+"PdI"", car(msg));
        msg = cdr(msg);
    }
    if (msg == NIL) {
        fprintf(stderr, ") ");
    } else {
        fprintf(stderr, " . %+"PdI") ", msg);
    }
}
static void print_stack(int_t sp) {
    if (IS_PAIR(sp)) {
        print_stack(cdr(sp));
        int_t item = car(sp);
        //fprintf(stderr, "%s[%"PdI"] ", cell_label(item), item);
        fprintf(stderr, "%+"PdI" ", item);
    }
}
static char *field_label(int_t f) {
    switch (f) {
        case FLD_T:     return "T";
        case FLD_X:     return "X";
        case FLD_Y:     return "Y";
        case FLD_Z:     return "Z";
    }
    return "<unknown>";
}
static char *operation_label(int_t op) {
    switch (op) {
        case ALU_ADD:   return "ADD";
        case ALU_SUB:   return "SUB";
        case ALU_MUL:   return "MUL";
    }
    return "<unknown>";
}
static char *relation_label(int_t r) {
    switch (r) {
        case CMP_EQ:    return "EQ";
        case CMP_GE:    return "GE";
        case CMP_GT:    return "GT";
        case CMP_LT:    return "LT";
        case CMP_LE:    return "LE";
        case CMP_NE:    return "NE";
    }
    return "<unknown>";
}
static char *effect_label(int_t e) {
    switch (e) {
        case ACT_SELF:      return "SELF";
        case ACT_SEND:      return "SEND";
        case ACT_CREATE:    return "CREATE";
        case ACT_BECOME:    return "BECOME";
        case ACT_ABORT:     return "ABORT";
        case ACT_COMMIT:    return "COMMIT";
    }
    return "<unknown>";
}
static void print_inst(int_t ip) {
    int_t proc = get_t(ip);
    fprintf(stderr, "%s", cell_label(proc));
    switch (proc) {
        case VM_cell: fprintf(stderr, "{n:%"PdI",k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_get:  fprintf(stderr, "{f:%s,k:%"PdI"}", field_label(get_x(ip)), get_y(ip)); break;
        case VM_set:  fprintf(stderr, "{f:%s,k:%"PdI"}", field_label(get_x(ip)), get_y(ip)); break;
        case VM_pair: fprintf(stderr, "{k:%"PdI"}", get_y(ip)); break;
        case VM_part: fprintf(stderr, "{k:%"PdI"}", get_y(ip)); break;
        case VM_push: fprintf(stderr, "{v:%"PdI",k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_depth:fprintf(stderr, "{k:%"PdI"}", get_y(ip)); break;
        case VM_drop: fprintf(stderr, "{n:%"PdI",k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_pick: fprintf(stderr, "{n:%"PdI",k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_dup:  fprintf(stderr, "{n:%"PdI",k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_alu:  fprintf(stderr, "{op:%s,k:%"PdI"}", operation_label(get_x(ip)), get_y(ip)); break;
        case VM_eq:   fprintf(stderr, "{n:%"PdI",k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_cmp:  fprintf(stderr, "{r:%s,k:%"PdI"}", relation_label(get_x(ip)), get_y(ip)); break;
        case VM_if:   fprintf(stderr, "{t:%"PdI",f:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_msg:  fprintf(stderr, "{i:%"PdI",k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_act:  fprintf(stderr, "{e:%s,k:%"PdI"}", effect_label(get_x(ip)), get_y(ip)); break;
        case VM_putc: fprintf(stderr, "{k:%"PdI"}", get_y(ip)); break;
        case VM_getc: fprintf(stderr, "{k:%"PdI"}", get_y(ip)); break;
        default:      fprintf(stderr, "{x:%"PdI",y:%"PdI",z:%"PdI"}", get_x(ip), get_y(ip), get_z(ip)); break;
    }
}
void continuation_trace() {
    print_event(GET_EP());
    fprintf(stderr, "%"PdI": ", GET_IP());
    print_stack(GET_SP());
    print_inst(GET_IP());
    fprintf(stderr, "\n");
}
void disassemble(int_t ip, int_t n) {
    while (n-- > 0) {
        fprintf(stderr, "%"PdI": ", ip);
        print_inst(ip);
        fprintf(stderr, "\n");
        ++ip;
    }
}

static char *db_cmd_token(char **pp) {
    char *p = *pp;
    while (*p > ' ') {
        ++p;
    }
    if (*p && (*p <= ' ')) {
        *p++ = '\0';
    }
    char *q = *pp;
    *pp = p;
    return q;
}
static int_t db_cmd_eq(char *actual, char *expect) {
    while (*expect) {
        if (*expect++ != *actual++) return FALSE;
    }
    return (*actual ? FALSE : TRUE);
}
static int_t db_num_cmd(char *cmd) {
    int_t n = 0;
    nat_t d;
    while ((d = NAT(*cmd++ - '0')) < 10) {
        n = (n * 10) + d;
    }
    return n;
}
int_t debugger() {
#if RUN_DEBUGGER
    static int_t run = FALSE;   // single-stepping
#else
    static int_t run = TRUE;    // free-running
#endif
    static int_t bp_ip = 0;
    static int_t s_cnt = 0;
    static int_t n_cnt = 0;
    static int_t n_ep = 0;
    static char buf[32];        // command buffer

    int_t skip = (run ? TRUE : FALSE);
    if (!skip && (s_cnt > 0)) {
        if (--s_cnt) skip = TRUE;
    }
    if (!skip && n_ep) {
        if (n_ep != GET_EP()) {
            skip = TRUE;
        } else if (n_cnt > 0) {
            if (--n_cnt) skip = TRUE;
        }
    }
    if (GET_IP() == bp_ip) {
        skip = FALSE;
    }
    if (skip) {
        ITRACE(continuation_trace());
        return TRUE;  // continue
    }
    run = FALSE;
    s_cnt = 0;
    n_cnt = 0;
    n_ep = 0;
    while (1) {
        continuation_trace();
        fprintf(stderr, "# ");  // debugger prompt
        char *p = fgets(buf, sizeof(buf), stdin);
        if (!p) {
            fprintf(stderr, "\n");
            return FALSE;                   // exit
        }
        char *cmd = db_cmd_token(&p);
        if (*cmd == 'q') return FALSE;      // quit
        if (*cmd == 'b') {                  // break(point)
            cmd = db_cmd_token(&p);
            int ip = GET_IP();
            if (*cmd) {
                ip = db_num_cmd(cmd);
            }
            bp_ip = ip;
            fprintf(stderr, "break at ip=%"PdI"\n", bp_ip);
            continue;
        }
        if (*cmd == 's') {                  // step
            cmd = db_cmd_token(&p);
            int cnt = db_num_cmd(cmd);
            s_cnt = ((cnt < 1) ? 1 : cnt);
            return TRUE;
        }
        if (*cmd == 'n') {                  // next
            cmd = db_cmd_token(&p);
            int cnt = db_num_cmd(cmd);
            n_cnt = ((cnt < 1) ? 1 : cnt);
            n_ep = GET_EP();
            return TRUE;
        }
        if (*cmd == 'd') {                  // disasm
            cmd = db_cmd_token(&p);
            int cnt = db_num_cmd(cmd);
            cnt = ((cnt < 1) ? 1 : cnt);
            cmd = db_cmd_token(&p);
            int ip = GET_IP();
            if (*cmd) {
                ip = db_num_cmd(cmd);
            }
            disassemble(ip, cnt);
            continue;
        }
        if (*cmd == 'r') {                  // regs
            fprintf(stderr, "ip=%"PdI" sp=%"PdI" ep=%"PdI"\n",
                GET_IP(), GET_SP(), GET_EP());
            continue;
        }
        if (*cmd == 'c') {                  // continue
            run = TRUE;
            return TRUE;
        }
        fprintf(stderr, "h[elp] b[reak] c[ontinue] s[tep] n[ext] d[isasm] r[egs] q[uit]\n");
    }
}
#endif // INCLUDE_DEBUG

/*
 * bootstrap
 */

int main(int argc, char const *argv[])
{
    DEBUG(fprintf(stderr, "PROC_MAX=%"PuI" CELL_MAX=%"PuI"\n", PROC_MAX, CELL_MAX));
    DEBUG(hexdump("cell memory", ((int_t *)cell_zero), 32*4));
    int_t result = runtime();
    DEBUG(debug_print("main result", result));
    DEBUG(fprintf(stderr, "free_cnt=%"PdI" cell_top=%"PdI"\n", gc_free_cnt, cell_top));
    return 0;
}

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
