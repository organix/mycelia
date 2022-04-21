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
#define XTRACE(x)       // include/exclude execution trace
#else
#define DEBUG(x)        // exclude debug instrumentation
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

int_t sane = 0;  // run-away loop prevention
#define SANITY (420)

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
PROC_DECL(Boolean);
PROC_DECL(Null);
PROC_DECL(Pair);
PROC_DECL(Symbol);
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
PROC_DECL(vm_self);
PROC_DECL(vm_send);
PROC_DECL(vm_new);
PROC_DECL(vm_beh);
PROC_DECL(vm_end);
PROC_DECL(vm_putc);
PROC_DECL(vm_getc);
PROC_DECL(vm_debug);

#define Undef_T     (-1)
#define Boolean_T   (-2)
#define Null_T      (-3)
#define Pair_T      (-4)
#define Symbol_T    (-5)
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
#define VM_self     (-25)
#define VM_send     (-26)
#define VM_new      (-27)
#define VM_beh      (-28)
#define VM_end      (-29)
#define VM_putc     (-30)
#define VM_getc     (-31)
#define VM_debug    (-32)

#define PROC_MAX    NAT(sizeof(proc_table) / sizeof(proc_t))
proc_t proc_table[] = {
    vm_debug,
    vm_getc,
    vm_putc,
    vm_end,
    vm_beh,
    vm_new,
    vm_send,
    vm_self,
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
    Symbol,
    Pair,
    Null,
    Boolean,
    Undef,
};
proc_t *proc_zero = &proc_table[PROC_MAX];  // base for proc offsets

#if INCLUDE_DEBUG
static char *proc_label(int_t proc) {
    static char *label[] = {
        "Undef_T",
        "Boolean_T",
        "Null_T",
        "Pair_T",
        "Symbol_T",
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
        "VM_self",
        "VM_send",
        "VM_new",
        "VM_beh",
        "VM_end",
        "VM_putc",
        "VM_getc",
        "VM_debug",
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

// VM_end thread action
#define END_ABORT   (-1)
#define END_STOP    (0)
#define END_COMMIT  (+1)

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
    { .t=Boolean_T,     .x=FALSE,       .y=FALSE,       .z=UNDEF        },
    { .t=Boolean_T,     .x=TRUE,        .y=TRUE,        .z=UNDEF        },
    { .t=Null_T,        .x=NIL,         .y=NIL,         .z=UNDEF        },
    { .t=Undef_T,       .x=UNDEF,       .y=UNDEF,       .z=UNDEF        },
    { .t=Unit_T,        .x=UNIT,        .y=UNIT,        .z=UNDEF        },
//    { .t=Event_T,       .x=A_BOOT,      .y=NIL,         .z=NIL          },  // <--- START = (A_BOOT)
    { .t=Event_T,       .x=171,         .y=NIL,         .z=NIL          },  // <--- START = (A_TEST)
    { .t=Actor_T,       .x=A_BOOT+1,    .y=UNDEF,       .z=UNDEF        },  // <--- A_BOOT
    { .t=VM_push,       .x='>',         .y=A_BOOT+2,    .z=UNDEF        },
    { .t=VM_putc,       .x=UNDEF,       .y=A_BOOT+3,    .z=UNDEF        },
    { .t=VM_push,       .x=' ',         .y=A_BOOT+4,    .z=UNDEF        },
    { .t=VM_putc,       .x=UNDEF,       .y=A_BOOT+5,    .z=UNDEF        },
    { .t=VM_push,       .x=NIL,         .y=A_BOOT+6,    .z=UNDEF        },
    { .t=VM_self,       .x=UNDEF,       .y=A_BOOT+7,    .z=UNDEF        },
    { .t=VM_send,       .x=0,           .y=A_BOOT+8,    .z=UNDEF        },
    { .t=VM_push,       .x=A_BOOT+11,   .y=A_BOOT+9,    .z=UNDEF        },
    { .t=VM_beh,        .x=0,           .y=A_BOOT+10,   .z=UNDEF        },
    { .t=VM_end,        .x=END_COMMIT,  .y=UNDEF,       .z=UNDEF        },
    { .t=VM_getc,       .x=UNDEF,       .y=A_BOOT+12,   .z=UNDEF        },  // +11
    { .t=VM_pick,       .x=1,           .y=A_BOOT+13,   .z=UNDEF        },
    { .t=VM_push,       .x='\0',        .y=A_BOOT+14,   .z=UNDEF        },
    { .t=VM_cmp,        .x=CMP_LT,      .y=A_BOOT+15,   .z=UNDEF        },
    { .t=VM_if,         .x=A_BOOT+21,   .y=A_BOOT+16,   .z=UNDEF        },
    { .t=VM_putc,       .x=UNDEF,       .y=A_BOOT+17,   .z=UNDEF        },
//    { .t=VM_debug,      .x=7331,        .y=A_BOOT+17,   .z=UNDEF        },
    { .t=VM_push,       .x=NIL,         .y=A_BOOT+18,   .z=UNDEF        },
    { .t=VM_self,       .x=UNDEF,       .y=A_BOOT+19,   .z=UNDEF        },
    { .t=VM_send,       .x=0,           .y=A_BOOT+20,   .z=UNDEF        },
    { .t=VM_end,        .x=END_COMMIT,  .y=UNDEF,       .z=UNDEF        },  // +21
    { .t=VM_drop,       .x=1,           .y=A_BOOT+20,   .z=UNDEF        },  // A_BOOT #22

#define A_CLOCK (A_BOOT+22)
    { .t=Actor_T,       .x=A_CLOCK+3,   .y=UNDEF,       .z=UNDEF        },  // note: skipping output...
    { .t=VM_push,       .x='.',         .y=A_CLOCK+2,   .z=UNDEF        },
    { .t=VM_putc,       .x=UNDEF,       .y=A_CLOCK+3,   .z=UNDEF        },
    { .t=VM_end,        .x=END_COMMIT,  .y=UNDEF,       .z=UNDEF        },  // A_CLOCK #4

#define A_PRINT (A_CLOCK+4)
    { .t=Actor_T,       .x=A_PRINT+1,   .y=UNDEF,       .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=A_PRINT+2,   .z=UNDEF        },
    { .t=VM_debug,      .x=7331,        .y=A_PRINT+3,   .z=UNDEF        },
    { .t=VM_end,        .x=END_COMMIT,  .y=UNDEF,       .z=UNDEF        },  // A_PRINT #4
/*
(define empty-env
  (CREATE
    (BEH (cust _index)
      (SEND cust #undefined))))
*/

#define EMPTY_ENV (A_PRINT+4)
    { .t=Actor_T,       .x=EMPTY_ENV+1, .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=UNDEF,       .y=EMPTY_ENV+2, .z=UNDEF        },
    { .t=VM_msg,        .x=1,           .y=EMPTY_ENV+3, .z=UNDEF        },
    { .t=VM_send,       .x=0,           .y=EMPTY_ENV+4, .z=UNDEF        },
    { .t=VM_end,        .x=END_COMMIT,  .y=UNDEF,       .z=UNDEF        },  // EMPTY_ENV #5
/*
(define bound-beh  ; lookup variable by De Bruijn index
  (lambda (value next)
    (BEH (cust index)
      (define index (- index 1))
      (if (zero? index)
        (SEND cust value)
        (SEND next (list cust index))))))
*/

#define BOUND_BEH (EMPTY_ENV+5)
//  { .t=VM_push,       .x=_value_,     .y=BOUND_BEH-1, .z=UNDEF        },
//  { .t=VM_push,       .x=_next_,      .y=BOUND_BEH+0, .z=UNDEF        },
    { .t=VM_msg,        .x=2,           .y=BOUND_BEH+1, .z=UNDEF        },  // index
    { .t=VM_push,       .x=1,           .y=BOUND_BEH+2, .z=UNDEF        },  // 1
    { .t=VM_alu,        .x=ALU_SUB,     .y=BOUND_BEH+3, .z=UNDEF        },  // index-1
    { .t=VM_pick,       .x=1,           .y=BOUND_BEH+4, .z=UNDEF        },  // index-1 index-1
    { .t=VM_eq,         .x=0,           .y=BOUND_BEH+5, .z=UNDEF        },  // index-1 == 0
    { .t=VM_if,         .x=BOUND_BEH+14,.y=BOUND_BEH+6, .z=UNDEF        },
    { .t=VM_push,       .x=NIL,         .y=BOUND_BEH+7, .z=UNDEF        },  // ()
    { .t=VM_pick,       .x=2,           .y=BOUND_BEH+8, .z=UNDEF        },  // index-1
    { .t=VM_pair,       .x=1,           .y=BOUND_BEH+9, .z=UNDEF        },  // (index-1)
    { .t=VM_msg,        .x=1,           .y=BOUND_BEH+10,.z=UNDEF        },  // cust
    { .t=VM_pair,       .x=1,           .y=BOUND_BEH+11,.z=UNDEF        },  // (cust index-1)
    { .t=VM_pick,       .x=3,           .y=BOUND_BEH+12,.z=UNDEF        },  // next
    { .t=VM_send,       .x=0,           .y=BOUND_BEH+13,.z=UNDEF        },  // (next cust index-1) | (cust value)
    { .t=VM_end,        .x=END_COMMIT,  .y=UNDEF,       .z=UNDEF        },
    { .t=VM_pick,       .x=3,           .y=BOUND_BEH+15,.z=UNDEF        },  // value
    { .t=VM_msg,        .x=1,           .y=BOUND_BEH+12,.z=UNDEF        },  // cust -- BOUND_BEH #16+2
/*
(define const-beh
  (lambda (value)
    (BEH (cust _)             ; eval
      (SEND cust value))))
*/

#define CONST_BEH (BOUND_BEH+16)
//  { .t=VM_push,       .x=_value_,     .y=CONST_BEH+0, .z=UNDEF        },
    { .t=VM_msg,        .x=1,           .y=CONST_BEH+1, .z=UNDEF        },  // cust
    { .t=VM_send,       .x=0,           .y=CONST_BEH+2, .z=UNDEF        },  // (cust . value)
    { .t=VM_end,        .x=END_COMMIT,  .y=UNDEF,       .z=UNDEF        },  // CONST_BEH #3
#define CONST_7 (CONST_BEH+3)
    { .t=Actor_T,       .x=CONST_7+1,   .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=7,           .y=CONST_BEH,   .z=UNDEF        },  // value = 7
/*
(define var-beh
  (lambda (index)
    (BEH (cust env)           ; eval
      (SEND env (list cust index)))))
*/

#define VAR_BEH (CONST_7+2)
//  { .t=VM_push,       .x=_index_,     .y=VAR_BEH+0,   .z=UNDEF        },
    { .t=VM_push,       .x=NIL,         .y=VAR_BEH+1,   .z=UNDEF        },  // ()
    { .t=VM_pick,       .x=2,           .y=VAR_BEH+2,   .z=UNDEF        },  // index
    { .t=VM_msg,        .x=1,           .y=VAR_BEH+3,   .z=UNDEF        },  // cust
    { .t=VM_pair,       .x=2,           .y=VAR_BEH+4,   .z=UNDEF        },  // (cust index)
    { .t=VM_msg,        .x=2,           .y=VAR_BEH+5,   .z=UNDEF        },  // env
    { .t=VM_send,       .x=0,           .y=VAR_BEH+6,   .z=UNDEF        },  // (env cust index)
    { .t=VM_end,        .x=END_COMMIT,  .y=UNDEF,       .z=UNDEF        },  // VAR_BEH #7
#define VAR_1 (VAR_BEH+7)
    { .t=Actor_T,       .x=VAR_1+1,     .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=1,           .y=VAR_BEH,     .z=UNDEF        },  // index = 1
/*
(define k-apply-beh
  (lambda (cust oper env)
    (BEH arg
      (SEND oper
        (list cust arg env)))))
*/

#define K_APPLY (VAR_1+2)
//  { .t=VM_push,       .x=_cust_,      .y=K_APPLY-2,   .z=UNDEF        },
//  { .t=VM_push,       .x=_oper_,      .y=K_APPLY-1,   .z=UNDEF        },
//  { .t=VM_push,       .x=_env_,       .y=K_APPLY+0,   .z=UNDEF        },
    { .t=VM_push,       .x=NIL,         .y=K_APPLY+1,   .z=UNDEF        },  // ()
    { .t=VM_pick,       .x=2,           .y=K_APPLY+2,   .z=UNDEF        },  // env
    { .t=VM_msg,        .x=0,           .y=K_APPLY+3,   .z=UNDEF        },  // arg
    { .t=VM_pick,       .x=6,           .y=K_APPLY+4,   .z=UNDEF        },  // cust
    { .t=VM_pair,       .x=3,           .y=K_APPLY+5,   .z=UNDEF        },  // (cust arg env)
    { .t=VM_pick,       .x=3,           .y=K_APPLY+6,   .z=UNDEF        },  // oper
    { .t=VM_send,       .x=0,           .y=K_APPLY+7,   .z=UNDEF        },  // (oper cust arg env)
    { .t=VM_end,        .x=END_COMMIT,  .y=UNDEF,       .z=UNDEF        },  // K_APPLY #8

/*
(define appl-beh
  (lambda (oper senv)
    (BEH (cust param . opt-env)
      (if (null? opt-env)
        (SEND cust SELF)      ; eval
        (SEND param           ; apply
          (list (CREATE (k-apply-beh cust oper senv)) (car opt-env)))
      ))))
*/
#define APPL_BEH (K_APPLY+8)
//  { .t=VM_push,       .x=_oper_,      .y=APPL_BEH-1,  .z=UNDEF        },
//  { .t=VM_push,       .x=_env_,       .y=APPL_BEH+0,  .z=UNDEF        },
    { .t=VM_msg,        .x=-2,          .y=APPL_BEH+1,  .z=UNDEF        },  // opt-env
    { .t=VM_eq,         .x=NIL,         .y=APPL_BEH+2,  .z=UNDEF        },  // opt-env == ()
    { .t=VM_if,         .x=APPL_BEH+21, .y=APPL_BEH+3,  .z=UNDEF        },

//  { .t=VM_push,       .x=_env_,       .y=K_APPLY+0,   .z=UNDEF        },
    { .t=VM_push,       .x=VM_push,     .y=APPL_BEH+4,  .z=UNDEF        },  // VM_push
    { .t=VM_pick,       .x=2,           .y=APPL_BEH+5,  .z=UNDEF        },  // env
    { .t=VM_push,       .x=K_APPLY,     .y=APPL_BEH+6,  .z=UNDEF        },  // K_APPLY
    { .t=VM_cell,       .x=3,           .y=APPL_BEH+7,  .z=UNDEF        },  // {t:VM_push, x:env, y:K_APPLY}

//  { .t=VM_push,       .x=_oper_,      .y=K_APPLY-1,   .z=UNDEF        },
    { .t=VM_push,       .x=VM_push,     .y=APPL_BEH+8,  .z=UNDEF        },  // VM_push
    { .t=VM_pick,       .x=4,           .y=APPL_BEH+9,  .z=UNDEF        },  // oper
    { .t=VM_pick,       .x=3,           .y=APPL_BEH+10, .z=UNDEF        },  // K_APPLY-1
    { .t=VM_cell,       .x=3,           .y=APPL_BEH+11, .z=UNDEF        },  // {t:VM_push, x:oper, y:K_APPLY-1}

//  { .t=VM_push,       .x=_cust_,      .y=K_APPLY-2,   .z=UNDEF        },
    { .t=VM_push,       .x=VM_push,     .y=APPL_BEH+12, .z=UNDEF        },  // VM_push
    { .t=VM_msg,        .x=1,           .y=APPL_BEH+13, .z=UNDEF        },  // cust
    { .t=VM_pick,       .x=3,           .y=APPL_BEH+14, .z=UNDEF        },  // K_APPLY-2
    { .t=VM_cell,       .x=3,           .y=APPL_BEH+15, .z=UNDEF        },  // {t:VM_push, x:cust, y:K_APPLY-2}

    { .t=VM_new,        .x=0,           .y=APPL_BEH+16, .z=UNDEF        },  // k_apply

    { .t=VM_push,       .x=NIL,         .y=APPL_BEH+17, .z=UNDEF        },  // ()
    { .t=VM_msg,        .x=3,           .y=APPL_BEH+18, .z=UNDEF        },  // env
    { .t=VM_pick,       .x=3,           .y=APPL_BEH+19, .z=UNDEF        },  // k_apply
    { .t=VM_pair,       .x=2,           .y=APPL_BEH+20, .z=UNDEF        },  // (k_apply env)
    { .t=VM_msg,        .x=2,           .y=APPL_BEH+23, .z=UNDEF        },  // param

    { .t=VM_self,       .x=UNDEF,       .y=APPL_BEH+22, .z=UNDEF        },  // SELF
    { .t=VM_msg,        .x=1,           .y=APPL_BEH+23, .z=UNDEF        },  // cust
    { .t=VM_send,       .x=0,           .y=APPL_BEH+24, .z=UNDEF        },  // (cust . SELF) | (param k_apply env)
    { .t=VM_end,        .x=END_COMMIT,  .y=UNDEF,       .z=UNDEF        },  // APPL_BEH #25

/*
(define oper-beh
  (lambda (body)
    (BEH (cust arg . opt-env)
      (if (null? opt-env)
        (SEND cust SELF)      ; eval
        (SEND body            ; apply
          (list cust (CREATE (bound-beh arg (car opt-env)))))
      ))))
*/
#define OPER_BEH (APPL_BEH+25)
//  { .t=VM_push,       .x=_body_,      .y=OPER_BEH+0,  .z=UNDEF        },
    { .t=VM_msg,        .x=-2,          .y=OPER_BEH+1,  .z=UNDEF        },  // opt-env
    { .t=VM_eq,         .x=NIL,         .y=OPER_BEH+2,  .z=UNDEF        },  // opt-env == ()
    { .t=VM_if,         .x=OPER_BEH+17, .y=OPER_BEH+3,  .z=UNDEF        },

//  { .t=VM_push,       .x=_next_,      .y=BOUND_BEH+0, .z=UNDEF        },
    { .t=VM_push,       .x=VM_push,     .y=OPER_BEH+4,  .z=UNDEF        },  // VM_push
    { .t=VM_msg,        .x=3,           .y=OPER_BEH+5,  .z=UNDEF        },  // next = env
    { .t=VM_push,       .x=BOUND_BEH,   .y=OPER_BEH+6,  .z=UNDEF        },  // BOUND_BEH
    { .t=VM_cell,       .x=3,           .y=OPER_BEH+7,  .z=UNDEF        },  // {t:VM_push, x:next, y:BOUND_BEH}

//  { .t=VM_push,       .x=_value_,     .y=BOUND_BEH-1, .z=UNDEF        },
    { .t=VM_push,       .x=VM_push,     .y=OPER_BEH+8,  .z=UNDEF        },  // VM_push
    { .t=VM_msg,        .x=2,           .y=OPER_BEH+9,  .z=UNDEF        },  // value = arg
    { .t=VM_pick,       .x=3,           .y=OPER_BEH+10, .z=UNDEF        },  // BOUND_BEH-1
    { .t=VM_cell,       .x=3,           .y=OPER_BEH+11, .z=UNDEF        },  // {t:VM_push, x:value, y:BOUND_BEH-1}

    { .t=VM_new,        .x=0,           .y=OPER_BEH+12, .z=UNDEF        },  // ext-env

    { .t=VM_push,       .x=NIL,         .y=OPER_BEH+13, .z=UNDEF        },  // ()
    { .t=VM_pick,       .x=2,           .y=OPER_BEH+14, .z=UNDEF        },  // ext-env
    { .t=VM_msg,        .x=1,           .y=OPER_BEH+15, .z=UNDEF        },  // cust
    { .t=VM_pair,       .x=2,           .y=OPER_BEH+16, .z=UNDEF        },  // (cust ext-env)
    { .t=VM_pick,       .x=4,           .y=OPER_BEH+19, .z=UNDEF        },  // body

    { .t=VM_self,       .x=UNDEF,       .y=OPER_BEH+18, .z=UNDEF        },  // SELF
    { .t=VM_msg,        .x=1,           .y=OPER_BEH+19, .z=UNDEF        },  // cust
    { .t=VM_send,       .x=0,           .y=OPER_BEH+20, .z=UNDEF        },  // (cust . SELF) | (body cust ext-env)
    { .t=VM_end,        .x=END_COMMIT,  .y=UNDEF,       .z=UNDEF        },  // OPER_BEH #21

/*
(define op-lambda             ; (lambda <body>)
  (CREATE
    (BEH (cust body . opt-env)
      (if (null? opt-env)
        (SEND cust SELF)      ; eval
        (SEND cust            ; apply
          (CREATE (appl-beh (CREATE (oper-beh body)) (car opt-env))))
      ))))
*/
#define OP_LAMBDA (OPER_BEH+21)
    { .t=Actor_T,       .x=OP_LAMBDA+1, .y=UNDEF,       .z=UNDEF        },
    { .t=VM_msg,        .x=-2,          .y=OP_LAMBDA+2, .z=UNDEF        },  // opt-env
    { .t=VM_eq,         .x=NIL,         .y=OP_LAMBDA+3, .z=UNDEF        },  // opt-env == ()
    { .t=VM_if,         .x=OP_LAMBDA+10,.y=OP_LAMBDA+4, .z=UNDEF        },

    { .t=VM_msg,        .x=2,           .y=OP_LAMBDA+5, .z=UNDEF        },  // body
    { .t=VM_push,       .x=OPER_BEH,    .y=OP_LAMBDA+6, .z=UNDEF        },  // OPER_BEH
    { .t=VM_new,        .x=1,           .y=OP_LAMBDA+7, .z=UNDEF        },  // oper

    //{ .t=VM_pick,       .x=1,           .y=OP_LAMBDA+0, .z=UNDEF        },  // oper
    { .t=VM_msg,        .x=3,           .y=OP_LAMBDA+8,.z=UNDEF        },  // env
    { .t=VM_push,       .x=APPL_BEH,    .y=OP_LAMBDA+9,.z=UNDEF        },  // APPL_BEH
    { .t=VM_new,        .x=2,           .y=OP_LAMBDA+11,.z=UNDEF        },  // appl

    { .t=VM_self,       .x=UNDEF,       .y=OP_LAMBDA+11,.z=UNDEF        },  // SELF
    { .t=VM_msg,        .x=1,           .y=OP_LAMBDA+12,.z=UNDEF        },  // cust
    { .t=VM_send,       .x=0,           .y=OP_LAMBDA+13,.z=UNDEF        },  // (cust . SELF) | (cust . appl)
    { .t=VM_end,        .x=END_COMMIT,  .y=UNDEF,       .z=UNDEF        },  // OP_LAMBDA #14

/*
(define k-call-beh
  (lambda (msg)
    (BEH oper
      (SEND oper msg))))
*/
#define K_CALL (OP_LAMBDA+14)
//  { .t=VM_push,       .x=_msg_,       .y=K_CALL+0,    .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=K_CALL+1,    .z=UNDEF        },  // oper
    { .t=VM_send,       .x=0,           .y=K_CALL+2,    .z=UNDEF        },  // (oper . msg)
    { .t=VM_end,        .x=END_COMMIT,  .y=UNDEF,       .z=UNDEF        },  // K_CALL #3

/*
(define comb-beh
  (lambda (comb param)
    (BEH (cust env)           ; eval
      (SEND comb
        (list (CREATE (k-call-beh (list cust param env))) env)))))
*/
#define COMB_BEH (K_CALL+3)
//  { .t=VM_push,       .x=_comb_,      .y=COMB_BEH-1,  .z=UNDEF        },
//  { .t=VM_push,       .x=_param_,     .y=COMB_BEH+0,  .z=UNDEF        },
    { .t=VM_push,       .x=NIL,         .y=COMB_BEH+1,  .z=UNDEF        },  // ()
    { .t=VM_msg,        .x=2,           .y=COMB_BEH+2,  .z=UNDEF        },  // env

//  { .t=VM_push,       .x=_msg_,       .y=K_CALL+0,    .z=UNDEF        },
    { .t=VM_push,       .x=VM_push,     .y=COMB_BEH+3,  .z=UNDEF        },  // VM_push
    { .t=VM_push,       .x=NIL,         .y=COMB_BEH+4,  .z=UNDEF        },  // ()
    { .t=VM_msg,        .x=2,           .y=COMB_BEH+5,  .z=UNDEF        },  // env
    { .t=VM_pick,       .x=6,           .y=COMB_BEH+6,  .z=UNDEF        },  // param
    { .t=VM_msg,        .x=1,           .y=COMB_BEH+7,  .z=UNDEF        },  // cust
    { .t=VM_pair,       .x=3,           .y=COMB_BEH+8,  .z=UNDEF        },  // msg = (cust param env)
    { .t=VM_push,       .x=K_CALL,      .y=COMB_BEH+9,  .z=UNDEF        },  // K_CALL
    { .t=VM_cell,       .x=3,           .y=COMB_BEH+10, .z=UNDEF        },  // {t:VM_push, x:msg, y:K_CALL}

    { .t=VM_new,        .x=0,           .y=COMB_BEH+11, .z=UNDEF        },  // k_call

    { .t=VM_pair,       .x=2,           .y=COMB_BEH+12, .z=UNDEF        },  // (k_call env)
    { .t=VM_pick,       .x=3,           .y=COMB_BEH+13, .z=UNDEF        },  // comb
    { .t=VM_send,       .x=0,           .y=COMB_BEH+14, .z=UNDEF        },  // (comb k_call env)
    { .t=VM_end,        .x=END_COMMIT,  .y=UNDEF,       .z=UNDEF        },  // COMB_BEH #15

#define OP_I (COMB_BEH+15)
    { .t=Actor_T,       .x=OP_I+1,      .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=VAR_1,       .y=OPER_BEH,    .z=UNDEF        },  // body = VAR_1
#define AP_I (OP_I+2)
    { .t=Actor_T,       .x=AP_I+1,      .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=OP_I,        .y=AP_I+2,      .z=UNDEF        },  // oper = OP_I
    { .t=VM_push,       .x=EMPTY_ENV,   .y=APPL_BEH,    .z=UNDEF        },  // env = EMPTY_ENV

#define LAMBDA_I (AP_I+3)
    { .t=Actor_T,       .x=LAMBDA_I+1,  .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=OP_LAMBDA,   .y=LAMBDA_I+2,  .z=UNDEF        },  // comb = OP_LAMBDA
    { .t=VM_push,       .x=VAR_1,       .y=COMB_BEH,    .z=UNDEF        },  // param = VAR_1

#define EXPR_I (LAMBDA_I+3)
    { .t=Actor_T,       .x=EXPR_I+1,    .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=LAMBDA_I,    .y=EXPR_I+2,    .z=UNDEF        },  // comb = LAMBDA_I
    { .t=VM_push,       .x=CONST_7,     .y=COMB_BEH,    .z=UNDEF        },  // param = CONST_7

#define BOUND_42 (EXPR_I+3)
    { .t=Actor_T,       .x=BOUND_42+1,  .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=42,          .y=BOUND_42+2,  .z=UNDEF        },  // value = 42
    { .t=VM_push,       .x=EMPTY_ENV,   .y=BOUND_BEH,   .z=UNDEF        },  // next = EMPTY_ENV
#define A_TEST (BOUND_42+3)
    { .t=Actor_T,       .x=A_TEST+1,    .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=BOUND_42,    .y=A_TEST+2,    .z=UNDEF        },  // BOUND_42
    { .t=VM_push,       .x=A_PRINT,     .y=A_TEST+3,    .z=UNDEF        },  // A_PRINT
    { .t=VM_push,       .x=EXPR_I,      .y=A_TEST+4,    .z=UNDEF        },  // EXPR_I
    { .t=VM_send,       .x=2,           .y=A_TEST+5,    .z=UNDEF        },  // (EXPR_I A_PRINT BOUND_42)
    { .t=VM_end,        .x=END_COMMIT,  .y=UNDEF,       .z=UNDEF        },  // A_TEST #6
};
cell_t *cell_zero = &cell_table[0];  // base for cell offsets
int_t cell_next = NIL;  // head of cell free-list (or NIL if empty)
int_t cell_top = A_TEST+6; // limit of allocated cell memory

static struct { int_t addr; char *label; } symbol_table[] = {
    { FALSE, "FALSE" },
    { TRUE, "TRUE" },
    { NIL, "NIL" },
    { UNDEF, "UNDEF" },
    { UNIT, "UNIT" },
    { START, "START" },
    { A_BOOT, "A_BOOT" },
    { A_CLOCK, "A_CLOCK" },
    { A_PRINT, "A_PRINT" },
    { EMPTY_ENV, "EMPTY_ENV" },
    { BOUND_BEH, "BOUND_BEH" },
    { CONST_BEH, "CONST_BEH" },
    { CONST_7, "CONST_7" },
    { VAR_BEH, "VAR_BEH" },
    { VAR_1, "VAR_1" },
    { K_APPLY, "K_APPLY" },
    { APPL_BEH, "APPL_BEH" },
    { OPER_BEH, "OPER_BEH" },
    { OP_LAMBDA, "OP_LAMBDA" },
    { K_CALL, "K_CALL" },
    { COMB_BEH, "COMB_BEH" },
    { OP_I, "OP_I" },
    { AP_I, "AP_I" },
    { LAMBDA_I, "LAMBDA_I" },
    { EXPR_I, "EXPR_I" },
    { BOUND_42, "BOUND_42" },
    { A_TEST, "A_TEST" },
    { -1, "" },
};
void dump_symbol_table() {
    for (int i = 0; symbol_table[i].addr >= 0; ++i) {
        fprintf(stderr, "%5"PdI": %s\n",
            symbol_table[i].addr, symbol_table[i].label);
    }
}
char *get_symbol_label(int_t addr) {
    int i = 0;
    while (symbol_table[i].addr >= 0) {
        if (addr == symbol_table[i].addr) break;
        ++i;
    }
    return symbol_table[i].label;
}

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
    sane = SANITY;
    while (IS_PAIR(val)) {
        ++len;
        val = cdr(val);
        if (sane-- == 0) return panic("insane list_len");
    }
    return len;
}

// WARNING! destuctive reverse in-place and append
int_t append_reverse(int_t head, int_t tail) {
    sane = SANITY;
    while (IS_PAIR(head)) {
        int_t rest = cdr(head);
        set_cdr(head, tail);
        tail = head;
        head = rest;
        if (sane-- == 0) return panic("insane append_reverse");
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

#if INCLUDE_DEBUG
static int_t event_q_dump() {
    debug_print("e_queue_head", e_queue_head);
    int_t ep = e_queue_head;
    sane = SANITY;
    while (ep != NIL) {
        fprintf(stderr, "-> %"PdI"{act=%"PdI",msg=%"PdI"}%s",
            ep, get_x(ep), get_y(ep), ((get_z(ep)==NIL)?"\n":""));
        ep = get_z(ep);
        if (sane-- == 0) return panic("insane event_q_dump");
    }
    return UNIT;
}
#endif

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

#if INCLUDE_DEBUG
static int_t cont_q_dump() {
    debug_print("k_queue_head", k_queue_head);
    int_t kp = k_queue_head;
    sane = SANITY;
    while (kp != NIL) {
        fprintf(stderr, "-> %"PdI"{ip=%"PdI",sp=%"PdI",ep=%"PdI"}%s",
            kp, get_t(kp), get_x(kp), get_y(kp), ((get_z(kp)==NIL)?"\n":""));
        kp = get_z(kp);
        if (sane-- == 0) return panic("insane cont_q_dump");
    }
    return UNIT;
}
#endif

/*
 * runtime (virtual machine engine)
 */

#if RUN_DEBUGGER
int_t runtime_trace = TRUE;
#else
int_t runtime_trace = FALSE;
#endif

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
    sane = SANITY;
    while (IS_PAIR(sp)) {
        int_t rest = cdr(sp);
        XFREE(sp);
        if (sane-- == 0) return panic("insane stack_clear");
    }
    return SET_SP(NIL);
}

typedef long clk_t;  // **MUST** be a _signed_ type to represent past/future
#define CLKS_PER_SEC ((clk_t)(CLOCKS_PER_SEC))
static clk_t clk_ticks() {
    return (clk_t)clock();
}
int_t clk_handler = A_CLOCK;
clk_t clk_timeout = 0;
static int_t interrupt() {
    clk_t now = clk_ticks();
    clk_t dt = (now - clk_timeout);
    XTRACE(fprintf(stderr, "clock (%ld - %ld) = %ld\n", (long)now, (long)clk_timeout, (long)dt));
    if (dt < 0) {
        return FALSE;
    }
    sane = SANITY;
    while (dt > 0) {
        XTRACE(fprintf(stderr, "clock (%ld - %ld) = %ld <%d>\n",
            (long)now, (long)clk_timeout, (long)dt, (dt > 0)));
        clk_timeout += CLKS_PER_SEC;
        dt = (now - clk_timeout);
        if (sane-- == 0) return panic("insane clk_timeout");
    }
    int_t sec = (now / CLKS_PER_SEC);
    if (IS_ACTOR(clk_handler)) {
        int_t ev = cell_new(Event_T, clk_handler, sec, NIL);
        DEBUG(debug_print("clock event", ev));
        event_q_put(ev);
    }
    return TRUE;
}
static int_t dispatch() {
    XTRACE(event_q_dump());
    int_t event = event_q_pop();
    XTRACE(debug_print("dispatch event", event));
    if (event == UNDEF) {  // event queue empty
        return UNDEF;
    }
    int_t target = get_x(event);
    int_t proc = get_t(target);
    ASSERT(IS_PROC(proc));
    int_t cont = call_proc(proc, target, event);
    if (cont == FALSE) {  // target busy
        event_q_put(event);  // re-queue event
        return FALSE;
    }
#if INCLUDE_DEBUG
    if (runtime_trace) {
        fprintf(stderr, "thread spawn: %"PdI"{ip=%"PdI",sp=%"PdI",ep=%"PdI"}\n",
            cont, get_t(cont), get_x(cont), get_y(cont));
    }
#endif
    cont_q_put(cont);  // enqueue continuation
    return cont;
}
static int_t execute() {
    XTRACE(cont_q_dump());
    if (cont_q_empty()) {
        return error("no live threads");  // no more instructions to execute...
    }
    // execute next continuation
    XTRACE(debug_print("execute cont", k_queue_head));
    int_t ip = GET_IP();
    int_t proc = get_t(ip);
    ASSERT(IS_PROC(proc));
#if INCLUDE_DEBUG
    if (!debugger()) return FALSE;  // debugger quit
#endif
    ip = call_proc(proc, ip, GET_EP());
    SET_IP(ip);  // update IP
    int_t cont = cont_q_pop();
    XTRACE(debug_print("execute done", cont));
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
        rv = interrupt();   // service interrupts (if any)
        rv = dispatch();    // dispatch next event (if any)
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

PROC_DECL(Boolean) {
    return error("Boolean message not understood");
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

PROC_DECL(Unit) {
    return error("Unit message not understood");
}

PROC_DECL(Actor) {
//    return error("Actor message not understood");
    int_t actor = self;
    int_t event = arg;
    ASSERT(actor == get_x(event));
    if (get_y(actor) != UNDEF) return FALSE;  // actor busy
    int_t beh = get_x(actor);  // actor behavior (initial IP)
    // begin actor transaction
    set_y(actor, NIL);  // empty set of new events
    set_z(actor, UNDEF);  // no BECOME (yet...)
    // spawn new "thread" to handle event
    int_t cont = cell_new(beh, NIL, event, NIL);  // ip=beh, sp=(), ep=event
    return cont;
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

static int_t pop_pairs(int_t n) {
    int_t c;
    if (n > 0) {
        int_t h = stack_pop();
        int_t t = pop_pairs(n - 1);
        c = cons(h, t);
    } else {
        c = stack_pop();
    }
    return c;
}
PROC_DECL(vm_pair) {
    int_t n = get_x(self);
    int_t c = pop_pairs(n);
    stack_push(c);
    return get_y(self);
}

static void push_parts(int_t n, int_t xs) {
    if (n > 0) {
        push_parts((n - 1), cdr(xs));
        int_t x = car(xs);
        stack_push(x);
    } else {
        stack_push(xs);
    }
}
PROC_DECL(vm_part) {
    int_t n = get_x(self);
    int_t c = stack_pop();
    push_parts(n, c);
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
    sane = SANITY;
    while (IS_PAIR(sp)) {  // count items on stack
        ++v;
        sp = cdr(sp);
        if (sane-- == 0) return panic("insane vm_depth");
    }
    stack_push(v);
    return get_y(self);
}

PROC_DECL(vm_drop) {
    int_t n = get_x(self);
    sane = SANITY;
    while (n-- > 0) {
        stack_pop();
        if (sane-- == 0) return panic("insane vm_drop");
    }
    return get_y(self);
}

PROC_DECL(vm_pick) {
    int_t n = get_x(self);
    int_t v = UNDEF;
    int_t sp = GET_SP();
    sane = SANITY;
    while (n-- > 0) {  // copy n-th item to top of stack
        v = car(sp);
        sp = cdr(sp);
        if (sane-- == 0) return panic("insane vm_pick");
    }
    stack_push(v);
    return get_y(self);
}

PROC_DECL(vm_dup) {
    int_t n = get_x(self);
    int_t dup = NIL;
    int_t sp = GET_SP();
    sane = SANITY;
    while (n-- > 0) {  // copy n items from stack
        dup = cons(car(sp), dup);
        sp = cdr(sp);
        if (sane-- == 0) return panic("insane vm_dup");
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
        sane = SANITY;
        while (IS_PAIR(m)) {
            if (--i == 0) {
                v = car(m);
                break;
            }
            m = cdr(m);
            if (sane-- == 0) return panic("insane vm_msg");
        }
    } else {  // use -i to select the i-th tail
        sane = SANITY;
        while (IS_PAIR(m)) {
            m = cdr(m);
            if (++i == 0) break;
            if (sane-- == 0) return panic("insane vm_msg");
        }
        v = m;
    }
    stack_push(v);
    return get_y(self);
}

PROC_DECL(vm_self) {
    int_t ep = GET_EP();
    int_t me = get_x(ep);
    stack_push(me);
    return get_y(self);
}

static int_t pop_list(int_t n) {
    int_t c;
    if (n > 0) {
        int_t h = stack_pop();
        int_t t = pop_list(n - 1);
        c = cons(h, t);
    } else {
        c = NIL;
    }
    return c;
}
PROC_DECL(vm_send) {
    int_t n = get_x(self);
    int_t ep = GET_EP();
    int_t me = get_x(ep);
    int_t a = stack_pop();  // target
    if (!IS_ACTOR(a)) {
        set_y(me, UNDEF);  // abort actor transaction
        return error("SEND requires an Actor");
    }
    int_t m = NIL;
    if (n == 0) {
        m = stack_pop();  // message
    } else if (n > 0) {
        m = pop_list(n);  // compose message
    } else {
        return error("vm_send (n < 0) invalid");
    }
    int_t ev = cell_new(Event_T, a, m, get_y(me));
    set_y(me, ev);
    return get_y(self);
}

PROC_DECL(vm_new) {
    int_t n = get_x(self);
    if (n < 0) return error("vm_new (n < 0) invalid");
    int_t b = stack_pop();  // behavior
    while (n--) {
        // compose behavior
        int_t v = stack_pop();  // value
        b = cell_new(VM_push, v, b, UNDEF);
    }
    int_t a = cell_new(Actor_T, b, UNDEF, UNDEF);
    stack_push(a);
    return get_y(self);
}

PROC_DECL(vm_beh) {
    int_t n = get_x(self);
    int_t ep = GET_EP();
    int_t me = get_x(ep);
    if (n == 0) {
        int_t b = stack_pop();  // behavior
        ASSERT(get_z(me) == UNDEF);  // BECOME only allowed once
        set_z(me, b);
    } else {
        return error("vm_beh (n != 0) not implemented");
    }
    return get_y(self);
}

PROC_DECL(vm_end) {
    int_t n = get_x(self);
    int_t ep = GET_EP();
    int_t me = get_x(ep);
    int_t rv = UNIT;  // STOP
    if (n < 0) {  // ABORT
        int_t r = stack_pop();  // reason
        DEBUG(debug_print("ABORT!", r));
        stack_clear();
        set_y(me, UNDEF);  // abort actor transaction
        rv = FALSE;
    } else if (n > 0) {  // COMMIT
        stack_clear();
        int_t b = get_z(me);
        if (b != UNDEF) {
            set_x(me, b);  // BECOME new behavior
        }
        int_t e = get_y(me);
        sane = SANITY;
        while (e != NIL) {
            int_t es = get_z(e);
            event_q_put(e);
            e = es;
            if (sane-- == 0) return panic("insane COMMIT");
        }
        set_y(me, UNDEF);  // commit actor transaction
        rv = TRUE;
    }
    return rv;  // terminate thread
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

PROC_DECL(vm_debug) {
    int_t x = get_x(self);
    int_t v = stack_pop();
    fprintf(stderr, "%"PdI"", x);
    debug_print("", v);
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
    sane = SANITY;
    while (IS_PAIR(msg)) {
        fprintf(stderr, " %+"PdI"", car(msg));
        msg = cdr(msg);
        if (sane-- == 0) panic("insane print_event");
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
static char *end_label(int_t t) {
    if (t < 0) return "ABORT";
    if (t > 0) return "COMMIT";
    return "STOP";
}
static void print_inst(int_t ip) {
    int_t proc = get_t(ip);
    fprintf(stderr, "%s", cell_label(proc));
    switch (proc) {
        case VM_cell: fprintf(stderr, "{n:%"PdI",k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_get:  fprintf(stderr, "{f:%s,k:%"PdI"}", field_label(get_x(ip)), get_y(ip)); break;
        case VM_set:  fprintf(stderr, "{f:%s,k:%"PdI"}", field_label(get_x(ip)), get_y(ip)); break;
        case VM_pair: fprintf(stderr, "{n:%"PdI",k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_part: fprintf(stderr, "{n:%"PdI",k:%"PdI"}", get_x(ip), get_y(ip)); break;
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
        case VM_self: fprintf(stderr, "{k:%"PdI"}", get_y(ip)); break;
        case VM_send: fprintf(stderr, "{n:%"PdI",k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_new:  fprintf(stderr, "{n:%"PdI",k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_beh:  fprintf(stderr, "{n:%"PdI",k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_end:  fprintf(stderr, "{t:%s}", end_label(get_x(ip))); break;
        case VM_putc: fprintf(stderr, "{k:%"PdI"}", get_y(ip)); break;
        case VM_getc: fprintf(stderr, "{k:%"PdI"}", get_y(ip)); break;
        case VM_debug:fprintf(stderr, "{k:%"PdI"}", get_y(ip)); break;
        default: {
            if (IS_PROC(proc)) {
                fprintf(stderr, "{x:%"PdI",y:%"PdI",z:%"PdI"}", get_x(ip), get_y(ip), get_z(ip));
            } else {
                fprintf(stderr, "{t:%"PdI",x:%"PdI",y:%"PdI",z:%"PdI"}",
                    get_t(ip), get_x(ip), get_y(ip), get_z(ip));
            }
            break;
        }
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
    sane = SANITY;
    while (n-- > 0) {
        char *label = get_symbol_label(ip);
        if (*label) {
            fprintf(stderr, "%s\n", label);
        }
        fprintf(stderr, "%5"PdI": ", ip);
        fprintf(stderr, "%5"PdI" ", get_t(ip));
        fprintf(stderr, "%5"PdI" ", get_x(ip));
        fprintf(stderr, "%5"PdI" ", get_y(ip));
        fprintf(stderr, "%5"PdI"  ", get_z(ip));
        print_inst(ip);
        fprintf(stderr, "\n");
        ++ip;
        if (sane-- == 0) panic("insane disassemble");
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
    sane = SANITY;
    while (*expect) {
        if (*expect++ != *actual++) return FALSE;
        if (sane-- == 0) return panic("insane db_cmd_eq");
    }
    return (*actual ? FALSE : TRUE);
}
static int_t db_num_cmd(char *cmd) {
    int_t n = 0;
    nat_t d;
    sane = SANITY;
    while ((d = NAT(*cmd++ - '0')) < 10) {
        n = (n * 10) + d;
        if (sane-- == 0) return panic("insane db_num_cmd");
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
        if (runtime_trace) {
            continuation_trace();
        }
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
            if (bp_ip) {
                fprintf(stderr, "break at ip=%"PdI"\n", bp_ip);
            } else {
                fprintf(stderr, "no breakpoint\n");
            }
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
        if (*cmd == 't') {                  // trace
            runtime_trace = (runtime_trace ? FALSE : TRUE);
            fprintf(stderr, "instruction tracing %s\n",
                (runtime_trace ? "on" : "off"));
            continue;
        }
        if (*cmd == 'i') {
            char *cmd = db_cmd_token(&p);
            if (*cmd == 'r') {              // info regs
                fprintf(stderr, "ip=%"PdI" sp=%"PdI" ep=%"PdI" free=%"PdI"\n",
                    GET_IP(), GET_SP(), GET_EP(), cell_next);
                continue;
            }
            if (*cmd == 't') {              // info threads
                cont_q_dump();
                continue;
            }
            if (*cmd == 'e') {              // info events
                event_q_dump();
                continue;
            }
            fprintf(stderr, "info: r[egs] t[hreads] e[vents]\n");
            continue;
        }
        if (*cmd == 'c') {                  // continue
            run = TRUE;
            return TRUE;
        }
        if (*cmd == 'h') {
            char *cmd = db_cmd_token(&p);
            switch (*cmd) {
                case 'h' : fprintf(stderr, "h[elp] <command> -- get help on <command>\n"); continue;
                case 'b' : fprintf(stderr, "b[reak] <inst> -- set breakpoint at <inst> (0=none, default: IP)\n"); continue;
                case 'c' : fprintf(stderr, "c[ontinue] -- continue running freely\n"); continue;
                case 's' : fprintf(stderr, "s[tep] <n> -- set <n> instructions (default: 1)\n"); continue;
                case 'n' : fprintf(stderr, "n[ext] <n> -- next <n> instructions in thread (default: 1)\n"); continue;
                case 'd' : fprintf(stderr, "d[isasm] <n> <inst> -- disassemble <n> instructions (defaults: 1 IP)\n"); continue;
                case 't' : fprintf(stderr, "t[race] -- toggle instruction tracing (default: on)\n"); continue;
                case 'i' : fprintf(stderr, "i[nfo] <topic> -- get information on <topic>\n"); continue;
                case 'q' : fprintf(stderr, "q[uit] -- quit runtime\n"); continue;
            }
        }
        fprintf(stderr, "h[elp] b[reak] c[ontinue] s[tep] n[ext] d[isasm] t[race] i[nfo] q[uit]\n");
    }
}
#endif // INCLUDE_DEBUG

/*
 * bootstrap
 */

int main(int argc, char const *argv[])
{
    DEBUG(fprintf(stderr, "PROC_MAX=%"PuI" CELL_MAX=%"PuI"\n", PROC_MAX, CELL_MAX));
    //DEBUG(hexdump("cell memory", ((int_t *)cell_zero), 16*4));
    DEBUG(dump_symbol_table());
    clk_timeout = clk_ticks();
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
