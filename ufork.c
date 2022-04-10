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

#if INCLUDE_DEBUG
#define DEBUG(x)    x   // include/exclude debug instrumentation
#define ITRACE(x)   x   // include/exclude instruction trace
#define XTRACE(x)       // include/exclude execution trace
#else
#define DEBUG(x)        // include/exclude debug instrumentation
#define ITRACE(x)       // include/exclude instruction trace
#define XTRACE(x)       // include/exclude execution trace
#endif

#define USE_INT16_T   1 // define "machine word" as int16_t from <stdint.h>
#define USE_INT32_T   0 // define "machine word" as int32_t from <stdint.h>
#define USE_INT64_T   0 // define "machine word" as int64_t from <stdint.h>
#define USE_INTPTR_T  0 // define "machine word" as intptr_t from <stdint.h>

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

#define ASSERT(cond)    if (!(cond)) return failure(__FILE__, __LINE__)

/*
 * debugging tools
 */
#if INCLUDE_DEBUG

// FORWARD DECLARATIONS
void debug_print(char *label, int_t addr);
void continuation_trace();

#if USE_INT16_T || (USE_INTPTR_T && (__SIZEOF_POINTER__ == 2))
static void hexdump(char *label, int_t *addr, size_t cnt) {
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
static void hexdump(char *label, int_t *addr, size_t cnt) {
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
static void hexdump(char *label, int_t *addr, size_t cnt) {
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

#endif // INCLUDE_DEBUG

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
PROC_DECL(Free);  // FIXME: consider using FALSE instead?
PROC_DECL(vm_push);
PROC_DECL(vm_drop);
PROC_DECL(vm_dup);
PROC_DECL(vm_eq);
PROC_DECL(vm_lt);
PROC_DECL(vm_if);
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
#define VM_push     (-10)
#define VM_drop     (-11)
#define VM_dup      (-12)
#define VM_eq       (-13)
#define VM_lt       (-14)
#define VM_if       (-15)
#define VM_putc     (-16)
#define VM_getc     (-17)

#define PROC_MAX    NAT(sizeof(proc_table) / sizeof(proc_t))
proc_t proc_table[] = {
    vm_getc,
    vm_putc,
    vm_if,
    vm_lt,
    vm_eq,
    vm_dup,
    vm_drop,
    vm_push,
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
        "VM_push",
        "VM_drop",
        "VM_dup",
        "VM_eq",
        "VM_lt",
        "VM_if",
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

/*
 * heap memory management (cells)
 */

#define FALSE       (0)
#define TRUE        (1)
#define NIL         (2)
#define UNDEF       (3)
#define UNIT        (4)
#define START       (5)
#define A_BOOT      (6)

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

#define CELL_MAX NAT(1<<10)  // 1K cells
cell_t cell_table[CELL_MAX] = {
    { .t = Boolean_T,   .x = FALSE,     .y = FALSE,     .z = UNDEF      },
    { .t = Boolean_T,   .x = TRUE,      .y = TRUE,      .z = UNDEF      },
    { .t = Null_T,      .x = NIL,       .y = NIL,       .z = UNDEF      },
    { .t = Undef_T,     .x = UNDEF,     .y = UNDEF,     .z = UNDEF      },
    { .t = Unit_T,      .x = UNIT,      .y = UNIT,      .z = UNDEF      },
    { .t = Event_T,     .x = A_BOOT,    .y = NIL,       .z = NIL        },  // <--- START
    { .t = Actor_T,     .x = START+2,   .y = UNDEF,     .z = UNDEF      },  // <--- A_BOOT
    { .t = VM_push,     .x = '>',       .y = START+3,   .z = UNDEF      },
    { .t = VM_putc,     .x = UNDEF,     .y = START+4,   .z = UNDEF      },
    { .t = VM_push,     .x = ' ',       .y = START+5,   .z = UNDEF      },
    { .t = VM_putc,     .x = UNDEF,     .y = START+6,   .z = UNDEF      },
    { .t = VM_getc,     .x = UNDEF,     .y = START+7,   .z = UNDEF      },  // +6
    { .t = VM_dup,      .x = 1,         .y = START+8,   .z = UNDEF      },
    { .t = VM_push,     .x = '\0',      .y = START+9,   .z = UNDEF      },
    { .t = VM_lt,       .x = UNDEF,     .y = START+10,  .z = UNDEF      },
    { .t = VM_if,       .x = UNIT,      .y = START+11,  .z = UNDEF      },
    { .t = VM_putc,     .x = UNDEF,     .y = START+6,   .z = UNDEF      },
};
cell_t *cell_zero = &cell_table[0];  // base for cell offsets
int_t cell_next = NIL;  // head of cell free-list (or NIL if empty)
int_t cell_top = START+12; // limit of allocated cell memory

#define get_t(n) (cell_zero[(n)].t)
#define get_x(n) (cell_zero[(n)].x)
#define get_y(n) (cell_zero[(n)].y)
#define get_z(n) (cell_zero[(n)].z)

#define set_t(n,v) (cell_zero[(n)].t = (v))
#define set_x(n,v) (cell_zero[(n)].x = (v))
#define set_y(n,v) (cell_zero[(n)].y = (v))
#define set_z(n,v) (cell_zero[(n)].z = (v))

#define IS_PAIR(n)  (get_t(n) == Pair_T)
#define IS_BOOL(n)  (get_t(n) == Boolean_T)

PROC_DECL(Free) {
    return panic("DISPATCH TO FREE CELL!");
}

static int gc_free_cnt = 0;

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
    SET_SP(cons(value, GET_SP()));
    return value;
}

int_t stack_pop() {
    int_t value = UNDEF;
    if (IS_PAIR(GET_SP())) {
        value = car(GET_SP());
        SET_SP(cdr(GET_SP()));
    }
    XTRACE(debug_print("stack pop", value));
    return value;
}

int_t runtime() {
    while (1) {
        int_t event = event_q_pop();
        XTRACE(debug_print("runtime event", event));
        if (event != UNDEF) {
            // start new "thread" to handle event
            int_t actor = get_x(event);
            if (get_y(actor) == UNDEF) {  // actor ready
                set_y(actor, NIL);  // begin actor transaction
                set_z(actor, UNDEF);  // no BECOME
                int_t cont = cell_new(get_x(actor), get_y(event), event, NIL);
                ITRACE(debug_print("runtime spawn", cont));
                cont_q_put(cont);  // enqueue continuation
            } else {  // actor busy
                event_q_put(event);  // re-queue event
            }
        }
        XTRACE(debug_print("runtime cont", k_queue_head));
        if (cont_q_empty()) break;  // no more instructions to execute...
        // execute next continuation
        int_t ip = GET_IP();
        int_t proc = get_t(ip);
        ITRACE(continuation_trace());
        ip = call_proc(proc, ip, GET_EP());
        SET_IP(ip);  // update IP
        int_t cont = cont_q_pop();
        XTRACE(debug_print("runtime done", cont));
        if (ip >= START) {
            cont_q_put(cont);  // enqueue continuation
        }
    }
    return UNIT;
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

PROC_DECL(vm_push) {
    int_t v = get_x(self);
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

PROC_DECL(vm_eq) {
    int_t y = stack_pop();
    int_t x = stack_pop();
    //stack_push(equal(x, y));
    stack_push(x == y);  // identity, not equality
    return get_y(self);
}

PROC_DECL(vm_lt) {
    int_t m = stack_pop();
    int_t n = stack_pop();
    stack_push((n < m) ? TRUE : FALSE);
    return get_y(self);
}

PROC_DECL(vm_if) {
    int_t b = stack_pop();
    // FIXME: check for UNDEF? ...if so, then what?
    return ((b == FALSE) ? get_y(self) : get_x(self));
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
 * bootstrap
 */

#if INCLUDE_DEBUG
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

static void print_stack(int_t sp) {
    if (IS_PAIR(sp)) {
        print_stack(cdr(sp));
        int_t item = car(sp);
        //fprintf(stderr, "%s[%"PdI"] ", cell_label(item), item);
        fprintf(stderr, "%"PdI" ", item);
    }
}
static void print_inst(int_t ip) {
    int_t proc = get_t(ip);
    fprintf(stderr, "%s", cell_label(proc));
    switch (proc) {
        case VM_push: fprintf(stderr, "{v:%"PdI", k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_drop: fprintf(stderr, "{n:%"PdI", k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_dup:  fprintf(stderr, "{n:%"PdI", k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_eq:   fprintf(stderr, "{k:%"PdI"}", get_y(ip)); break;
        case VM_lt:   fprintf(stderr, "{k:%"PdI"}", get_y(ip)); break;
        case VM_if:   fprintf(stderr, "{t:%"PdI", f:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_putc: fprintf(stderr, "{k:%"PdI"}", get_y(ip)); break;
        case VM_getc: fprintf(stderr, "{k:%"PdI"}", get_y(ip)); break;
        default: {
            fprintf(stderr, "(%"PdI",", get_t(proc));
            fprintf(stderr, " %"PdI",", get_x(ip));
            fprintf(stderr, " %"PdI",", get_y(ip));
            fprintf(stderr, " %"PdI")", get_z(ip));
        }
    }
}
void continuation_trace() {
    print_stack(GET_SP());
    print_inst(GET_IP());
    //print_event(GET_EP());
    fprintf(stderr, "\n");
}
#endif

int main(int argc, char const *argv[])
{
    DEBUG(fprintf(stderr, "PROC_MAX=%"PuI" CELL_MAX=%"PuI"\n", PROC_MAX, CELL_MAX));
    DEBUG(hexdump("cell memory", ((int_t *)cell_zero), 24*4));
    int_t result = runtime();
    DEBUG(debug_print("main result", result));
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
