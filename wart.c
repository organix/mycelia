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

#define PRINT_OBJ_DETAIL    0       // print code & data in objects

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
#define SET_GC(v)   ((v) | VAL_GC)
#define CLR_GC(v)   ((v) & ~VAL_GC)

// immediate values

#define MK_BOOL(z)  ((z) ? TRUE : FALSE)

#define IMM_24T     VAL(0x000000FC)
#define IMM_16T     VAL(0x0000FF00)
#define IMM_8T      VAL(0x00FF0000)

#define FALSE       VAL(0x0000FFFD)
#define TRUE        VAL(0x0100FFFD)
#define NIL         VAL(0x0200FFFD)
#define UNIT        VAL(0x0300FFFD)
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

// FORWARD DECLARATIONS
void newline();
void print(i32 value);
void debug_print(char *label, i32 value);

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
    return SET_GC(MK_OBJ(v));
}

i32 cons(i32 car, i32 cdr) {
    i32 v = cell_new();
    if (!IS_CELL(v)) return UNDEF;
    i32 ofs = TO_PTR(v) >> 3;
    cell[ofs].cons.car = car;
    cell[ofs].cons.cdr = cdr;
    return SET_GC(v);
}

#define list_0  NIL
#define list_1(v1)  cons((v1), NIL)
#define list_2(v1,v2)  cons((v1), cons((v2), NIL))
#define list_3(v1,v2,v3)  cons((v1), cons((v2), cons((v3), NIL)))

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

i32 set_car(i32 v, i32 a) {
    if (!IS_CELL(v)) panic("set_car() on non-cell");
    i32 ofs = TO_PTR(v) >> 3;
    return (cell[ofs].cons.car = a);
}

i32 set_cdr(i32 v, i32 d) {
    if (!IS_CELL(v)) panic("set_cdr() on non-cell");
    i32 ofs = TO_PTR(v) >> 3;
    return (cell[ofs].cons.cdr = d);
}

#define caar(v)  car(car(v))
#define cadr(v)  car(cdr(v))
#define cdar(v)  cdr(car(v))
#define cddr(v)  cdr(car(v))

#define ref_1(v)  car(v)
#define ref_2(v)  car(cdr(v))
#define ref_3(v)  car(cdr(cdr(v)))

i32 get_code(i32 v) {
    ASSERT(IS_OBJ(v));
    i32 ofs = TO_PTR(v) >> 3;
    return cell[ofs].obj.code;
}

i32 get_data(i32 v) {
    ASSERT(IS_OBJ(v));
    i32 ofs = TO_PTR(v) >> 3;
    return cell[ofs].obj.data;
}

i32 set_code(i32 v, i32 code) {
    if (!IS_OBJ(v)) panic("set_code() on non-object");
    i32 ofs = TO_PTR(v) >> 3;
    return (cell[ofs].obj.code = code);
}

i32 set_data(i32 v, i32 data) {
    if (!IS_OBJ(v)) panic("set_data() on non-object");
    i32 ofs = TO_PTR(v) >> 3;
    return (cell[ofs].obj.data = data);
}

/*
 * interned strings (symbols)
 */

#define INTERN_MAX (1024)
char intern[INTERN_MAX] = {
    5, 'q', 'u', 'o', 't', 'e',
    5, 't', 'y', 'p', 'e', 'q',
    4, 'e', 'v', 'a', 'l',
    5, 'a', 'p', 'p', 'l', 'y',
    2, 'i', 'f',
    3, 'm', 'a', 'p',
    4, 'f', 'o', 'l', 'd',
    5, 'f', 'o', 'l', 'd', 'r',
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

i32 sym_quote() {
    static i32 sym = UNDEF;
    if (sym == UNDEF) {
        sym = symbol("quote");
    }
    return sym;
}

i32 sym_typeq() {
    static i32 sym = UNDEF;
    if (sym == UNDEF) {
        sym = symbol("typeq");
    }
    return sym;
}

i32 sym_eval() {
    static i32 sym = UNDEF;
    if (sym == UNDEF) {
        sym = symbol("eval");
    }
    return sym;
}

/*
 * procedures
 */

PROC_DECL(fail) {
    DEBUG(fprintf(stderr, "fail: self=%"PRIx32", args=%"PRIx32"\n", self, args));
    XDEBUG(debug_print("fail self", self));
    XDEBUG(debug_print("fail args", args));
    return error("FAILED");
}

// FORWARD DECLARATIONS
PROC_DECL(actor);
PROC_DECL(sink_beh);
PROC_DECL(Unit);
PROC_DECL(assert_beh);

#define PROC_MAX (1024)
typedef PROC_DECL((*proc_ptr_t));
proc_ptr_t proc[PROC_MAX] = {
    fail,
    actor,
    sink_beh,
    Unit,
    assert_beh,
    0
};
enum proc_idx {
    p_fail,
    p_actor,
    p_sink_beh,
    p_Unit,
    p_assert_beh,
    p_unused,
} proc_idx_t;

PROC_DECL(proc_call) {
    ASSERT(IS_PROC(self));
    i32 idx = TO_PROC(self);
    return (proc[idx])(self, args);
}

PROC_DECL(obj_call) {
    ASSERT(IS_OBJ(self));
    i32 idx = TO_PROC(get_code(self));
    return (proc[idx])(self, args);
}

/*
 * actor primitives
 */

i32 actor_beh(i32 code, i32 data) {
    ASSERT(IS_PROC(code));
    return obj_new(code, data);
}

i32 effect_new() {
    return cons(NIL, cons(NIL, NIL));  // empty effect
}

inline int is_actor(i32 v) {
    return IS_OBJ(v) && (cell[TO_PTR(v) >> 3].obj.code == MK_PROC(p_actor));
}

i32 actor_create(i32 beh) {
    ASSERT(IS_OBJ(beh));
    return obj_new(MK_PROC(p_actor), beh);
}

i32 effect_create(i32 effect, i32 new_actor) {
    ASSERT(is_actor(new_actor));
    ASSERT(IS_CELL(effect));
    i32 created = cons(new_actor, car(effect));
    if (!IS_CELL(created)) return UNDEF;
    set_car(effect, created);
    return effect;
}

PROC_DECL(actor) {
    XDEBUG(debug_print("actor self", self));
    XDEBUG(debug_print("actor args", args));
    ASSERT(is_actor(self));
    i32 beh = get_data(self);
    ASSERT(IS_OBJ(beh));
    i32 idx = TO_PROC(get_code(beh));
    return (proc[idx])(self, args);  // target actor, instead of beh, as `self`
}

i32 actor_send(i32 target, i32 msg) {
    ASSERT(is_actor(target));
    return cons(target, msg);
}

i32 effect_send(i32 effect, i32 new_event) {
    ASSERT(IS_CELL(new_event));
    ASSERT(IS_CELL(effect));
    i32 rest = cdr(effect);
    ASSERT(IS_CELL(rest));
    i32 sent = cons(new_event, car(rest));
    if (!IS_CELL(sent)) return UNDEF;
    set_car(rest, sent);
    return effect;
}

i32 effect_become(i32 effect, i32 new_beh) {
    ASSERT(IS_OBJ(new_beh));
    ASSERT(IS_CELL(effect));
    i32 rest = cdr(effect);
    ASSERT(IS_CELL(rest));
    if (cdr(rest) != NIL) return error("must only BECOME once");
    set_cdr(rest, new_beh);
    return effect;
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

i32 apply_effect(i32 self, i32 effect) {
    if (!IS_CELL(effect)) return UNDEF;
    i32 actors = car(effect);
    if (actors == FAIL) return effect;  // error thrown
    i32 rest = cdr(effect);
    cell_free(effect);
    while (IS_CELL(actors)) {  // free list, but not actors
        i32 next = cdr(actors);
        cell_free(actors);
        actors = next;
    }
    i32 events = car(rest);
    i32 beh = cdr(rest);
    cell_free(rest);
    // update behavior
    if (IS_OBJ(beh) && is_actor(self)) {
        set_data(self, beh);
    }
    // add events to dispatch queue
    return event_q_append(events);
}

i32 event_dispatch() {
    i32 event = event_q_take();
    if (!IS_CELL(event)) return UNDEF;  // nothing to dispatch
    i32 target = car(event);
    i32 msg = cdr(event);
    cell_free(event);
    // invoke actor behavior
    i32 effect = obj_call(target, msg);
    return apply_effect(target, effect);
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

i32 get_locals(i32 self) {
    ASSERT(is_actor(self));
    i32 beh = get_data(self);
    return get_data(beh);
}

#define local_1(self) ref_1(get_locals(self))
#define local_2(self) ref_2(get_locals(self))
#define local_3(self) ref_3(get_locals(self))

PROC_DECL(sink_beh) {
    i32 effect = effect_new();
    XDEBUG(debug_print("sink_beh self", self));
    XDEBUG(debug_print("sink_beh args", args));
    return effect;
}

PROC_DECL(assert_beh) {
    i32 effect = effect_new();
    XDEBUG(debug_print("assert_beh self", self));
    i32 expect = get_locals(self);
    if (expect != args) {
        XDEBUG(debug_print("assert_beh actual", args));
        XDEBUG(debug_print("assert_beh expect", expect));
        return panic("assert_beh expect != actual");
    }
    return effect;
}

/*
 * higher-order actors
 */

i32 a_unit = UNDEF;

// locals: (T)
static PROC_DECL(Type) {
    i32 cust = ref_1(args);
    i32 req = ref_2(args);
    if (req == sym_typeq()) {
        i32 effect = effect_new();
        i32 T = local_1(self);
        if (T == ref_3(args)) {
            effect = effect_send(effect, actor_send(cust, TRUE));
        } else {
            effect = effect_send(effect, actor_send(cust, FALSE));
        }
        return effect;
    }
    return UNDEF;
}

// locals: (T)
static PROC_DECL(SeType) {
    i32 cust = ref_1(args);
    i32 req = ref_2(args);
    if (req == sym_eval()) {
        i32 effect = effect_new();
        effect = effect_send(effect, actor_send(cust, self));
        return effect;
    }
    return Type(self, args);  // delegate to Type
}

// locals: (T=Unit)
PROC_DECL(Unit) {
    return SeType(self, args);  // delegate to SeType
}

// construct higher-order actor environment
i32 actor_bootstrap() {
    i32 a, b, T;

    T = MK_PROC(p_Unit);
    b = actor_beh(T, list_1(T));
    a = actor_create(b);
    a_unit = CLR_GC(a);

    return OK;
}

/*
 * unit tests
 */

void newline() {
    printf("\n");
    fflush(stdout);
}

void print(i32 value) {
    if (IS_IMM(value)) {
        if (IS_INT(value)) {
            if (value == INF) {
                printf("#inf");
            } else {
                printf("%"PRIi32"", TO_INT(value));
            }
        } else {
            i32 t24 = (value & IMM_24T) >> 2;
            i32 v24 = (value >> 8) & 0xFFFFFF;
            if (t24 == 0x3F) {
                i32 t16 = (value & IMM_16T) >> 8;
                i32 v16 = (value >> 16) & 0xFFFF;
                if (t16 == 0xFF) {
                    i32 t8 = (value & IMM_8T) >> 16;
                    i32 v8 = (value >> 24) & 0xFF;
                    switch (value) {
                        case FALSE: printf("#f"); break;
                        case TRUE: printf("#t"); break;
                        case NIL: printf("()"); break;
                        case UNIT: printf("#unit"); break;
                        case FAIL: printf("#fail"); break;
                        case UNDEF: printf("#undefined"); break;
                        default: printf("#<%"PRIi32":%02"PRIx32">", t8, v8);
                    }
                } else if (t16 == 0x00) {
                    //printf("#sym-%"PRIi32"", v16);
                    //printf(" ");
                    char *p = intern + v16;
                    printf("%.*s", (int)(*p), (p + 1));
                } else if (t16 == 0x01) {
                    printf("#proc-%"PRIi32"", v16);
                } else {
                    printf("#<%"PRIi32":%04"PRIx32">", t16, v16);
                }
            } else if (t24 == 0x00) {
                printf("#U+%04"PRIX32"", v24);
            } else {
                printf("#<%"PRIi32":%06"PRIx32">", t24, v24);
            }
        }
    } else if (IS_PTR(value)) {
        if (IS_CELL(value)) {
            char *s = "(";
            while (IS_CELL(value)) {
                printf("%s", s);
                print(car(value));
                s = " ";
                value = cdr(value);
            };
            if (value != NIL) {
                printf(" . ");
                print(value);
            }
            printf(")");
        } else if (IS_OBJ(value)) {
            i32 code = get_code(value);
            i32 data = get_data(value);
            if (code == MK_PROC(p_actor)) {
                i32 ofs = TO_PTR(value) >> 3;
                printf("#actor-%"PRIi32"", ofs);
#if PRINT_OBJ_DETAIL
                printf("<");
                i32 beh = data;
                if (IS_OBJ(beh)) {
                    code = get_code(beh);
                    data = get_data(beh);
                    print(code);
                    printf(",");
                    print(data);
                }
                printf(">");
#endif
            } else {
                i32 ofs = TO_PTR(value) >> 3;
                printf("#object-%"PRIi32"", ofs);
#if PRINT_OBJ_DETAIL
                printf("<");
                print(code);
                printf(",");
                print(data);
                printf(">");
#endif
            }
        }
    } else {
        printf("#<%08"PRIx32">", value);
    }
}

void debug_print(char *label, i32 value) {
    fprintf(stderr, "%s:", label);
    fprintf(stderr, " %08"PRIx32"", value);
    if (IS_IMM(value)) {
        fprintf(stderr, " IMM");
        if (IS_INT(value)) {
            fprintf(stderr, " INT");
        } else {
            i32 t24 = (value & IMM_24T);
            if (t24 == IMM_24T) {
                i32 t16 = (value & IMM_16T);
                if (t16 == IMM_16T) {
                    i32 t8 = (value & IMM_8T);
                    fprintf(stderr, " t8=%"PRIi32"", (t8 >> 16));
                    fprintf(stderr, ":%02"PRIx32"", (value >> 24) & 0xFF);
                } else {
                    fprintf(stderr, " t16=%"PRIi32"", (t16 >> 8));
                    fprintf(stderr, ":%04"PRIx32"", (value >> 16) & 0xFFFF);
                }
            } else {
                fprintf(stderr, " t24=%"PRIi32"", (t24 >> 2));
                fprintf(stderr, ":%06"PRIx32"", (value >> 8) & 0xFFFFFF);
            }
        }
    }
    if (IS_PTR(value)) {
        fprintf(stderr, " PTR");
        if (IS_GC(value)) fprintf(stderr, "+GC");
        if (IS_CELL(value)) {
            i32 ofs = TO_PTR(value) >> 3;
            fprintf(stderr, " cell[%"PRIi32"]", ofs);
            fprintf(stderr, " car=%"PRIx32" cdr=%"PRIx32"",
                cell[ofs].cons.car, cell[ofs].cons.cdr);
        }
        if (IS_OBJ(value)) {
            i32 ofs = TO_PTR(value) >> 3;
            if (cell[ofs].obj.code == MK_PROC(p_actor)) {
                fprintf(stderr, " actor[%"PRIi32"]", ofs);
                i32 beh = cell[ofs].obj.data;
                fprintf(stderr, " beh=%"PRIx32"", beh);
                if (IS_OBJ(beh)) {
                    ofs = TO_PTR(beh) >> 3;
                    fprintf(stderr, "->[%"PRIi32"]", ofs);
                    fprintf(stderr, " code=%"PRIx32" data=%"PRIx32"",
                        cell[ofs].obj.code, cell[ofs].obj.data);
                }
            } else {
                fprintf(stderr, " obj[%"PRIi32"]", ofs);
                fprintf(stderr, " code=%"PRIx32" data=%"PRIx32"",
                    cell[ofs].obj.code, cell[ofs].obj.data);
            }
        }
    }
    //fprintf(stderr, "\n");
    fprintf(stderr, " ");
    fflush(stderr);
    print(value);
    newline();
}

i32 test_actors() {
    ASSERT(actor_bootstrap() == OK);

    i32 effect = effect_new();
    DEBUG(debug_print("test_actors new effect", effect));
    i32 b = actor_beh(MK_PROC(p_sink_beh), UNDEF);
    DEBUG(debug_print("test_actors b", b));
    i32 a = actor_create(b);
    DEBUG(debug_print("test_actors a", a));
    effect = effect_create(effect, a);
    DEBUG(debug_print("test_actors create effect", effect));
    i32 m = list_2(sym_eval(), NIL);
    i32 e = actor_send(a, m);
    DEBUG(debug_print("test_actors e", e));
    effect = effect_send(effect, e);
    DEBUG(debug_print("test_actors send effect", effect));
    i32 x = apply_effect(UNDEF, effect);
    DEBUG(debug_print("test_actors apply effect", x));
    i32 r = event_dispatch();
    XDEBUG(debug_print("test_actors event_dispatch", r));

    effect = effect_new();
    // a_unit is self-evaluating
    b = actor_beh(MK_PROC(p_assert_beh), a_unit);
    a = actor_create(b);
    effect = effect_create(effect, a);
    m = list_3(a, sym_eval(), NIL);
    e = actor_send(a_unit, m);
    effect = effect_send(effect, e);
    // a_unit has Unit type
    b = actor_beh(MK_PROC(p_assert_beh), TRUE);
    a = actor_create(b);
    effect = effect_create(effect, a);
    m = list_3(a, sym_typeq(), MK_PROC(p_Unit));
    e = actor_send(a_unit, m);
    effect = effect_send(effect, e);
    // dispatch all pending events
    ASSERT(apply_effect(UNDEF, effect) == OK);
    r = event_loop();
    XDEBUG(debug_print("test_actors event_loop", r));

    return OK;
}

i32 unit_tests() {
    i32 v, v0, v1, v2;
    i32 n;
    i64 dv;
    cell_t c;

    XDEBUG(debug_print("unit_tests ZERO", ZERO));
    XDEBUG(debug_print("unit_tests ONE", ONE));
    XDEBUG(debug_print("unit_tests INF", INF));
    XDEBUG(debug_print("unit_tests TRUE", FALSE));
    XDEBUG(debug_print("unit_tests FALSE", TRUE));
    XDEBUG(debug_print("unit_tests NIL", NIL));
    XDEBUG(debug_print("unit_tests UNIT", UNIT));
    XDEBUG(debug_print("unit_tests FAIL", FAIL));
    XDEBUG(debug_print("unit_tests UNDEF", UNDEF));

    v = cons(MK_INT(123), MK_INT(456));
    ASSERT(IS_CELL(v));
    ASSERT(!IS_OBJ(v));
    ASSERT(!IS_IMM(v));
    XDEBUG(debug_print("unit_tests cons v", v));
    XDEBUG(debug_print("unit_tests cons car(v)", car(v)));
    XDEBUG(debug_print("unit_tests cons cdr(v)", cdr(v)));
    ASSERT(TO_INT(car(v)) == 123);
    ASSERT(TO_INT(cdr(v)) == 456);

    v0 = cons(v, NIL);
    XDEBUG(debug_print("unit_tests cons v0", v0));
    ASSERT(IS_CELL(v0));

    v1 = list_3(MK_INT(1), MK_INT(2), MK_INT(3));
    XDEBUG(debug_print("unit_tests cons v1", v1));
    ASSERT(IS_CELL(v1));

    v2 = cell_free(v0);
    ASSERT(v2 == NIL);

    v2 = obj_new(MK_PROC(p_fail), v1);
    XDEBUG(debug_print("unit_tests obj_new v2", v2));
    ASSERT(IS_OBJ(v2));
    ASSERT(!IS_CELL(v2));
    ASSERT(!IS_IMM(v2));
    ASSERT(TO_PTR(v2) == TO_PTR(v0));  // re-used cell?
    v1 = proc_call(get_code(v2), get_data(v2));

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

    v0 = sym_eval();
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

    XDEBUG(debug_print("unit_tests symbol v0", v0));
    XDEBUG(debug_print("unit_tests symbol v1", v1));
    XDEBUG(debug_print("unit_tests symbol v2", v2));

    if (test_actors() != OK) return UNDEF;

    dv = cell_usage();
    c.raw = dv;
    fprintf(stderr, "cell usage: free=%"PRIi32" total=%"PRIi32" max=%"PRIi32"\n",
        TO_INT(c.cons.car), TO_INT(c.cons.cdr), VAL(CELL_MAX));

    return OK;
}

/*
 * bootstrap
 */

int main(int argc, char const *argv[])
{
    i32 result = unit_tests();
    XDEBUG(debug_print("result", result));
    return (result == OK ? 0 : 1);
}
