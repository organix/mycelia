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
#include <inttypes.h>   // for PRIiPTR, PRIuPTR, PRIXPTR, etc.
#include <time.h>       // for clock_t, clock(), etc.

#define DEBUG(x)    x   // include/exclude debug instrumentation

#define USE_INT32_T   1 // define "machine word" as int32_t from <stdint.h>
#define USE_INT64_T   0 // define "machine word" as int64_t from <stdint.h>
#define USE_INTPTR_T  0 // define "machine word" as intptr_t from <stdint.h>

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

void debug_print(char *label, int_t addr);  // FORWARD DECLARATION

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
PROC_DECL(Free);
PROC_DECL(fn_emit);

#define Undef_T     (0)
#define Null_T      (1)
#define Pair_T      (2)
#define Symbol_T    (3)
#define Boolean_T   (4)
#define Unit_T      (5)
#define Free_T      (6)
#define FN_emit     (7)

proc_t proc_table[] = {
    Undef,
    Null,
    Pair,
    Symbol,
    Boolean,
    Unit,
    Free,  // free-cell marker
    fn_emit,
};
proc_t *proc_zero = &proc_table[0];  // base for proc offsets
#define PROC_MAX    NAT(sizeof(proc_table) / sizeof(proc_t))

int_t panic(char *reason);  // FORWARD DECLARATION
int_t error(char *reason);  // FORWARD DECLARATION
int_t failure(char *_file_, int _line_);  // FORWARD DECLARATION
#define ASSERT(cond)    if (!(cond)) return failure(__FILE__, __LINE__)

/*
 * heap memory management (cells)
 */

#define FALSE       (0)
#define TRUE        (1)
#define NIL         (2)
#define UNDEF       (3)
#define UNIT        (4)
#define A_EMIT      (5)
#define START       (6)

#define CELL_MAX (1<<12)  // 4K cells
cell_t cell_table[CELL_MAX] = {
    { .t = Boolean_T,   .x = FALSE,     .y = FALSE,     .z = UNDEF  },
    { .t = Boolean_T,   .x = TRUE,      .y = TRUE,      .z = UNDEF  },
    { .t = Null_T,      .x = NIL,       .y = NIL,       .z = UNDEF  },
    { .t = Undef_T,     .x = UNDEF,     .y = UNDEF,     .z = UNDEF  },
    { .t = Unit_T,      .x = UNIT,      .y = UNIT,      .z = UNDEF  },
    { .t = FN_emit,     .x = UNDEF,     .y = UNDEF,     .z = UNDEF  },
};
cell_t *cell_zero = &cell_table[0];  // base for cell offsets
int_t cell_next = NIL;  // head of cell free-list (or NIL if empty)
int_t cell_top = START; // limit of allocated cell memory

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

#define car(addr) get_x(addr)
#define cdr(addr) get_y(addr)

int_t equal(int_t x, int_t y) {
    if (x == y) return TRUE;
    while (IS_PAIR(x) && IS_PAIR(y)) {
        if (!equal(car(x), car(y))) break;
        x = cdr(x);
        y = cdr(y);
        if (x == y) return TRUE;
    }
    return 0;
}

int_t list_len(int_t val) {
    int_t len = 0;
    while (IS_PAIR(val)) {
        ++len;
        val = cdr(val);
    }
    return len;
}

/*
 * bootstrap
 */

PROC_DECL(Undef) {
    return error("Undef not implemented");
}

PROC_DECL(Null) {
    return error("Null not implemented");
}

PROC_DECL(Pair) {
    return error("Pair not implemented");
}

PROC_DECL(Symbol) {
    return error("Symbol not implemented");
}

PROC_DECL(Boolean) {
    return error("Boolean not implemented");
}

PROC_DECL(Unit) {
    return error("Unit not implemented");
}

PROC_DECL(fn_emit) {
    DEBUG(debug_print("fn_emit self", self));
    DEBUG(debug_print("fn_emit arg", arg));
    return UNIT;
}

void debug_print(char *label, int_t addr) {
    fprintf(stderr, "%s: t=%"PdI" x=%"PdI" y=%"PdI" z=%"PdI"\n",
        label, get_t(addr), get_x(addr), get_y(addr), get_z(addr));
}

int main(int argc, char const *argv[])
{
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
