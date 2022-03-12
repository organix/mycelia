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

#define TO_INT(v)   ((v) >> 2)
#define TO_PTR(v)   ((v) & ~PTR_MASK)

#define MK_INT(n)   VAL((n) << 2)
#define MK_CELL(p)  ((VAL(p) & ~PTR_MASK) | PTR_CELL)
#define MK_OBJ(p)   ((VAL(p) & ~PTR_MASK) | PTR_OBJ)
#define SET_GC(v)   ((v) |= VAL_GC)
#define CLR_GC(v)   ((v) &= ~VAL_GC)

// Immediate Values

#define MK_BOOL(z)  ((z) ? TRUE : FALSE)

#define FALSE       VAL(0x0000FFFD)
#define TRUE        VAL(0x0001FFFD)
#define NIL         VAL(0x0002FFFD)
#define UNDEF       VAL(0xFF00FFFD)

#define ZERO        VAL(0x00000000)
#define ONE         VAL(0x00000004)
#define INF         VAL(0x80000000)

// Procedure declaration

#define PROC_DECL(name)  i32 name(i32 self, i32 msg)

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
    if (!IS_CELL(v)) return UNDEF;
    i32 ofs = TO_PTR(v) >> 3;
    return cell[ofs].cons.car;
}

i32 cdr(i32 v) {
    if (!IS_CELL(v)) return UNDEF;
    i32 ofs = TO_PTR(v) >> 3;
    return cell[ofs].cons.cdr;
}

/*
 * unit tests
 */
i32 unit_tests() {
    i32 v, v0, v1, v2;
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

    dv = cell_usage();
    c.raw = dv;
    fprintf(stderr, "cell usage: %"PRIi32" free, %"PRIi32" total, %"PRIi32" max\n",
        TO_INT(c.cons.car), TO_INT(c.cons.cdr), VAL(CELL_MAX));

    return OK;
}

/*
 * bootstrap
 */

int main(int argc, char const *argv[])
{
    i32 result = unit_tests();
    fprintf(stderr, "result = %"PRIi32"\n", result);
    return (result == OK ? 0 : 1);
}
