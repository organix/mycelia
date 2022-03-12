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
#include <stdint.h>  // for intptr_t, uintptr_t, uint8_t, uint16_t, etc.
#include <inttypes.h>  // for PRIiPTR, PRIuPTR, PRIXPTR, etc.

#define DEBUG(x)   // include/exclude debug instrumentation
#define XDEBUG(x) x // include/exclude extra debugging

#define inline /*inline*/
//#define inline __inline__

typedef int32_t i32;
typedef int64_t i64;

#define OK      ((i32)0)

typedef union cell {
    struct {
        i32     car;
        i32     cdr;
    }       cons;
    struct {
        i32     code;
        i32     data;
    }       obj;
    i64     raw;
    void   *addr;
} cell_t;

/*
____  3322 2222 2222 1111  1111 1100 0000 0000
i32:  1098 7654 3210 9876  5432 1098 7654 3210
      xxxx xxxx xxxx xxxx  xxxx xxxx xxxx xg00
                                           ^^^-- Varient
                                           |+--- Pointer
                                           +---- GC Trace
*/
#define VAL(x)      ((i32)(x))

#define VAL_VAR     VAL(1<<0)
#define VAL_PTR     VAL(1<<1)
#define VAL_GC      VAL(1<<2)

#define VAL_MASK    (VAL_PTR | VAL_VAR)

#define IMM_INT     VAL(0)
#define IMM_VAL     (VAL_VAR)
#define PTR_CELL    (VAL_PTR)
#define PTR_OBJ     (VAL_PTR | VAL_VAR)

#define PTR_MASK    (VAL_GC | VAL_PTR | VAL_VAR)
#define PTR_GC      (VAL_GC | VAL_PTR)

#define IS_INT(v)   (((v)&VAL_MASK) == IMM_INT)
#define IS_CELL(v)  (((v)&VAL_MASK) == PTR_CELL)
#define IS_OBJ(v)   (((v)&VAL_MASK) == PTR_OBJ)

#define IS_IMM(v)   (((v)&VAL_PTR) == 0)
#define IS_PTR(v)   (((v)&VAL_PTR) != 0)
#define IS_GC(v)    (((v)&PTR_GC) == PTR_GC)

#define TO_INT(v)   (VAL(v) >> 2)
#define TO_PTR(v)   (VAL(v) & ~PTR_MASK)

#define MK_INT(n)   VAL((n) << 2)
#define MK_CELL(p)  ((VAL(p) & ~PTR_MASK) | PTR_CELL)
#define MK_OBJ(p)   ((VAL(p) & ~PTR_MASK) | PTR_OBJ)
#define SET_GC(v)   ((v) |= VAL_GC)
#define CLR_GC(v)   ((v) &= ~VAL_GC)

// immediate values

#define MK_BOOL(z)  ((z) ? TRUE : FALSE)

#define FALSE       VAL(0x0000FFFD)
#define TRUE        VAL(0x0100FFFD)
#define NIL         VAL(0x0200FFFD)
#define FAIL        VAL(0x0E00FFFD)
#define UNDEF       VAL(0xFF00FFFD)

#define SYM         VAL(0x000000FD)
#define IS_SYM(v)   (((v) & 0x0000FFFF) == SYM)
#define MK_SYM(s)   (((s) << 16) | SYM)
#define TO_SYM(v)   (((v) >> 16) & 0xFFFF)

#define ZERO        VAL(0x00000000)
#define ONE         VAL(0x00000004)
#define INF         VAL(0x80000000)

// procedure declaration

#define PROC_DECL(name)  i32 name(i32 self, i32 args)

#define PROC        VAL(0x000001FD)
#define IS_PROC(v)  (((v) & 0x0000FFFF) == PROC)
#define MK_PROC(n)  (((n) << 16) | PROC)
#define TO_PROC(v)  (((v) >> 16) & 0xFFFF)

/*
 * error handling
 */

i32 panic(char *reason) {
    fprintf(stderr, "\nPANIC! %s\n", reason);
    exit(-1);
    return UNDEF;  // not reached, but typed for easy swap with error()
}

i32 error(char *reason) {
    fprintf(stderr, "\nERROR! %s\n", reason);
    return UNDEF;
}

i32 failure(char *_file_, int _line_) {
    fprintf(stderr, "\nASSERT FAILED! %s:%d\n", _file_, _line_);
    return UNDEF;
}

#define ASSERT(cond)    if (!(cond)) return failure(__FILE__, __LINE__)

/*
 * heap memory management (cells)
 */

#define CELL_MAX (1024)
cell_t cell[CELL_MAX] = {
    { .cons.car = CELL_MAX, .cons.cdr = 1 },  // root cell
    { .cons.car = 0, .cons.cdr = 0 },  // end of free-list
};

static i64 cell_usage() {
    i32 count = 0;
    i32 prev = 0;
    i32 next = cell[prev].cons.cdr;
    while (cell[next].cons.cdr) {
        ++count;
        prev = next;
        next = cell[prev].cons.cdr;
    }
    cell_t acc = {
        .cons.car = MK_INT(count),      // free cells
        .cons.cdr = MK_INT(next - 1),   // heap cells
    };
    return acc.raw;
}

i32 cell_new() {
    i32 head = cell[0].cons.cdr;
    i32 next = cell[head].cons.cdr;
    if (next) {
        // use cell from free-list
        cell[0].cons.cdr = next;
        return MK_CELL(head << 3);
    }
    next = head + 1;
    if (next < CELL_MAX) {
        // extend top of heap
        cell[next].raw = 0;  // clear new end cell
        cell[0].cons.cdr = next;
        return MK_CELL(head << 3);
    }
    return error("out of cell memory");
}

i32 cell_free(i32 v) {
    if (IS_PTR(v)) {
        i32 ofs = TO_PTR(v) >> 3;
        cell[ofs].raw = 0;  // clear free cell
        // link into free-list
        cell[ofs].cons.cdr = cell[0].cons.cdr;
        cell[0].cons.cdr = ofs;
    }
    return NIL;
}

i32 obj_new(i32 code, i32 data) {
    i32 v = cell_new();
    if (!IS_CELL(v)) return UNDEF;
    i32 ofs = TO_PTR(v) >> 3;
    cell[ofs].obj.code = code;
    cell[ofs].obj.data = data;
    return MK_OBJ(v);
}

i32 cons(i32 car, i32 cdr) {
    i32 v = cell_new();
    if (!IS_CELL(v)) return UNDEF;
    i32 ofs = TO_PTR(v) >> 3;
    cell[ofs].cons.car = car;
    cell[ofs].cons.cdr = cdr;
    return v;
}

i32 car(i32 v) {
    ASSERT(IS_CELL(v));
    i32 ofs = TO_PTR(v) >> 3;
    return cell[ofs].cons.car;
}

i32 cdr(i32 v) {
    ASSERT(IS_CELL(v));
    i32 ofs = TO_PTR(v) >> 3;
    return cell[ofs].cons.cdr;
}

/*
 * interned strings (symbols)
 */

#define INTERN_MAX (1024)
char intern[INTERN_MAX] = {
    5, 't', 'y', 'p', 'e', 'q',
    4, 'e', 'v', 'a', 'l',
    5, 'a', 'p', 'p', 'l', 'y',
    2, 'i', 'f',
    3, 'm', 'a', 'p',
    6, 'r', 'e', 'd', 'u', 'c', 'e',
    4, 'b', 'i', 'n', 'd',
    6, 'l', 'o', 'o', 'k', 'u', 'p',
    5, 'm', 'a', 't', 'c', 'h',
    7, 'c', 'o', 'n', 't', 'e', 'n', 't',
    0,  // end of interned strings
};

i32 symbol(char *s) {
    i32 j;
    i32 n = 0;
    while (s[n]) ++n;  // compute c-string length
    i32 i = 0;
    while (intern[i]) {
        i32 m = intern[i++];  // symbol length
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

/*
 * procedures
 */

PROC_DECL(fail) {
    XDEBUG(fprintf(stderr, "fail: self=%"PRIx32", args=%"PRIx32"\n", self, args));
    return error("FAILED");
}

// FORWARD DECLARATIONS
PROC_DECL(actor);
PROC_DECL(sink_beh);

#define PROC_MAX (1024)
typedef PROC_DECL((*proc_ptr_t));
proc_ptr_t proc[PROC_MAX] = {
    fail,
    actor,
    sink_beh,
    0
};
enum proc_idx {
    p_fail,
    p_actor,
    p_sink_beh,
    p_unused,
} proc_idx_t;

PROC_DECL(proc_call) {
    ASSERT(IS_PROC(self));
    i32 idx = TO_PROC(self);
    return (proc[idx])(self, args);
}

PROC_DECL(obj_call) {
    ASSERT(IS_OBJ(self));
    i32 ofs = TO_PTR(self) >> 3;
    i32 idx = TO_PROC(cell[ofs].obj.code);
    return (proc[idx])(self, args);
}

/*
 * actor primitives
 */

inline int is_actor(i32 v) {
    return IS_OBJ(v) && (cell[TO_PTR(v) >> 3].obj.code == MK_PROC(p_actor));
}

PROC_DECL(actor) {
    XDEBUG(fprintf(stderr, "actor: self=%"PRIx32", args=%"PRIx32"\n", self, args));
    ASSERT(is_actor(self));
    i32 ofs = TO_PTR(self) >> 3;
    i32 beh = cell[ofs].obj.data;
    ASSERT(IS_OBJ(beh));
    ofs = TO_PTR(beh) >> 3;
    i32 idx = TO_PROC(cell[ofs].obj.code);
    return (proc[idx])(self, args);  // target actor, instead of beh, as `self`
}

cell_t event_q = { .cons.car = NIL, .cons.cdr = NIL };

i32 event_q_append(i32 events) {
    if (events == NIL) return OK;  // nothing to add
    ASSERT(IS_CELL(events));
    // find the end of events
    i32 tail = events;
    i32 ofs = TO_PTR(tail) >> 3;
    while (cell[ofs].cons.cdr != NIL) {
        tail = cell[ofs].cons.cdr;
        ofs = TO_PTR(tail) >> 3;
    }
    // append events on event_q
    if (event_q.cons.car == NIL) {
        event_q.cons.car = events;
    } else {
        ofs = TO_PTR(event_q.cons.cdr) >> 3;
        cell[ofs].cons.cdr = events;
    }
    event_q.cons.cdr = tail;
    return OK;
}

i32 event_q_take() {
    if (event_q.cons.car == NIL) return UNDEF; // event queue empty
    i32 head = event_q.cons.car;
    i32 ofs = TO_PTR(head) >> 3;
    event_q.cons.car = cell[ofs].cons.cdr;
    if (event_q.cons.car == NIL) {
        event_q.cons.cdr = NIL;  // empty queue
    }
    i32 event = cell[ofs].cons.car;
    cell_free(head);
    return event;
}

i32 event_dispatch() {
    i32 event = event_q_take();
    if (!IS_CELL(event)) return UNDEF;  // nothing to dispatch
    i32 ofs = TO_PTR(event) >> 3;
    i32 target = cell[ofs].cons.car;
    i32 msg = cell[ofs].cons.cdr;
    i32 effect = obj_call(target, msg);  // invoke actor behavior
    if (!IS_CELL(effect)) return UNDEF;  // behavior failed
    ofs = TO_PTR(effect) >> 3;
    if (cell[ofs].cons.car == FAIL) {
        // error thrown
        return effect;
    }
    i32 actors = cell[ofs].cons.car;
    i32 rest = cell[ofs].cons.cdr;
    cell_free(effect);
    while (IS_CELL(actors)) {  // free list, but not actors
        ofs = TO_PTR(actors) >> 3;
        i32 next = cell[ofs].cons.cdr;
        cell_free(actors);
        actors = next;
    }
    ofs = TO_PTR(rest) >> 3;
    i32 events = cell[ofs].cons.car;
    i32 beh = cell[ofs].cons.cdr;
    cell_free(rest);
    // update behavior
    ofs = TO_PTR(target) >> 3;
    cell[ofs].obj.data = beh;
    // add events to dispatch queue
    return event_q_append(events);
}

i32 event_loop() {
    i32 result = OK;
    while (result == OK) {
        result = event_dispatch();
    }
    return result;
}

/*
 * actor behaviors
 */

PROC_DECL(sink_beh) {
    i32 effect = cons(NIL, cons(NIL, NIL));  // empty effect
    if (!IS_CELL(effect)) return UNDEF;  // allocation failure
    DEBUG(fprintf(stderr, "sink_beh: self=%"PRIx32", args=%"PRIx32"\n", self, args));
    return effect;
}

/*
 * unit tests
 */

i32 unit_tests() {
    i32 v, v0, v1, v2;
    i32 n;
    i64 dv;
    cell_t c;

    v = cons(MK_INT(123), MK_INT(456));
    ASSERT(IS_CELL(v));
    ASSERT(!IS_OBJ(v));
    ASSERT(!IS_IMM(v));
    ASSERT(TO_INT(car(v)) == 123);
    ASSERT(TO_INT(cdr(v)) == 456);

    v0 = cons(v, NIL);
    ASSERT(IS_CELL(v0));

    v1 = cons(1, cons(2, cons(3, NIL)));
    ASSERT(IS_CELL(v1));

    v2 = cell_free(v0);
    ASSERT(v2 == NIL);

    v2 = obj_new(MK_PROC(p_fail), v1);
    ASSERT(IS_OBJ(v2));
    ASSERT(!IS_CELL(v2));
    ASSERT(!IS_IMM(v2));
    ASSERT(TO_PTR(v2) == TO_PTR(v0));  // re-used cell?
    n = TO_PTR(v2) >> 3;
    v1 = proc_call(cell[n].obj.code, cell[n].obj.data);

    v = cell_free(v);
    v2 = cell_free(v2);
    ASSERT(v2 == NIL);

    dv = cell_usage();
    c.raw = dv;
    fprintf(stderr, "cell usage: free=%"PRIi32" total=%"PRIi32" max=%"PRIi32"\n",
        TO_INT(c.cons.car), TO_INT(c.cons.cdr), VAL(CELL_MAX));
    ASSERT(c.cons.car == MK_INT(2));
    ASSERT(c.cons.cdr == MK_INT(5));

    v = symbol("eval");
    ASSERT(IS_SYM(v));
    ASSERT(IS_IMM(v));

    v0 = symbol("eval");
    ASSERT(IS_SYM(v0));
    ASSERT(v == v0);
    v0 = symbol("match");
    ASSERT(IS_SYM(v0));
    ASSERT(v != v0);

    v1 = symbol("foo");
    ASSERT(IS_SYM(v1));
    v2 = symbol("bar");
    ASSERT(IS_SYM(v2));
    ASSERT(v1 != v2);
    v = symbol("foo");
    ASSERT(IS_SYM(v));
    ASSERT(v1 == v);

    fprintf(stderr, "symbols: v0=%"PRIx32" v1=%"PRIx32" v2=%"PRIx32"\n", v0, v1, v2);

    return OK;
}

/*
 * bootstrap
 */

int main(int argc, char const *argv[])
{
    i32 result = unit_tests();
    fprintf(stderr, "result: %"PRIi32"\n", result);
    return (result == OK ? 0 : 1);
}
