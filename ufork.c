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
#define EXPLICIT_FREE 1 // explicitly free known-dead memory
#define MARK_SWEEP_GC 1 // stop-the-world garbage collection
#define RUNTIME_STATS 1 // collect statistics on the runtime
#define SCM_PEG_TOOLS 0 // include PEG tools for LISP/Scheme (+232 cells)
#define BOOTSTRAP_LIB 1 // include bootstrap library definitions
#define EVLIS_IS_PAR  0 // concurrent argument-list evaluation
#define SCM_ASM_TOOLS 1 // include assembly tools for LISP/Scheme

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
#define XFREE(x)    UNDEF
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

// WASM base types
typedef int32_t i32;
typedef int64_t i64;
#define I32(x) ((i32)(x))
#define I64(x) ((i64)(x))

#define INT(n) ((int_t)(n))
#define NAT(n) ((nat_t)(n))
#define PTR(n) ((ptr_t)(n))

#define MSB(n) NAT(~(NAT(-1)>>(n)))
#define MSB1   NAT(MSB(1))
#define MSB2   NAT(MSB1>>1)

#define TO_INT(x) INT(INT(NAT(x) << 1) >> 1)
#define TO_FIX(n) INT(TO_INT(n) + MSB1)
#define IS_FIX(n) (NAT((n) - MSB2) < MSB1)

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
int_t console_putc(int_t c);
int_t console_getc();
void print_sexpr(int_t x);
#if INCLUDE_DEBUG
void hexdump(char *label, int_t *addr, size_t cnt);
void debug_print(char *label, int_t addr);
void print_addr(char *prefix, int_t addr);
void print_event(int_t ep);
void print_inst(int_t ip);
void print_list(int_t xs);
void continuation_trace();
int_t debugger();
#endif // INCLUDE_DEBUG

#define ASSERT(cond)    if (!(cond)) return failure(__FILE__, __LINE__)

// constant values
#define FALSE       (0)
#define TRUE        (1)
#define NIL         (2)
#define UNDEF       (3)
#define UNIT        (4)
#define START       (5)

/*
 * native code procedures
 */

// FORWARD DECLARATIONS
PROC_DECL(Fixnum);
PROC_DECL(Proc);
PROC_DECL(Undef);
PROC_DECL(Boolean);
PROC_DECL(Null);
PROC_DECL(Pair);
PROC_DECL(Symbol);
PROC_DECL(Fexpr);
PROC_DECL(Actor);
PROC_DECL(Event);
PROC_DECL(Free);
PROC_DECL(vm_typeq);
PROC_DECL(vm_cell);
PROC_DECL(vm_get);
PROC_DECL(vm_set);
PROC_DECL(vm_pair);
PROC_DECL(vm_part);
PROC_DECL(vm_nth);
PROC_DECL(vm_push);
PROC_DECL(vm_depth);
PROC_DECL(vm_drop);
PROC_DECL(vm_pick);
PROC_DECL(vm_dup);
PROC_DECL(vm_roll);
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
PROC_DECL(vm_cvt);
PROC_DECL(vm_putc);
PROC_DECL(vm_getc);
PROC_DECL(vm_debug);

#define Fixnum_T    (1)
#define Proc_T      (0)
#define Undef_T     (-1)
#define Boolean_T   (-2)
#define Null_T      (-3)
#define Pair_T      (-4)
#define Symbol_T    (-5)
#define Fexpr_T     (-6)
#define Actor_T     (-7)
#define Event_T     (-8)
#define Free_T      (-9)
#define VM_typeq    (-10)
#define VM_cell     (-11)
#define VM_get      (-12)
#define VM_set      (-13)
#define VM_pair     (-14)
#define VM_part     (-15)
#define VM_nth      (-16)
#define VM_push     (-17)
#define VM_depth    (-18)
#define VM_drop     (-19)
#define VM_pick     (-20)
#define VM_dup      (-21)
#define VM_roll     (-22)
#define VM_alu      (-23)
#define VM_eq       (-24)
#define VM_cmp      (-25)
#define VM_if       (-26)
#define VM_msg      (-27)
#define VM_self     (-28)
#define VM_send     (-29)
#define VM_new      (-30)
#define VM_beh      (-31)
#define VM_end      (-32)
#define VM_cvt      (-33)
#define VM_putc     (-34)
#define VM_getc     (-35)
#define VM_debug    (-36)

#define PROC_MAX    NAT(sizeof(proc_table) / sizeof(proc_t))
proc_t proc_table[] = {
    vm_debug,
    vm_getc,
    vm_putc,
    vm_cvt,
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
    vm_roll,
    vm_dup,
    vm_pick,
    vm_drop,
    vm_depth,
    vm_push,
    vm_nth,
    vm_part,
    vm_pair,
    vm_set,
    vm_get,
    vm_cell,
    vm_typeq,
    Free,  // free-cell marker
    Event,
    Actor,
    Fexpr,
    Symbol,
    Pair,
    Null,
    Boolean,
    Undef,
};
proc_t *proc_zero = &proc_table[PROC_MAX];  // base for proc offsets

static char *proc_label(int_t proc) {
    static char *label[] = {
        "Undef_T",
        "Boolean_T",
        "Null_T",
        "Pair_T",
        "Symbol_T",
        "Fexpr_T",
        "Actor_T",
        "Event_T",
        "Free_T",
        "VM_typeq",
        "VM_cell",
        "VM_get",
        "VM_set",
        "VM_pair",
        "VM_part",
        "VM_nth",
        "VM_push",
        "VM_depth",
        "VM_drop",
        "VM_pick",
        "VM_dup",
        "VM_roll",
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
        "VM_cvt",
        "VM_putc",
        "VM_getc",
        "VM_debug",
    };
    if (proc == Fixnum_T) return "Fixnum_T";
    if (proc == Proc_T) return "Proc_T";
    nat_t ofs = NAT(Undef_T - proc);
    if (ofs < PROC_MAX) return label[ofs];
    return "<unknown>";
}

int_t call_proc(int_t proc, int_t self, int_t arg) {
    //XTRACE(debug_print("call_proc proc", proc));
    XTRACE(debug_print("call_proc self", self));
    if (proc == Fixnum_T) return Fixnum(self, arg);
    if (proc == Proc_T) return Proc(self, arg);
    int_t result = UNDEF;
    nat_t ofs = NAT(Undef_T - proc);
    if (ofs < PROC_MAX) {
        result = (proc_zero[proc])(self, arg);
    } else {
        result = error("procedure expected");
    }
    return result;
}

// VM_get/VM_set fields
#define FLD_T       (0)
#define FLD_X       (1)
#define FLD_Y       (2)
#define FLD_Z       (3)

// VM_alu operations
#define ALU_NOT     (0)
#define ALU_AND     (1)
#define ALU_OR      (2)
#define ALU_XOR     (3)
#define ALU_ADD     (4)
#define ALU_SUB     (5)
#define ALU_MUL     (6)

// VM_cmp relations
#define CMP_EQ      (0)
#define CMP_GE      (1)
#define CMP_GT      (2)
#define CMP_LT      (3)
#define CMP_LE      (4)
#define CMP_NE      (5)
#define CMP_CLS     (6)

// VM_end thread action
#define END_ABORT   (-1)
#define END_STOP    (0)
#define END_COMMIT  (+1)
#define END_RELEASE (+2)

// VM_cvt conversions
#define CVT_INT_FIX (0)
#define CVT_FIX_INT (1)
#define CVT_LST_NUM (2)
#define CVT_LST_SYM (3)

/*
 * character classes
 */

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

int_t char_in_class(int_t n, int_t c) {
    return (((n & ~0x7F) == 0) && ((char_class[n] & c) != 0));
}

/*
 * heap memory management (cells)
 */

static char *cell_label(int_t cell) {
    static char *label[] = {
        "FALSE",
        "TRUE",
        "NIL",
        "UNDEF",
        "UNIT",
    };
    if (IS_FIX(cell)) return "fix";
    if (cell < 0) return proc_label(cell);
    if (cell < START) return label[cell];
    return "cell";
}

#define CELL_MAX NAT(1<<14)  // 16K cells
cell_t cell_table[CELL_MAX] = {
    { .t=Boolean_T,     .x=UNDEF,       .y=UNDEF,       .z=UNDEF        },  // FALSE = #f
    { .t=Boolean_T,     .x=UNDEF,       .y=UNDEF,       .z=UNDEF        },  // TRUE = #t
    { .t=Null_T,        .x=UNDEF,       .y=UNDEF,       .z=UNDEF        },  // NIL = ()
    { .t=Undef_T,       .x=UNDEF,       .y=UNDEF,       .z=UNDEF        },  // UNDEF = #?
    { .t=Null_T,        .x=UNDEF,       .y=UNDEF,       .z=UNDEF        },  // UNIT = #unit
    { .t=Event_T,       .x=91,          .y=NIL,         .z=NIL          },  // <--- START = (A_BOOT)

#define RV_SELF (START+1)
    { .t=VM_self,       .x=UNDEF,       .y=RV_SELF+1,   .z=UNDEF        },  // value = SELF
#define CUST_SEND (RV_SELF+1)
    { .t=VM_msg,        .x=1,           .y=CUST_SEND+1, .z=UNDEF        },  // cust
#define SEND_0 (CUST_SEND+1)
    { .t=VM_send,       .x=0,           .y=SEND_0+1,    .z=UNDEF        },  // (cust . msg)
#define COMMIT (SEND_0+1)
    { .t=VM_end,        .x=END_COMMIT,  .y=UNDEF,       .z=UNDEF        },  // commit actor transaction

#define RESEND (COMMIT+1)
    { .t=VM_msg,        .x=0,           .y=RESEND+1,    .z=UNDEF        },  // msg
    { .t=VM_self,       .x=UNDEF,       .y=SEND_0,      .z=UNDEF        },  // SELF

#define RELEASE_0 (RESEND+2)
    { .t=VM_send,       .x=0,           .y=RELEASE_0+1, .z=UNDEF        },  // (cust . msg)
#define RELEASE (RELEASE_0+1)
    { .t=VM_end,        .x=END_RELEASE, .y=UNDEF,       .z=UNDEF        },  // commit transaction and free actor

#define RV_FALSE (RELEASE+1)
    { .t=VM_push,       .x=FALSE,       .y=CUST_SEND,   .z=UNDEF        },  // FALSE
#define RV_TRUE (RV_FALSE+1)
    { .t=VM_push,       .x=TRUE,        .y=CUST_SEND,   .z=UNDEF        },  // TRUE
#define RV_NIL (RV_TRUE+1)
    { .t=VM_push,       .x=NIL,         .y=CUST_SEND,   .z=UNDEF        },  // NIL
#define RV_UNDEF (RV_NIL+1)
    { .t=VM_push,       .x=UNDEF,       .y=CUST_SEND,   .z=UNDEF        },  // UNDEF
#define RV_UNIT (RV_UNDEF+1)
    { .t=VM_push,       .x=UNIT,        .y=CUST_SEND,   .z=UNDEF        },  // UNIT
#define RV_ZERO (RV_UNIT+1)
    { .t=VM_push,       .x=TO_FIX(0),   .y=CUST_SEND,   .z=UNDEF        },  // +0
#define RV_ONE (RV_ZERO+1)
    { .t=VM_push,       .x=TO_FIX(1),   .y=CUST_SEND,   .z=UNDEF        },  // +1

#define S_VALUE (RV_ONE+1)
//  { .t=VM_push,       .x=_in_,        .y=S_VALUE+0,   .z=UNDEF        },  // (token . next) -or- NIL
    { .t=VM_pick,       .x=1,           .y=S_VALUE+1,   .z=UNDEF        },  // in
    { .t=VM_msg,        .x=0,           .y=SEND_0,      .z=UNDEF        },  // cust

#define S_GETC (S_VALUE+2)
#define S_END_X (S_GETC+9)
#define S_VAL_X (S_GETC+10)
    { .t=VM_getc,       .x=UNDEF,       .y=S_GETC+1,    .z=UNDEF        },  // ch
    { .t=VM_pick,       .x=1,           .y=S_GETC+2,    .z=UNDEF        },  // ch ch
    { .t=VM_push,       .x=TO_FIX('\0'),.y=S_GETC+3,    .z=UNDEF        },
    { .t=VM_cmp,        .x=CMP_LT,      .y=S_GETC+4,    .z=UNDEF        },  // ch < '\0'
    { .t=VM_if,         .x=S_END_X,     .y=S_GETC+5,    .z=UNDEF        },

    { .t=VM_push,       .x=S_GETC,      .y=S_GETC+6,    .z=UNDEF        },  // S_GETC
    { .t=VM_new,        .x=0,           .y=S_GETC+7,    .z=UNDEF        },  // next
    { .t=VM_pick,       .x=2,           .y=S_GETC+8,    .z=UNDEF        },  // ch
    { .t=VM_pair,       .x=1,           .y=S_VAL_X,     .z=UNDEF        },  // in = (ch . next)

    { .t=VM_push,       .x=NIL,         .y=S_GETC+10,   .z=UNDEF        },  // in = ()

    { .t=VM_push,       .x=S_VALUE,     .y=S_GETC+11,   .z=UNDEF        },  // S_VALUE
    { .t=VM_beh,        .x=1,           .y=RESEND,      .z=UNDEF        },  // BECOME (S_VALUE in)

#define S_LIST_B (S_GETC+12)
//  { .t=VM_push,       .x=_list_,      .y=S_LIST_B+0,  .z=UNDEF        },
    { .t=VM_pick,       .x=1,           .y=S_LIST_B+1,  .z=UNDEF        },  // list
    { .t=VM_typeq,      .x=Pair_T,      .y=S_LIST_B+2,  .z=UNDEF        },  // list has type Pair_T
    { .t=VM_if,         .x=S_LIST_B+3,  .y=S_END_X,     .z=UNDEF        },  // list

    { .t=VM_part,       .x=1,           .y=S_LIST_B+4,  .z=UNDEF        },  // tail head
    { .t=VM_roll,       .x=2,           .y=S_LIST_B+5,  .z=UNDEF        },  // head tail
    { .t=VM_push,       .x=S_LIST_B,    .y=S_LIST_B+6,  .z=UNDEF        },  // S_LIST_B
    { .t=VM_new,        .x=1,           .y=S_LIST_B+7,  .z=UNDEF        },  // next
    { .t=VM_roll,       .x=2,           .y=S_LIST_B+8,  .z=UNDEF        },  // head
    { .t=VM_pair,       .x=1,           .y=S_VAL_X,     .z=UNDEF        },  // in = (head . next)

#define G_START (S_LIST_B+9)
//  { .t=VM_push,       .x=_custs_,     .y=G_START-1,   .z=UNDEF        },  // (ok . fail)
//  { .t=VM_push,       .x=_ptrn_,      .y=G_START+0,   .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=G_START+1,   .z=UNDEF        },  // in
    { .t=VM_push,       .x=UNDEF,       .y=G_START+2,   .z=UNDEF        },  // value = UNDEF
    { .t=VM_pick,       .x=4,           .y=G_START+3,   .z=UNDEF        },  // custs
    { .t=VM_pair,       .x=2,           .y=G_START+4,   .z=UNDEF        },  // (custs value . in)
    { .t=VM_pick,       .x=2,           .y=SEND_0,      .z=UNDEF        },  // ptrn

#define G_CALL_B (G_START+5)
//  { .t=VM_push,       .x=_symbol_,    .y=G_CALL_B+0,  .z=UNDEF        },  // name = symbol
    { .t=VM_get,        .x=FLD_Z,       .y=G_CALL_B+1,  .z=UNDEF        },  // ptrn = lookup(name)
    { .t=VM_msg,        .x=0,           .y=G_CALL_B+2,  .z=UNDEF        },  // (custs value . in)
    { .t=VM_roll,       .x=2,           .y=SEND_0,      .z=UNDEF        },  // (ptrn custs value . in)

#define G_LANG (G_CALL_B+3)
    { .t=Actor_T,       .x=G_LANG+1,    .y=NIL,         .z=UNDEF        },
    { .t=VM_push,       .x=UNDEF,       .y=G_CALL_B,    .z=UNDEF        },  // {x:symbol} patched by A_BOOT

/*
(define empty-env
  (CREATE
    (BEH (cust . _)
      (SEND cust #undefined))))
*/
#define EMPTY_ENV (G_LANG+2)
    { .t=Actor_T,       .x=RV_UNDEF,    .y=NIL,         .z=UNDEF        },

/*
(define global-env
  (CREATE
    (BEH (cust . key)
      (SEND cust (get_z key)) )))  ; extract value from global symbol table
*/
#define GLOBAL_ENV (EMPTY_ENV+1)
    { .t=Actor_T,       .x=GLOBAL_ENV+1,.y=NIL,         .z=UNDEF        },
    { .t=VM_msg,        .x=-1,          .y=GLOBAL_ENV+2,.z=UNDEF        },  // symbol = key
    { .t=VM_get,        .x=FLD_Z,       .y=CUST_SEND,   .z=UNDEF        },  // get_z(symbol)

/*
(define bound-beh
  (lambda (var val env)
    (BEH (cust . key)  ; FIXME: implement (cust key value) to "bind"?
      (if (eq? key var)
        (SEND cust val)
        (SEND env (cons cust key))
      ))))
*/
#define BOUND_BEH (GLOBAL_ENV+3)
//  { .t=VM_push,       .x=_var_,       .y=BOUND_BEH-2, .z=UNDEF        },
//  { .t=VM_push,       .x=_val_,       .y=BOUND_BEH-1, .z=UNDEF        },
//  { .t=VM_push,       .x=_env_,       .y=BOUND_BEH+0, .z=UNDEF        },
    { .t=VM_msg,        .x=-1,          .y=BOUND_BEH+1, .z=UNDEF        },  // key
    { .t=VM_pick,       .x=4,           .y=BOUND_BEH+2, .z=UNDEF        },  // var
    { .t=VM_cmp,        .x=CMP_EQ,      .y=BOUND_BEH+3, .z=UNDEF        },  // key == var
    { .t=VM_if,         .x=BOUND_BEH+4, .y=BOUND_BEH+5, .z=UNDEF        },

    { .t=VM_pick,       .x=2,           .y=CUST_SEND,   .z=UNDEF        },  // val

    { .t=VM_msg,        .x=0,           .y=BOUND_BEH+6, .z=UNDEF        },  // msg
    { .t=VM_pick,       .x=2,           .y=SEND_0,      .z=UNDEF        },  // env

#define REPL_R (BOUND_BEH+7)
#define REPL_E (REPL_R+8)
#define REPL_P (REPL_E+8)
#define REPL_L (REPL_P+3)
#define REPL_F (REPL_L+4)
    { .t=VM_push,       .x=REPL_F,      .y=REPL_R+1,    .z=UNDEF        },  // fail = REPL_F
    { .t=VM_push,       .x=REPL_E,      .y=REPL_R+2,    .z=UNDEF        },  // ok = REPL_E
    { .t=VM_pair,       .x=1,           .y=REPL_R+3,    .z=UNDEF        },  // custs = (ok . fail)
    { .t=VM_push,       .x=G_LANG,      .y=REPL_R+4,    .z=UNDEF        },  // ptrn = G_LANG
    { .t=VM_push,       .x=G_START,     .y=REPL_R+5,    .z=UNDEF        },  // G_START
    { .t=VM_new,        .x=2,           .y=REPL_R+6,    .z=UNDEF        },  // start
    { .t=VM_push,       .x=S_GETC,      .y=REPL_R+7,    .z=UNDEF        },  // S_GETC
    { .t=VM_new,        .x=0,           .y=SEND_0,      .z=UNDEF        },  // src

    { .t=Actor_T,       .x=REPL_E+1,    .y=NIL,         .z=UNDEF        },
    { .t=VM_msg,        .x=1,           .y=REPL_E+2,    .z=UNDEF        },  // sexpr
    { .t=VM_debug,      .x=TO_FIX(888), .y=REPL_E+3,    .z=UNDEF        },

    //{ .t=VM_push,       .x=GLOBAL_ENV,  .y=REPL_E+4,    .z=UNDEF        },  // env = GLOBAL_ENV
    { .t=VM_push,       .x=NIL,         .y=REPL_E+4,    .z=UNDEF        },  // env = ()
    { .t=VM_msg,        .x=1,           .y=REPL_E+5,    .z=UNDEF        },  // form = sexpr
    { .t=VM_push,       .x=REPL_P,      .y=REPL_E+6,    .z=UNDEF        },  // cust = REPL_P
    { .t=VM_push,       .x=210,         .y=REPL_E+7,    .z=UNDEF        },  // M_EVAL  <--------------- UPDATE THIS MANUALLY!
    { .t=VM_send,       .x=3,           .y=COMMIT,      .z=UNDEF        },  // (M_EVAL cust form env)

    { .t=Actor_T,       .x=REPL_P+1,    .y=NIL,         .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=REPL_P+2,    .z=UNDEF        },
    { .t=VM_debug,      .x=TO_FIX(999), .y=REPL_L,      .z=UNDEF        },

    { .t=VM_push,       .x=TO_FIX('>'), .y=REPL_L+1,    .z=UNDEF        },
    { .t=VM_putc,       .x=UNDEF,       .y=REPL_L+2,    .z=UNDEF        },
    { .t=VM_push,       .x=TO_FIX(' '), .y=REPL_L+3,    .z=UNDEF        },
    { .t=VM_putc,       .x=UNDEF,       .y=REPL_R,      .z=UNDEF        },

    { .t=Actor_T,       .x=REPL_F+1,    .y=NIL,         .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=REPL_F+2,    .z=UNDEF        },
    { .t=VM_debug,      .x=TO_FIX(666), .y=COMMIT,      .z=UNDEF        },

#define A_BOOT (REPL_F+3)
    { .t=Actor_T,       .x=A_BOOT+1,    .y=NIL,         .z=UNDEF        },  // <--- A_BOOT
    { .t=VM_push,       .x=G_LANG+1,    .y=A_BOOT+2,    .z=UNDEF        },  // cell to patch
    { .t=VM_push,       .x=A_BOOT+5,    .y=A_BOOT+3,    .z=UNDEF        },  // "peg-lang" string
    { .t=VM_cvt,        .x=CVT_LST_SYM, .y=A_BOOT+4,    .z=UNDEF        },
    { .t=VM_set,        .x=FLD_X,       .y=REPL_L,      .z=UNDEF        },  // set_x(symbol)

    { .t=Pair_T,        .x=TO_FIX('p'), .y=A_BOOT+6,    .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('e'), .y=A_BOOT+7,    .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('g'), .y=A_BOOT+8,    .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('-'), .y=A_BOOT+9,    .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('l'), .y=A_BOOT+10,   .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('a'), .y=A_BOOT+11,   .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('n'), .y=A_BOOT+12,   .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('g'), .y=NIL,         .z=UNDEF        },

//
// Clock device driver
//

#define A_CLOCK (A_BOOT+13)
    { .t=Actor_T,       .x=A_CLOCK+1,   .y=NIL,         .z=UNDEF        },
    { .t=VM_push,       .x=TO_FIX(-1),  .y=A_CLOCK+2,   .z=UNDEF        },
#define CLOCK_BEH (A_CLOCK+2)
#if 0
    { .t=VM_msg,        .x=0,           .y=A_CLOCK+3,   .z=UNDEF        },
    { .t=VM_push,       .x=CLOCK_BEH,   .y=A_CLOCK+4,   .z=UNDEF        },
    { .t=VM_beh,        .x=1,           .y=COMMIT,      .z=UNDEF        },
#else
    { .t=VM_push,       .x=A_CLOCK+1,   .y=A_CLOCK+3,   .z=UNDEF        },  // address of VM_push instruction
    { .t=VM_msg,        .x=0,           .y=A_CLOCK+4,   .z=UNDEF        },  // clock value
    { .t=VM_set,        .x=FLD_X,       .y=COMMIT,      .z=UNDEF        },  // update stored value (WARNING! SELF-MODIFYING CODE)
#endif

//
// Low-level Actor idioms
//

/*
(define tag-beh
  (lambda (cust)
    (BEH msg
      (SEND cust (cons SELF msg))
    )))
*/
#define TAG_BEH (A_CLOCK+5)
//  { .t=VM_push,       .x=_cust_,      .y=TAG_BEH+0,   .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=TAG_BEH+1,   .z=UNDEF        },  // msg
    { .t=VM_self,       .x=UNDEF,       .y=TAG_BEH+2,   .z=UNDEF        },  // SELF
    { .t=VM_pair,       .x=1,           .y=TAG_BEH+3,   .z=UNDEF        },  // (SELF . msg)
    { .t=VM_pick,       .x=2,           .y=SEND_0,      .z=UNDEF        },  // cust

#define K_JOIN_H (TAG_BEH+4)
//  { .t=VM_push,       .x=_cust_,      .y=K_JOIN_H-2,  .z=UNDEF        },
//  { .t=VM_push,       .x=_head_,      .y=K_JOIN_H-1,  .z=UNDEF        },
//  { .t=VM_push,       .x=_k_tail_,    .y=K_JOIN_H+0,  .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=K_JOIN_H+1,  .z=UNDEF        },  // (tag . value)
    { .t=VM_part,       .x=1,           .y=K_JOIN_H+2,  .z=UNDEF        },  // value tag
    { .t=VM_roll,       .x=3,           .y=K_JOIN_H+3,  .z=UNDEF        },  // k_tail
    { .t=VM_cmp,        .x=CMP_EQ,      .y=K_JOIN_H+4,  .z=UNDEF        },  // (tag == k_tail)
    { .t=VM_if,         .x=K_JOIN_H+5,  .y=RELEASE,     .z=UNDEF        },  // WRONG TAG!
    { .t=VM_roll,       .x=2,           .y=K_JOIN_H+6,  .z=UNDEF        },  // value head
    { .t=VM_pair,       .x=1,           .y=K_JOIN_H+7,  .z=UNDEF        },  // pair = (head . value)
    { .t=VM_roll,       .x=2,           .y=RELEASE_0,   .z=UNDEF        },  // pair cust

#define K_JOIN_T (K_JOIN_H+8)
//  { .t=VM_push,       .x=_cust_,      .y=K_JOIN_T-2,  .z=UNDEF        },
//  { .t=VM_push,       .x=_tail_,      .y=K_JOIN_T-1,  .z=UNDEF        },
//  { .t=VM_push,       .x=_k_head_,    .y=K_JOIN_T+0,  .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=K_JOIN_T+1,  .z=UNDEF        },  // (tag . value)
    { .t=VM_part,       .x=1,           .y=K_JOIN_T+2,  .z=UNDEF        },  // value tag
    { .t=VM_roll,       .x=3,           .y=K_JOIN_T+3,  .z=UNDEF        },  // k_head
    { .t=VM_cmp,        .x=CMP_EQ,      .y=K_JOIN_T+4,  .z=UNDEF        },  // (tag == k_head)
    { .t=VM_if,         .x=K_JOIN_T+5,  .y=RELEASE,     .z=UNDEF        },  // WRONG TAG!
    { .t=VM_pair,       .x=1,           .y=K_JOIN_T+6,  .z=UNDEF        },  // pair = (value . tail)
    { .t=VM_roll,       .x=2,           .y=RELEASE_0,   .z=UNDEF        },  // pair cust

/*
(define join-beh
  (lambda (cust k_head k_tail)
    (BEH (tag . value))
      ;
      ))
*/
#define JOIN_BEH (K_JOIN_T+7)
//  { .t=VM_push,       .x=_cust_,      .y=JOIN_BEH-2,  .z=UNDEF        },
//  { .t=VM_push,       .x=_k_head_,    .y=JOIN_BEH-1,  .z=UNDEF        },
//  { .t=VM_push,       .x=_k_tail_,    .y=JOIN_BEH+0,  .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=JOIN_BEH+1,  .z=UNDEF        },  // (tag . value)
    { .t=VM_part,       .x=1,           .y=JOIN_BEH+2,  .z=UNDEF        },  // value tag

    { .t=VM_pick,       .x=4,           .y=JOIN_BEH+3,  .z=UNDEF        },  // k_head
    { .t=VM_pick,       .x=2,           .y=JOIN_BEH+4,  .z=UNDEF        },  // tag
    { .t=VM_cmp,        .x=CMP_EQ,      .y=JOIN_BEH+5,  .z=UNDEF        },  // (tag == k_head)
    { .t=VM_if,         .x=JOIN_BEH+6,  .y=JOIN_BEH+11, .z=UNDEF        },

    { .t=VM_roll,       .x=5,           .y=JOIN_BEH+7,  .z=UNDEF        },  // k_head k_tail value tag cust
    { .t=VM_roll,       .x=3,           .y=JOIN_BEH+8,  .z=UNDEF        },  // k_head k_tail tag cust value
    { .t=VM_roll,       .x=4,           .y=JOIN_BEH+9,  .z=UNDEF        },  // k_head tag cust value k_tail
    { .t=VM_push,       .x=K_JOIN_H,    .y=JOIN_BEH+10, .z=UNDEF        },  // K_JOIN_H
    { .t=VM_beh,        .x=3,           .y=COMMIT,      .z=UNDEF        },  // BECOME (K_JOIN_H cust value k_tail)

    { .t=VM_pick,       .x=3,           .y=JOIN_BEH+12, .z=UNDEF        },  // k_tail
    { .t=VM_pick,       .x=2,           .y=JOIN_BEH+13, .z=UNDEF        },  // tag
    { .t=VM_cmp,        .x=CMP_EQ,      .y=JOIN_BEH+14, .z=UNDEF        },  // (tag == k_tail)
    { .t=VM_if,         .x=JOIN_BEH+15, .y=COMMIT,      .z=UNDEF        },

    { .t=VM_roll,       .x=5,           .y=JOIN_BEH+16, .z=UNDEF        },  // k_head k_tail value tag cust
    { .t=VM_roll,       .x=3,           .y=JOIN_BEH+17, .z=UNDEF        },  // k_head k_tail tag cust value
    { .t=VM_roll,       .x=5,           .y=JOIN_BEH+18, .z=UNDEF        },  // k_tail tag cust value k_head
    { .t=VM_push,       .x=K_JOIN_T,    .y=JOIN_BEH+19, .z=UNDEF        },  // K_JOIN_T
    { .t=VM_beh,        .x=3,           .y=COMMIT,      .z=UNDEF        },  // BECOME (K_JOIN_T cust value k_head)

/*
(define fork-beh
  (lambda (cust head tail)
    (BEH (h-req t-req))
      ;
      ))
*/
#define FORK_BEH (JOIN_BEH+20)
//  { .t=VM_push,       .x=_tail_,      .y=FORK_BEH-2,  .z=UNDEF        },
//  { .t=VM_push,       .x=_head_,      .y=FORK_BEH-1,  .z=UNDEF        },
//  { .t=VM_push,       .x=_cust_,      .y=FORK_BEH+0,  .z=UNDEF        },

    { .t=VM_self,       .x=UNDEF,       .y=FORK_BEH+1,  .z=UNDEF        },  // self
    { .t=VM_push,       .x=TAG_BEH,     .y=FORK_BEH+2,  .z=UNDEF        },  // TAG_BEH
    { .t=VM_new,        .x=1,           .y=FORK_BEH+3,  .z=UNDEF        },  // k_head

    { .t=VM_msg,        .x=1,           .y=FORK_BEH+4,  .z=UNDEF        },  // h_req
    { .t=VM_pick,       .x=2,           .y=FORK_BEH+5,  .z=UNDEF        },  // k_head
    { .t=VM_pair,       .x=1,           .y=FORK_BEH+6,  .z=UNDEF        },  // msg = (k_head . h_req)
    { .t=VM_roll,       .x=4,           .y=FORK_BEH+7,  .z=UNDEF        },  // tail cust k_head msg head
    { .t=VM_send,       .x=0,           .y=FORK_BEH+8,  .z=UNDEF        },  // (head . msg)

    { .t=VM_self,       .x=UNDEF,       .y=FORK_BEH+9,  .z=UNDEF        },  // self
    { .t=VM_push,       .x=TAG_BEH,     .y=FORK_BEH+10, .z=UNDEF        },  // TAG_BEH
    { .t=VM_new,        .x=1,           .y=FORK_BEH+11, .z=UNDEF        },  // k_tail

    { .t=VM_msg,        .x=2,           .y=FORK_BEH+12, .z=UNDEF        },  // t_req
    { .t=VM_pick,       .x=2,           .y=FORK_BEH+13, .z=UNDEF        },  // k_tail
    { .t=VM_pair,       .x=1,           .y=FORK_BEH+14, .z=UNDEF        },  // msg = (k_tail . t_req)
    { .t=VM_roll,       .x=5,           .y=FORK_BEH+15, .z=UNDEF        },  // cust k_head k_tail msg tail
    { .t=VM_send,       .x=0,           .y=FORK_BEH+16, .z=UNDEF        },  // (tail . msg)

    { .t=VM_push,       .x=JOIN_BEH,    .y=FORK_BEH+17, .z=UNDEF        },  // JOIN_BEH
    { .t=VM_beh,        .x=3,           .y=COMMIT,      .z=UNDEF        },  // BECOME (JOIN_BEH cust k_head k_tail)

//
// Static Symbols
//

#define S_IGNORE (FORK_BEH+18)
    { .t=Symbol_T,      .x=0,           .y=S_IGNORE+1,  .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('_'), .y=NIL,         .z=UNDEF        },

#define S_QUOTE (S_IGNORE+2)
    { .t=Symbol_T,      .x=0,           .y=S_QUOTE+1,   .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('q'), .y=S_QUOTE+2,   .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('u'), .y=S_QUOTE+3,   .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('o'), .y=S_QUOTE+4,   .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('t'), .y=S_QUOTE+5,   .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('e'), .y=NIL,         .z=UNDEF        },

#define S_QQUOTE (S_QUOTE+6)
    { .t=Symbol_T,      .x=0,           .y=S_QQUOTE+1,  .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('q'), .y=S_QQUOTE+2,  .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('u'), .y=S_QQUOTE+3,  .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('a'), .y=S_QQUOTE+4,  .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('s'), .y=S_QQUOTE+5,  .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('i'), .y=S_QQUOTE+6,  .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('q'), .y=S_QQUOTE+7,  .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('u'), .y=S_QQUOTE+8,  .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('o'), .y=S_QQUOTE+9,  .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('t'), .y=S_QQUOTE+10, .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('e'), .y=NIL,         .z=UNDEF        },

#define S_UNQUOTE (S_QQUOTE+11)
    { .t=Symbol_T,      .x=0,           .y=S_UNQUOTE+1, .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('u'), .y=S_UNQUOTE+2, .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('n'), .y=S_UNQUOTE+3, .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('q'), .y=S_UNQUOTE+4, .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('u'), .y=S_UNQUOTE+5, .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('o'), .y=S_UNQUOTE+6, .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('t'), .y=S_UNQUOTE+7, .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('e'), .y=NIL,         .z=UNDEF        },

#define S_QSPLICE (S_UNQUOTE+8)
    { .t=Symbol_T,      .x=0,           .y=S_QSPLICE+1, .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('u'), .y=S_QSPLICE+2, .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('n'), .y=S_QSPLICE+3, .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('q'), .y=S_QSPLICE+4, .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('u'), .y=S_QSPLICE+5, .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('o'), .y=S_QSPLICE+6, .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('t'), .y=S_QSPLICE+7, .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('e'), .y=S_QSPLICE+8, .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('-'), .y=S_QSPLICE+9, .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('s'), .y=S_QSPLICE+10,.z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('p'), .y=S_QSPLICE+11,.z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('l'), .y=S_QSPLICE+12,.z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('i'), .y=S_QSPLICE+13,.z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('c'), .y=S_QSPLICE+14,.z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('i'), .y=S_QSPLICE+15,.z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('n'), .y=S_QSPLICE+16,.z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX('g'), .y=NIL,         .z=UNDEF        },

//
// Meta-circular LISP/Scheme Interpreter
//

#define M_EVAL (S_QSPLICE+17)
#define K_COMBINE (M_EVAL+20)
#define K_APPLY_F (K_COMBINE+14)
#define M_APPLY (K_APPLY_F+4)
#define M_LOOKUP (M_APPLY+17)
#define M_EVLIS_P (M_LOOKUP+23)
#define M_EVLIS_K (M_EVLIS_P+4)
#define M_EVLIS (M_EVLIS_K+6)
#define FX_PAR (M_EVLIS+14)
#define OP_PAR (FX_PAR+1)
#define M_ZIP_IT (OP_PAR+20)
#define M_ZIP_K (M_ZIP_IT+12)
#define M_ZIP_P (M_ZIP_K+6)
#define M_ZIP_R (M_ZIP_P+9)
#define M_ZIP_S (M_ZIP_R+11)
#define M_ZIP (M_ZIP_S+7)
#define CLOSURE_B (M_ZIP+6)
#define M_EVAL_B (CLOSURE_B+13)
#define FEXPR_B (M_EVAL_B+5)
#define K_SEQ_B (FEXPR_B+15)
#define M_IF_K (K_SEQ_B+15)

/*
(define eval
  (lambda (form env)
    (if (symbol? form)                  ; bound variable
      (lookup form env)
      (if (pair? form)                  ; combination
        (let ((fn    (eval (car form) env))
              (opnds (cdr form)))
          (if (actor? fn)               ; _applicative_
            (CALL fn (evlis opnds env))
            (if (fexpr?)                ; _operative_
              (CALL (get-x fn) (list opnds env))
              #?)))
        form))))                        ; self-evaluating form
*/
    { .t=Actor_T,       .x=M_EVAL+1,    .y=NIL,         .z=UNDEF        },  // (cust form env)
    { .t=VM_msg,        .x=2,           .y=M_EVAL+2,    .z=UNDEF        },  // form = arg1
    { .t=VM_typeq,      .x=Symbol_T,    .y=M_EVAL+3,    .z=UNDEF        },  // form has type Symbol_T
    { .t=VM_if,         .x=M_EVAL+4,    .y=M_EVAL+6,    .z=UNDEF        },

    { .t=VM_msg,        .x=0,           .y=M_EVAL+5,    .z=UNDEF        },  // msg = (cust form env)
    { .t=VM_push,       .x=M_LOOKUP,    .y=SEND_0,      .z=UNDEF        },  // (M_LOOKUP cust key alist)

    { .t=VM_msg,        .x=2,           .y=M_EVAL+7,    .z=UNDEF        },  // form = arg1
    { .t=VM_typeq,      .x=Pair_T,      .y=M_EVAL+8,    .z=UNDEF        },  // form has type Pair_T
    { .t=VM_if,         .x=M_EVAL+10,   .y=M_EVAL+9,    .z=UNDEF        },

    { .t=VM_msg,        .x=2,           .y=CUST_SEND,   .z=UNDEF        },  // self-eval form

/*
      (if (pair? form)                  ; combination
        (let ((fn    (eval (car form) env))
              (opnds (cdr form)))
*/
    { .t=VM_msg,        .x=3,           .y=M_EVAL+11,   .z=UNDEF        },  // env
    { .t=VM_msg,        .x=2,           .y=M_EVAL+12,   .z=UNDEF        },  // form
    { .t=VM_part,       .x=1,           .y=M_EVAL+13,   .z=UNDEF        },  // tail head

    { .t=VM_msg,        .x=3,           .y=M_EVAL+14,   .z=UNDEF        },  // env
    { .t=VM_roll,       .x=3,           .y=M_EVAL+15,   .z=UNDEF        },  // opnds = tail
    { .t=VM_msg,        .x=1,           .y=M_EVAL+16,   .z=UNDEF        },  // cust
    { .t=VM_push,       .x=K_COMBINE,   .y=M_EVAL+17,   .z=UNDEF        },  // K_COMBINE
    { .t=VM_new,        .x=3,           .y=M_EVAL+18,   .z=UNDEF        },  // k_combine = (K_COMBINE env tail cust)

    { .t=VM_push,       .x=M_EVAL,      .y=M_EVAL+19,   .z=UNDEF        },  // M_EVAL
    { .t=VM_send,       .x=3,           .y=COMMIT,      .z=UNDEF        },  // (M_EVAL k_combine head env)

/*
          (if (actor? fn)               ; _applicative_
            (CALL fn (evlis opnds env))
            (if (fexpr?)                ; _operative_
              (CALL (get-x fn) (list opnds env))
              #?)))
*/
//  { .t=VM_push,       .x=_env_,       .y=K_COMBINE-2, .z=UNDEF        },
//  { .t=VM_push,       .x=_opnds_,     .y=K_COMBINE-1, .z=UNDEF        },
//  { .t=VM_push,       .x=_cust_,      .y=K_COMBINE+0, .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=K_COMBINE+1, .z=UNDEF        },  // fn
    { .t=VM_typeq,      .x=Actor_T,     .y=K_COMBINE+2, .z=UNDEF        },  // fn has type Actor_T
    { .t=VM_if,         .x=K_COMBINE+9, .y=K_COMBINE+3, .z=UNDEF        },

    { .t=VM_msg,        .x=0,           .y=K_COMBINE+4, .z=UNDEF        },  // fn
    { .t=VM_typeq,      .x=Fexpr_T,     .y=K_COMBINE+5, .z=UNDEF        },  // fn has type Fexpr_T
    { .t=VM_if,         .x=K_COMBINE+6, .y=RV_UNDEF,    .z=UNDEF        },

    { .t=VM_msg,        .x=0,           .y=K_COMBINE+7, .z=UNDEF        },  // env opnds cust fn
    { .t=VM_get,        .x=FLD_X,       .y=K_COMBINE+8, .z=UNDEF        },  // oper = get_x(fn)
    { .t=VM_send,       .x=3,           .y=RELEASE,     .z=UNDEF        },  // (oper cust args env)

// env opnds cust
    { .t=VM_msg,        .x=0,           .y=K_COMBINE+10,.z=UNDEF        },  // fn
    { .t=VM_push,       .x=K_APPLY_F,   .y=K_COMBINE+11,.z=UNDEF        },  // K_APPLY_F
    { .t=VM_new,        .x=2,           .y=K_COMBINE+12,.z=UNDEF        },  // k_apply = (K_APPLY_F cust fn)

#if EVLIS_IS_PAR
    { .t=VM_push,       .x=OP_PAR,      .y=K_COMBINE+13,.z=UNDEF        },  // OP_PAR
    { .t=VM_send,       .x=3,           .y=RELEASE,     .z=UNDEF        },  // (OP_PAR k_apply opnds env)
#else
    { .t=VM_push,       .x=M_EVLIS,     .y=K_COMBINE+13,.z=UNDEF        },  // M_EVLIS
    { .t=VM_send,       .x=3,           .y=RELEASE,     .z=UNDEF        },  // (M_EVLIS k_apply opnds env)
#endif

/*
            (CALL fn (evlis opnds env))
*/
//  { .t=VM_push,       .x=_cust_,      .y=K_APPLY_F-1, .z=UNDEF        },
//  { .t=VM_push,       .x=_fn_,        .y=K_APPLY_F+0, .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=K_APPLY_F+1, .z=UNDEF        },  // args
    { .t=VM_roll,       .x=3,           .y=K_APPLY_F+2, .z=UNDEF        },  // fn args cust
    { .t=VM_pair,       .x=1,           .y=K_APPLY_F+3, .z=UNDEF        },  // fn (cust . args)
    { .t=VM_roll,       .x=2,           .y=RELEASE_0,   .z=UNDEF        },  // (cust . args) fn

/*
(define apply
  (lambda (fn args env)
    (if (actor? fn)                     ; _compiled_
      (CALL fn args)
      (if (fexpr? fn)                   ; _interpreted_
        (CALL (get-x fn) (list args env))
        #?))))
*/
    { .t=Actor_T,       .x=M_APPLY+1,   .y=NIL,         .z=UNDEF        },  // (cust fn args env)
    { .t=VM_msg,        .x=2,           .y=M_APPLY+2,   .z=UNDEF        },  // fn = arg1
    { .t=VM_typeq,      .x=Actor_T,     .y=M_APPLY+3,   .z=UNDEF        },  // fn has type Actor_T
    { .t=VM_if,         .x=M_APPLY+4,   .y=M_APPLY+8,   .z=UNDEF        },

    { .t=VM_msg,        .x=3,           .y=M_APPLY+5,   .z=UNDEF        },  // args
    { .t=VM_msg,        .x=1,           .y=M_APPLY+6,   .z=UNDEF        },  // cust
    { .t=VM_pair,       .x=1,           .y=M_APPLY+7,   .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=2,           .y=SEND_0,      .z=UNDEF        },  // fn

    { .t=VM_msg,        .x=2,           .y=M_APPLY+9,   .z=UNDEF        },  // fn = arg1
    { .t=VM_typeq,      .x=Fexpr_T,     .y=M_APPLY+10,  .z=UNDEF        },  // fn has type Fexpr_T
    { .t=VM_if,         .x=M_APPLY+11,  .y=RV_UNDEF,    .z=UNDEF        },

    { .t=VM_msg,        .x=4,           .y=M_APPLY+12,  .z=UNDEF        },  // env
    { .t=VM_msg,        .x=3,           .y=M_APPLY+13,  .z=UNDEF        },  // args
    { .t=VM_msg,        .x=1,           .y=M_APPLY+14,  .z=UNDEF        },  // cust
    { .t=VM_msg,        .x=2,           .y=M_APPLY+15,  .z=UNDEF        },  // fn
    { .t=VM_get,        .x=FLD_X,       .y=M_APPLY+16,  .z=UNDEF        },  // oper = get_x(fn)
    { .t=VM_send,       .x=3,           .y=COMMIT,      .z=UNDEF        },  // (oper cust args env)

/*
(define lookup                          ; look up variable binding in environment
  (lambda (key env)
    (if (pair? env)                     ; association list
      (if (eq? (caar env) key)
        (cdar env)
        (lookup key (cdr env)))
      (if (actor? env)
        (CALL env key)                  ; delegate to environment actor
        (if (symbol? key)
          (get-z key)                   ; get top-level binding
          #?))))                        ; value is undefined
*/
    { .t=Actor_T,       .x=M_LOOKUP+1,  .y=NIL,         .z=UNDEF        },  // (cust key env)
    { .t=VM_msg,        .x=3,           .y=M_LOOKUP+2,  .z=UNDEF        },  // env = arg2

    { .t=VM_pick,       .x=1,           .y=M_LOOKUP+3,  .z=UNDEF        },  // env env
    { .t=VM_typeq,      .x=Pair_T,      .y=M_LOOKUP+4,  .z=UNDEF        },  // env has type Pair_T
    { .t=VM_if,         .x=M_LOOKUP+5,  .y=M_LOOKUP+11, .z=UNDEF        },

    { .t=VM_part,       .x=1,           .y=M_LOOKUP+6,  .z=UNDEF        },  // tail head
    { .t=VM_part,       .x=1,           .y=M_LOOKUP+7,  .z=UNDEF        },  // tail value name
    { .t=VM_msg,        .x=2,           .y=M_LOOKUP+8,  .z=UNDEF        },  // key = arg1
    { .t=VM_cmp,        .x=CMP_EQ,      .y=M_LOOKUP+9,  .z=UNDEF        },  // (name == key)
    { .t=VM_if,         .x=CUST_SEND,   .y=M_LOOKUP+10, .z=UNDEF        },
    { .t=VM_drop,       .x=1,           .y=M_LOOKUP+2,  .z=UNDEF        },  // env = tail

    { .t=VM_pick,       .x=1,           .y=M_LOOKUP+12, .z=UNDEF        },  // env env
    { .t=VM_typeq,      .x=Actor_T,     .y=M_LOOKUP+13, .z=UNDEF        },  // env has type Actor_T
    { .t=VM_if,         .x=M_LOOKUP+14, .y=M_LOOKUP+18, .z=UNDEF        },

    { .t=VM_msg,        .x=2,           .y=M_LOOKUP+15, .z=UNDEF        },  // key = arg1
    { .t=VM_msg,        .x=1,           .y=M_LOOKUP+16, .z=UNDEF        },  // cust = arg0
    { .t=VM_pair,       .x=1,           .y=M_LOOKUP+17, .z=UNDEF        },  // (cust . key)
    { .t=VM_roll,       .x=2,           .y=SEND_0,      .z=UNDEF        },  // (cust . key) env

    { .t=VM_msg,        .x=2,           .y=M_LOOKUP+19, .z=UNDEF        },  // key = arg1
    { .t=VM_pick,       .x=1,           .y=M_LOOKUP+20, .z=UNDEF        },  // key key
    { .t=VM_typeq,      .x=Symbol_T,    .y=M_LOOKUP+21, .z=UNDEF        },  // key has type Symbol_T
    { .t=VM_if,         .x=M_LOOKUP+22, .y=RV_UNDEF,    .z=UNDEF        },

    { .t=VM_get,        .x=FLD_Z,       .y=CUST_SEND,   .z=UNDEF        },  // global binding from Symbol_T

/*
(define evlis                           ; map `eval` over a list of operands
  (lambda (opnds env)
    (if (pair? opnds)
      (cons (eval (car opnds) env) (evlis (cdr opnds) env))
      ())))                             ; value is NIL
*/
//  { .t=VM_push,       .x=_cust_,      .y=M_EVLIS_P-1, .z=UNDEF        },
//  { .t=VM_push,       .x=_head_,      .y=M_EVLIS_P+0, .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=M_EVLIS_P+1, .z=UNDEF        },  // tail
    { .t=VM_roll,       .x=2,           .y=M_EVLIS_P+2, .z=UNDEF        },  // head
    { .t=VM_pair,       .x=1,           .y=M_EVLIS_P+3, .z=UNDEF        },  // (head . tail)
    { .t=VM_roll,       .x=2,           .y=RELEASE_0,   .z=UNDEF        },  // cust

//  { .t=VM_push,       .x=_env_,       .y=M_EVLIS_K-2, .z=UNDEF        },
//  { .t=VM_push,       .x=_rest_,      .y=M_EVLIS_K-1, .z=UNDEF        },
//  { .t=VM_push,       .x=_cust_,      .y=M_EVLIS_K+0, .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=M_EVLIS_K+1, .z=UNDEF        },  // head
    { .t=VM_push,       .x=M_EVLIS_P,   .y=M_EVLIS_K+2, .z=UNDEF        },  // M_EVLIS_P
    { .t=VM_beh,        .x=2,           .y=M_EVLIS_K+3, .z=UNDEF        },  // BECOME (M_EVLIS_P cust head)
    { .t=VM_self,       .x=UNDEF,       .y=M_EVLIS_K+4, .z=UNDEF        },  // SELF
    { .t=VM_push,       .x=M_EVLIS,     .y=M_EVLIS_K+5, .z=UNDEF        },  // M_EVLIS
    { .t=VM_send,       .x=3,           .y=COMMIT,      .z=UNDEF        },  // (M_EVLIS SELF rest env)

    { .t=Actor_T,       .x=M_EVLIS+1,   .y=NIL,         .z=UNDEF        },  // (cust opnds env)
    { .t=VM_msg,        .x=2,           .y=M_EVLIS+2,   .z=UNDEF        },  // opnds = arg1
    { .t=VM_typeq,      .x=Pair_T,      .y=M_EVLIS+3,   .z=UNDEF        },  // opnds has type Pair_T
    { .t=VM_if,         .x=M_EVLIS+4,   .y=RV_NIL,      .z=UNDEF        },

    { .t=VM_msg,        .x=3,           .y=M_EVLIS+5,   .z=UNDEF        },  // env = arg2
    { .t=VM_msg,        .x=2,           .y=M_EVLIS+6,   .z=UNDEF        },  // opnds = arg1
    { .t=VM_part,       .x=1,           .y=M_EVLIS+7,   .z=UNDEF        },  // rest first

    { .t=VM_pick,       .x=3,           .y=M_EVLIS+8,   .z=UNDEF        },  // env
    { .t=VM_roll,       .x=3,           .y=M_EVLIS+9,   .z=UNDEF        },  // rest
    { .t=VM_msg,        .x=1,           .y=M_EVLIS+10,  .z=UNDEF        },  // cust
    { .t=VM_push,       .x=M_EVLIS_K,   .y=M_EVLIS+11,  .z=UNDEF        },  // M_EVLIS_K
    { .t=VM_new,        .x=3,           .y=M_EVLIS+12,  .z=UNDEF        },  // k_eval = (M_EVLIS_K env rest cust)

    { .t=VM_push,       .x=M_EVAL,      .y=M_EVLIS+13,  .z=UNDEF        },  // M_EVAL
    { .t=VM_send,       .x=3,           .y=COMMIT,      .z=UNDEF        },  // (M_EVAL k_eval first env)

/*
(define op-par                          ; (par . <exprs>)
  (CREATE
    (BEH (cust opnds env)
      (if (pair? opnds)
        (SEND
          (CREATE (fork-beh cust eval op-par))
          (list ((car opnds) env) ((cdr opnds) env)))
        (SEND cust ()))
      )))
*/
    { .t=Fexpr_T,       .x=OP_PAR,      .y=UNDEF,       .z=UNDEF        },  // (par . <exprs>)

    { .t=Actor_T,       .x=OP_PAR+1,    .y=NIL,         .z=UNDEF        },  // (cust opnds env)
    { .t=VM_msg,        .x=2,           .y=OP_PAR+2,    .z=UNDEF        },  // exprs = opnds
    { .t=VM_typeq,      .x=Pair_T,      .y=OP_PAR+3,    .z=UNDEF        },  // exprs has type Pair_T
    { .t=VM_if,         .x=OP_PAR+4,    .y=RV_NIL,      .z=UNDEF        },

    { .t=VM_push,       .x=NIL,         .y=OP_PAR+5,    .z=UNDEF        },  // ()
    { .t=VM_msg,        .x=3,           .y=OP_PAR+6,    .z=UNDEF        },  // env
    { .t=VM_msg,        .x=2,           .y=OP_PAR+7,    .z=UNDEF        },  // exprs = opnds
    { .t=VM_nth,        .x=-1,          .y=OP_PAR+8,    .z=UNDEF        },  // cdr(exprs)
    { .t=VM_pair,       .x=2,           .y=OP_PAR+9,    .z=UNDEF        },  // t_req = (cdr(exprs) env)

    { .t=VM_push,       .x=NIL,         .y=OP_PAR+10,   .z=UNDEF        },  // ()
    { .t=VM_msg,        .x=3,           .y=OP_PAR+11,   .z=UNDEF        },  // env
    { .t=VM_msg,        .x=2,           .y=OP_PAR+12,   .z=UNDEF        },  // exprs = opnds
    { .t=VM_nth,        .x=1,           .y=OP_PAR+13,   .z=UNDEF        },  // car(exprs)
    { .t=VM_pair,       .x=2,           .y=OP_PAR+14,   .z=UNDEF        },  // h_req = (car(exprs) env)

    { .t=VM_push,       .x=OP_PAR,      .y=OP_PAR+15,   .z=UNDEF        },  // tail = OP_PAR
    { .t=VM_push,       .x=M_EVAL,      .y=OP_PAR+16,   .z=UNDEF        },  // head = M_EVAL
    { .t=VM_msg,        .x=1,           .y=OP_PAR+17,   .z=UNDEF        },  // cust
    { .t=VM_push,       .x=FORK_BEH,    .y=OP_PAR+18,   .z=UNDEF        },  // FORK_BEH
    { .t=VM_new,        .x=3,           .y=OP_PAR+19,   .z=UNDEF        },  // ev_fork = (FORK_BEH OP_PAR M_EVAL cust)

    { .t=VM_send,       .x=2,           .y=COMMIT,      .z=UNDEF        },  // (ev_fork h_req t_req)

/*
(define var-name? (lambda (x) (if (symbol? x) (if (eq? x '_) #f #t) #f)))
(define zip-it                          ; extend `env` by binding names `x` to values `y`
  (lambda (x y xs ys env)
    (cond
      ((pair? x)
        (if (null? (cdr x))
          (zip-it (car x) (car y) xs ys env)
          (zip-it (car x) (car y) (cons (cdr x) xs) (cons (cdr y) ys) env)))
      ((var-name? x)
        (zip-it xs ys () () (cons (cons x y) env)))
      ((null? xs)
        env)
      (#t
        (zip-it xs ys () () env))
    )))
*/
//  { .t=VM_push,       .x=_ys_,        .y=M_ZIP_IT-4,  .z=UNDEF        },
//  { .t=VM_push,       .x=_xs_,        .y=M_ZIP_IT-3,  .z=UNDEF        },
//  { .t=VM_push,       .x=_y_,         .y=M_ZIP_IT-2,  .z=UNDEF        },
//  { .t=VM_push,       .x=_x_,         .y=M_ZIP_IT-1,  .z=UNDEF        },
//  { .t=VM_push,       .x=_env_,       .y=M_ZIP_IT+0,  .z=UNDEF        },

// ys xs y x env
    { .t=VM_pick,       .x=2,           .y=M_ZIP_IT+1,  .z=UNDEF        },  // x
    { .t=VM_typeq,      .x=Pair_T,      .y=M_ZIP_IT+2,  .z=UNDEF        },  // x has type Pair_T
    { .t=VM_if,         .x=M_ZIP_P,     .y=M_ZIP_IT+3,  .z=UNDEF        },

    { .t=VM_pick,       .x=2,           .y=M_ZIP_IT+4,  .z=UNDEF        },  // x
    { .t=VM_typeq,      .x=Symbol_T,    .y=M_ZIP_IT+5,  .z=UNDEF        },  // x has type Symbol_T
    { .t=VM_if,         .x=M_ZIP_IT+6,  .y=M_ZIP_IT+9,  .z=UNDEF        },

    { .t=VM_pick,       .x=2,           .y=M_ZIP_IT+7,  .z=UNDEF        },  // x
    { .t=VM_eq,         .x=S_IGNORE,    .y=M_ZIP_IT+8,  .z=UNDEF        },  // (x == '_)
    { .t=VM_if,         .x=M_ZIP_IT+9,  .y=M_ZIP_S,     .z=UNDEF        },

    { .t=VM_pick,       .x=4,           .y=M_ZIP_IT+10, .z=UNDEF        },  // xs
    { .t=VM_eq,         .x=NIL,         .y=M_ZIP_IT+11, .z=UNDEF        },  // (xs == NIL)
    { .t=VM_if,         .x=CUST_SEND,   .y=M_ZIP_K,     .z=UNDEF        },  // return(env)

// ys xs y x env
    { .t=VM_roll,       .x=-3,          .y=M_ZIP_K+1,   .z=UNDEF        },  // ys xs env y x
    { .t=VM_drop,       .x=2,           .y=M_ZIP_K+2,   .z=UNDEF        },  // ys xs env
    { .t=VM_push,       .x=NIL,         .y=M_ZIP_K+3,   .z=UNDEF        },  // ys xs env ()
    { .t=VM_roll,       .x=-4,          .y=M_ZIP_K+4,   .z=UNDEF        },  // () ys xs env
    { .t=VM_push,       .x=NIL,         .y=M_ZIP_K+5,   .z=UNDEF        },  // () ys xs env ()
    { .t=VM_roll,       .x=-4,          .y=M_ZIP_IT,    .z=UNDEF        },  // () () ys xs env

/*
        (if (null? (cdr x))
          (zip-it (car x) (car y) xs ys env)
*/
// ys xs y x env
    { .t=VM_pick,       .x=2,           .y=M_ZIP_P+1,   .z=UNDEF        },  // x
    { .t=VM_nth,        .x=-1,          .y=M_ZIP_P+2,   .z=UNDEF        },  // cdr(x)
    { .t=VM_eq,         .x=NIL,         .y=M_ZIP_P+3,   .z=UNDEF        },  // (cdr(x) == NIL)
    { .t=VM_if,         .x=M_ZIP_P+4,   .y=M_ZIP_R,     .z=UNDEF        },

    { .t=VM_roll,       .x=3,           .y=M_ZIP_P+5,   .z=UNDEF        },  // ys xs x env y
    { .t=VM_nth,        .x=1,           .y=M_ZIP_P+6,   .z=UNDEF        },  // ys xs x env car(y)
    { .t=VM_roll,       .x=3,           .y=M_ZIP_P+7,   .z=UNDEF        },  // ys xs env car(y) x
    { .t=VM_nth,        .x=1,           .y=M_ZIP_P+8,   .z=UNDEF        },  // ys xs env car(y) car(x)
    { .t=VM_roll,       .x=3,           .y=M_ZIP_IT,    .z=UNDEF        },  // ys xs car(y) car(x) env

/*
          (zip-it (car x) (car y) (cons (cdr x) xs) (cons (cdr y) ys) env)))
*/
// ys xs y x env
    { .t=VM_roll,       .x=5,           .y=M_ZIP_R+1,   .z=UNDEF        },  // xs y x env ys
    { .t=VM_roll,       .x=4,           .y=M_ZIP_R+2,   .z=UNDEF        },  // xs x env ys y
    { .t=VM_part,       .x=1,           .y=M_ZIP_R+3,   .z=UNDEF        },  // xs x env ys cdr(y) car(y)
    { .t=VM_roll,       .x=-6,          .y=M_ZIP_R+4,   .z=UNDEF        },  // car(y) xs x env ys cdr(y)
    { .t=VM_pair,       .x=1,           .y=M_ZIP_R+5,   .z=UNDEF        },  // car(y) xs x env (cdr(y) . ys)
    { .t=VM_roll,       .x=-5,          .y=M_ZIP_R+6,   .z=UNDEF        },  // (cdr(y) . ys) car(y) xs x env
// ys' y' xs x env
    { .t=VM_roll,       .x=-3,          .y=M_ZIP_R+7,   .z=UNDEF        },  // ys' y' env xs x
    { .t=VM_part,       .x=1,           .y=M_ZIP_R+8,   .z=UNDEF        },  // ys' y' env xs cdr(x) car(x)
    { .t=VM_roll,       .x=-4,          .y=M_ZIP_R+9,   .z=UNDEF        },  // ys' y' car(x) env xs cdr(x)
    { .t=VM_pair,       .x=1,           .y=M_ZIP_R+10,  .z=UNDEF        },  // ys' y' car(x) env (cdr(x) . xs)
    { .t=VM_roll,       .x=-4,          .y=M_ZIP_IT,    .z=UNDEF        },  // ys' (cdr(x) . xs) y' car(x) env

/*
        (zip-it xs ys () () (cons (cons x y) env)))
*/
// ys xs y x env
    { .t=VM_roll,       .x=-3,          .y=M_ZIP_S+1,   .z=UNDEF        },  // ys xs env y x
    { .t=VM_pair,       .x=1,           .y=M_ZIP_S+2,   .z=UNDEF        },  // ys xs env (x . y)
    { .t=VM_pair,       .x=1,           .y=M_ZIP_S+3,   .z=UNDEF        },  // ys xs ((x . y) . env)
    { .t=VM_push,       .x=NIL,         .y=M_ZIP_S+4,   .z=UNDEF        },  // ys xs env' ()
    { .t=VM_roll,       .x=-4,          .y=M_ZIP_S+5,   .z=UNDEF        },  // () ys xs env'
    { .t=VM_push,       .x=NIL,         .y=M_ZIP_S+6,   .z=UNDEF        },  // () ys xs env' ()
    { .t=VM_roll,       .x=-4,          .y=M_ZIP_IT,    .z=UNDEF        },  // () () ys xs env'

/*
(define zip                             ; extend `env` by binding names `x` to values `y`
  (lambda (x y env)
    (zip-it x y () () env)))
*/
    { .t=Actor_T,       .x=M_ZIP+1,     .y=NIL,         .z=UNDEF        },  // (cust x y env)
    { .t=VM_push,       .x=NIL,         .y=M_ZIP+2,     .z=UNDEF        },  // ys = ()
    { .t=VM_push,       .x=NIL,         .y=M_ZIP+3,     .z=UNDEF        },  // xs = ()
    { .t=VM_msg,        .x=3,           .y=M_ZIP+4,     .z=UNDEF        },  // y = arg2
    { .t=VM_msg,        .x=2,           .y=M_ZIP+5,     .z=UNDEF        },  // x = arg1
    { .t=VM_msg,        .x=4,           .y=M_ZIP_IT,    .z=UNDEF        },  // env = arg3

/*
(define closure-beh                     ; lexically-bound applicative procedure
  (lambda (frml body env)
    (BEH (cust . args)
      (evbody #unit body (zip frml args (scope env))))))
*/
//  { .t=VM_push,       .x=_frml_,      .y=CLOSURE_B-2, .z=UNDEF        },
//  { .t=VM_push,       .x=_body_,      .y=CLOSURE_B-1, .z=UNDEF        },
//  { .t=VM_push,       .x=_env_,       .y=CLOSURE_B+0, .z=UNDEF        },
    { .t=VM_pick,       .x=1,           .y=CLOSURE_B+1, .z=UNDEF        },  // env
    { .t=VM_push,       .x=UNDEF,       .y=CLOSURE_B+2, .z=UNDEF        },  // #?
    { .t=VM_push,       .x=S_IGNORE,    .y=CLOSURE_B+3, .z=UNDEF        },  // '_
    { .t=VM_pair,       .x=1,           .y=CLOSURE_B+4, .z=UNDEF        },  // ('_ . #?)
    { .t=VM_pair,       .x=1,           .y=CLOSURE_B+5, .z=UNDEF        },  // env' = (('_ . #?) . env)

    { .t=VM_msg,        .x=-1,          .y=CLOSURE_B+6, .z=UNDEF        },  // args
    { .t=VM_pick,       .x=5,           .y=CLOSURE_B+7, .z=UNDEF        },  // frml

    { .t=VM_msg,        .x=1,           .y=CLOSURE_B+8, .z=UNDEF        },  // cust
    { .t=VM_pick,       .x=6,           .y=CLOSURE_B+9, .z=UNDEF        },  // body
    { .t=VM_push,       .x=M_EVAL_B,    .y=CLOSURE_B+10,.z=UNDEF        },  // M_EVAL_B
    { .t=VM_new,        .x=2,           .y=CLOSURE_B+11,.z=UNDEF        },  // k_eval = (M_EVAL_B cust body)

    { .t=VM_push,       .x=M_ZIP,       .y=CLOSURE_B+12,.z=UNDEF        },  // M_ZIP
    { .t=VM_send,       .x=4,           .y=COMMIT,      .z=UNDEF        },  // (M_ZIP k_eval frml args env')

//  { .t=VM_push,       .x=_cust_,      .y=M_EVAL_B-1,  .z=UNDEF        },
//  { .t=VM_push,       .x=_body_,      .y=M_EVAL_B-0,  .z=UNDEF        },
    { .t=VM_push,       .x=UNIT,        .y=M_EVAL_B+1,  .z=UNDEF        },  // UNIT
    { .t=VM_roll,       .x=-3,          .y=M_EVAL_B+2,  .z=UNDEF        },  // #unit cust body

    { .t=VM_msg,        .x=0,           .y=M_EVAL_B+3,  .z=UNDEF        },  // env
    { .t=VM_push,       .x=K_SEQ_B,     .y=M_EVAL_B+4,  .z=UNDEF        },  // K_SEQ_B
    { .t=VM_new,        .x=3,           .y=SEND_0,      .z=UNDEF        },  // k-seq = (K_SEQ_B cust body env)

/*
(define fexpr-beh                       ; lexically-bound operative procedure
  (lambda (frml body denv)
    (BEH (cust opnds senv)
      (evbody #unit body (zip frml (cons denv opnds) (scope senv))))))
*/
//  { .t=VM_push,       .x=_frml_,      .y=FEXPR_B-2,   .z=UNDEF        },
//  { .t=VM_push,       .x=_body_,      .y=FEXPR_B-1,   .z=UNDEF        },
//  { .t=VM_push,       .x=_senv_,      .y=FEXPR_B+0,   .z=UNDEF        },
    { .t=VM_pick,       .x=1,           .y=FEXPR_B+1,   .z=UNDEF        },  // senv
    { .t=VM_push,       .x=UNDEF,       .y=FEXPR_B+2,   .z=UNDEF        },  // #?
    { .t=VM_push,       .x=S_IGNORE,    .y=FEXPR_B+3,   .z=UNDEF        },  // '_
    { .t=VM_pair,       .x=1,           .y=FEXPR_B+4,   .z=UNDEF        },  // ('_ . #?)
    { .t=VM_pair,       .x=1,           .y=FEXPR_B+5,   .z=UNDEF        },  // env' = (('_ . #?) . senv)

    { .t=VM_msg,        .x=2,           .y=FEXPR_B+6,   .z=UNDEF        },  // opnds
    { .t=VM_msg,        .x=3,           .y=FEXPR_B+7,   .z=UNDEF        },  // denv
    { .t=VM_pair,       .x=1,           .y=FEXPR_B+8,   .z=UNDEF        },  // opnds' = (denv . opnds)

    { .t=VM_pick,       .x=5,           .y=FEXPR_B+9,   .z=UNDEF        },  // frml'

    { .t=VM_msg,        .x=1,           .y=FEXPR_B+10,  .z=UNDEF        },  // cust
    { .t=VM_pick,       .x=6,           .y=FEXPR_B+11,  .z=UNDEF        },  // body
    { .t=VM_push,       .x=M_EVAL_B,    .y=FEXPR_B+12,  .z=UNDEF        },  // M_EVAL_B
    { .t=VM_new,        .x=2,           .y=FEXPR_B+13,  .z=UNDEF        },  // k_eval = (M_EVAL_B cust body)

    { .t=VM_push,       .x=M_ZIP,       .y=FEXPR_B+14,  .z=UNDEF        },  // M_ZIP
    { .t=VM_send,       .x=4,           .y=COMMIT,      .z=UNDEF        },  // (M_ZIP k_eval frml' opnds' env')

/*
(define k-seq-beh
  (lambda (cust body env)
    (BEH value
      (if (pair? body)
        (SEND
          (CREATE (k-seq-beh cust (cdr body) env))  ; BECOME this...
          (eval (car body) env))
        (SEND cust value)) )))
*/
//  { .t=VM_push,       .x=_cust_,      .y=K_SEQ_B-2,   .z=UNDEF        },
//  { .t=VM_push,       .x=_body_,      .y=K_SEQ_B-1,   .z=UNDEF        },
//  { .t=VM_push,       .x=_env_,       .y=K_SEQ_B+0,   .z=UNDEF        },
    { .t=VM_pick,       .x=2,           .y=K_SEQ_B+1,   .z=UNDEF        },  // body
    { .t=VM_typeq,      .x=Pair_T,      .y=K_SEQ_B+2,   .z=UNDEF        },  // body has type Pair_T
    { .t=VM_if,         .x=K_SEQ_B+5,   .y=K_SEQ_B+3,   .z=UNDEF        },

    { .t=VM_msg,        .x=0,           .y=K_SEQ_B+4,   .z=UNDEF        },  // value
    { .t=VM_roll,       .x=4,           .y=RELEASE_0,   .z=UNDEF        },  // (cust . value)

    { .t=VM_roll,       .x=2,           .y=K_SEQ_B+6,   .z=UNDEF        },  // cust env body
    { .t=VM_part,       .x=1,           .y=K_SEQ_B+7,   .z=UNDEF        },  // rest first

    { .t=VM_pick,       .x=3,           .y=K_SEQ_B+8,   .z=UNDEF        },  // env
    { .t=VM_roll,       .x=2,           .y=K_SEQ_B+9,   .z=UNDEF        },  // expr = first
    { .t=VM_self,       .x=UNDEF,       .y=K_SEQ_B+10,  .z=UNDEF        },  // cust = SELF
    { .t=VM_push,       .x=M_EVAL,      .y=K_SEQ_B+11,  .z=UNDEF        },  // M_EVAL
    { .t=VM_send,       .x=3,           .y=K_SEQ_B+12,  .z=UNDEF        },  // (M_EVAL SELF first env)

    { .t=VM_roll,       .x=-2,          .y=K_SEQ_B+13,  .z=UNDEF        },  // cust rest env
    { .t=VM_push,       .x=K_SEQ_B,     .y=K_SEQ_B+14,  .z=UNDEF        },  // K_SEQ_B
    { .t=VM_beh,        .x=3,           .y=COMMIT,      .z=UNDEF        },  // BECOME (K_SEQ_B cust rest env)

/*
(define evalif                          ; if `test` is #f, evaluate `altn`,
  (lambda (test cnsq altn env)          ; otherwise evaluate `cnsq`.
    (if test
      (eval cnsq env)
      (eval altn env))))
*/
//  { .t=VM_push,       .x=_cust_,      .y=M_IF_K-2,    .z=UNDEF        },
//  { .t=VM_push,       .x=_env_,       .y=M_IF_K-1,    .z=UNDEF        },
//  { .t=VM_push,       .x=_cont_,      .y=M_IF_K+0,    .z=UNDEF        },  // (cnsq altn)
    { .t=VM_msg,        .x=0,           .y=M_IF_K+1,    .z=UNDEF        },  // bool
    { .t=VM_if,         .x=M_IF_K+2,    .y=M_IF_K+3,    .z=UNDEF        },

    { .t=VM_nth,        .x=1,           .y=M_IF_K+4,    .z=UNDEF        },  // cnsq

    { .t=VM_nth,        .x=2,           .y=M_IF_K+4,    .z=UNDEF        },  // altn

    { .t=VM_pick,       .x=3,           .y=M_IF_K+5,    .z=UNDEF        },  // cust
    { .t=VM_push,       .x=M_EVAL,      .y=M_IF_K+6,    .z=UNDEF        },  // M_EVAL
    { .t=VM_send,       .x=3,           .y=RELEASE,     .z=UNDEF        },  // (M_EVAL cust cnsq/altn env)

/*
(define bind-env                        ; update binding in environment
  (lambda (key val env)
    (if (pair? env)                     ; association list
      (if (eq? (caar env) '_)
        (seq                            ; insert new binding
          (set-cdr env (cons (car env) (cdr env)))
          (set-car env (cons key val)))
        (if (eq? (caar env) key)
          (set-cdr (car env) val)       ; mutate binding
          (bind-env key val (cdr env))))
      (if (symbol? key)
        (set-z key val)))               ; set top-level binding
    #unit))                             ; value is UNIT
*/
#define M_BIND_E (M_IF_K+7)
    { .t=Actor_T,       .x=M_BIND_E+1,  .y=NIL,         .z=UNDEF        },  // (cust key val env)
    { .t=VM_msg,        .x=4,           .y=M_BIND_E+2,  .z=UNDEF        },  // env = arg3

    { .t=VM_pick,       .x=1,           .y=M_BIND_E+3,  .z=UNDEF        },  // env env
    { .t=VM_typeq,      .x=Pair_T,      .y=M_BIND_E+4,  .z=UNDEF        },  // env has type Pair_T
    { .t=VM_if,         .x=M_BIND_E+5,  .y=M_BIND_E+25, .z=UNDEF        },

    { .t=VM_pick,       .x=1,           .y=M_BIND_E+6,  .z=UNDEF        },  // env env
    { .t=VM_part,       .x=1,           .y=M_BIND_E+7,  .z=UNDEF        },  // cdr(env) car(env)
    { .t=VM_pick,       .x=1,           .y=M_BIND_E+8,  .z=UNDEF        },  // car(env) car(env)
    { .t=VM_nth,        .x=1,           .y=M_BIND_E+9,  .z=UNDEF        },  // caar(env)
    { .t=VM_eq,         .x=S_IGNORE,    .y=M_BIND_E+10, .z=UNDEF        },  // (caar(env) == '_)
    { .t=VM_if,         .x=M_BIND_E+11, .y=M_BIND_E+17, .z=UNDEF        },

    { .t=VM_pair,       .x=1,           .y=M_BIND_E+12, .z=UNDEF        },  // (car(env) . cdr(env))
    { .t=VM_set,        .x=FLD_Y,       .y=M_BIND_E+13, .z=UNDEF        },  // set-cdr

    { .t=VM_msg,        .x=3,           .y=M_BIND_E+14, .z=UNDEF        },  // val = arg2
    { .t=VM_msg,        .x=2,           .y=M_BIND_E+15, .z=UNDEF        },  // key = arg1
    { .t=VM_pair,       .x=1,           .y=M_BIND_E+16, .z=UNDEF        },  // (key . val)
    { .t=VM_set,        .x=FLD_X,       .y=RV_UNIT,     .z=UNDEF        },  // set-car

    { .t=VM_pick,       .x=1,           .y=M_BIND_E+18, .z=UNDEF        },  // car(env) car(env)
    { .t=VM_nth,        .x=1,           .y=M_BIND_E+19, .z=UNDEF        },  // caar(env)
    { .t=VM_msg,        .x=2,           .y=M_BIND_E+20, .z=UNDEF        },  // key = arg1
    { .t=VM_cmp,        .x=CMP_EQ,      .y=M_BIND_E+21, .z=UNDEF        },  // (caar(env) == key)
    { .t=VM_if,         .x=M_BIND_E+22, .y=M_BIND_E+24, .z=UNDEF        },

    { .t=VM_msg,        .x=3,           .y=M_BIND_E+23, .z=UNDEF        },  // val = arg2
    { .t=VM_set,        .x=FLD_Y,       .y=RV_UNIT,     .z=UNDEF        },  // set-cdr

    { .t=VM_drop,       .x=1,           .y=M_BIND_E+2,  .z=UNDEF        },  // (bind-env key val (cdr env))

    { .t=VM_msg,        .x=2,           .y=M_BIND_E+26, .z=UNDEF        },  // key = arg1
    { .t=VM_typeq,      .x=Symbol_T,    .y=M_BIND_E+27, .z=UNDEF        },  // key has type Symbol_T
    { .t=VM_if,         .x=M_BIND_E+28, .y=RV_UNIT,     .z=UNDEF        },

    { .t=VM_msg,        .x=2,           .y=M_BIND_E+29, .z=UNDEF        },  // key = arg1
    { .t=VM_msg,        .x=3,           .y=M_BIND_E+30, .z=UNDEF        },  // val = arg2
    { .t=VM_set,        .x=FLD_Z,       .y=RV_UNIT,     .z=UNDEF        },  // bind(key, val)

/*
(define op-quote                        ; (quote <form>)
  (CREATE
    (BEH (cust opnds env)
      (SEND cust
        (car opnds)
      ))))
*/
#define FX_QUOTE (M_BIND_E+31)
#define OP_QUOTE (FX_QUOTE+1)
    { .t=Fexpr_T,       .x=OP_QUOTE,    .y=UNDEF,       .z=UNDEF        },  // (quote <form>)

    { .t=Actor_T,       .x=OP_QUOTE+1,  .y=NIL,         .z=UNDEF        },  // (cust opnds env)
    { .t=VM_msg,        .x=2,           .y=OP_QUOTE+2,  .z=UNDEF        },  // opnds
    { .t=VM_nth,        .x=1,           .y=CUST_SEND,   .z=UNDEF        },  // form = car(opnds)

/*
(define op-lambda                       ; (lambda <frml> . <body>)
  (CREATE
    (BEH (cust opnds env)
      (SEND cust
        (CREATE (closure-beh (car opnds) (cdr opnds) env))
      ))))
*/
#define FX_LAMBDA (OP_QUOTE+3)
#define OP_LAMBDA (FX_LAMBDA+1)
    { .t=Fexpr_T,       .x=OP_LAMBDA,   .y=UNDEF,       .z=UNDEF        },  // (lambda <frml> . <body>)

    { .t=Actor_T,       .x=OP_LAMBDA+1, .y=NIL,         .z=UNDEF        },  // (cust opnds env)
    { .t=VM_msg,        .x=2,           .y=OP_LAMBDA+2, .z=UNDEF        },  // opnds
    { .t=VM_nth,        .x=1,           .y=OP_LAMBDA+3, .z=UNDEF        },  // frml = car(opnds)
    { .t=VM_msg,        .x=2,           .y=OP_LAMBDA+4, .z=UNDEF        },  // opnds
    { .t=VM_nth,        .x=-1,          .y=OP_LAMBDA+5, .z=UNDEF        },  // body = cdr(opnds)
    { .t=VM_msg,        .x=3,           .y=OP_LAMBDA+6, .z=UNDEF        },  // env
    { .t=VM_push,       .x=CLOSURE_B,   .y=OP_LAMBDA+7, .z=UNDEF        },  // CLOSURE_B
    { .t=VM_new,        .x=3,           .y=CUST_SEND,   .z=UNDEF        },  // closure = (CLOSURE_B frml body env)

/*
(define op-vau                          ; (vau <frml> <evar> . <body>)
  (CREATE
    (BEH (cust opnds env)
      (SEND cust
        (cell Fexpr_T
          (CREATE (fexpr-beh (cons (cadr opnds) (car opnds)) (cddr opnds) env)))
      ))))
*/
#define FX_VAU (OP_LAMBDA+8)
#define OP_VAU (FX_VAU+1)
    { .t=Fexpr_T,       .x=OP_VAU,      .y=UNDEF,       .z=UNDEF        },  // (vau <frml> <evar> . <body>)

    { .t=Actor_T,       .x=OP_VAU+1,    .y=NIL,         .z=UNDEF        },  // (cust opnds env)
    { .t=VM_push,       .x=Fexpr_T,     .y=OP_VAU+2,    .z=UNDEF        },  // Fexpr_T

    { .t=VM_msg,        .x=2,           .y=OP_VAU+3,    .z=UNDEF        },  // opnds
    { .t=VM_nth,        .x=1,           .y=OP_VAU+4,    .z=UNDEF        },  // frml = car(opnds)
    { .t=VM_msg,        .x=2,           .y=OP_VAU+5,    .z=UNDEF        },  // opnds
    { .t=VM_nth,        .x=2,           .y=OP_VAU+6,    .z=UNDEF        },  // evar = cadr(opnds)
    { .t=VM_pair,       .x=1,           .y=OP_VAU+7,    .z=UNDEF        },  // frml' = (evar . frml)

    { .t=VM_msg,        .x=2,           .y=OP_VAU+8,    .z=UNDEF        },  // opnds
    { .t=VM_nth,        .x=-2,          .y=OP_VAU+9,    .z=UNDEF        },  // body = cddr(opnds)
    { .t=VM_msg,        .x=3,           .y=OP_VAU+10,   .z=UNDEF        },  // senv = env
    { .t=VM_push,       .x=FEXPR_B,     .y=OP_VAU+11,   .z=UNDEF        },  // FEXPR_B
    { .t=VM_new,        .x=3,           .y=OP_VAU+12,   .z=UNDEF        },  // oper = (FEXPR_B frml' body senv)

    { .t=VM_cell,       .x=2,           .y=CUST_SEND,   .z=UNDEF        },  // fexpr = {t:Fexpr_T, x:oper}

/*
(define k-define-beh
  (lambda (cust symbol env)
    (BEH value
      (SEND cust
        (bind-env symbol value env) ))))
*/
#define K_DEF_B (OP_VAU+13)
//  { .t=VM_push,       .x=_env_,       .y=K_DEF_B-2,   .z=UNDEF        },
//  { .t=VM_push,       .x=_symbol_,    .y=K_DEF_B-1,   .z=UNDEF        },
//  { .t=VM_push,       .x=_cust_,      .y=K_DEF_B+0,   .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=K_DEF_B+1,   .z=UNDEF        },  // value
    { .t=VM_roll,       .x=-3,          .y=K_DEF_B+2,   .z=UNDEF        },  // env value symbol cust
    { .t=VM_push,       .x=M_BIND_E,    .y=K_DEF_B+3,   .z=UNDEF        },  // M_BIND_E
    { .t=VM_send,       .x=4,           .y=RELEASE,     .z=UNDEF        },  // (M_BIND_E cust symbol value env)

/*
(define op-define                       ; (define <symbol> <expr>)
  (CREATE
    (BEH (cust opnds env)
      (SEND cust
        (bind-env (car opnds) (eval (cadr opnds) env) env)
      ))))
*/
#define FX_DEFINE (K_DEF_B+4)
#define OP_DEFINE (FX_DEFINE+1)
    { .t=Fexpr_T,       .x=OP_DEFINE,   .y=UNDEF,       .z=UNDEF        },  // (define <symbol> <expr>)

    { .t=Actor_T,       .x=OP_DEFINE+1, .y=NIL,         .z=UNDEF        },  // (cust opnds env)
    { .t=VM_msg,        .x=3,           .y=OP_DEFINE+2, .z=UNDEF        },  // env
    { .t=VM_msg,        .x=2,           .y=OP_DEFINE+3, .z=UNDEF        },  // opnds
    { .t=VM_nth,        .x=2,           .y=OP_DEFINE+4, .z=UNDEF        },  // expr = cadr(opnds)

    { .t=VM_msg,        .x=3,           .y=OP_DEFINE+5, .z=UNDEF        },  // env
    { .t=VM_msg,        .x=2,           .y=OP_DEFINE+6, .z=UNDEF        },  // opnds
    { .t=VM_nth,        .x=1,           .y=OP_DEFINE+7, .z=UNDEF        },  // symbol = car(opnds)
    { .t=VM_msg,        .x=1,           .y=OP_DEFINE+8, .z=UNDEF        },  // cust
    { .t=VM_push,       .x=K_DEF_B,     .y=OP_DEFINE+9, .z=UNDEF        },  // K_DEF_B
    { .t=VM_new,        .x=3,           .y=OP_DEFINE+10,.z=UNDEF        },  // k_define = (K_DEF_B env symbol cust)

    { .t=VM_push,       .x=M_EVAL,      .y=OP_DEFINE+11,.z=UNDEF        },  // M_EVAL
    { .t=VM_send,       .x=3,           .y=COMMIT,      .z=UNDEF        },  // (M_EVAL k_define expr env)

/*
(define op-if                           ; (if <pred> <cnsq> <altn>)
  (CREATE
    (BEH (cust opnds env)
      (SEND cust
        (evalif (eval (car opnds) env) (cadr opnds) (caddr opnds) env)
      ))))
*/
#define FX_IF (OP_DEFINE+12)
#define OP_IF (FX_IF+1)
    { .t=Fexpr_T,       .x=OP_IF,       .y=UNDEF,       .z=UNDEF        },  // (if <pred> <cnsq> <altn>)

    { .t=Actor_T,       .x=OP_IF+1,     .y=NIL,         .z=UNDEF        },  // (cust opnds env)
    { .t=VM_msg,        .x=3,           .y=OP_IF+2,     .z=UNDEF        },  // env
    { .t=VM_msg,        .x=2,           .y=OP_IF+3,     .z=UNDEF        },  // opnds
    { .t=VM_part,       .x=1,           .y=OP_IF+4,     .z=UNDEF        },  // (cnsq altn) pred

    { .t=VM_msg,        .x=1,           .y=OP_IF+5,     .z=UNDEF        },  // cust
    { .t=VM_msg,        .x=3,           .y=OP_IF+6,     .z=UNDEF        },  // env
    { .t=VM_roll,       .x=4,           .y=OP_IF+7,     .z=UNDEF        },  // cont = (cnsq altn)
    { .t=VM_push,       .x=M_IF_K,      .y=OP_IF+8,     .z=UNDEF        },  // M_IF_K
    { .t=VM_new,        .x=3,           .y=OP_IF+9,     .z=UNDEF        },  // k_if = (M_IF_K cust env cont)

    { .t=VM_push,       .x=M_EVAL,      .y=OP_IF+10,    .z=UNDEF        },  // M_EVAL
    { .t=VM_send,       .x=3,           .y=COMMIT,      .z=UNDEF        },  // (M_EVAL k_if pred env)

/*
(define op-cond                         ; (cond (<test> <expr>) . <clauses>)
  (CREATE
    (BEH (cust opnds env)
      (if (pair? (car opnds))
        (if (eval (caar opnds) env)
          (SEND cust (eval (cadar opnds) env))
          (SEND SELF (list cust (cdr opnds) env)))
        (SEND cust #?)) )))
*/
#define FX_COND (OP_IF+11)
#define OP_COND (FX_COND+1)
#define K_COND (OP_COND+17)
    { .t=Fexpr_T,       .x=OP_COND,     .y=UNDEF,       .z=UNDEF        },  // (cond (<test> <expr>) . <clauses>)

    { .t=Actor_T,       .x=OP_COND+1,   .y=NIL,         .z=UNDEF        },  // (cust opnds env)
    { .t=VM_msg,        .x=2,           .y=OP_COND+2,   .z=UNDEF        },  // opnds
    { .t=VM_typeq,      .x=Pair_T,      .y=OP_COND+3,   .z=UNDEF        },  // opnds has type Pair_T
    { .t=VM_if,         .x=OP_COND+4,   .y=RV_UNDEF,    .z=UNDEF        },

    { .t=VM_msg,        .x=2,           .y=OP_COND+5,   .z=UNDEF        },  // opnds
    { .t=VM_part,       .x=1,           .y=OP_COND+6,   .z=UNDEF        },  // rest first
    { .t=VM_part,       .x=2,           .y=OP_COND+7,   .z=UNDEF        },  // rest () expr test

    { .t=VM_msg,        .x=3,           .y=OP_COND+8,   .z=UNDEF        },  // env
    { .t=VM_roll,       .x=2,           .y=OP_COND+9,   .z=UNDEF        },  // test

    { .t=VM_msg,        .x=1,           .y=OP_COND+10,  .z=UNDEF        },  // cust
    { .t=VM_roll,       .x=4,           .y=OP_COND+11,  .z=UNDEF        },  // expr
    { .t=VM_roll,       .x=6,           .y=OP_COND+12,  .z=UNDEF        },  // opnds' = rest
    { .t=VM_msg,        .x=3,           .y=OP_COND+13,  .z=UNDEF        },  // env
    { .t=VM_push,       .x=K_COND,      .y=OP_COND+14,  .z=UNDEF        },  // K_COND
    { .t=VM_new,        .x=4,           .y=OP_COND+15,  .z=UNDEF        },  // k_cond = (K_COND cust expr opnds' env)

    { .t=VM_push,       .x=M_EVAL,      .y=OP_COND+16,  .z=UNDEF        },  // M_EVAL
    { .t=VM_send,       .x=3,           .y=COMMIT,      .z=UNDEF        },  // (M_EVAL k_cond test env)

//  { .t=VM_push,       .x=_cust_,      .y=K_COND-3,    .z=UNDEF        },
//  { .t=VM_push,       .x=_expr_,      .y=K_COND-2,    .z=UNDEF        },
//  { .t=VM_push,       .x=_opnds_,     .y=K_COND-1,    .z=UNDEF        },
//  { .t=VM_push,       .x=_env_,       .y=K_COND+0,    .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=K_COND+1,    .z=UNDEF        },  // test result
    { .t=VM_if,         .x=K_COND+2,    .y=K_COND+6,    .z=UNDEF        },

    { .t=VM_roll,       .x=3,           .y=K_COND+3,    .z=UNDEF        },  // cust opnds env expr
    { .t=VM_roll,       .x=4,           .y=K_COND+4,    .z=UNDEF        },  // opnds env expr cust
    { .t=VM_push,       .x=M_EVAL,      .y=K_COND+5,    .z=UNDEF        },  // M_EVAL
    { .t=VM_send,       .x=3,           .y=RELEASE,     .z=UNDEF        },  // (M_EVAL cust expr env)

    { .t=VM_roll,       .x=2,           .y=K_COND+7,    .z=UNDEF        },  // cust expr env opnds
    { .t=VM_roll,       .x=4,           .y=K_COND+8,    .z=UNDEF        },  // expr env opnds cust
    { .t=VM_push,       .x=OP_COND,     .y=K_COND+9,    .z=UNDEF        },  // OP_COND
    { .t=VM_send,       .x=3,           .y=RELEASE,     .z=UNDEF        },  // (OP_COND cust opnds env)

/*
(define op-seq                          ; (seq . <body>)
  (CREATE
    (BEH (cust opnds env)
      ;(SEND cust (evbody #unit opnds env))
      (SEND (CREATE (k-seq-beh cust opnds env)) #unit)
    )))
*/
#define FX_SEQ (K_COND+10)
#define OP_SEQ (FX_SEQ+1)
    { .t=Fexpr_T,       .x=OP_SEQ,      .y=UNDEF,       .z=UNDEF        },  // (seq . <body>)

    { .t=Actor_T,       .x=OP_SEQ+1,    .y=NIL,         .z=UNDEF        },  // (cust opnds env)
    { .t=VM_push,       .x=UNIT,        .y=OP_SEQ+2,    .z=UNDEF        },  // UNIT

    { .t=VM_msg,        .x=1,           .y=OP_SEQ+3,    .z=UNDEF        },  // cust
    { .t=VM_msg,        .x=2,           .y=OP_SEQ+4,    .z=UNDEF        },  // body = opnds
    { .t=VM_msg,        .x=3,           .y=OP_SEQ+5,    .z=UNDEF        },  // env
    { .t=VM_push,       .x=K_SEQ_B,     .y=OP_SEQ+6,    .z=UNDEF        },  // K_SEQ_B
    { .t=VM_new,        .x=3,           .y=SEND_0,      .z=UNDEF        },  // k-seq = (K_SEQ_B cust opnds env)

//
// Global LISP/Scheme Procedures
//

#define F_LIST (OP_SEQ+7)
    { .t=Actor_T,       .x=F_LIST+1,    .y=NIL,         .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=-1,          .y=CUST_SEND,   .z=UNDEF        },  // args

#define F_CONS (F_LIST+2)
    { .t=Actor_T,       .x=F_CONS+1,    .y=NIL,         .z=UNDEF        },  // (cust . args)
#if 1
    { .t=VM_msg,        .x=3,           .y=F_CONS+2,    .z=UNDEF        },  // tail = arg2
    { .t=VM_msg,        .x=2,           .y=F_CONS+3,    .z=UNDEF        },  // head = arg1
#else
    { .t=VM_msg,        .x=-1,          .y=F_CONS+2,    .z=UNDEF        },  // (head tail)
    { .t=VM_part,       .x=2,           .y=F_CONS+3,    .z=UNDEF        },  // () tail head
#endif
    { .t=VM_pair,       .x=1,           .y=CUST_SEND,   .z=UNDEF        },  // (head . tail)

#define F_CAR (F_CONS+4)
    { .t=Actor_T,       .x=F_CAR+1,     .y=NIL,         .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=2,           .y=F_CAR+2,     .z=UNDEF        },  // pair = arg1
    { .t=VM_nth,        .x=1,           .y=CUST_SEND,   .z=UNDEF        },  // car(pair)

#define F_CDR (F_CAR+3)
    { .t=Actor_T,       .x=F_CDR+1,     .y=NIL,         .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=2,           .y=F_CDR+2,     .z=UNDEF        },  // pair = arg1
    { .t=VM_nth,        .x=-1,          .y=CUST_SEND,   .z=UNDEF        },  // cdr(pair)

#define F_CADR (F_CDR+3)
    { .t=Actor_T,       .x=F_CADR+1,    .y=NIL,         .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=2,           .y=F_CADR+2,    .z=UNDEF        },  // pair = arg1
    { .t=VM_nth,        .x=2,           .y=CUST_SEND,   .z=UNDEF        },  // cadr(pair)

#define F_CADDR (F_CADR+3)
    { .t=Actor_T,       .x=F_CADDR+1,   .y=NIL,         .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=2,           .y=F_CADDR+2,   .z=UNDEF        },  // pair = arg1
    { .t=VM_nth,        .x=3,           .y=CUST_SEND,   .z=UNDEF        },  // caddr(pair)

#define F_NTH (F_CADDR+3)
    { .t=Actor_T,       .x=F_NTH+1,     .y=NIL,         .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=0,           .y=F_NTH+2,     .z=UNDEF        },  // msg = (cust . args)

    { .t=VM_push,       .x=VM_nth,      .y=F_NTH+3,     .z=UNDEF        },  // VM_nth
    { .t=VM_msg,        .x=2,           .y=F_NTH+4,     .z=UNDEF        },  // index = arg1
    { .t=VM_cvt,        .x=CVT_FIX_INT, .y=F_NTH+5,     .z=UNDEF        },  // TO_INT(index)
    { .t=VM_push,       .x=CUST_SEND,   .y=F_NTH+6,     .z=UNDEF        },  // CUST_SEND
    { .t=VM_cell,       .x=3,           .y=F_NTH+7,     .z=UNDEF        },  // beh = {t:VM_nth, x:index, k:CUST_SEND}

    { .t=VM_push,       .x=VM_msg,      .y=F_NTH+8,     .z=UNDEF        },  // VM_msg
    { .t=VM_push,       .x=3,           .y=F_NTH+9,     .z=UNDEF        },  // 3
    { .t=VM_roll,       .x=3,           .y=F_NTH+10,    .z=UNDEF        },  // beh
    { .t=VM_cell,       .x=3,           .y=F_NTH+11,    .z=UNDEF        },  // beh' = {t:VM_msg, x:3, k:beh}

    { .t=VM_new,        .x=0,           .y=SEND_0,      .z=UNDEF        },  // (k_nth cust . args)

#define F_NULL_P (F_NTH+12)
    { .t=Actor_T,       .x=F_NULL_P+1,  .y=NIL,         .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=-1,          .y=F_NULL_P+2,  .z=UNDEF        },  // args
    { .t=VM_pick,       .x=1,           .y=F_NULL_P+3,  .z=UNDEF        },  // args args
    { .t=VM_typeq,      .x=Pair_T,      .y=F_NULL_P+4,  .z=UNDEF        },  // args has type Pair_T
    { .t=VM_if,         .x=F_NULL_P+5,  .y=RV_TRUE,     .z=UNDEF        },
    { .t=VM_part,       .x=1,           .y=F_NULL_P+6,  .z=UNDEF        },  // rest first
    { .t=VM_eq,         .x=NIL,         .y=F_NULL_P+7,  .z=UNDEF        },  // first == NIL
    { .t=VM_if,         .x=F_NULL_P+2,  .y=RV_FALSE,    .z=UNDEF        },

#define F_TYPE_P (F_NULL_P+8)
//  { .t=VM_push,       .x=_type_,      .y=F_TYPE_P+0,  .z=UNDEF        },
    { .t=VM_msg,        .x=-1,          .y=F_TYPE_P+1,  .z=UNDEF        },  // args
    { .t=VM_pick,       .x=1,           .y=F_TYPE_P+2,  .z=UNDEF        },  // args args
    { .t=VM_typeq,      .x=Pair_T,      .y=F_TYPE_P+3,  .z=UNDEF        },  // args has type Pair_T
    { .t=VM_if,         .x=F_TYPE_P+4,  .y=RV_TRUE,     .z=UNDEF        },
    { .t=VM_part,       .x=1,           .y=F_TYPE_P+5,  .z=UNDEF        },  // rest first
    { .t=VM_get,        .x=FLD_T,       .y=F_TYPE_P+6,  .z=UNDEF        },  // get_t(first)
    { .t=VM_pick,       .x=3,           .y=F_TYPE_P+7,  .z=UNDEF        },  // type
    { .t=VM_cmp,        .x=CMP_EQ,      .y=F_TYPE_P+8,  .z=UNDEF        },  // get_t(first) == type
    { .t=VM_if,         .x=F_TYPE_P+1,  .y=RV_FALSE,    .z=UNDEF        },

#define F_PAIR_P (F_TYPE_P+9)
    { .t=Actor_T,       .x=F_PAIR_P+1,  .y=NIL,         .z=UNDEF        },  // (cust . args)
    { .t=VM_push,       .x=Pair_T,      .y=F_TYPE_P,    .z=UNDEF        },  // type = Pair_T

#define F_BOOL_P (F_PAIR_P+2)
    { .t=Actor_T,       .x=F_BOOL_P+1,  .y=NIL,         .z=UNDEF        },  // (cust . args)
    { .t=VM_push,       .x=Boolean_T,   .y=F_TYPE_P,    .z=UNDEF        },  // type = Boolean_T

#define F_NUM_P (F_BOOL_P+2)
    { .t=Actor_T,       .x=F_NUM_P+1,   .y=NIL,         .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=-1,          .y=F_NUM_P+2,   .z=UNDEF        },  // args
    { .t=VM_pick,       .x=1,           .y=F_NUM_P+3,   .z=UNDEF        },  // args args
    { .t=VM_typeq,      .x=Pair_T,      .y=F_NUM_P+4,   .z=UNDEF        },  // args has type Pair_T
    { .t=VM_if,         .x=F_NUM_P+5,   .y=RV_TRUE,     .z=UNDEF        },
    { .t=VM_part,       .x=1,           .y=F_NUM_P+6,   .z=UNDEF        },  // rest first
    { .t=VM_typeq,      .x=Fixnum_T,    .y=F_NUM_P+7,   .z=UNDEF        },  // first has type Fixnum_T
    { .t=VM_if,         .x=F_NUM_P+2,   .y=RV_FALSE,    .z=UNDEF        },

#define F_SYM_P (F_NUM_P+8)
    { .t=Actor_T,       .x=F_SYM_P+1,   .y=NIL,         .z=UNDEF        },  // (cust . args)
    { .t=VM_push,       .x=Symbol_T,    .y=F_TYPE_P,    .z=UNDEF        },  // type = Symbol_T

#define F_ACT_P (F_SYM_P+2)
    { .t=Actor_T,       .x=F_ACT_P+1,   .y=NIL,         .z=UNDEF        },  // (cust . args)
    { .t=VM_push,       .x=Actor_T,     .y=F_TYPE_P,    .z=UNDEF        },  // type = Actor_T

#define F_EQ_P (F_ACT_P+2)
    { .t=Actor_T,       .x=F_EQ_P+1,    .y=NIL,         .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=-2,          .y=F_EQ_P+2,    .z=UNDEF        },  // rest = cdr(args)
    { .t=VM_pick,       .x=1,           .y=F_EQ_P+3,    .z=UNDEF        },  // rest rest
    { .t=VM_typeq,      .x=Pair_T,      .y=F_EQ_P+4,    .z=UNDEF        },  // rest has type Pair_T
    { .t=VM_if,         .x=F_EQ_P+5,    .y=RV_TRUE,     .z=UNDEF        },
    { .t=VM_part,       .x=1,           .y=F_EQ_P+6,    .z=UNDEF        },  // rest first
    { .t=VM_msg,        .x=2,           .y=F_EQ_P+7,    .z=UNDEF        },  // car(args)
    { .t=VM_cmp,        .x=CMP_EQ,      .y=F_EQ_P+8,    .z=UNDEF        },  // first == car(args)
    { .t=VM_if,         .x=F_EQ_P+2,    .y=RV_FALSE,    .z=UNDEF        },

#define F_NUM_EQ (F_EQ_P+9)
    { .t=Actor_T,       .x=F_NUM_EQ+1,  .y=NIL,         .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=-1,          .y=F_NUM_EQ+2,  .z=UNDEF        },  // args
    { .t=VM_pick,       .x=1,           .y=F_NUM_EQ+3,  .z=UNDEF        },  // args args
    { .t=VM_typeq,      .x=Pair_T,      .y=F_NUM_EQ+4,  .z=UNDEF        },  // args has type Pair_T
    { .t=VM_if,         .x=F_NUM_EQ+5,  .y=RV_TRUE,     .z=UNDEF        },

    { .t=VM_part,       .x=1,           .y=F_NUM_EQ+6,  .z=UNDEF        },  // rest first
    { .t=VM_pick,       .x=1,           .y=F_NUM_EQ+7,  .z=UNDEF        },  // rest first first
    { .t=VM_typeq,      .x=Fixnum_T,    .y=F_NUM_EQ+8,  .z=UNDEF        },  // first has type Fixnum_T
    { .t=VM_if,         .x=F_NUM_EQ+9,  .y=RV_UNDEF,    .z=UNDEF        },

    { .t=VM_pick,       .x=2,           .y=F_NUM_EQ+10, .z=UNDEF        },  // rest
    { .t=VM_typeq,      .x=Pair_T,      .y=F_NUM_EQ+11, .z=UNDEF        },  // rest has type Pair_T
    { .t=VM_if,         .x=F_NUM_EQ+12, .y=RV_TRUE,     .z=UNDEF        },

    { .t=VM_roll,       .x=2,           .y=F_NUM_EQ+13, .z=UNDEF        },  // first rest
    { .t=VM_part,       .x=1,           .y=F_NUM_EQ+14, .z=UNDEF        },  // first rest second
    { .t=VM_pick,       .x=1,           .y=F_NUM_EQ+15, .z=UNDEF        },  // second second
    { .t=VM_typeq,      .x=Fixnum_T,    .y=F_NUM_EQ+16, .z=UNDEF        },  // second has type Fixnum_T
    { .t=VM_if,         .x=F_NUM_EQ+17, .y=RV_UNDEF,    .z=UNDEF        },

    { .t=VM_roll,       .x=3,           .y=F_NUM_EQ+18, .z=UNDEF        },  // rest second first
    { .t=VM_pick,       .x=2,           .y=F_NUM_EQ+19, .z=UNDEF        },  // rest second first second
    { .t=VM_cmp,        .x=CMP_EQ,      .y=F_NUM_EQ+20, .z=UNDEF        },  // first == second
    { .t=VM_if,         .x=F_NUM_EQ+9,  .y=RV_FALSE,    .z=UNDEF        },

#define F_NUM_LT (F_NUM_EQ+21)
    { .t=Actor_T,       .x=F_NUM_LT+1,  .y=NIL,         .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=-1,          .y=F_NUM_LT+2,  .z=UNDEF        },  // args
    { .t=VM_pick,       .x=1,           .y=F_NUM_LT+3,  .z=UNDEF        },  // args args
    { .t=VM_typeq,      .x=Pair_T,      .y=F_NUM_LT+4,  .z=UNDEF        },  // args has type Pair_T
    { .t=VM_if,         .x=F_NUM_LT+5,  .y=RV_TRUE,     .z=UNDEF        },

    { .t=VM_part,       .x=1,           .y=F_NUM_LT+6,  .z=UNDEF        },  // rest first
    { .t=VM_pick,       .x=1,           .y=F_NUM_LT+7,  .z=UNDEF        },  // rest first first
    { .t=VM_typeq,      .x=Fixnum_T,    .y=F_NUM_LT+8,  .z=UNDEF        },  // first has type Fixnum_T
    { .t=VM_if,         .x=F_NUM_LT+9,  .y=RV_UNDEF,    .z=UNDEF        },

    { .t=VM_pick,       .x=2,           .y=F_NUM_LT+10, .z=UNDEF        },  // rest
    { .t=VM_typeq,      .x=Pair_T,      .y=F_NUM_LT+11, .z=UNDEF        },  // rest has type Pair_T
    { .t=VM_if,         .x=F_NUM_LT+12, .y=RV_TRUE,     .z=UNDEF        },

    { .t=VM_roll,       .x=2,           .y=F_NUM_LT+13, .z=UNDEF        },  // first rest
    { .t=VM_part,       .x=1,           .y=F_NUM_LT+14, .z=UNDEF        },  // first rest second
    { .t=VM_pick,       .x=1,           .y=F_NUM_LT+15, .z=UNDEF        },  // second second
    { .t=VM_typeq,      .x=Fixnum_T,    .y=F_NUM_LT+16, .z=UNDEF        },  // second has type Fixnum_T
    { .t=VM_if,         .x=F_NUM_LT+17, .y=RV_UNDEF,    .z=UNDEF        },

    { .t=VM_roll,       .x=3,           .y=F_NUM_LT+18, .z=UNDEF        },  // rest second first
    { .t=VM_pick,       .x=2,           .y=F_NUM_LT+19, .z=UNDEF        },  // rest second first second
    { .t=VM_cmp,        .x=CMP_LT,      .y=F_NUM_LT+20, .z=UNDEF        },  // first < second
    { .t=VM_if,         .x=F_NUM_LT+9,  .y=RV_FALSE,    .z=UNDEF        },

#define F_NUM_LE (F_NUM_LT+21)
    { .t=Actor_T,       .x=F_NUM_LE+1,  .y=NIL,         .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=-1,          .y=F_NUM_LE+2,  .z=UNDEF        },  // args
    { .t=VM_pick,       .x=1,           .y=F_NUM_LE+3,  .z=UNDEF        },  // args args
    { .t=VM_typeq,      .x=Pair_T,      .y=F_NUM_LE+4,  .z=UNDEF        },  // args has type Pair_T
    { .t=VM_if,         .x=F_NUM_LE+5,  .y=RV_TRUE,     .z=UNDEF        },

    { .t=VM_part,       .x=1,           .y=F_NUM_LE+6,  .z=UNDEF        },  // rest first
    { .t=VM_pick,       .x=1,           .y=F_NUM_LE+7,  .z=UNDEF        },  // rest first first
    { .t=VM_typeq,      .x=Fixnum_T,    .y=F_NUM_LE+8,  .z=UNDEF        },  // first has type Fixnum_T
    { .t=VM_if,         .x=F_NUM_LE+9,  .y=RV_UNDEF,    .z=UNDEF        },

    { .t=VM_pick,       .x=2,           .y=F_NUM_LE+10, .z=UNDEF        },  // rest
    { .t=VM_typeq,      .x=Pair_T,      .y=F_NUM_LE+11, .z=UNDEF        },  // rest has type Pair_T
    { .t=VM_if,         .x=F_NUM_LE+12, .y=RV_TRUE,     .z=UNDEF        },

    { .t=VM_roll,       .x=2,           .y=F_NUM_LE+13, .z=UNDEF        },  // first rest
    { .t=VM_part,       .x=1,           .y=F_NUM_LE+14, .z=UNDEF        },  // first rest second
    { .t=VM_pick,       .x=1,           .y=F_NUM_LE+15, .z=UNDEF        },  // second second
    { .t=VM_typeq,      .x=Fixnum_T,    .y=F_NUM_LE+16, .z=UNDEF        },  // second has type Fixnum_T
    { .t=VM_if,         .x=F_NUM_LE+17, .y=RV_UNDEF,    .z=UNDEF        },

    { .t=VM_roll,       .x=3,           .y=F_NUM_LE+18, .z=UNDEF        },  // rest second first
    { .t=VM_pick,       .x=2,           .y=F_NUM_LE+19, .z=UNDEF        },  // rest second first second
    { .t=VM_cmp,        .x=CMP_LE,      .y=F_NUM_LE+20, .z=UNDEF        },  // first <= second
    { .t=VM_if,         .x=F_NUM_LE+9,  .y=RV_FALSE,    .z=UNDEF        },

#define F_NUM_ADD (F_NUM_LE+21)
    { .t=Actor_T,       .x=F_NUM_ADD+1, .y=NIL,         .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=-1,          .y=F_NUM_ADD+2, .z=UNDEF        },  // args
    { .t=VM_pick,       .x=1,           .y=F_NUM_ADD+3, .z=UNDEF        },  // args args
    { .t=VM_typeq,      .x=Pair_T,      .y=F_NUM_ADD+4, .z=UNDEF        },  // args has type Pair_T
    { .t=VM_if,         .x=F_NUM_ADD+5, .y=RV_ZERO,     .z=UNDEF        },

    { .t=VM_part,       .x=1,           .y=F_NUM_ADD+6, .z=UNDEF        },  // rest first
    { .t=VM_pick,       .x=1,           .y=F_NUM_ADD+7, .z=UNDEF        },  // rest first first
    { .t=VM_typeq,      .x=Fixnum_T,    .y=F_NUM_ADD+8, .z=UNDEF        },  // first has type Fixnum_T
    { .t=VM_if,         .x=F_NUM_ADD+9, .y=RV_UNDEF,    .z=UNDEF        },

    { .t=VM_pick,       .x=2,           .y=F_NUM_ADD+10,.z=UNDEF        },  // rest
    { .t=VM_typeq,      .x=Pair_T,      .y=F_NUM_ADD+11,.z=UNDEF        },  // rest has type Pair_T
    { .t=VM_if,         .x=F_NUM_ADD+12,.y=CUST_SEND,   .z=UNDEF        },

    { .t=VM_roll,       .x=2,           .y=F_NUM_ADD+13,.z=UNDEF        },  // first rest
    { .t=VM_part,       .x=1,           .y=F_NUM_ADD+14,.z=UNDEF        },  // first rest second
    { .t=VM_pick,       .x=1,           .y=F_NUM_ADD+15,.z=UNDEF        },  // second second
    { .t=VM_typeq,      .x=Fixnum_T,    .y=F_NUM_ADD+16,.z=UNDEF        },  // second has type Fixnum_T
    { .t=VM_if,         .x=F_NUM_ADD+17,.y=RV_UNDEF,    .z=UNDEF        },

    { .t=VM_roll,       .x=3,           .y=F_NUM_ADD+18,.z=UNDEF        },  // rest second first
    { .t=VM_roll,       .x=2,           .y=F_NUM_ADD+19,.z=UNDEF        },  // rest first second
    { .t=VM_alu,        .x=ALU_ADD,     .y=F_NUM_ADD+9, .z=UNDEF        },  // first + second

#define F_NUM_SUB (F_NUM_ADD+20)
    { .t=Actor_T,       .x=F_NUM_SUB+1, .y=NIL,         .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=-1,          .y=F_NUM_SUB+2, .z=UNDEF        },  // args
    { .t=VM_pick,       .x=1,           .y=F_NUM_SUB+3, .z=UNDEF        },  // args args
    { .t=VM_typeq,      .x=Pair_T,      .y=F_NUM_SUB+4, .z=UNDEF        },  // args has type Pair_T
    { .t=VM_if,         .x=F_NUM_SUB+5, .y=RV_ZERO,     .z=UNDEF        },

    { .t=VM_part,       .x=1,           .y=F_NUM_SUB+6, .z=UNDEF        },  // rest first
    { .t=VM_pick,       .x=1,           .y=F_NUM_SUB+7, .z=UNDEF        },  // rest first first
    { .t=VM_typeq,      .x=Fixnum_T,    .y=F_NUM_SUB+8, .z=UNDEF        },  // first has type Fixnum_T
    { .t=VM_if,         .x=F_NUM_SUB+9, .y=RV_UNDEF,    .z=UNDEF        },

    { .t=VM_pick,       .x=2,           .y=F_NUM_SUB+10,.z=UNDEF        },  // rest
    { .t=VM_typeq,      .x=Pair_T,      .y=F_NUM_SUB+11,.z=UNDEF        },  // rest has type Pair_T
    { .t=VM_if,         .x=F_NUM_SUB+15,.y=F_NUM_SUB+12,.z=UNDEF        },

    { .t=VM_push,       .x=0,           .y=F_NUM_SUB+13,.z=UNDEF        },  // +0
    { .t=VM_roll,       .x=2,           .y=F_NUM_SUB+14,.z=UNDEF        },  // +0 first
    { .t=VM_alu,        .x=ALU_SUB,     .y=CUST_SEND,   .z=UNDEF        },  // +0 - first

    { .t=VM_roll,       .x=2,           .y=F_NUM_SUB+16,.z=UNDEF        },  // first rest
    { .t=VM_part,       .x=1,           .y=F_NUM_SUB+17,.z=UNDEF        },  // first rest second
    { .t=VM_pick,       .x=1,           .y=F_NUM_SUB+18,.z=UNDEF        },  // second second
    { .t=VM_typeq,      .x=Fixnum_T,    .y=F_NUM_SUB+19,.z=UNDEF        },  // second has type Fixnum_T
    { .t=VM_if,         .x=F_NUM_SUB+20,.y=RV_UNDEF,    .z=UNDEF        },

    { .t=VM_roll,       .x=3,           .y=F_NUM_SUB+21,.z=UNDEF        },  // rest second first
    { .t=VM_roll,       .x=2,           .y=F_NUM_SUB+22,.z=UNDEF        },  // rest first second
    { .t=VM_alu,        .x=ALU_SUB,     .y=F_NUM_SUB+23,.z=UNDEF        },  // first - second

    { .t=VM_pick,       .x=2,           .y=F_NUM_SUB+24,.z=UNDEF        },  // rest
    { .t=VM_typeq,      .x=Pair_T,      .y=F_NUM_SUB+25,.z=UNDEF        },  // rest has type Pair_T
    { .t=VM_if,         .x=F_NUM_SUB+15,.y=CUST_SEND,   .z=UNDEF        },

#define F_NUM_MUL (F_NUM_SUB+26)
    { .t=Actor_T,       .x=F_NUM_MUL+1, .y=NIL,         .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=-1,          .y=F_NUM_MUL+2, .z=UNDEF        },  // args
    { .t=VM_pick,       .x=1,           .y=F_NUM_MUL+3, .z=UNDEF        },  // args args
    { .t=VM_typeq,      .x=Pair_T,      .y=F_NUM_MUL+4, .z=UNDEF        },  // args has type Pair_T
    { .t=VM_if,         .x=F_NUM_MUL+5, .y=RV_ONE,      .z=UNDEF        },

    { .t=VM_part,       .x=1,           .y=F_NUM_MUL+6, .z=UNDEF        },  // rest first
    { .t=VM_pick,       .x=1,           .y=F_NUM_MUL+7, .z=UNDEF        },  // rest first first
    { .t=VM_typeq,      .x=Fixnum_T,    .y=F_NUM_MUL+8, .z=UNDEF        },  // first has type Fixnum_T
    { .t=VM_if,         .x=F_NUM_MUL+9, .y=RV_UNDEF,    .z=UNDEF        },

    { .t=VM_pick,       .x=2,           .y=F_NUM_MUL+10,.z=UNDEF        },  // rest
    { .t=VM_typeq,      .x=Pair_T,      .y=F_NUM_MUL+11,.z=UNDEF        },  // rest has type Pair_T
    { .t=VM_if,         .x=F_NUM_MUL+12,.y=CUST_SEND,   .z=UNDEF        },

    { .t=VM_roll,       .x=2,           .y=F_NUM_MUL+13,.z=UNDEF        },  // first rest
    { .t=VM_part,       .x=1,           .y=F_NUM_MUL+14,.z=UNDEF        },  // first rest second
    { .t=VM_pick,       .x=1,           .y=F_NUM_MUL+15,.z=UNDEF        },  // second second
    { .t=VM_typeq,      .x=Fixnum_T,    .y=F_NUM_MUL+16,.z=UNDEF        },  // second has type Fixnum_T
    { .t=VM_if,         .x=F_NUM_MUL+17,.y=RV_UNDEF,    .z=UNDEF        },

    { .t=VM_roll,       .x=3,           .y=F_NUM_MUL+18,.z=UNDEF        },  // rest second first
    { .t=VM_roll,       .x=2,           .y=F_NUM_MUL+19,.z=UNDEF        },  // rest first second
    { .t=VM_alu,        .x=ALU_MUL,     .y=F_NUM_MUL+9, .z=UNDEF        },  // first * second

#define F_LST_NUM (F_NUM_MUL+20)
    { .t=Actor_T,       .x=F_LST_NUM+1, .y=NIL,         .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=2,           .y=F_LST_NUM+2, .z=UNDEF        },  // chars = arg1
    { .t=VM_cvt,        .x=CVT_LST_NUM, .y=CUST_SEND,   .z=UNDEF        },  // lst_num(chars)

#define F_LST_SYM (F_LST_NUM+3)
    { .t=Actor_T,       .x=F_LST_SYM+1, .y=NIL,         .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=2,           .y=F_LST_SYM+2, .z=UNDEF        },  // chars = arg1
    { .t=VM_cvt,        .x=CVT_LST_SYM, .y=CUST_SEND,   .z=UNDEF        },  // lst_sym(chars)

#if SCM_ASM_TOOLS
//
// Assembly-language Tools
//

#define F_INT_FIX (F_LST_SYM+3)
    { .t=Actor_T,       .x=F_INT_FIX+1, .y=NIL,         .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=2,           .y=F_INT_FIX+2, .z=UNDEF        },  // rawint = arg1
    { .t=VM_cvt,        .x=CVT_INT_FIX, .y=CUST_SEND,   .z=UNDEF        },  // TO_FIX(rawint)

#define F_FIX_INT (F_INT_FIX+3)
    { .t=Actor_T,       .x=F_FIX_INT+1, .y=NIL,         .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=2,           .y=F_FIX_INT+2, .z=UNDEF        },  // fixnum = arg1
    { .t=VM_cvt,        .x=CVT_FIX_INT, .y=CUST_SEND,   .z=UNDEF        },  // TO_INT(fixnum)

#define F_CELL (F_FIX_INT+3)
    { .t=Actor_T,       .x=F_CELL+1,    .y=NIL,         .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=2,           .y=F_CELL+2,    .z=UNDEF        },  // T = arg1
    { .t=VM_msg,        .x=3,           .y=F_CELL+3,    .z=UNDEF        },  // X = arg2
    { .t=VM_msg,        .x=4,           .y=F_CELL+4,    .z=UNDEF        },  // Y = arg3
    { .t=VM_msg,        .x=5,           .y=F_CELL+5,    .z=UNDEF        },  // Z = arg4
    { .t=VM_cell,       .x=4,           .y=CUST_SEND,   .z=UNDEF        },  // cell(T, X, Y, Z)

#define ASM_END (F_CELL+6)
#else // !SCM_ASM_TOOLS
#define ASM_END (F_LST_SYM+3)
#endif // SCM_ASM_TOOLS

//
// Parsing Expression Grammar (PEG) behaviors
//

#define G_EMPTY (ASM_END+0)
    { .t=Actor_T,       .x=G_EMPTY+1,   .y=NIL,         .z=UNDEF        },
#define G_EMPTY_B (G_EMPTY+1)
    { .t=VM_msg,        .x=-2,          .y=G_EMPTY+2,   .z=UNDEF        },  // in
    { .t=VM_push,       .x=NIL,         .y=G_EMPTY+3,   .z=UNDEF        },  // ()
    { .t=VM_pair,       .x=1,           .y=G_EMPTY+4,   .z=UNDEF        },  // (() . in)
    { .t=VM_msg,        .x=1,           .y=G_EMPTY+5,   .z=UNDEF        },  // custs = (ok . fail)
    { .t=VM_nth,        .x=1,           .y=SEND_0,      .z=UNDEF        },  // ok = car(custs)

#define G_FAIL (G_EMPTY+6)
    { .t=Actor_T,       .x=G_FAIL+1,    .y=NIL,         .z=UNDEF        },
#define G_FAIL_B (G_FAIL+1)
    { .t=VM_msg,        .x=-2,          .y=G_FAIL+2,    .z=UNDEF        },  // in
    { .t=VM_msg,        .x=1,           .y=G_FAIL+3,    .z=UNDEF        },  // custs = (ok . fail)
    { .t=VM_nth,        .x=-1,          .y=SEND_0,      .z=UNDEF        },  // fail = cdr(custs)

#define G_NEXT_K (G_FAIL+4)
//  { .t=VM_push,       .x=_cust_,      .y=G_NEXT_K-1,  .z=UNDEF        },
//  { .t=VM_push,       .x=_value_,     .y=G_NEXT_K+0,  .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=G_NEXT_K+1,  .z=UNDEF        },  // in
    { .t=VM_roll,       .x=2,           .y=G_NEXT_K+2,  .z=UNDEF        },  // value
    { .t=VM_pair,       .x=1,           .y=G_NEXT_K+3,  .z=UNDEF        },  // (value . in)
    { .t=VM_roll,       .x=2,           .y=RELEASE_0,   .z=UNDEF        },  // cust

#define G_ANY (G_NEXT_K+4)
    { .t=Actor_T,       .x=G_ANY+1,     .y=NIL,         .z=UNDEF        },
    { .t=VM_msg,        .x=1,           .y=G_ANY+2,     .z=UNDEF        },  // custs = (ok . fail)
    { .t=VM_part,       .x=1,           .y=G_ANY+3,     .z=UNDEF        },  // fail ok
    { .t=VM_msg,        .x=-2,          .y=G_ANY+4,     .z=UNDEF        },  // in
    { .t=VM_eq,         .x=NIL,         .y=G_ANY+5,     .z=UNDEF        },  // in == ()
    { .t=VM_if,         .x=G_ANY+13,    .y=G_ANY+6,     .z=UNDEF        },

    { .t=VM_msg,        .x=-2,          .y=G_ANY+7,     .z=UNDEF        },  // in
    { .t=VM_part,       .x=1,           .y=G_ANY+8,     .z=UNDEF        },  // next token
    { .t=VM_pick,       .x=3,           .y=G_ANY+9,     .z=UNDEF        },  // ok
    { .t=VM_pick,       .x=2,           .y=G_ANY+10,    .z=UNDEF        },  // token
    { .t=VM_push,       .x=G_NEXT_K,    .y=G_ANY+11,    .z=UNDEF        },  // G_NEXT_K
    { .t=VM_new,        .x=2,           .y=G_ANY+12,    .z=UNDEF        },  // k_next
    { .t=VM_pick,       .x=3,           .y=SEND_0,      .z=UNDEF        },  // next

    { .t=VM_push,       .x=NIL,         .y=G_ANY+14,    .z=UNDEF        },  // ()
    { .t=VM_pick,       .x=3,           .y=SEND_0,      .z=UNDEF        },  // fail

#define G_EQ_B (G_ANY+15)
//  { .t=VM_push,       .x=_value_,     .y=G_EQ_B+0,    .z=UNDEF        },
    { .t=VM_msg,        .x=1,           .y=G_EQ_B+1,    .z=UNDEF        },  // custs = (ok . fail)
    { .t=VM_part,       .x=1,           .y=G_EQ_B+2,    .z=UNDEF        },  // fail ok
    { .t=VM_msg,        .x=-2,          .y=G_EQ_B+3,    .z=UNDEF        },  // in
    { .t=VM_eq,         .x=NIL,         .y=G_EQ_B+4,    .z=UNDEF        },  // in == ()
    { .t=VM_if,         .x=G_EQ_B+17,   .y=G_EQ_B+5,    .z=UNDEF        },

    { .t=VM_msg,        .x=-2,          .y=G_EQ_B+6,    .z=UNDEF        },  // in
    { .t=VM_part,       .x=1,           .y=G_EQ_B+7,    .z=UNDEF        },  // next token
    { .t=VM_pick,       .x=1,           .y=G_EQ_B+8,    .z=UNDEF        },  // token token
    { .t=VM_pick,       .x=6,           .y=G_EQ_B+9,    .z=UNDEF        },  // value
    { .t=VM_cmp,        .x=CMP_NE,      .y=G_EQ_B+10,   .z=UNDEF        },  // token != value
    { .t=VM_if,         .x=G_EQ_B+16,   .y=G_EQ_B+11,   .z=UNDEF        },

    { .t=VM_pick,       .x=3,           .y=G_EQ_B+12,   .z=UNDEF        },  // ok
    { .t=VM_pick,       .x=2,           .y=G_EQ_B+13,   .z=UNDEF        },  // token
    { .t=VM_push,       .x=G_NEXT_K,    .y=G_EQ_B+14,   .z=UNDEF        },  // G_NEXT_K
    { .t=VM_new,        .x=2,           .y=G_EQ_B+15,   .z=UNDEF        },  // k_next
    { .t=VM_pick,       .x=3,           .y=SEND_0,      .z=UNDEF        },  // next

    { .t=VM_drop,       .x=2,           .y=G_EQ_B+17,   .z=UNDEF        },  // fail ok

    { .t=VM_msg,        .x=-2,          .y=G_EQ_B+18,   .z=UNDEF        },  // in
    { .t=VM_pick,       .x=3,           .y=SEND_0,      .z=UNDEF        },  // fail

#define G_FAIL_K (G_EQ_B+19)
//  { .t=VM_push,       .x=_msg_,       .y=G_FAIL_K-1,  .z=UNDEF        },
//  { .t=VM_push,       .x=_cust_,      .y=G_FAIL_K+0,  .z=UNDEF        },
    { .t=VM_send,       .x=0,           .y=RELEASE,     .z=UNDEF        },  // (cust . msg)

#define G_OR_B (G_FAIL_K+1)
//  { .t=VM_push,       .x=_first_,     .y=G_OR_B-1,    .z=UNDEF        },
//  { .t=VM_push,       .x=_rest_,      .y=G_OR_B+0,    .z=UNDEF        },
    { .t=VM_msg,        .x=-1,          .y=G_OR_B+1,    .z=UNDEF        },  // resume = (value . in)

    { .t=VM_msg,        .x=0,           .y=G_OR_B+2,    .z=UNDEF        },  // msg = (custs value . in)
    { .t=VM_pick,       .x=3,           .y=G_OR_B+3,    .z=UNDEF        },  // cust = rest
    { .t=VM_push,       .x=G_FAIL_K,    .y=G_OR_B+4,    .z=UNDEF        },  // G_FAIL_K
    { .t=VM_new,        .x=2,           .y=G_OR_B+5,    .z=UNDEF        },  // or_fail

    { .t=VM_msg,        .x=1,           .y=G_OR_B+6,    .z=UNDEF        },  // custs
    { .t=VM_nth,        .x=1,           .y=G_OR_B+7,    .z=UNDEF        },  // ok = car(custs)
    { .t=VM_pair,       .x=1,           .y=G_OR_B+8,    .z=UNDEF        },  // (ok . or_fail)
    { .t=VM_pair,       .x=1,           .y=G_OR_B+9,    .z=UNDEF        },  // ((ok . or_fail) . resume)

    { .t=VM_pick,       .x=3,           .y=SEND_0,      .z=UNDEF        },  // first

#define G_AND_PR (G_OR_B+10)
//  { .t=VM_push,       .x=_cust_,      .y=G_AND_PR-1,  .z=UNDEF        },
//  { .t=VM_push,       .x=_head_,      .y=G_AND_PR+0,  .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=G_AND_PR+1,  .z=UNDEF        },  // (value . in)
    { .t=VM_part,       .x=1,           .y=G_AND_PR+2,  .z=UNDEF        },  // in tail
    { .t=VM_roll,       .x=3,           .y=G_AND_PR+3,  .z=UNDEF        },  // head
    { .t=VM_pair,       .x=1,           .y=G_AND_PR+4,  .z=UNDEF        },  // (head . tail)
    { .t=VM_pair,       .x=1,           .y=G_AND_PR+5,  .z=UNDEF        },  // ((head . tail) . in)
    { .t=VM_roll,       .x=2,           .y=RELEASE_0,   .z=UNDEF        },  // cust
#define G_AND_OK (G_AND_PR+6)
//  { .t=VM_push,       .x=_rest_,      .y=G_AND_OK-2,  .z=UNDEF        },
//  { .t=VM_push,       .x=_and_fail_,  .y=G_AND_OK-1,  .z=UNDEF        },
//  { .t=VM_push,       .x=_ok_,        .y=G_AND_OK+0,  .z=UNDEF        },
    { .t=VM_msg,        .x=1,           .y=G_AND_OK+1,  .z=UNDEF        },  // head = value
    { .t=VM_push,       .x=G_AND_PR,    .y=G_AND_OK+2,  .z=UNDEF        },  // G_AND_PR
    { .t=VM_beh,        .x=2,           .y=G_AND_OK+3,  .z=UNDEF        },  // BECOME (G_AND_PR ok value)
    { .t=VM_msg,        .x=0,           .y=G_AND_OK+4,  .z=UNDEF        },  // resume = (value . in)
    { .t=VM_roll,       .x=2,           .y=G_AND_OK+5,  .z=UNDEF        },  // and_fail
    { .t=VM_self,       .x=UNDEF,       .y=G_AND_OK+6,  .z=UNDEF        },  // and_pair = SELF
    { .t=VM_pair,       .x=1,           .y=G_AND_OK+7,  .z=UNDEF        },  // (and_pair . and_fail)
    { .t=VM_pair,       .x=1,           .y=G_AND_OK+8,  .z=UNDEF        },  // ((and_pair . and_fail) . resume)
    { .t=VM_roll,       .x=2,           .y=SEND_0,      .z=UNDEF        },  // rest
#define G_AND_B (G_AND_OK+9)
//  { .t=VM_push,       .x=_first_,     .y=G_AND_B-1,   .z=UNDEF        },
//  { .t=VM_push,       .x=_rest_,      .y=G_AND_B+0,   .z=UNDEF        },
    { .t=VM_msg,        .x=-1,          .y=G_AND_B+1,   .z=UNDEF        },  // resume = (value . in)
    { .t=VM_msg,        .x=1,           .y=G_AND_B+2,   .z=UNDEF        },  // custs
    { .t=VM_nth,        .x=-1,          .y=G_AND_B+3,   .z=UNDEF        },  // fail = cdr(custs)

    { .t=VM_pick,       .x=3,           .y=G_AND_B+4,   .z=UNDEF        },  // rest

    { .t=VM_msg,        .x=-2,          .y=G_AND_B+5,   .z=UNDEF        },  // msg = in
    { .t=VM_pick,       .x=3,           .y=G_AND_B+6,   .z=UNDEF        },  // cust = fail
    { .t=VM_push,       .x=G_FAIL_K,    .y=G_AND_B+7,   .z=UNDEF        },  // G_FAIL_K
    { .t=VM_new,        .x=2,           .y=G_AND_B+8,   .z=UNDEF        },  // and_fail = (G_FAIL_K in fail)

    { .t=VM_msg,        .x=1,           .y=G_AND_B+9,   .z=UNDEF        },  // custs
    { .t=VM_nth,        .x=1,           .y=G_AND_B+10,  .z=UNDEF        },  // ok = car(custs)
    { .t=VM_push,       .x=G_AND_OK,    .y=G_AND_B+11,  .z=UNDEF        },  // G_AND_OK
    { .t=VM_new,        .x=3,           .y=G_AND_B+12,  .z=UNDEF        },  // and_ok = (G_AND_OK rest and_fail ok)

    { .t=VM_pair,       .x=1,           .y=G_AND_B+13,  .z=UNDEF        },  // (and_ok . fail)
    { .t=VM_pair,       .x=1,           .y=G_AND_B+14,  .z=UNDEF        },  // ((and_ok . fail) . resume)
    { .t=VM_pick,       .x=3,           .y=SEND_0,      .z=UNDEF        },  // first

#define G_NOT_B (G_AND_B+15)
//  { .t=VM_push,       .x=_ptrn_,      .y=G_NOT_B+0,   .z=UNDEF        },
    { .t=VM_msg,        .x=1,           .y=G_NOT_B+1,   .z=UNDEF        },  // custs
    { .t=VM_part,       .x=1,           .y=G_NOT_B+2,   .z=UNDEF        },  // fail ok

    { .t=VM_msg,        .x=-2,          .y=G_NOT_B+3,   .z=UNDEF        },  // in
    { .t=VM_push,       .x=UNIT,        .y=G_NOT_B+4,   .z=UNDEF        },  // value = UNIT
    { .t=VM_pair,       .x=1,           .y=G_NOT_B+5,   .z=UNDEF        },  // ctx = (#unit . in)
    { .t=VM_roll,       .x=2,           .y=G_NOT_B+6,   .z=UNDEF        },  // ok
    { .t=VM_push,       .x=RELEASE_0,   .y=G_NOT_B+7,   .z=UNDEF        },  // RELEASE_0
    { .t=VM_new,        .x=2,           .y=G_NOT_B+8,   .z=UNDEF        },  // fail' = (RELEASE_0 ctx ok)

    { .t=VM_msg,        .x=-2,          .y=G_NOT_B+9,   .z=UNDEF        },  // in
    { .t=VM_roll,       .x=3,           .y=G_NOT_B+10,  .z=UNDEF        },  // fail
    { .t=VM_push,       .x=RELEASE_0,   .y=G_NOT_B+11,  .z=UNDEF        },  // RELEASE_0
    { .t=VM_new,        .x=2,           .y=G_NOT_B+12,  .z=UNDEF        },  // ok' = (RELEASE_0 in fail)

    { .t=VM_pair,       .x=1,           .y=G_NOT_B+13,  .z=UNDEF        },  // custs' = (ok' . fail')
    { .t=VM_msg,        .x=-1,          .y=G_NOT_B+14,  .z=UNDEF        },  // ctx = (value . in)
    { .t=VM_roll,       .x=2,           .y=G_NOT_B+15,  .z=UNDEF        },  // ctx custs'
    { .t=VM_pair,       .x=1,           .y=G_NOT_B+16,  .z=UNDEF        },  // msg = (custs' value . in)
    { .t=VM_pick,       .x=2,           .y=SEND_0,      .z=UNDEF        },  // ptrn

/*
Optional(pattern) = Or(And(pattern, Empty), Empty)
Plus(pattern) = And(pattern, Star(pattern))
Star(pattern) = Or(Plus(pattern), Empty)
*/
#define G_OPT_B (G_NOT_B+17)
#define G_PLUS_B (G_OPT_B+6)
#define G_STAR_B (G_PLUS_B+5)
//  { .t=VM_push,       .x=_ptrn_,      .y=G_OPT_B+0,   .z=UNDEF        },
    { .t=VM_push,       .x=G_EMPTY,     .y=G_OPT_B+1,   .z=UNDEF        },  // G_EMPTY
    { .t=VM_push,       .x=G_AND_B,     .y=G_OPT_B+2,   .z=UNDEF        },  // G_AND_B
    { .t=VM_new,        .x=2,           .y=G_OPT_B+3,   .z=UNDEF        },  // ptrn' = (And ptrn Empty)
    { .t=VM_push,       .x=G_EMPTY,     .y=G_OPT_B+4,   .z=UNDEF        },  // G_EMPTY
    { .t=VM_push,       .x=G_OR_B,      .y=G_OPT_B+5,   .z=UNDEF        },  // G_OR_B
    { .t=VM_beh,        .x=2,           .y=RESEND,      .z=UNDEF        },  // BECOME (Or ptrn' Empty)

//  { .t=VM_push,       .x=_ptrn_,      .y=G_PLUS_B+0,  .z=UNDEF        },
    { .t=VM_pick,       .x=1,           .y=G_PLUS_B+1,  .z=UNDEF        },  // ptrn
    { .t=VM_push,       .x=G_STAR_B,    .y=G_PLUS_B+2,  .z=UNDEF        },  // G_STAR_B
    { .t=VM_new,        .x=1,           .y=G_PLUS_B+3,  .z=UNDEF        },  // star = (Star ptrn)
    { .t=VM_push,       .x=G_AND_B,     .y=G_PLUS_B+4,  .z=UNDEF        },  // G_AND_B
    { .t=VM_beh,        .x=2,           .y=RESEND,      .z=UNDEF        },  // BECOME (And ptrn star)

//  { .t=VM_push,       .x=_ptrn_,      .y=G_STAR_B+0,  .z=UNDEF        },
    { .t=VM_push,       .x=G_PLUS_B,    .y=G_STAR_B+1,  .z=UNDEF        },  // G_PLUS_B
    { .t=VM_new,        .x=1,           .y=G_STAR_B+2,  .z=UNDEF        },  // plus = (Plus ptrn)
    { .t=VM_push,       .x=G_EMPTY,     .y=G_STAR_B+3,  .z=UNDEF        },  // G_EMPTY
    { .t=VM_push,       .x=G_OR_B,      .y=G_STAR_B+4,  .z=UNDEF        },  // G_OR_B
    { .t=VM_beh,        .x=2,           .y=RESEND,      .z=UNDEF        },  // BECOME (Or plus Empty)

#define G_ALT_B (G_STAR_B+5)
//  { .t=VM_push,       .x=_ptrns_,     .y=G_ALT_B+0,   .z=UNDEF        },
    { .t=VM_pick,       .x=1,           .y=G_ALT_B+1,   .z=UNDEF        },  // ptrns
    { .t=VM_eq,         .x=NIL,         .y=G_ALT_B+2,   .z=UNDEF        },  // ptrns == ()
    { .t=VM_if,         .x=G_ALT_B+13,  .y=G_ALT_B+3,   .z=UNDEF        },

    { .t=VM_part,       .x=1,           .y=G_ALT_B+4,   .z=UNDEF        },  // tail head
    { .t=VM_pick,       .x=2,           .y=G_ALT_B+5,   .z=UNDEF        },  // tail
    { .t=VM_eq,         .x=NIL,         .y=G_ALT_B+6,   .z=UNDEF        },  // tail == ()
    { .t=VM_if,         .x=G_ALT_B+10,  .y=G_ALT_B+7,   .z=UNDEF        },

    { .t=VM_pick,       .x=2,           .y=G_ALT_B+8,   .z=UNDEF        },  // tail
    { .t=VM_push,       .x=G_ALT_B,     .y=G_ALT_B+9,   .z=UNDEF        },  // G_ALT_B
    { .t=VM_new,        .x=1,           .y=G_ALT_B+11,  .z=UNDEF        },  // rest = (Alt tail)

    { .t=VM_push,       .x=G_FAIL,      .y=G_ALT_B+11,  .z=UNDEF        },  // rest = G_FAIL
    { .t=VM_push,       .x=G_OR_B,      .y=G_ALT_B+12,  .z=UNDEF        },  // G_OR_B
    { .t=VM_beh,        .x=2,           .y=RESEND,      .z=UNDEF        },  // BECOME

    { .t=VM_push,       .x=G_FAIL_B,    .y=G_ALT_B+14,  .z=UNDEF        },  // G_FAIL_B
    { .t=VM_beh,        .x=0,           .y=RESEND,      .z=UNDEF        },  // BECOME

#define G_SEQ_B (G_ALT_B+15)
//  { .t=VM_push,       .x=_ptrns_,     .y=G_SEQ_B+0,   .z=UNDEF        },
    { .t=VM_pick,       .x=1,           .y=G_SEQ_B+1,   .z=UNDEF        },  // ptrns
    { .t=VM_eq,         .x=NIL,         .y=G_SEQ_B+2,   .z=UNDEF        },  // ptrns == ()
    { .t=VM_if,         .x=G_SEQ_B+13,  .y=G_SEQ_B+3,   .z=UNDEF        },

    { .t=VM_part,       .x=1,           .y=G_SEQ_B+4,   .z=UNDEF        },  // tail head
    { .t=VM_pick,       .x=2,           .y=G_SEQ_B+5,   .z=UNDEF        },  // tail
    { .t=VM_eq,         .x=NIL,         .y=G_SEQ_B+6,   .z=UNDEF        },  // tail == ()
    { .t=VM_if,         .x=G_SEQ_B+10,  .y=G_SEQ_B+7,   .z=UNDEF        },

    { .t=VM_pick,       .x=2,           .y=G_SEQ_B+8,   .z=UNDEF        },  // tail
    { .t=VM_push,       .x=G_SEQ_B,     .y=G_SEQ_B+9,   .z=UNDEF        },  // G_SEQ_B
    { .t=VM_new,        .x=1,           .y=G_SEQ_B+11,  .z=UNDEF        },  // rest = (Seq tail)

    { .t=VM_push,       .x=G_EMPTY,     .y=G_SEQ_B+11,  .z=UNDEF        },  // rest = G_EMPTY
    { .t=VM_push,       .x=G_AND_B,     .y=G_SEQ_B+12,  .z=UNDEF        },  // G_AND_B
    { .t=VM_beh,        .x=2,           .y=RESEND,      .z=UNDEF        },  // BECOME

    { .t=VM_push,       .x=G_EMPTY_B,   .y=G_SEQ_B+14,  .z=UNDEF        },  // G_EMPTY_B
    { .t=VM_beh,        .x=0,           .y=RESEND,      .z=UNDEF        },  // BECOME

#define G_CLS_B (G_SEQ_B+15)
//  { .t=VM_push,       .x=_class_,     .y=G_CLS_B+0,   .z=UNDEF        },
    { .t=VM_msg,        .x=1,           .y=G_CLS_B+1,   .z=UNDEF        },  // custs = (ok . fail)
    { .t=VM_part,       .x=1,           .y=G_CLS_B+2,   .z=UNDEF        },  // fail ok
    { .t=VM_msg,        .x=-2,          .y=G_CLS_B+3,   .z=UNDEF        },  // in
    { .t=VM_eq,         .x=NIL,         .y=G_CLS_B+4,   .z=UNDEF        },  // in == ()
    { .t=VM_if,         .x=G_CLS_B+18,  .y=G_CLS_B+5,   .z=UNDEF        },

    { .t=VM_msg,        .x=-2,          .y=G_CLS_B+6,   .z=UNDEF        },  // in
    { .t=VM_part,       .x=1,           .y=G_CLS_B+7,   .z=UNDEF        },  // next token
    { .t=VM_pick,       .x=1,           .y=G_CLS_B+8,   .z=UNDEF        },  // token token
    { .t=VM_pick,       .x=6,           .y=G_CLS_B+9,   .z=UNDEF        },  // class
    { .t=VM_cmp,        .x=CMP_CLS,     .y=G_CLS_B+10,  .z=UNDEF        },  // token in class
    { .t=VM_eq,         .x=FALSE,       .y=G_CLS_B+11,  .z=UNDEF        },  // token ~in class
    { .t=VM_if,         .x=G_CLS_B+17,  .y=G_CLS_B+12,  .z=UNDEF        },

    { .t=VM_pick,       .x=3,           .y=G_CLS_B+13,  .z=UNDEF        },  // ok
    { .t=VM_pick,       .x=2,           .y=G_CLS_B+14,  .z=UNDEF        },  // token
    { .t=VM_push,       .x=G_NEXT_K,    .y=G_CLS_B+15,  .z=UNDEF        },  // G_NEXT_K
    { .t=VM_new,        .x=2,           .y=G_CLS_B+16,  .z=UNDEF        },  // k_next
    { .t=VM_pick,       .x=3,           .y=SEND_0,      .z=UNDEF        },  // next

    { .t=VM_drop,       .x=2,           .y=G_CLS_B+18,  .z=UNDEF        },  // fail ok

    { .t=VM_msg,        .x=-2,          .y=G_CLS_B+19,  .z=UNDEF        },  // in
    { .t=VM_pick,       .x=3,           .y=SEND_0,      .z=UNDEF        },  // fail

#define G_PRED_K (G_CLS_B+20)
//  { .t=VM_push,       .x=_more_,      .y=G_PRED_K-1,  .z=UNDEF        },  // (value' . in')
//  { .t=VM_push,       .x=_msg0_,      .y=G_PRED_K+0,  .z=UNDEF        },  // ((ok . fail) value . in)
    { .t=VM_msg,        .x=0,           .y=G_PRED_K+1,  .z=UNDEF        },  // cond
    { .t=VM_if,         .x=G_PRED_K+5,  .y=G_PRED_K+2,  .z=UNDEF        },

    { .t=VM_part,       .x=1,           .y=G_PRED_K+3,  .z=UNDEF        },  // resume custs
    { .t=VM_part,       .x=1,           .y=G_PRED_K+4,  .z=UNDEF        },  // resume fail ok
    { .t=VM_drop,       .x=1,           .y=RELEASE_0,   .z=UNDEF        },  // resume fail

    { .t=VM_nth,        .x=1,           .y=G_PRED_K+6,  .z=UNDEF        },  // custs = (ok . fail)
    { .t=VM_nth,        .x=1,           .y=RELEASE_0,   .z=UNDEF        },  // ok

#define G_PRED_OK (G_PRED_K+7)
//  { .t=VM_push,       .x=_msg0_,      .y=G_PRED_OK-1, .z=UNDEF        },
//  { .t=VM_push,       .x=_pred_,      .y=G_PRED_OK+0, .z=UNDEF        },
    { .t=VM_msg,        .x=1,           .y=G_PRED_OK+1, .z=UNDEF        },  // value

    { .t=VM_msg,        .x=0,           .y=G_PRED_OK+2, .z=UNDEF        },  // more
    { .t=VM_roll,       .x=4,           .y=G_PRED_OK+3, .z=UNDEF        },  // msg0
    { .t=VM_push,       .x=G_PRED_K,    .y=G_PRED_OK+4, .z=UNDEF        },  // G_PRED_K
    { .t=VM_beh,        .x=2,           .y=G_PRED_OK+5, .z=UNDEF        },  // BECOME (G_PRED_K more msg0)
    { .t=VM_self,       .x=UNDEF,       .y=G_PRED_OK+6, .z=UNDEF        },  // k_pred = SELF

    { .t=VM_roll,       .x=3,           .y=G_PRED_OK+7, .z=UNDEF        },  // pred
    { .t=VM_send,       .x=2,           .y=COMMIT,      .z=UNDEF        },  // (pred k_pred value)

#define G_PRED_B (G_PRED_OK+8)
//  { .t=VM_push,       .x=_pred_,      .y=G_PRED_B-1,  .z=UNDEF        },
//  { .t=VM_push,       .x=_ptrn_,      .y=G_PRED_B+0,  .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=G_PRED_B+1,  .z=UNDEF        },  // (custs . resume)
    { .t=VM_part,       .x=1,           .y=G_PRED_B+2,  .z=UNDEF        },  // resume custs
    { .t=VM_part,       .x=1,           .y=G_PRED_B+3,  .z=UNDEF        },  // fail ok
    { .t=VM_drop,       .x=1,           .y=G_PRED_B+4,  .z=UNDEF        },  // fail

    { .t=VM_msg,        .x=0,           .y=G_PRED_B+5,  .z=UNDEF        },  // msg0 = (custs . resume)
    { .t=VM_pick,       .x=5,           .y=G_PRED_B+6,  .z=UNDEF        },  // pred
    { .t=VM_push,       .x=G_PRED_OK,   .y=G_PRED_B+7,  .z=UNDEF        },  // G_PRED_OK
    { .t=VM_new,        .x=2,           .y=G_PRED_B+8,  .z=UNDEF        },  // ok'

    { .t=VM_pair,       .x=1,           .y=G_PRED_B+9,  .z=UNDEF        },  // custs = (ok' . fail)
    { .t=VM_pair,       .x=1,           .y=G_PRED_B+10, .z=UNDEF        },  // msg = (custs . resume)
    { .t=VM_pick,       .x=2,           .y=SEND_0,      .z=UNDEF        },  // ptrn

#define G_XLAT_K (G_PRED_B+11)
//  { .t=VM_push,       .x=_cust_,      .y=G_XLAT_K-1,  .z=UNDEF        },
//  { .t=VM_push,       .x=_in_,        .y=G_XLAT_K+0,  .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=G_XLAT_K+1,  .z=UNDEF        },  // value
    { .t=VM_pair,       .x=1,           .y=G_XLAT_K+2,  .z=UNDEF        },  // (value . in)
    { .t=VM_roll,       .x=2,           .y=RELEASE_0,   .z=UNDEF        },  // cust

#define G_XLAT_OK (G_XLAT_K+3)
//  { .t=VM_push,       .x=_cust_,      .y=G_XLAT_OK-1, .z=UNDEF        },
//  { .t=VM_push,       .x=_func_,      .y=G_XLAT_OK+0, .z=UNDEF        },
    { .t=VM_msg,        .x=1,           .y=G_XLAT_OK+1, .z=UNDEF        },  // value

    { .t=VM_roll,       .x=3,           .y=G_XLAT_OK+2, .z=UNDEF        },  // cust
    { .t=VM_msg,        .x=-1,          .y=G_XLAT_OK+3, .z=UNDEF        },  // in
    { .t=VM_push,       .x=G_XLAT_K,    .y=G_XLAT_OK+4, .z=UNDEF        },  // G_XLAT_K
    { .t=VM_beh,        .x=2,           .y=G_XLAT_OK+5, .z=UNDEF        },  // BECOME (G_XLAT_K cust in)
    { .t=VM_self,       .x=UNDEF,       .y=G_XLAT_OK+6, .z=UNDEF        },  // k_xlat = SELF

    { .t=VM_roll,       .x=3,           .y=G_XLAT_OK+7, .z=UNDEF        },  // func
    { .t=VM_send,       .x=2,           .y=COMMIT,      .z=UNDEF        },  // (func k_xlat value)

#define G_XLAT_B (G_XLAT_OK+8)
//  { .t=VM_push,       .x=_func_,      .y=G_XLAT_B+0,  .z=UNDEF        },
//  { .t=VM_push,       .x=_ptrn_,      .y=G_XLAT_B-1,  .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=G_XLAT_B+1,  .z=UNDEF        },  // (custs . resume)
    { .t=VM_part,       .x=1,           .y=G_XLAT_B+2,  .z=UNDEF        },  // resume custs
    { .t=VM_part,       .x=1,           .y=G_XLAT_B+3,  .z=UNDEF        },  // fail ok

    { .t=VM_pick,       .x=5,           .y=G_XLAT_B+4,  .z=UNDEF        },  // func
    { .t=VM_push,       .x=G_XLAT_OK,   .y=G_XLAT_B+5,  .z=UNDEF        },  // G_XLAT_OK
    { .t=VM_new,        .x=2,           .y=G_XLAT_B+6,  .z=UNDEF        },  // ok'

    { .t=VM_pair,       .x=1,           .y=G_XLAT_B+7,  .z=UNDEF        },  // custs = (ok' . fail)
    { .t=VM_pair,       .x=1,           .y=G_XLAT_B+8,  .z=UNDEF        },  // msg = (custs . resume)
    { .t=VM_pick,       .x=2,           .y=SEND_0,      .z=UNDEF        },  // ptrn

#define S_CHAIN (G_XLAT_B+9)
#define S_BUSY_C (S_CHAIN+11)
#define S_NEXT_C (S_BUSY_C+17)
//  { .t=VM_push,       .x=_ptrn_,      .y=S_CHAIN-1,   .z=UNDEF        },
//  { .t=VM_push,       .x=_src_,       .y=S_CHAIN+0,   .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=S_CHAIN+1,   .z=UNDEF        },  // cust
    { .t=VM_pick,       .x=3,           .y=S_CHAIN+2,   .z=UNDEF        },  // ptrn
    { .t=VM_push,       .x=S_BUSY_C,    .y=S_CHAIN+3,   .z=UNDEF        },  // S_BUSY_C
    { .t=VM_beh,        .x=2,           .y=S_CHAIN+4,   .z=UNDEF        },  // BECOME (S_BUSY_C cust ptrn)

    { .t=VM_self,       .x=UNDEF,       .y=S_CHAIN+5,   .z=UNDEF        },  // fail = SELF
    { .t=VM_self,       .x=UNDEF,       .y=S_CHAIN+6,   .z=UNDEF        },  // ok = SELF
    { .t=VM_pair,       .x=1,           .y=S_CHAIN+7,   .z=UNDEF        },  // custs = (ok . fail)
    { .t=VM_roll,       .x=3,           .y=S_CHAIN+8,   .z=UNDEF        },  // ptrn
    { .t=VM_push,       .x=G_START,     .y=S_CHAIN+9,   .z=UNDEF        },  // G_START
    { .t=VM_new,        .x=2,           .y=S_CHAIN+10,  .z=UNDEF        },  // start = (G_START custs ptrn)
    { .t=VM_roll,       .x=2,           .y=SEND_0,      .z=UNDEF        },  // src

//  { .t=VM_push,       .x=_cust_,      .y=S_BUSY_C-1,  .z=UNDEF        },
//  { .t=VM_push,       .x=_ptrn_,      .y=S_BUSY_C+0,  .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=S_BUSY_C+1,  .z=UNDEF        },  // msg
    { .t=VM_typeq,      .x=Actor_T,     .y=S_BUSY_C+2,  .z=UNDEF        },  // msg has type Actor_T
    { .t=VM_if,         .x=RESEND,      .y=S_BUSY_C+3,  .z=UNDEF        },  // defer "get" requests

    { .t=VM_msg,        .x=-1,          .y=S_BUSY_C+4,  .z=UNDEF        },  // cdr(msg)
    { .t=VM_typeq,      .x=Pair_T,      .y=S_BUSY_C+5,  .z=UNDEF        },  // cdr(msg) has type Pair_T
    { .t=VM_if,         .x=S_BUSY_C+6,  .y=S_BUSY_C+12, .z=UNDEF        },  // treat failure as end

    { .t=VM_pick,       .x=1,           .y=S_BUSY_C+7,  .z=UNDEF        },  // ptrn
    { .t=VM_msg,        .x=-1,          .y=S_BUSY_C+8,  .z=UNDEF        },  // in
    { .t=VM_push,       .x=S_NEXT_C,    .y=S_BUSY_C+9,  .z=UNDEF        },  // S_NEXT_C
    { .t=VM_new,        .x=2,           .y=S_BUSY_C+10, .z=UNDEF        },  // next = (S_NEXT_C ptrn in)
    { .t=VM_msg,        .x=1,           .y=S_BUSY_C+11, .z=UNDEF        },  // token = value
    { .t=VM_pair,       .x=1,           .y=S_BUSY_C+13, .z=UNDEF        },  // in = (token . next)

    { .t=VM_push,       .x=NIL,         .y=S_BUSY_C+13, .z=UNDEF        },  // in = ()

    { .t=VM_push,       .x=S_VALUE,     .y=S_BUSY_C+14, .z=UNDEF        },  // S_VALUE
    { .t=VM_beh,        .x=1,           .y=S_BUSY_C+15, .z=UNDEF        },  // BECOME (S_VALUE in)
    { .t=VM_roll,       .x=2,           .y=S_BUSY_C+16, .z=UNDEF        },  // cust
    { .t=VM_self,       .x=UNDEF,       .y=SEND_0,      .z=UNDEF        },  // (SELF . cust)

//  { .t=VM_push,       .x=_ptrn_,      .y=S_NEXT_C-1,  .z=UNDEF        },
//  { .t=VM_push,       .x=_in_,        .y=S_NEXT_C+0,  .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=S_NEXT_C+1,  .z=UNDEF        },  // cust
    { .t=VM_pick,       .x=3,           .y=S_NEXT_C+2,  .z=UNDEF        },  // ptrn
    { .t=VM_push,       .x=S_BUSY_C,    .y=S_NEXT_C+3,  .z=UNDEF        },  // S_BUSY_C
    { .t=VM_beh,        .x=2,           .y=S_NEXT_C+4,  .z=UNDEF        },  // BECOME (S_BUSY_C cust ptrn)

    { .t=VM_push,       .x=UNDEF,       .y=S_NEXT_C+5,  .z=UNDEF        },  // value = UNDEF
    { .t=VM_self,       .x=UNDEF,       .y=S_NEXT_C+6,  .z=UNDEF        },  // fail = SELF
    { .t=VM_self,       .x=UNDEF,       .y=S_NEXT_C+7,  .z=UNDEF        },  // ok = SELF
    { .t=VM_pair,       .x=1,           .y=S_NEXT_C+8,  .z=UNDEF        },  // custs = (ok . fail)
    { .t=VM_pair,       .x=2,           .y=S_NEXT_C+9,  .z=UNDEF        },  // (custs value . in)
    { .t=VM_roll,       .x=2,           .y=SEND_0,      .z=UNDEF        },  // ptrn

//
// PEG tools
//

#if SCM_PEG_TOOLS
#define F_G_EQ (S_NEXT_C+10)
    { .t=Actor_T,       .x=F_G_EQ+1,    .y=NIL,         .z=UNDEF        },  // (peg-eq <token>)
    { .t=VM_msg,        .x=2,           .y=F_G_EQ+2,    .z=UNDEF        },  // token = arg1
    { .t=VM_push,       .x=G_EQ_B,      .y=F_G_EQ+3,    .z=UNDEF        },  // G_EQ_B
    { .t=VM_new,        .x=1,           .y=CUST_SEND,   .z=UNDEF        },  // (G_EQ_B token)

#define F_G_OR (F_G_EQ+4)
    { .t=Actor_T,       .x=F_G_OR+1,    .y=NIL,         .z=UNDEF        },  // (peg-or <first> <rest>)
    { .t=VM_msg,        .x=2,           .y=F_G_OR+2,    .z=UNDEF        },  // first = arg1
    { .t=VM_msg,        .x=3,           .y=F_G_OR+3,    .z=UNDEF        },  // rest = arg2
    { .t=VM_push,       .x=G_OR_B,      .y=F_G_OR+4,    .z=UNDEF        },  // G_OR_B
    { .t=VM_new,        .x=2,           .y=CUST_SEND,   .z=UNDEF        },  // (G_OR_B first rest)

#define F_G_AND (F_G_OR+5)
    { .t=Actor_T,       .x=F_G_AND+1,   .y=NIL,         .z=UNDEF        },  // (peg-and <first> <rest>)
    { .t=VM_msg,        .x=2,           .y=F_G_AND+2,   .z=UNDEF        },  // first = arg1
    { .t=VM_msg,        .x=3,           .y=F_G_AND+3,   .z=UNDEF        },  // rest = arg2
    { .t=VM_push,       .x=G_AND_B,     .y=F_G_AND+4,   .z=UNDEF        },  // G_AND_B
    { .t=VM_new,        .x=2,           .y=CUST_SEND,   .z=UNDEF        },  // (G_AND_B first rest)

#define F_G_NOT (F_G_AND+5)
    { .t=Actor_T,       .x=F_G_NOT+1,   .y=NIL,         .z=UNDEF        },  // (peg-not <peg>)
    { .t=VM_msg,        .x=2,           .y=F_G_NOT+2,   .z=UNDEF        },  // peg = arg1
    { .t=VM_push,       .x=G_NOT_B,     .y=F_G_NOT+3,   .z=UNDEF        },  // G_NOT_B
    { .t=VM_new,        .x=1,           .y=CUST_SEND,   .z=UNDEF        },  // (G_NOT_B peg)

#define F_G_CLS (F_G_NOT+4)
    { .t=Actor_T,       .x=F_G_CLS+1,   .y=NIL,         .z=UNDEF        },  // (peg-class . <classes>)
    { .t=VM_msg,        .x=0,           .y=F_G_CLS+2,   .z=UNDEF        },
    { .t=VM_part,       .x=1,           .y=F_G_CLS+3,   .z=UNDEF        },  // args cust
    { .t=VM_push,       .x=TO_FIX(0),   .y=F_G_CLS+4,   .z=UNDEF        },  // mask = +0
    { .t=VM_roll,       .x=3,           .y=F_G_CLS+5,   .z=UNDEF        },  // cust mask args

    { .t=VM_pick,       .x=1,           .y=F_G_CLS+6,   .z=UNDEF        },  // args args
    { .t=VM_typeq,      .x=Pair_T,      .y=F_G_CLS+7,   .z=UNDEF        },  // args has type Pair_T
    { .t=VM_if,         .x=F_G_CLS+8,   .y=F_G_CLS+12,  .z=UNDEF        },

    { .t=VM_part,       .x=1,           .y=F_G_CLS+9,   .z=UNDEF        },  // tail head
    { .t=VM_roll,       .x=3,           .y=F_G_CLS+10,  .z=UNDEF        },  // tail head mask
    { .t=VM_alu,        .x=ALU_OR,      .y=F_G_CLS+11,  .z=UNDEF        },  // mask |= head
    { .t=VM_roll,       .x=2,           .y=F_G_CLS+5,   .z=UNDEF        },  // mask tail

    { .t=VM_drop,       .x=1,           .y=F_G_CLS+13,  .z=UNDEF        },  // cust mask
    { .t=VM_push,       .x=G_CLS_B,     .y=F_G_CLS+14,  .z=UNDEF        },  // G_CLS_B
    { .t=VM_new,        .x=1,           .y=F_G_CLS+15,  .z=UNDEF        },  // ptrn = (G_CLS_B mask)
    { .t=VM_roll,       .x=2,           .y=SEND_0,      .z=UNDEF        },  // ptrn cust

#define F_G_OPT (F_G_CLS+16)
    { .t=Actor_T,       .x=F_G_OPT+1,   .y=NIL,         .z=UNDEF        },  // (peg-opt <peg>)
    { .t=VM_msg,        .x=2,           .y=F_G_OPT+2,   .z=UNDEF        },  // peg = arg1
    { .t=VM_push,       .x=G_OPT_B,     .y=F_G_OPT+3,   .z=UNDEF        },  // G_OPT_B
    { .t=VM_new,        .x=1,           .y=CUST_SEND,   .z=UNDEF        },  // (G_OPT_B peg)

#define F_G_PLUS (F_G_OPT+4)
    { .t=Actor_T,       .x=F_G_PLUS+1,  .y=NIL,         .z=UNDEF        },  // (peg-plus <peg>)
    { .t=VM_msg,        .x=2,           .y=F_G_PLUS+2,  .z=UNDEF        },  // peg = arg1
    { .t=VM_push,       .x=G_PLUS_B,    .y=F_G_PLUS+3,  .z=UNDEF        },  // G_PLUS_B
    { .t=VM_new,        .x=1,           .y=CUST_SEND,   .z=UNDEF        },  // (G_PLUS_B peg)

#define F_G_STAR (F_G_PLUS+4)
    { .t=Actor_T,       .x=F_G_STAR+1,  .y=NIL,         .z=UNDEF        },  // (peg-star <peg>)
    { .t=VM_msg,        .x=2,           .y=F_G_STAR+2,  .z=UNDEF        },  // peg = arg1
    { .t=VM_push,       .x=G_STAR_B,    .y=F_G_STAR+3,  .z=UNDEF        },  // G_STAR_B
    { .t=VM_new,        .x=1,           .y=CUST_SEND,   .z=UNDEF        },  // (G_STAR_B peg)

#define F_G_ALT (F_G_STAR+4)
    { .t=Actor_T,       .x=F_G_ALT+1,   .y=NIL,         .z=UNDEF        },  // (peg-alt . <pegs>)
    { .t=VM_msg,        .x=-1,          .y=F_G_ALT+2,   .z=UNDEF        },  // pegs = args
    { .t=VM_push,       .x=G_ALT_B,     .y=F_G_ALT+3,   .z=UNDEF        },  // G_ALT_B
    { .t=VM_new,        .x=1,           .y=CUST_SEND,   .z=UNDEF        },  // (G_ALT_B pegs)

#define F_G_SEQ (F_G_ALT+4)
    { .t=Actor_T,       .x=F_G_SEQ+1,   .y=NIL,         .z=UNDEF        },  // (peg-seq . <pegs>)
    { .t=VM_msg,        .x=-1,          .y=F_G_SEQ+2,   .z=UNDEF        },  // pegs = args
    { .t=VM_push,       .x=G_SEQ_B,     .y=F_G_SEQ+3,   .z=UNDEF        },  // G_SEQ_B
    { .t=VM_new,        .x=1,           .y=CUST_SEND,   .z=UNDEF        },  // (G_SEQ_B pegs)

#define FX_G_CALL (F_G_SEQ+4)
#define OP_G_CALL (FX_G_CALL+1)
    { .t=Fexpr_T,       .x=OP_G_CALL,   .y=UNDEF,       .z=UNDEF        },  // (peg-call <name>)

    { .t=Actor_T,       .x=OP_G_CALL+1, .y=NIL,         .z=UNDEF        },  // (cust opnds env)
    { .t=VM_msg,        .x=2,           .y=OP_G_CALL+2, .z=UNDEF        },  // opnds
    { .t=VM_nth,        .x=1,           .y=OP_G_CALL+3, .z=UNDEF        },  // name = car(opnds)
    { .t=VM_push,       .x=G_CALL_B,    .y=OP_G_CALL+4, .z=UNDEF        },  // G_CALL_B
    { .t=VM_new,        .x=1,           .y=CUST_SEND,   .z=UNDEF        },  // (G_CALL_B name)

#define F_G_PRED (OP_G_CALL+5)
    { .t=Actor_T,       .x=F_G_PRED+1,  .y=NIL,         .z=UNDEF        },  // (peg-pred <pred> <peg>)
    { .t=VM_msg,        .x=2,           .y=F_G_PRED+2,  .z=UNDEF        },  // pred = arg1
    { .t=VM_msg,        .x=3,           .y=F_G_PRED+3,  .z=UNDEF        },  // peg = arg2
    { .t=VM_push,       .x=G_PRED_B,    .y=F_G_PRED+4,  .z=UNDEF        },  // G_PRED_B
    { .t=VM_new,        .x=2,           .y=CUST_SEND,   .z=UNDEF        },  // (G_PRED_B pred peg)

#define F_G_XFORM (F_G_PRED+5)
    { .t=Actor_T,       .x=F_G_XFORM+1, .y=NIL,         .z=UNDEF        },  // (peg-xform func peg)
    { .t=VM_msg,        .x=2,           .y=F_G_XFORM+2, .z=UNDEF        },  // func = arg1
    { .t=VM_msg,        .x=3,           .y=F_G_XFORM+3, .z=UNDEF        },  // peg = arg2
    { .t=VM_push,       .x=G_XLAT_B,    .y=F_G_XFORM+4, .z=UNDEF        },  // G_XLAT_B
    { .t=VM_new,        .x=2,           .y=CUST_SEND,   .z=UNDEF        },  // (G_XLAT_B func peg)

#define F_S_LIST (F_G_XFORM+5)
    { .t=Actor_T,       .x=F_S_LIST+1,  .y=NIL,         .z=UNDEF        },  // (peg-source <list>)
    { .t=VM_msg,        .x=2,           .y=F_S_LIST+2,  .z=UNDEF        },  // list = arg1
    { .t=VM_push,       .x=S_LIST_B,    .y=F_S_LIST+3,  .z=UNDEF        },  // S_LIST_B
    { .t=VM_new,        .x=1,           .y=CUST_SEND,   .z=UNDEF        },  // src

#define F_G_START (F_S_LIST+4)
    { .t=Actor_T,       .x=F_G_START+1, .y=NIL,         .z=UNDEF        },  // (peg-start <peg> <src>)
    { .t=VM_msg,        .x=1,           .y=F_G_START+2, .z=UNDEF        },  // fail = cust
    { .t=VM_msg,        .x=1,           .y=F_G_START+3, .z=UNDEF        },  // ok = cust
    { .t=VM_pair,       .x=1,           .y=F_G_START+4, .z=UNDEF        },  // custs = (ok . fail)
    { .t=VM_msg,        .x=2,           .y=F_G_START+5, .z=UNDEF        },  // peg = arg1
    { .t=VM_push,       .x=G_START,     .y=F_G_START+6, .z=UNDEF        },  // G_START
    { .t=VM_new,        .x=2,           .y=F_G_START+7, .z=UNDEF        },  // start
    { .t=VM_msg,        .x=3,           .y=SEND_0,      .z=UNDEF        },  // src = arg2

#define F_S_CHAIN (F_G_START+8)
    { .t=Actor_T,       .x=F_S_CHAIN+1, .y=NIL,         .z=UNDEF        },  // (peg-chain <peg> <src>)
    { .t=VM_msg,        .x=2,           .y=F_S_CHAIN+2, .z=UNDEF        },  // peg = arg1
    { .t=VM_msg,        .x=3,           .y=F_S_CHAIN+3, .z=UNDEF        },  // src = arg2
    { .t=VM_push,       .x=S_CHAIN,     .y=F_S_CHAIN+4, .z=UNDEF        },  // S_CHAIN
    { .t=VM_new,        .x=2,           .y=CUST_SEND,   .z=UNDEF        },  // (S_CHAIN peg src)

//
// Pre-defined PEGs
//

/*
(define peg-end (peg-not peg-any))  ; end of input
*/
#define G_END (F_S_CHAIN+5)
#else // !SCM_PEG_TOOLS
#define G_END (S_NEXT_C+10)
#endif // SCM_PEG_TOOLS
    { .t=Actor_T,       .x=G_END+1,     .y=NIL,         .z=UNDEF        },  // (peg-not peg-any)
    { .t=VM_push,       .x=G_ANY,       .y=G_NOT_B,     .z=UNDEF        },

/*
(define lex-eol (peg-eq 10))  ; end of line
*/
#define G_EOL (G_END+2)
    { .t=Actor_T,       .x=G_EOL+1,     .y=NIL,         .z=UNDEF        },  // (peg-eq 10)
    { .t=VM_push,       .x=TO_FIX('\n'),.y=G_EQ_B,      .z=UNDEF        },  // value = '\n' = 10

/*
(define lex-optwsp (peg-star (peg-class WSP)))
*/
#define G_WSP (G_EOL+2)
    { .t=Actor_T,       .x=G_WSP+1,     .y=NIL,         .z=UNDEF        },  // (peg-class WSP)
    { .t=VM_push,       .x=WSP,         .y=G_CLS_B,     .z=UNDEF        },
#define G_WSP_S (G_WSP+2)
    { .t=Actor_T,       .x=G_WSP_S+1,   .y=NIL,         .z=UNDEF        },  // (peg-star (peg-class WSP))
    { .t=VM_push,       .x=G_WSP,       .y=G_STAR_B,    .z=UNDEF        },

/*
(define scm-to-eol (peg-or lex-eol (peg-and peg-any (peg-call scm-to-eol))))
*/
#define G_TO_EOL (G_WSP_S+2)
    { .t=Actor_T,       .x=G_TO_EOL+1,  .y=NIL,         .z=UNDEF        },  // (peg-or <first> <rest>)
    { .t=VM_push,       .x=G_EOL,       .y=G_TO_EOL+2,  .z=UNDEF        },  // first = lex-eol
    { .t=VM_push,       .x=G_TO_EOL+3,  .y=G_OR_B,      .z=UNDEF        },  // rest

    { .t=Actor_T,       .x=G_TO_EOL+4,  .y=NIL,         .z=UNDEF        },  // (peg-and <first> <rest>)
    { .t=VM_push,       .x=G_ANY,       .y=G_TO_EOL+5,  .z=UNDEF        },  // first = peg-any
    { .t=VM_push,       .x=G_TO_EOL,    .y=G_AND_B,     .z=UNDEF        },  // rest = scm-to-eol

/*
(define scm-comment (peg-and (peg-eq 59) scm-to-eol))
*/
#define G_SEMIC (G_TO_EOL+6)
    { .t=Actor_T,       .x=G_SEMIC+1,   .y=NIL,         .z=UNDEF        },  // (peg-eq 59)
    { .t=VM_push,       .x=TO_FIX(';'), .y=G_EQ_B,      .z=UNDEF        },  // value = ';' = 59
#define G_COMMENT (G_SEMIC+2)
    { .t=Actor_T,       .x=G_COMMENT+1, .y=NIL,         .z=UNDEF        },  // (peg-and <first> <rest>)
    { .t=VM_push,       .x=G_SEMIC,     .y=G_COMMENT+2, .z=UNDEF        },  // first = (peg-eq 59)
    { .t=VM_push,       .x=G_TO_EOL,    .y=G_AND_B,     .z=UNDEF        },  // rest = scm-to-eol

/*
(define scm-optwsp (peg-star (peg-or scm-comment (peg-class WSP))))
*/
#define G_OPTWSP (G_COMMENT+3)
    { .t=Actor_T,       .x=G_OPTWSP+1,  .y=NIL,         .z=UNDEF        },  // (peg-star <ptrn>)
    { .t=VM_push,       .x=G_OPTWSP+2,  .y=G_STAR_B,    .z=UNDEF        },  // ptrn

    { .t=Actor_T,       .x=G_OPTWSP+3,  .y=NIL,         .z=UNDEF        },  // (peg-or <first> <rest>)
    { .t=VM_push,       .x=G_COMMENT,   .y=G_OPTWSP+4,  .z=UNDEF        },  // first = scm-comment
    { .t=VM_push,       .x=G_WSP,       .y=G_OR_B,      .z=UNDEF        },  // rest = (peg-class WSP)

/*
(define lex-eot (peg-not (peg-class DGT UPR LWR SYM)))  ; end of token
*/
#define G_PRT (G_OPTWSP+5)
    { .t=Actor_T,       .x=G_PRT+1,     .y=NIL,         .z=UNDEF        },  // (peg-class DGT UPR LWR SYM)
    { .t=VM_push,   .x=DGT|UPR|LWR|SYM, .y=G_CLS_B,     .z=UNDEF        },
#define G_EOT (G_PRT+2)
    { .t=Actor_T,       .x=G_EOT+1,     .y=NIL,         .z=UNDEF        },  // (peg-not (peg-class DGT UPR LWR SYM))
    { .t=VM_push,       .x=G_PRT,       .y=G_NOT_B,     .z=UNDEF        },

/*
(define scm-ignore (peg-xform (lambda _ '_) (peg-and (peg-plus (peg-eq 95)) lex-eot)))
*/
#define G_UNDER (G_EOT+2)
    { .t=Actor_T,       .x=G_UNDER+1,   .y=NIL,         .z=UNDEF        },  // (peg-eq 95)
    { .t=VM_push,       .x=TO_FIX('_'), .y=G_EQ_B,      .z=UNDEF        },  // value = '_' = 95
#define F_IGN (G_UNDER+2)
    { .t=Actor_T,       .x=F_IGN+1,     .y=NIL,         .z=UNDEF        },  // (lambda _ '_)
    { .t=VM_push,       .x=S_IGNORE,    .y=CUST_SEND,   .z=UNDEF        },
#define G_IGN (F_IGN+2)
    { .t=Actor_T,       .x=G_IGN+1,     .y=NIL,         .z=UNDEF        },  // (peg-xform (lambda _ '_) <ptrn>)
    { .t=VM_push,       .x=F_IGN,       .y=G_IGN+2,     .z=UNDEF        },  // func = F_IGN
    { .t=VM_push,       .x=G_IGN+3,     .y=G_XLAT_B,    .z=UNDEF        },  // ptrn = ...

    { .t=Actor_T,       .x=G_IGN+4,     .y=NIL,         .z=UNDEF        },  // (peg-and <first> <rest>)
    { .t=VM_push,       .x=G_IGN+6,     .y=G_IGN+5,     .z=UNDEF        },  // first = (peg-plus (peg-eq 95))
    { .t=VM_push,       .x=G_EOT,       .y=G_AND_B,     .z=UNDEF        },  // rest = lex-eot

    { .t=Actor_T,       .x=G_IGN+7,     .y=NIL,         .z=UNDEF        },  // (peg-plus (peg-eq 95))
    { .t=VM_push,       .x=G_UNDER,     .y=G_PLUS_B,    .z=UNDEF        },  // ptrn = (peg-eq 95)

/*
(define scm-const (peg-xform cadr (peg-seq
  (peg-eq 35)
  (peg-alt
    (peg-xform (lambda _ #f) (peg-eq 102))
    (peg-xform (lambda _ #t) (peg-eq 116))
    (peg-xform (lambda _ #?) (peg-eq 63))
    (peg-xform (lambda _ #unit) (peg-seq (peg-eq 117) (peg-eq 110) (peg-eq 105) (peg-eq 116))))
  lex-eot)))
*/
#define G_HASH (G_IGN+8)
    { .t=Actor_T,       .x=G_HASH+1,    .y=NIL,         .z=UNDEF        },  // (peg-eq 35)
    { .t=VM_push,       .x=TO_FIX('#'), .y=G_EQ_B,      .z=UNDEF        },  // value = '#' = 35
#define G_LWR_U (G_HASH+2)
    { .t=Actor_T,       .x=G_LWR_U+1,   .y=NIL,         .z=UNDEF        },  // (peg-eq 117)
    { .t=VM_push,       .x=TO_FIX('u'), .y=G_EQ_B,      .z=UNDEF        },  // value = 'u' = 117
#define G_LWR_N (G_LWR_U+2)
    { .t=Actor_T,       .x=G_LWR_N+1,   .y=NIL,         .z=UNDEF        },  // (peg-eq 110)
    { .t=VM_push,       .x=TO_FIX('n'), .y=G_EQ_B,      .z=UNDEF        },  // value = 'n' = 110
#define G_LWR_I (G_LWR_N+2)
    { .t=Actor_T,       .x=G_LWR_I+1,   .y=NIL,         .z=UNDEF        },  // (peg-eq 105)
    { .t=VM_push,       .x=TO_FIX('i'), .y=G_EQ_B,      .z=UNDEF        },  // value = 'i' = 105
#define G_LWR_T (G_LWR_I+2)
    { .t=Actor_T,       .x=G_LWR_T+1,   .y=NIL,         .z=UNDEF        },  // (peg-eq 116)
    { .t=VM_push,       .x=TO_FIX('t'), .y=G_EQ_B,      .z=UNDEF        },  // value = 't' = 116
#define G_LWR_F (G_LWR_T+2)
    { .t=Actor_T,       .x=G_LWR_F+1,   .y=NIL,         .z=UNDEF        },  // (peg-eq 102)
    { .t=VM_push,       .x=TO_FIX('f'), .y=G_EQ_B,      .z=UNDEF        },  // value = 'f' = 102
#define G_QMARK (G_LWR_F+2)
    { .t=Actor_T,       .x=G_QMARK+1,   .y=NIL,         .z=UNDEF        },  // (peg-eq 63)
    { .t=VM_push,       .x=TO_FIX('?'), .y=G_EQ_B,      .z=UNDEF        },  // value = '?' = 63

#define F_FALSE (G_QMARK+2)
    { .t=Actor_T,       .x=RV_FALSE,    .y=NIL,         .z=UNDEF        },  // (lambda _ #f)
#define G_FALSE (F_FALSE+1)
    { .t=Actor_T,       .x=G_FALSE+1,   .y=NIL,         .z=UNDEF        },  // (peg-xform (lambda _ #f) (peg-eq 102))
    { .t=VM_push,       .x=F_FALSE,     .y=G_FALSE+2,   .z=UNDEF        },  // func = F_FALSE
    { .t=VM_push,       .x=G_LWR_F,     .y=G_XLAT_B,    .z=UNDEF        },  // ptrn = (peg-eq 102)

#define F_TRUE (G_FALSE+3)
    { .t=Actor_T,       .x=RV_TRUE,     .y=NIL,         .z=UNDEF        },  // (lambda _ #t)
#define G_TRUE (F_TRUE+1)
    { .t=Actor_T,       .x=G_TRUE+1,    .y=NIL,         .z=UNDEF        },  // (peg-xform (lambda _ #t) (peg-eq 116))
    { .t=VM_push,       .x=F_TRUE,      .y=G_TRUE+2,    .z=UNDEF        },  // func = F_TRUE
    { .t=VM_push,       .x=G_LWR_T,     .y=G_XLAT_B,    .z=UNDEF        },  // ptrn = (peg-eq 116)

#define F_UNDEF (G_TRUE+3)
    { .t=Actor_T,       .x=RV_UNDEF,    .y=NIL,         .z=UNDEF        },  // (lambda _ #?)
#define G_UNDEF (F_UNDEF+1)
    { .t=Actor_T,       .x=G_UNDEF+1,   .y=NIL,         .z=UNDEF        },  // (peg-xform (lambda _ #?) (peg-eq 63))
    { .t=VM_push,       .x=F_UNDEF,     .y=G_UNDEF+2,   .z=UNDEF        },  // func = F_UNDEF
    { .t=VM_push,       .x=G_QMARK,     .y=G_XLAT_B,    .z=UNDEF        },  // ptrn = G_QMARK

#define F_UNIT (G_UNDEF+3)
    { .t=Actor_T,       .x=RV_UNIT,     .y=NIL,         .z=UNDEF        },  // (lambda _ #unit)
#define G_UNIT (F_UNIT+1)
    { .t=Actor_T,       .x=G_UNIT+1,    .y=NIL,         .z=UNDEF        },  // (peg-xform (lambda _ #unit) <ptrn>)
    { .t=VM_push,       .x=F_UNIT,      .y=G_UNIT+2,    .z=UNDEF        },  // func = F_UNIT
    { .t=VM_push,       .x=G_UNIT+3,    .y=G_XLAT_B,    .z=UNDEF        },  // ptrn = (peg-seq (peg-eq 117) (peg-eq 110) (peg-eq 105) (peg-eq 116))

    { .t=Actor_T,       .x=G_UNIT+4,    .y=NIL,         .z=UNDEF        },  // (peg-and <first> <rest>)
    { .t=VM_push,       .x=G_LWR_U,     .y=G_UNIT+5,    .z=UNDEF        },  // first = (peg-eq 117)
    { .t=VM_push,       .x=G_UNIT+6,    .y=G_AND_B,     .z=UNDEF        },  // rest

    { .t=Actor_T,       .x=G_UNIT+7,    .y=NIL,         .z=UNDEF        },  // (peg-and <first> <rest>)
    { .t=VM_push,       .x=G_LWR_N,     .y=G_UNIT+8,    .z=UNDEF        },  // first = (peg-eq 110)
    { .t=VM_push,       .x=G_UNIT+9,    .y=G_AND_B,     .z=UNDEF        },  // rest

    { .t=Actor_T,       .x=G_UNIT+10,   .y=NIL,         .z=UNDEF        },  // (peg-and <first> <rest>)
    { .t=VM_push,       .x=G_LWR_I,     .y=G_UNIT+11,   .z=UNDEF        },  // first = (peg-eq 105)
    { .t=VM_push,       .x=G_LWR_T,     .y=G_AND_B,     .z=UNDEF        },  // rest = (peg-eq 116)

#define G_CONST (G_UNIT+12)
    { .t=Actor_T,       .x=G_CONST+1,   .y=NIL,         .z=UNDEF        },  // (peg-xform cadr <ptrn>)
    { .t=VM_push,       .x=F_CADR,      .y=G_CONST+2,   .z=UNDEF        },  // func = F_CADR
    { .t=VM_push,       .x=G_CONST+3,   .y=G_XLAT_B,    .z=UNDEF        },  // ptrn = (peg-seq (peg-eq 35) (peg-alt ...) lex-eot)

    { .t=Actor_T,       .x=G_CONST+4,   .y=NIL,         .z=UNDEF        },  // (peg-and <first> <rest>)
    { .t=VM_push,       .x=G_HASH,      .y=G_CONST+5,   .z=UNDEF        },  // first = (peg-eq 35)
    { .t=VM_push,       .x=G_CONST+6,   .y=G_AND_B,     .z=UNDEF        },  // rest

    { .t=Actor_T,       .x=G_CONST+7,   .y=NIL,         .z=UNDEF        },  // (peg-and <first> <rest>)
    { .t=VM_push,       .x=G_CONST+9,   .y=G_CONST+8,   .z=UNDEF        },  // first = (peg-alt G_FALSE G_TRUE G_UNDEF G_UNIT)
    { .t=VM_push,       .x=G_EOT,       .y=G_AND_B,     .z=UNDEF        },  // rest = lex-eot

    { .t=Actor_T,       .x=G_CONST+10,  .y=NIL,         .z=UNDEF        },  // (peg-or <first> <rest>)
    { .t=VM_push,       .x=G_FALSE,     .y=G_CONST+11,  .z=UNDEF        },  // first = G_FALSE
    { .t=VM_push,       .x=G_CONST+12,  .y=G_OR_B,      .z=UNDEF        },  // rest

    { .t=Actor_T,       .x=G_CONST+13,  .y=NIL,         .z=UNDEF        },  // (peg-or <first> <rest>)
    { .t=VM_push,       .x=G_TRUE,      .y=G_CONST+14,  .z=UNDEF        },  // first = G_TRUE
    { .t=VM_push,       .x=G_CONST+15,  .y=G_OR_B,      .z=UNDEF        },  // rest

    { .t=Actor_T,       .x=G_CONST+16,  .y=NIL,         .z=UNDEF        },  // (peg-or <first> <rest>)
    { .t=VM_push,       .x=G_UNDEF,     .y=G_CONST+17,  .z=UNDEF        },  // first = G_UNDEF
    { .t=VM_push,       .x=G_UNIT,      .y=G_OR_B,      .z=UNDEF        },  // rest = G_UNIT

/*
(define lex-sign (peg-or (peg-eq 45) (peg-eq 43)))  ; [-+]
*/
#define G_M_SGN (G_CONST+18)
    { .t=Actor_T,       .x=G_M_SGN+1,   .y=NIL,         .z=UNDEF        },  // (peg-eq 45)
    { .t=VM_push,       .x=TO_FIX('-'), .y=G_EQ_B,      .z=UNDEF        },  // value = '-' = 45
#define G_P_SGN (G_M_SGN+2)
    { .t=Actor_T,       .x=G_P_SGN+1,   .y=NIL,         .z=UNDEF        },  // (peg-eq 43)
    { .t=VM_push,       .x=TO_FIX('+'), .y=G_EQ_B,      .z=UNDEF        },  // value = '+' = 43
#define G_SIGN (G_P_SGN+2)
    { .t=Actor_T,       .x=G_SIGN+1,    .y=NIL,         .z=UNDEF        },  // (peg-or <first> <rest>)
    { .t=VM_push,       .x=G_M_SGN,     .y=G_SIGN+2,    .z=UNDEF        },  // first = (peg-eq 45)
    { .t=VM_push,       .x=G_P_SGN,     .y=G_OR_B,      .z=UNDEF        },  // rest = (peg-eq 43)

/*
(define lex-digit (peg-or (peg-class DGT) (peg-eq 95)))  ; [0-9_]
*/
#define G_DGT (G_SIGN+3)
    { .t=Actor_T,       .x=G_DGT+1,     .y=NIL,         .z=UNDEF        },  // (peg-class DGT)
    { .t=VM_push,       .x=DGT,         .y=G_CLS_B,     .z=UNDEF        },  // class = [0-9]
#define G_DIGIT (G_DGT+2)
    { .t=Actor_T,       .x=G_DIGIT+1,   .y=NIL,         .z=UNDEF        },  // (peg-or <first> <rest>)
    { .t=VM_push,       .x=G_DGT,       .y=G_DIGIT+2,   .z=UNDEF        },  // first = (peg-class DGT)
    { .t=VM_push,       .x=G_UNDER,     .y=G_OR_B,      .z=UNDEF        },  // rest = (peg-eq 95)

/*
(define lex-digits (peg-xform car (peg-and (peg-plus lex-digit) lex-eot)))
*/
#define G_DIGITS (G_DIGIT+3)
    { .t=Actor_T,       .x=G_DIGITS+1,  .y=NIL,         .z=UNDEF        },  // (peg-xform car <ptrn>)
    { .t=VM_push,       .x=F_CAR,       .y=G_DIGITS+2,  .z=UNDEF        },  // func = F_CAR
    { .t=VM_push,       .x=G_DIGITS+3,  .y=G_XLAT_B,    .z=UNDEF        },  // ptrn = (peg-and (peg-plus lex-digit) lex-eot)

    { .t=Actor_T,       .x=G_DIGITS+4,  .y=NIL,         .z=UNDEF        },  // (peg-and <first> <rest>)
    { .t=VM_push,       .x=G_DIGITS+6,  .y=G_DIGITS+5,  .z=UNDEF        },  // first = (peg-plus lex-digit)
    { .t=VM_push,       .x=G_EOT,       .y=G_AND_B,     .z=UNDEF        },  // rest = lex-eot

    { .t=Actor_T,       .x=G_DIGITS+7,  .y=NIL,         .z=UNDEF        },  // (peg-plus <ptrn>)
    { .t=VM_push,       .x=G_DIGIT,     .y=G_PLUS_B,    .z=UNDEF        },  // ptrn = lex-digit

/*
(define lex-number (peg-xform list->number (peg-or (peg-and lex-sign lex-digits) lex-digits)))
*/
#define G_NUMBER (G_DIGITS+8)
    { .t=Actor_T,       .x=G_NUMBER+1,  .y=NIL,         .z=UNDEF        },  // (peg-xform list->number <ptrn>)
    { .t=VM_push,       .x=F_LST_NUM,   .y=G_NUMBER+2,  .z=UNDEF        },  // func = F_LST_NUM
    { .t=VM_push,       .x=G_NUMBER+3,  .y=G_XLAT_B,    .z=UNDEF        },  // ptrn = (peg-or (peg-and lex-sign lex-digits) lex-digits)

    { .t=Actor_T,       .x=G_NUMBER+4,  .y=NIL,         .z=UNDEF        },  // (peg-or <first> <rest>)
    { .t=VM_push,       .x=G_NUMBER+6,  .y=G_NUMBER+5,  .z=UNDEF        },  // first = (peg-and lex-sign lex-digits)
    { .t=VM_push,       .x=G_DIGITS,    .y=G_OR_B,      .z=UNDEF        },  // rest = lex-digits

    { .t=Actor_T,       .x=G_NUMBER+7,  .y=NIL,         .z=UNDEF        },  // (peg-and <first> <rest>)
    { .t=VM_push,       .x=G_SIGN,      .y=G_NUMBER+8,  .z=UNDEF        },  // first = lex-sign
    { .t=VM_push,       .x=G_DIGITS,    .y=G_AND_B,     .z=UNDEF        },  // rest = lex-digits

/*
(define scm-symbol (peg-xform list->symbol (peg-plus (peg-class DGT UPR LWR SYM))))
*/
#define G_SYMBOL (G_NUMBER+9)
    { .t=Actor_T,       .x=G_SYMBOL+1,  .y=NIL,         .z=UNDEF        },  // (peg-xform list->symbol <ptrn>)
    { .t=VM_push,       .x=F_LST_SYM,   .y=G_SYMBOL+2,  .z=UNDEF        },  // func = F_LST_SYM
    { .t=VM_push,       .x=G_SYMBOL+3,  .y=G_XLAT_B,    .z=UNDEF        },  // ptrn = (peg-plus (peg-class DGT UPR LWR SYM))

    { .t=Actor_T,       .x=G_SYMBOL+4,  .y=NIL,         .z=UNDEF        },  // (peg-plus <ptrn>)
    { .t=VM_push,       .x=G_PRT,       .y=G_PLUS_B,    .z=UNDEF        },  // ptrn = (peg-class DGT UPR LWR SYM)

#define G_OPEN (G_SYMBOL+5)
    { .t=Actor_T,       .x=G_OPEN+1,    .y=NIL,         .z=UNDEF        },  // (peg-eq 40)
    { .t=VM_push,       .x=TO_FIX('('), .y=G_EQ_B,      .z=UNDEF        },  // value = '(' = 40
#define G_DOT (G_OPEN+2)
    { .t=Actor_T,       .x=G_DOT+1,     .y=NIL,         .z=UNDEF        },  // (peg-eq 46)
    { .t=VM_push,       .x=TO_FIX('.'), .y=G_EQ_B,      .z=UNDEF        },  // value = '.' = 46
#define G_CLOSE (G_DOT+2)
    { .t=Actor_T,       .x=G_CLOSE+1,   .y=NIL,         .z=UNDEF        },  // (peg-eq 41)
    { .t=VM_push,       .x=TO_FIX(')'), .y=G_EQ_B,      .z=UNDEF        },  // value = ')' = 41
#define G_QUOTE (G_CLOSE+2)
    { .t=Actor_T,       .x=G_QUOTE+1,   .y=NIL,         .z=UNDEF        },  // (peg-eq 39)
    { .t=VM_push,       .x=TO_FIX('\''),.y=G_EQ_B,      .z=UNDEF        },  // value = '\'' = 39
#define G_BQUOTE (G_QUOTE+2)
    { .t=Actor_T,       .x=G_BQUOTE+1,  .y=NIL,         .z=UNDEF        },  // (peg-eq 96)
    { .t=VM_push,       .x=TO_FIX('`'), .y=G_EQ_B,      .z=UNDEF        },  // value = '`' = 96
#define G_COMMA (G_BQUOTE+2)
    { .t=Actor_T,       .x=G_COMMA+1,   .y=NIL,         .z=UNDEF        },  // (peg-eq 44)
    { .t=VM_push,       .x=TO_FIX(','), .y=G_EQ_B,      .z=UNDEF        },  // value = ',' = 44
#define G_AT (G_COMMA+2)
    { .t=Actor_T,       .x=G_AT+1,      .y=NIL,         .z=UNDEF        },  // (peg-eq 64)
    { .t=VM_push,       .x=TO_FIX('@'), .y=G_EQ_B,      .z=UNDEF        },  // value = '@' = 64

/*
(define scm-quoted (peg-alt
  (peg-xform (lambda (x) (list 'quote (cdr x)))
    (peg-and (peg-eq 39) (peg-call scm-expr)))
  (peg-xform (lambda (x) (list 'quasiquote (cdr x)))
    (peg-and (peg-eq 96) (peg-call scm-expr)))
  (peg-xform (lambda (x) (list 'unquote-splicing (cddr x)))
    (peg-and (peg-eq 44) (peg-and (peg-eq 64) (peg-call scm-expr))))
  (peg-xform (lambda (x) (list 'unquote (cdr x)))
    (peg-and (peg-eq 44) (peg-call scm-expr)))
  ))
*/
#define F_QUOTED (G_AT+2)
    { .t=Actor_T,       .x=F_QUOTED+1,  .y=NIL,         .z=UNDEF        },  // (lambda (x) (list 'quote (cdr x)))
    { .t=VM_push,       .x=NIL,         .y=F_QUOTED+2,  .z=UNDEF        },  // ()
    { .t=VM_msg,        .x=2,           .y=F_QUOTED+3,  .z=UNDEF        },  // arg1
    { .t=VM_nth,        .x=-1,          .y=F_QUOTED+4,  .z=UNDEF        },  // value = cdr(arg1)
    { .t=VM_push,       .x=S_QUOTE,     .y=F_QUOTED+5,  .z=UNDEF        },  // S_QUOTE
    { .t=VM_pair,       .x=2,           .y=CUST_SEND,   .z=UNDEF        },  // (S_QUOTE value)
#define F_QQUOTED (F_QUOTED+6)
    { .t=Actor_T,       .x=F_QQUOTED+1, .y=NIL,         .z=UNDEF        },  // (lambda (x) (list 'quasiquote (cdr x)))
    { .t=VM_push,       .x=NIL,         .y=F_QQUOTED+2, .z=UNDEF        },  // ()
    { .t=VM_msg,        .x=2,           .y=F_QQUOTED+3, .z=UNDEF        },  // arg1
    { .t=VM_nth,        .x=-1,          .y=F_QQUOTED+4, .z=UNDEF        },  // value = cdr(arg1)
    { .t=VM_push,       .x=S_QQUOTE,    .y=F_QQUOTED+5, .z=UNDEF        },  // S_QQUOTE
    { .t=VM_pair,       .x=2,           .y=CUST_SEND,   .z=UNDEF        },  // (S_QQUOTE value)
#define F_UNQUOTED (F_QQUOTED+6)
    { .t=Actor_T,       .x=F_UNQUOTED+1,.y=NIL,         .z=UNDEF        },  // (lambda (x) (list 'unquote (cdr x)))
    { .t=VM_push,       .x=NIL,         .y=F_UNQUOTED+2,.z=UNDEF        },  // ()
    { .t=VM_msg,        .x=2,           .y=F_UNQUOTED+3,.z=UNDEF        },  // arg1
    { .t=VM_nth,        .x=-1,          .y=F_UNQUOTED+4,.z=UNDEF        },  // value = cdr(arg1)
    { .t=VM_push,       .x=S_UNQUOTE,   .y=F_UNQUOTED+5,.z=UNDEF        },  // S_UNQUOTE
    { .t=VM_pair,       .x=2,           .y=CUST_SEND,   .z=UNDEF        },  // (S_UNQUOTE value)
#define F_QSPLICED (F_UNQUOTED+6)
    { .t=Actor_T,       .x=F_QSPLICED+1,.y=NIL,         .z=UNDEF        },  // (lambda (x) (list 'unquote-splicing (cddr x)))
    { .t=VM_push,       .x=NIL,         .y=F_QSPLICED+2,.z=UNDEF        },  // ()
    { .t=VM_msg,        .x=2,           .y=F_QSPLICED+3,.z=UNDEF        },  // arg1
    { .t=VM_nth,        .x=-2,          .y=F_QSPLICED+4,.z=UNDEF        },  // value = cddr(arg1)
    { .t=VM_push,       .x=S_QSPLICE,   .y=F_QSPLICED+5,.z=UNDEF        },  // S_QSPLICE
    { .t=VM_pair,       .x=2,           .y=CUST_SEND,   .z=UNDEF        },  // (S_QSPLICE value)
#define F_NIL (F_QSPLICED+6)
    { .t=Actor_T,       .x=RV_NIL,      .y=NIL,         .z=UNDEF        },  // (lambda _ ())

#define G_QUOTED (F_NIL+1)
#define G_DOTTED (G_QUOTED+36)
#define G_TAIL (G_DOTTED+15)
#define G_LIST (G_TAIL+18)
#define G_EXPR (G_LIST+6)
#define G_SEXPR (G_EXPR+15)
    { .t=Actor_T,       .x=G_QUOTED+1,  .y=NIL,         .z=UNDEF        },  // (peg-or <first> <rest>)
    { .t=VM_push,       .x=G_QUOTED+3,  .y=G_QUOTED+2,  .z=UNDEF        },  // first
    { .t=VM_push,       .x=G_QUOTED+9,  .y=G_OR_B,      .z=UNDEF        },  // rest

    { .t=Actor_T,       .x=G_QUOTED+4,  .y=NIL,         .z=UNDEF        },  // (peg-xform <func> <ptrn>)
    { .t=VM_push,       .x=F_QUOTED,    .y=G_QUOTED+5,  .z=UNDEF        },  // func
    { .t=VM_push,       .x=G_QUOTED+6,  .y=G_XLAT_B,    .z=UNDEF        },  // ptrn

    { .t=Actor_T,       .x=G_QUOTED+7,  .y=NIL,         .z=UNDEF        },  // (peg-and <first> <rest>)
    { .t=VM_push,       .x=G_QUOTE,     .y=G_QUOTED+8,  .z=UNDEF        },  // first = (peg-eq 39)
    { .t=VM_push,       .x=G_SEXPR,     .y=G_AND_B,     .z=UNDEF        },  // rest = scm-expr

    { .t=Actor_T,       .x=G_QUOTED+10, .y=NIL,         .z=UNDEF        },  // (peg-or <first> <rest>)
    { .t=VM_push,       .x=G_QUOTED+12, .y=G_QUOTED+11, .z=UNDEF        },  // first
    { .t=VM_push,       .x=G_QUOTED+18, .y=G_OR_B,      .z=UNDEF        },  // rest

    { .t=Actor_T,       .x=G_QUOTED+13, .y=NIL,         .z=UNDEF        },  // (peg-xform <func> <ptrn>)
    { .t=VM_push,       .x=F_QQUOTED,   .y=G_QUOTED+14, .z=UNDEF        },  // func
    { .t=VM_push,       .x=G_QUOTED+15, .y=G_XLAT_B,    .z=UNDEF        },  // ptrn

    { .t=Actor_T,       .x=G_QUOTED+16, .y=NIL,         .z=UNDEF        },  // (peg-and <first> <rest>)
    { .t=VM_push,       .x=G_BQUOTE,    .y=G_QUOTED+17, .z=UNDEF        },  // first = (peg-eq 96)
    { .t=VM_push,       .x=G_SEXPR,     .y=G_AND_B,     .z=UNDEF        },  // rest = scm-expr

    { .t=Actor_T,       .x=G_QUOTED+19, .y=NIL,         .z=UNDEF        },  // (peg-or <first> <rest>)
    { .t=VM_push,       .x=G_QUOTED+21, .y=G_QUOTED+20, .z=UNDEF        },  // first
    { .t=VM_push,       .x=G_QUOTED+30, .y=G_OR_B,      .z=UNDEF        },  // rest

    { .t=Actor_T,       .x=G_QUOTED+22, .y=NIL,         .z=UNDEF        },  // (peg-xform <func> <ptrn>)
    { .t=VM_push,       .x=F_QSPLICED,  .y=G_QUOTED+23, .z=UNDEF        },  // func
    { .t=VM_push,       .x=G_QUOTED+24, .y=G_XLAT_B,    .z=UNDEF        },  // ptrn

    { .t=Actor_T,       .x=G_QUOTED+25, .y=NIL,         .z=UNDEF        },  // (peg-and <first> <rest>)
    { .t=VM_push,       .x=G_COMMA,     .y=G_QUOTED+26, .z=UNDEF        },  // first = (peg-eq 44)
    { .t=VM_push,       .x=G_QUOTED+27, .y=G_AND_B,     .z=UNDEF        },  // rest

    { .t=Actor_T,       .x=G_QUOTED+28, .y=NIL,         .z=UNDEF        },  // (peg-and <first> <rest>)
    { .t=VM_push,       .x=G_AT,        .y=G_QUOTED+29, .z=UNDEF        },  // first = (peg-eq 64)
    { .t=VM_push,       .x=G_SEXPR,     .y=G_AND_B,     .z=UNDEF        },  // rest = scm-expr

    { .t=Actor_T,       .x=G_QUOTED+31, .y=NIL,         .z=UNDEF        },  // (peg-xform <func> <ptrn>)
    { .t=VM_push,       .x=F_UNQUOTED,  .y=G_QUOTED+32, .z=UNDEF        },  // func
    { .t=VM_push,       .x=G_QUOTED+33, .y=G_XLAT_B,    .z=UNDEF        },  // ptrn

    { .t=Actor_T,       .x=G_QUOTED+34, .y=NIL,         .z=UNDEF        },  // (peg-and <first> <rest>)
    { .t=VM_push,       .x=G_COMMA,     .y=G_QUOTED+35, .z=UNDEF        },  // first = (peg-eq 44)
    { .t=VM_push,       .x=G_SEXPR,     .y=G_AND_B,     .z=UNDEF        },  // rest = scm-expr

/*
(define scm-dotted (peg-xform caddr
  (peg-seq scm-optwsp (peg-eq 46) (peg-call scm-sexpr) scm-optwsp (peg-eq 41))))
*/
    { .t=Actor_T,       .x=G_DOTTED+1,  .y=NIL,         .z=UNDEF        },  // (peg-xform <func> <ptrn>)
    { .t=VM_push,       .x=F_CADDR,     .y=G_DOTTED+2,  .z=UNDEF        },  // func = caddr
    { .t=VM_push,       .x=G_DOTTED+3,  .y=G_XLAT_B,    .z=UNDEF        },  // ptrn

    { .t=Actor_T,       .x=G_DOTTED+4,  .y=NIL,         .z=UNDEF        },  // (peg-and <first> <rest>)
    { .t=VM_push,       .x=G_OPTWSP,    .y=G_DOTTED+5,  .z=UNDEF        },  // first = scm-optwsp
    { .t=VM_push,       .x=G_DOTTED+6,  .y=G_AND_B,     .z=UNDEF        },  // rest

    { .t=Actor_T,       .x=G_DOTTED+7,  .y=NIL,         .z=UNDEF        },  // (peg-and <first> <rest>)
    { .t=VM_push,       .x=G_DOT,       .y=G_DOTTED+8,  .z=UNDEF        },  // first = (peg-eq 46)
    { .t=VM_push,       .x=G_DOTTED+9,  .y=G_AND_B,     .z=UNDEF        },  // rest

    { .t=Actor_T,       .x=G_DOTTED+10, .y=NIL,         .z=UNDEF        },  // (peg-and <first> <rest>)
    { .t=VM_push,       .x=G_SEXPR,     .y=G_DOTTED+11, .z=UNDEF        },  // first = scm-sexpr
    { .t=VM_push,       .x=G_DOTTED+12, .y=G_AND_B,     .z=UNDEF        },  // rest

    { .t=Actor_T,       .x=G_DOTTED+13, .y=NIL,         .z=UNDEF        },  // (peg-and <first> <rest>)
    { .t=VM_push,       .x=G_OPTWSP,    .y=G_DOTTED+14, .z=UNDEF        },  // first = scm-optwsp
    { .t=VM_push,       .x=G_CLOSE,     .y=G_AND_B,     .z=UNDEF        },  // rest = (peg-eq 41)

/*
(define scm-tail (peg-xform cdr (peg-and
  scm-optwsp
  (peg-or
    (peg-xform (lambda _ ()) (peg-eq 41))
    (peg-and
      (peg-call scm-expr)
      (peg-or scm-dotted (peg-call scm-tail)) )) )))
*/
    { .t=Actor_T,       .x=G_TAIL+1,    .y=NIL,         .z=UNDEF        },  // (peg-xform <func> <ptrn>)
    { .t=VM_push,       .x=F_CDR,       .y=G_TAIL+2,    .z=UNDEF        },  // func = cdr
    { .t=VM_push,       .x=G_TAIL+3,    .y=G_XLAT_B,    .z=UNDEF        },  // ptrn

    { .t=Actor_T,       .x=G_TAIL+4,    .y=NIL,         .z=UNDEF        },  // (peg-and <first> <rest>)
    { .t=VM_push,       .x=G_OPTWSP,    .y=G_TAIL+5,    .z=UNDEF        },  // first = scm-optwsp
    { .t=VM_push,       .x=G_TAIL+6,    .y=G_AND_B,     .z=UNDEF        },  // rest

    { .t=Actor_T,       .x=G_TAIL+7,    .y=NIL,         .z=UNDEF        },  // (peg-or <first> <rest>)
    { .t=VM_push,       .x=G_TAIL+9,    .y=G_TAIL+8,    .z=UNDEF        },  // first = (peg-xform ...)
    { .t=VM_push,       .x=G_TAIL+12,   .y=G_OR_B,      .z=UNDEF        },  // rest

    { .t=Actor_T,       .x=G_TAIL+10,   .y=NIL,         .z=UNDEF        },  // (peg-xform <func> <ptrn>)
    { .t=VM_push,       .x=F_NIL,       .y=G_TAIL+11,   .z=UNDEF        },  // func = (lambda _ ())
    { .t=VM_push,       .x=G_CLOSE,     .y=G_XLAT_B,    .z=UNDEF        },  // ptrn = (peg-eq 41)

    { .t=Actor_T,       .x=G_TAIL+13,   .y=NIL,         .z=UNDEF        },  // (peg-and <first> <rest>)
    { .t=VM_push,       .x=G_EXPR,      .y=G_TAIL+14,   .z=UNDEF        },  // first = scm-expr
    { .t=VM_push,       .x=G_TAIL+15,   .y=G_AND_B,     .z=UNDEF        },  // rest

    { .t=Actor_T,       .x=G_TAIL+16,   .y=NIL,         .z=UNDEF        },  // (peg-or <first> <rest>)
    { .t=VM_push,       .x=G_DOTTED,    .y=G_TAIL+17,   .z=UNDEF        },  // first = scm-dotted
    { .t=VM_push,       .x=G_TAIL,      .y=G_OR_B,      .z=UNDEF        },  // rest = scm-tail

/*
(define scm-list (peg-xform cdr (peg-and (peg-eq 40) scm-tail)))
*/
    { .t=Actor_T,       .x=G_LIST+1,    .y=NIL,         .z=UNDEF        },  // (peg-xform <func> <ptrn>)
    { .t=VM_push,       .x=F_CDR,       .y=G_LIST+2,    .z=UNDEF        },  // func = cdr
    { .t=VM_push,       .x=G_LIST+3,    .y=G_XLAT_B,    .z=UNDEF        },  // ptrn

    { .t=Actor_T,       .x=G_LIST+4,    .y=NIL,         .z=UNDEF        },  // (peg-and <first> <rest>)
    { .t=VM_push,       .x=G_OPEN,      .y=G_LIST+5,    .z=UNDEF        },  // first = (peg-eq 40)
    { .t=VM_push,       .x=G_TAIL,      .y=G_AND_B,     .z=UNDEF        },  // rest = scm-tail

/*
(define scm-expr (peg-alt scm-list scm-ignore scm-const lex-number scm-symbol scm-quoted))
*/
    { .t=Actor_T,       .x=G_EXPR+1,    .y=NIL,         .z=UNDEF        },  // (peg-or <first> <rest>)
    { .t=VM_push,       .x=G_LIST,      .y=G_EXPR+2,    .z=UNDEF        },  // first = scm-list
    { .t=VM_push,       .x=G_EXPR+3,    .y=G_OR_B,      .z=UNDEF        },  // rest

    { .t=Actor_T,       .x=G_EXPR+4,    .y=NIL,         .z=UNDEF        },  // (peg-or <first> <rest>)
    { .t=VM_push,       .x=G_IGN,       .y=G_EXPR+5,    .z=UNDEF        },  // first = scm-ignore
    { .t=VM_push,       .x=G_EXPR+6,    .y=G_OR_B,      .z=UNDEF        },  // rest

    { .t=Actor_T,       .x=G_EXPR+7,    .y=NIL,         .z=UNDEF        },  // (peg-or <first> <rest>)
    { .t=VM_push,       .x=G_CONST,     .y=G_EXPR+8,    .z=UNDEF        },  // first = scm-const
    { .t=VM_push,       .x=G_EXPR+9,    .y=G_OR_B,      .z=UNDEF        },  // rest

    { .t=Actor_T,       .x=G_EXPR+10,   .y=NIL,         .z=UNDEF        },  // (peg-or <first> <rest>)
    { .t=VM_push,       .x=G_NUMBER,    .y=G_EXPR+11,   .z=UNDEF        },  // first = lex-number
    { .t=VM_push,       .x=G_EXPR+12,   .y=G_OR_B,      .z=UNDEF        },  // rest

    { .t=Actor_T,       .x=G_EXPR+13,   .y=NIL,         .z=UNDEF        },  // (peg-or <first> <rest>)
    { .t=VM_push,       .x=G_SYMBOL,    .y=G_EXPR+14,   .z=UNDEF        },  // first = scm-symbol
    { .t=VM_push,       .x=G_QUOTED,    .y=G_OR_B,      .z=UNDEF        },  // rest = scm-quoted

/*
(define scm-sexpr (peg-xform cdr (peg-and scm-optwsp scm-expr)))
*/
    { .t=Actor_T,       .x=G_SEXPR+1,   .y=NIL,         .z=UNDEF        },  // (peg-xform <func> <ptrn>)
    { .t=VM_push,       .x=F_CDR,       .y=G_SEXPR+2,   .z=UNDEF        },  // func = cdr
    { .t=VM_push,       .x=G_SEXPR+3,   .y=G_XLAT_B,    .z=UNDEF        },  // ptrn

    { .t=Actor_T,       .x=G_SEXPR+4,   .y=NIL,         .z=UNDEF        },  // (peg-and <first> <rest>)
    { .t=VM_push,       .x=G_OPTWSP,    .y=G_SEXPR+5,   .z=UNDEF        },  // first = scm-optwsp
    { .t=VM_push,       .x=G_EXPR,      .y=G_AND_B,     .z=UNDEF        },  // rest = scm-expr

#define S_EMPTY (G_SEXPR+6)
    { .t=Actor_T,       .x=S_EMPTY+1,   .y=NIL,         .z=UNDEF        },
    { .t=VM_push,       .x=NIL,         .y=S_VALUE,     .z=UNDEF        },  // ()

#define A_PRINT (S_EMPTY+2)
    { .t=Actor_T,       .x=A_PRINT+1,   .y=NIL,         .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=A_PRINT+2,   .z=UNDEF        },
    { .t=VM_debug,      .x=TO_FIX(7331),.y=COMMIT,      .z=UNDEF        },

#define A_QUIT (A_PRINT+3)
    { .t=Actor_T,       .x=A_QUIT+1,    .y=NIL,         .z=UNDEF        },
    { .t=VM_end,        .x=END_STOP,    .y=UNDEF,       .z=UNDEF        },  // kill thread

#define CELL_BASE (A_QUIT+2)
};
cell_t *cell_zero = &cell_table[0];  // base for cell offsets
int_t cell_next = NIL;  // head of cell free-list (or NIL if empty)
int_t cell_top = CELL_BASE; // limit of allocated cell memory

static struct { int_t addr; char *label; } symbol_table[] = {
    { FALSE, "FALSE" },
    { TRUE, "TRUE" },
    { NIL, "NIL" },
    { UNDEF, "UNDEF" },
    { UNIT, "UNIT" },
    { START, "START" },

    { RV_SELF, "RV_SELF" },
    { CUST_SEND, "CUST_SEND" },
    { SEND_0, "SEND_0" },
    { COMMIT, "COMMIT" },
    { RESEND, "RESEND" },
    { RELEASE_0, "RELEASE_0" },
    { RELEASE, "RELEASE" },
    { RV_FALSE, "RV_FALSE" },
    { RV_TRUE, "RV_TRUE" },
    { RV_NIL, "RV_NIL" },
    { RV_UNDEF, "RV_UNDEF" },
    { RV_UNIT, "RV_UNIT" },
    { RV_ZERO, "RV_ZERO" },
    { RV_ONE, "RV_ONE" },

    { S_VALUE, "S_VALUE" },
    { S_GETC, "S_GETC" },
    { S_END_X, "S_END_X" },
    { S_VAL_X, "S_VAL_X" },
    { S_LIST_B, "S_LIST_B" },
    { G_START, "G_START" },
    { G_CALL_B, "G_CALL_B" },
    { G_LANG, "G_LANG" },
    { EMPTY_ENV, "EMPTY_ENV" },
    { GLOBAL_ENV, "GLOBAL_ENV" },
    { BOUND_BEH, "BOUND_BEH" },

    { REPL_R, "REPL_R" },
    { REPL_E, "REPL_E" },
    { REPL_P, "REPL_P" },
    { REPL_L, "REPL_L" },
    { REPL_F, "REPL_F" },
    { A_BOOT, "A_BOOT" },

    { A_CLOCK, "A_CLOCK" },
    { CLOCK_BEH, "CLOCK_BEH" },

    { TAG_BEH, "TAG_BEH" },
    { K_JOIN_H, "K_JOIN_H" },
    { K_JOIN_T, "K_JOIN_T" },
    { JOIN_BEH, "JOIN_BEH" },
    { FORK_BEH, "FORK_BEH" },

    { S_IGNORE, "S_IGNORE" },
    { S_QUOTE, "S_QUOTE" },
    { S_QQUOTE, "S_QQUOTE" },
    { S_UNQUOTE, "S_UNQUOTE" },
    { S_QSPLICE, "S_QSPLICE" },

    { M_EVAL, "M_EVAL" },
    { K_COMBINE, "K_COMBINE" },
    { K_APPLY_F, "K_APPLY_F" },
    { M_APPLY, "M_APPLY" },
    { M_LOOKUP, "M_LOOKUP" },
    { M_EVLIS_P, "M_EVLIS_P" },
    { M_EVLIS_K, "M_EVLIS_K" },
    { M_EVLIS, "M_EVLIS" },
    { FX_PAR, "FX_PAR" },
    { OP_PAR, "OP_PAR" },
    { M_ZIP_IT, "M_ZIP_IT" },
    { M_ZIP_K, "M_ZIP_K" },
    { M_ZIP_P, "M_ZIP_P" },
    { M_ZIP_R, "M_ZIP_R" },
    { M_ZIP_S, "M_ZIP_S" },
    { M_ZIP, "M_ZIP" },
    { CLOSURE_B, "CLOSURE_B" },
    { M_EVAL_B, "M_EVAL_B" },
    { FEXPR_B, "FEXPR_B" },
    { K_SEQ_B, "K_SEQ_B" },
    { M_IF_K, "M_IF_K" },

    { M_BIND_E, "M_BIND_E" },
    { FX_QUOTE, "FX_QUOTE" },
    { OP_QUOTE, "OP_QUOTE" },
    { FX_LAMBDA, "FX_LAMBDA" },
    { OP_LAMBDA, "OP_LAMBDA" },
    { FX_VAU, "FX_VAU" },
    { OP_VAU, "OP_VAU" },
    { K_DEF_B, "K_DEF_B" },
    { FX_DEFINE, "FX_DEFINE" },
    { OP_DEFINE, "OP_DEFINE" },
    { FX_IF, "FX_IF" },
    { OP_IF, "OP_IF" },
    { FX_COND, "FX_COND" },
    { OP_COND, "OP_COND" },
    { K_COND, "K_COND" },
    { FX_SEQ, "FX_SEQ" },
    { OP_SEQ, "OP_SEQ" },

    { F_LIST, "F_LIST" },
    { F_CONS, "F_CONS" },
    { F_CAR, "F_CAR" },
    { F_CDR, "F_CDR" },
    { F_CADR, "F_CADR" },
    { F_CADDR, "F_CADDR" },
    { F_NTH, "F_NTH" },
    { F_NULL_P, "F_NULL_P" },
    { F_TYPE_P, "F_TYPE_P" },
    { F_PAIR_P, "F_PAIR_P" },
    { F_BOOL_P, "F_BOOL_P" },
    { F_NUM_P, "F_NUM_P" },
    { F_SYM_P, "F_SYM_P" },
    { F_ACT_P, "F_ACT_P" },
    { F_EQ_P, "F_EQ_P" },
    { F_NUM_EQ, "F_NUM_EQ" },
    { F_NUM_LT, "F_NUM_LT" },
    { F_NUM_LE, "F_NUM_LE" },
    { F_NUM_ADD, "F_NUM_ADD" },
    { F_NUM_SUB, "F_NUM_SUB" },
    { F_NUM_MUL, "F_NUM_MUL" },
    { F_LST_NUM, "F_LST_NUM" },
    { F_LST_SYM, "F_LST_SYM" },

#if SCM_ASM_TOOLS
    { F_INT_FIX, "F_INT_FIX" },
    { F_FIX_INT, "F_FIX_INT" },
    { F_CELL, "F_CELL" },
#endif // SCM_ASM_TOOLS

    { G_EMPTY, "G_EMPTY" },
    { G_FAIL, "G_FAIL" },
    { G_NEXT_K, "G_NEXT_K" },
    { G_ANY, "G_ANY" },
    { G_EQ_B, "G_EQ_B" },
    { G_FAIL_K, "G_FAIL_K" },
    { G_OR_B, "G_OR_B" },
    { G_AND_PR, "G_AND_PR" },
    { G_AND_OK, "G_AND_OK" },
    { G_AND_B, "G_AND_B" },
    { G_NOT_B, "G_NOT_B" },
    { G_OPT_B, "G_OPT_B" },
    { G_PLUS_B, "G_PLUS_B" },
    { G_STAR_B, "G_STAR_B" },
    { G_ALT_B, "G_ALT_B" },
    { G_SEQ_B, "G_SEQ_B" },
    { G_CLS_B, "G_CLS_B" },
    { G_PRED_K, "G_PRED_K" },
    { G_PRED_OK, "G_PRED_OK" },
    { G_PRED_B, "G_PRED_B" },
    { G_XLAT_K, "G_XLAT_K" },
    { G_XLAT_OK, "G_XLAT_OK" },
    { G_XLAT_B, "G_XLAT_B" },
    { S_CHAIN, "S_CHAIN" },
    { S_BUSY_C, "S_BUSY_C" },
    { S_NEXT_C, "S_NEXT_C" },

#if SCM_PEG_TOOLS
    { F_G_EQ, "F_G_EQ" },
    { F_G_OR, "F_G_OR" },
    { F_G_AND, "F_G_AND" },
    { F_G_NOT, "F_G_NOT" },
    { F_G_CLS, "F_G_CLS" },
    { F_G_OPT, "F_G_OPT" },
    { F_G_PLUS, "F_G_PLUS" },
    { F_G_STAR, "F_G_STAR" },
    { F_G_ALT, "F_G_ALT" },
    { F_G_SEQ, "F_G_SEQ" },
    { FX_G_CALL, "FX_G_CALL" },
    { OP_G_CALL, "OP_G_CALL" },
    { F_G_PRED, "F_G_PRED" },
    { F_G_XFORM, "F_G_XFORM" },
    { F_S_LIST, "F_S_LIST" },
    { F_G_START, "F_G_START" },
    { F_S_CHAIN, "F_S_CHAIN" },
#endif // SCM_PEG_TOOLS

    { G_END, "G_END" },
    { G_EOL, "G_EOL" },
    { G_WSP, "G_WSP" },
    { G_WSP_S, "G_WSP_S" },
    { G_TO_EOL, "G_TO_EOL" },
    { G_SEMIC, "G_SEMIC" },
    { G_COMMENT, "G_COMMENT" },
    { G_OPTWSP, "G_OPTWSP" },
    { G_PRT, "G_PRT" },
    { G_EOT, "G_EOT" },
    { G_UNDER, "G_UNDER" },
    { F_IGN, "F_IGN" },
    { G_IGN, "G_IGN" },
    { G_HASH, "G_HASH" },
    { G_LWR_U, "G_LWR_U" },
    { G_LWR_N, "G_LWR_N" },
    { G_LWR_I, "G_LWR_I" },
    { G_LWR_T, "G_LWR_T" },
    { G_LWR_F, "G_LWR_F" },
    { G_QMARK, "G_QMARK" },
    { F_FALSE, "F_FALSE" },
    { G_FALSE, "G_FALSE" },
    { F_TRUE, "F_TRUE" },
    { G_TRUE, "G_TRUE" },
    { F_UNDEF, "F_UNDEF" },
    { G_UNDEF, "G_UNDEF" },
    { F_UNIT, "F_UNIT" },
    { G_UNIT, "G_UNIT" },
    { G_CONST, "G_CONST" },
    { G_M_SGN, "G_M_SGN" },
    { G_P_SGN, "G_P_SGN" },
    { G_SIGN, "G_SIGN" },
    { G_DGT, "G_DGT" },
    { G_DIGIT, "G_DIGIT" },
    { G_DIGITS, "G_DIGITS" },
    { G_NUMBER, "G_NUMBER" },
    { G_SYMBOL, "G_SYMBOL" },
    { G_OPEN, "G_OPEN" },
    { G_DOT, "G_DOT" },
    { G_CLOSE, "G_CLOSE" },
    { G_QUOTE, "G_QUOTE" },
    { G_BQUOTE, "G_BQUOTE" },
    { G_COMMA, "G_COMMA" },
    { G_AT, "G_AT" },
    { F_QUOTED, "F_QUOTED" },
    { F_QQUOTED, "F_QQUOTED" },
    { F_UNQUOTED, "F_UNQUOTED" },
    { F_QSPLICED, "F_QSPLICED" },
    { F_NIL, "F_NIL" },
    { G_QUOTED, "G_QUOTED" },
    { G_DOTTED, "G_DOTTED" },
    { G_TAIL, "G_TAIL" },
    { G_LIST, "G_LIST" },
    { G_EXPR, "G_EXPR" },
    { G_SEXPR, "G_SEXPR" },

    { S_EMPTY, "S_EMPTY" },
    { A_PRINT, "A_PRINT" },
    { A_QUIT, "A_QUIT" },
    { CELL_BASE, "CELL_BASE" },
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

#define IS_CELL(n)  (NAT(n) < cell_top)
#define IN_HEAP(n)  (((n)>=START) && ((n)<cell_top))

#define IS_PROC(n)  (((n) < 0) && !IS_FIX(n))
#define IS_BOOL(n)  (((n) == FALSE) || ((n) == TRUE))

#define TYPEQ(t,n)  (IS_CELL(n) && (get_t(n) == (t)))
#define IS_FREE(n)  TYPEQ(Free_T,(n))
#define IS_PAIR(n)  TYPEQ(Pair_T,(n))
#define IS_ACTOR(n) TYPEQ(Actor_T,(n))
#define IS_FEXPR(n) TYPEQ(Fexpr_T,(n))
#define IS_SYM(n)   TYPEQ(Symbol_T,(n))

int_t get_proc(int_t value) {  // get dispatch proc for _value_
    if (IS_FIX(value)) return Fixnum_T;
    if (IS_PROC(value)) return Proc_T;
    if (IS_CELL(value)) return get_t(value);
    return error("no dispatch proc for value");
}

static i32 gc_free_cnt;  // FORWARD DECLARATION

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
    ASSERT(IN_HEAP(addr));
    ASSERT(!IS_FREE(addr));  // prevent double-free
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

// WARNING! destuctive reverse-in-place and append
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

// return integer for character string
int_t fixnum(int_t str) {  // FIXME: add `base` parameter
    int_t num = 0;
    int_t neg = UNDEF;
    while (IS_PAIR(str)) {
        int_t ch = TO_INT(car(str));
        str = cdr(str);
        if (char_in_class(ch, DGT)) {
            num = (10 * num) + (ch - '0');
        } else if (ch == '_') {
            // ignore separator
        } else {
            if (neg == UNDEF) {
                if (ch == '-') {
                    neg = TRUE;
                    continue;
                } else if (ch == '+') {
                    neg = FALSE;
                    continue;
                }
            }
            break;  // illegal character
        }
        if (neg == UNDEF) {
            neg = FALSE;
        }
    }
    if (neg == TRUE) {
        num = -num;
    }
    return TO_FIX(num);
}

/*
 * garbage collection (reclaiming the heap)
 */

#if MARK_SWEEP_GC
#define GC_LO_BITS(val) I32(I32(val) & 0x1F)
#define GC_HI_BITS(val) I32(I32(val) >> 5)

#define GC_MAX_BITS GC_HI_BITS(CELL_MAX)
#define GC_RESERVED (I32(1 << GC_LO_BITS(START)) - 1)

i32 gc_bits[GC_MAX_BITS] = { GC_RESERVED };  // in-use mark bits
static i32 gc_free_cnt = 0;  // number of cells in free-list

i32 gc_clear() {  // clear all GC bits (except RESERVED)
    i32 cnt = gc_free_cnt;
    cell_next = NIL;  // empty the free-list
    gc_free_cnt = 0;
    gc_bits[0] = GC_RESERVED;
    for (int_t i = 1; i < GC_MAX_BITS; ++i) {
        gc_bits[i] = 0;
    }
    return cnt;
}

static i32 gc_get_mark(int_t val) {
    return (gc_bits[GC_HI_BITS(val)] & I32(1 << GC_LO_BITS(val)));
}

static void gc_set_mark(int_t val) {
    gc_bits[GC_HI_BITS(val)] |= I32(1 << GC_LO_BITS(val));
}

static void gc_clr_mark(int_t val) {
    gc_bits[GC_HI_BITS(val)] &= ~I32(1 << GC_LO_BITS(val));
}

static void gc_dump_map() {  // dump memory allocation map
    for (int_t a = 0; a < cell_top; ++a) {
    //for (int_t a = 0; a < CELL_MAX; ++a) {
        if (a && ((a & 0x3F) == 0)) {
            fprintf(stderr, "\n");
        }
        char c = (gc_get_mark(a) ? 'x' : '.');
        if (a >= cell_top) c = '-';
#if 1
        /* extra detail */
        if (c != '.') {
            int_t t = get_t(a);
            if (t < 0) c = 't';         // "typed" cell
            if (t < Free_T) c = 'i';    // instruction
            if (t == Event_T) c = 'E';  // Event_T
            if (t == Actor_T) c = 'A';  // Actor_T
            if (t == Fexpr_T) c = 'F';  // Fexpr_T
            if (t == Symbol_T) c = 'S'; // Symbol_T
            if (t == Pair_T) c = 'p';   // Pair_T
            if (t == Free_T) c = 'f';   // Free_T <-- should not happen
            if (t >= START) c = 'K';    // continuation
        }
#endif
        fprintf(stderr, "%c", c);
    }
    fprintf(stderr, "\n");
}

i32 gc_mark_cells(int_t val) {  // mark cells reachable from `val`
    i32 cnt = 0;
    while (IN_HEAP(val)) {
        if (gc_get_mark(val)) {
            break;  // cell already marked
        }
        if (IS_FREE(val)) {
            //DEBUG(debug_print("gc_mark_cells", val));
            break;  // don't mark free cells
        }
        gc_set_mark(val);
        ++cnt;
        cnt += gc_mark_cells(get_t(val));   // recurse on t
        cnt += gc_mark_cells(get_x(val));   // recurse on x
        cnt += gc_mark_cells(get_z(val));   // recurse on z
        val = get_y(val);                   // iterate over y
    }
    return cnt;
}

// FORWARD DECLARATIONS
static int_t sym_intern[256];
int_t e_queue_head;  
int_t k_queue_head;
static int_t gc_root_set = NIL;

void gc_add_root(int_t addr) {
    gc_root_set = cons(addr, gc_root_set);
}

i32 gc_mark_roots(int_t dump) {  // mark cells reachable from the root-set
    i32 cnt = START-1;
    for (int i = 0; i < 256; ++i) {
        if (sym_intern[i]) {
            cnt += gc_mark_cells(sym_intern[i]);
        }
    }
    cnt += gc_mark_cells(e_queue_head);
    cnt += gc_mark_cells(k_queue_head);
    cnt += gc_mark_cells(gc_root_set);
    if (dump == TRUE) {
        gc_dump_map();
    }
    return cnt;
}

i32 gc_sweep() {  // free unmarked cells
    i32 cnt = 0;
    int_t next = cell_top;
    while (--next >= START) {
        if (!gc_get_mark(next)) {
            cell_reclaim(next);
            ++cnt;
        }
    }
    return cnt;
}

i32 gc_mark_and_sweep(int_t dump) {
    i32 t = I32(cell_top);
    i32 f = gc_clear();
    i32 m = gc_mark_roots(dump);
    i32 a = gc_sweep();
    if (dump) {
        fprintf(stderr,
            "gc: top=%"PRId32" free=%"PRId32" used=%"PRId32" avail=%"PRId32"\n",
            t, f, m, a);
    }
    return m;
}
#endif // MARK_SWEEP_GC

/*
 * symbol/character-string
 */

static uint32_t crc_table[] = {  // CRC-32 (cksum)
0x00000000,
0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b,
0x1a864db2, 0x1e475005, 0x2608edb8, 0x22c9f00f, 0x2f8ad6d6,
0x2b4bcb61, 0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd,
0x4c11db70, 0x48d0c6c7, 0x4593e01e, 0x4152fda9, 0x5f15adac,
0x5bd4b01b, 0x569796c2, 0x52568b75, 0x6a1936c8, 0x6ed82b7f,
0x639b0da6, 0x675a1011, 0x791d4014, 0x7ddc5da3, 0x709f7b7a,
0x745e66cd, 0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5, 0xbe2b5b58,
0xbaea46ef, 0xb7a96036, 0xb3687d81, 0xad2f2d84, 0xa9ee3033,
0xa4ad16ea, 0xa06c0b5d, 0xd4326d90, 0xd0f37027, 0xddb056fe,
0xd9714b49, 0xc7361b4c, 0xc3f706fb, 0xceb42022, 0xca753d95,
0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1, 0xe13ef6f4,
0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d, 0x34867077, 0x30476dc0,
0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5,
0x2ac12072, 0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16,
0x018aeb13, 0x054bf6a4, 0x0808d07d, 0x0cc9cdca, 0x7897ab07,
0x7c56b6b0, 0x71159069, 0x75d48dde, 0x6b93dddb, 0x6f52c06c,
0x6211e6b5, 0x66d0fb02, 0x5e9f46bf, 0x5a5e5b08, 0x571d7dd1,
0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e, 0xbfa1b04b,
0xbb60adfc, 0xb6238b25, 0xb2e29692, 0x8aad2b2f, 0x8e6c3698,
0x832f1041, 0x87ee0df6, 0x99a95df3, 0x9d684044, 0x902b669d,
0x94ea7b2a, 0xe0b41de7, 0xe4750050, 0xe9362689, 0xedf73b3e,
0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2, 0xc6bcf05f,
0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34,
0xdc3abded, 0xd8fba05a, 0x690ce0ee, 0x6dcdfd59, 0x608edb80,
0x644fc637, 0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb,
0x4f040d56, 0x4bc510e1, 0x46863638, 0x42472b8f, 0x5c007b8a,
0x58c1663d, 0x558240e4, 0x51435d53, 0x251d3b9e, 0x21dc2629,
0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5, 0x3f9b762c,
0x3b5a6b9b, 0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623, 0xf12f560e,
0xf5ee4bb9, 0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65,
0xeba91bbc, 0xef68060b, 0xd727bbb6, 0xd3e6a601, 0xdea580d8,
0xda649d6f, 0xc423cd6a, 0xc0e2d0dd, 0xcda1f604, 0xc960ebb3,
0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7, 0xae3afba2,
0xaafbe615, 0xa7b8c0cc, 0xa379dd7b, 0x9b3660c6, 0x9ff77d71,
0x92b45ba8, 0x9675461f, 0x8832161a, 0x8cf30bad, 0x81b02d74,
0x857130c3, 0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640,
0x4e8ee645, 0x4a4ffbf2, 0x470cdd2b, 0x43cdc09c, 0x7b827d21,
0x7f436096, 0x7200464f, 0x76c15bf8, 0x68860bfd, 0x6c47164a,
0x61043093, 0x65c52d24, 0x119b4be9, 0x155a565e, 0x18197087,
0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d,
0x2056cd3a, 0x2d15ebe3, 0x29d4f654, 0xc5a92679, 0xc1683bce,
0xcc2b1d17, 0xc8ea00a0, 0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb,
0xdbee767c, 0xe3a1cbc1, 0xe760d676, 0xea23f0af, 0xeee2ed18,
0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4, 0x89b8fd09,
0x8d79e0be, 0x803ac667, 0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662,
0x933eb0bb, 0x97ffad0c, 0xafb010b1, 0xab710d06, 0xa6322bdf,
0xa2f33668, 0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4
};

uint32_t add_crc(uint32_t crc, uint8_t octet) {
    octet ^= (crc >> 24);
    return (crc << 8) ^ crc_table[octet];
}

uint32_t list_crc(int_t val) {
    uint32_t crc = 0;
    int_t len = 0;
    sane = SANITY;
    // compute crc from octets
    while (IS_PAIR(val)) {
        int_t ch = TO_INT(car(val));
        crc = add_crc(crc, (uint8_t)ch);
        ++len;
        val = cdr(val);
        if (sane-- == 0) return panic("insane list_crc");
    }
    // include length in crc
    while (len) {
        crc = add_crc(crc, (uint8_t)len);
        len >>= 8;
    }
    return ~crc;  // complement result
}

int_t cstr_to_list(char *s) {
    int_t xs = NIL;
    while (s && *s) {
        int_t c = TO_FIX(0xFF & *s++);
        xs = cons(c, xs);
    }
    return append_reverse(xs, NIL);
}

int_t sym_new(int_t str) {
    int_t hash = (int_t)list_crc(str);
    return cell_new(Symbol_T, hash, str, UNDEF);
}

#define cstr_intern(s) symbol(cstr_to_list(s))

#define SYM_MAX (1<<8)  // 256
#define SYM_MASK (SYM_MAX-1)
static int_t sym_intern[SYM_MAX];

// return interned symbol for character string
int_t symbol(int_t str) {
    int_t sym = sym_new(str);
    int_t hash = get_x(sym);
    int_t slot = hash & SYM_MASK;
    int_t chain = sym_intern[slot];
    if (!chain) {
        chain = NIL;
        sym_intern[slot] = chain;  // fix static init
    }
    while (IS_PAIR(chain)) {
        int_t s = car(chain);
        if ((hash == get_x(s)) && equal(str, get_y(s))) {
            sym = XFREE(sym);
            return s;  // found interned symbol
        }
        chain = cdr(chain);
    }
    // add symbol to hash-chain
    sym_intern[slot] = cons(sym, sym_intern[slot]);
    return sym;
}

// install static symbol into symbol table
static void sym_install(int_t sym) {
    int_t str = get_y(sym);
    int_t hash = (int_t)list_crc(str);
    set_x(sym, hash);
    int_t slot = hash & SYM_MASK;
    int_t chain = sym_intern[slot];
    if (!chain) {
        chain = NIL;
        sym_intern[slot] = chain;  // fix static init
    }
    // add symbol to hash-chain
    sym_intern[slot] = cons(sym, sym_intern[slot]);
}

void print_symbol(int_t symbol) {
    if (IS_SYM(symbol)) {
        for (int_t p = get_y(symbol); IS_PAIR(p); p = cdr(p)) {
            int_t ch = TO_INT(car(p));
            char c = '~';
            if ((ch >= ' ') || (ch < 0x7F)) {
                c = (ch & 0x7F);
            }
            fprintf(stderr, "%c", c);
        }
    } else {
        print_addr("", symbol);
    }
}
#if INCLUDE_DEBUG
static void print_intern(int_t hash) {
    int_t slot = hash & SYM_MASK;
    int_t chain = sym_intern[slot];
    if (!chain) {
        fprintf(stderr, "--\n");
    } else {
        char c = '(';
        while (IS_PAIR(chain)) {
            fprintf(stderr, "%c", c);
            int_t s = car(chain);
            fprintf(stderr, "%"PxI":", get_x(s));
            print_symbol(s);
            c = ' ';
            chain = cdr(chain);
        }
        fprintf(stderr, ")\n");
    }
}
static int_t test_symbol_intern() {
    ASSERT(cstr_intern("_") == cstr_intern("_"));
    for (int_t slot = 0; slot < SYM_MAX; ++slot) {
        print_intern(slot);
    }
    return UNIT;
}
#endif // INCLUDE_DEBUG

#define bind_global(cstr,val) set_z(cstr_intern(cstr), (val))

int_t init_global_env() {
    sym_install(S_IGNORE);
    sym_install(S_QUOTE);
    sym_install(S_QQUOTE);
    sym_install(S_UNQUOTE);
    sym_install(S_QSPLICE);

    bind_global("peg-lang", G_SEXPR);  // language parser start symbol
    bind_global("empty-env", EMPTY_ENV);
    bind_global("global-env", GLOBAL_ENV);

    bind_global("eval", M_EVAL);
    bind_global("apply", M_APPLY);
    bind_global("quote", FX_QUOTE);
    bind_global("lambda", FX_LAMBDA);
    bind_global("vau", FX_VAU);
    bind_global("define", FX_DEFINE);
    bind_global("if", FX_IF);
    bind_global("cond", FX_COND);
#if !EVLIS_IS_PAR
    bind_global("par", FX_PAR);
#endif
    bind_global("seq", FX_SEQ);
    bind_global("list", F_LIST);
    bind_global("cons", F_CONS);
    bind_global("car", F_CAR);
    bind_global("cdr", F_CDR);
    bind_global("eq?", F_EQ_P);
    bind_global("pair?", F_PAIR_P);
    bind_global("symbol?", F_SYM_P);
    bind_global("cadr", F_CADR);
    bind_global("caddr", F_CADDR);
    bind_global("nth", F_NTH);
    bind_global("null?", F_NULL_P);
    bind_global("boolean?", F_BOOL_P);
    bind_global("number?", F_NUM_P);
    bind_global("actor?", F_ACT_P);
    bind_global("=", F_NUM_EQ);
    bind_global("<", F_NUM_LT);
    bind_global("<=", F_NUM_LE);
    bind_global("+", F_NUM_ADD);
    bind_global("-", F_NUM_SUB);
    bind_global("*", F_NUM_MUL);
    bind_global("list->number", F_LST_NUM);
    bind_global("list->symbol", F_LST_SYM);

#if (SCM_PEG_TOOLS || SCM_ASM_TOOLS)
    bind_global("CTL", TO_FIX(CTL));
    bind_global("DGT", TO_FIX(DGT));
    bind_global("UPR", TO_FIX(UPR));
    bind_global("LWR", TO_FIX(LWR));
    bind_global("DLM", TO_FIX(DLM));
    bind_global("SYM", TO_FIX(SYM));
    bind_global("HEX", TO_FIX(HEX));
    bind_global("WSP", TO_FIX(WSP));
#endif

#if SCM_PEG_TOOLS
    bind_global("peg-empty", G_EMPTY);
    bind_global("peg-fail", G_FAIL);
    bind_global("peg-any", G_ANY);
    bind_global("peg-eq", F_G_EQ);
    bind_global("peg-or", F_G_OR);
    bind_global("peg-and", F_G_AND);
    bind_global("peg-not", F_G_NOT);
    bind_global("peg-class", F_G_CLS);
    bind_global("peg-opt", F_G_OPT);
    bind_global("peg-plus", F_G_PLUS);
    bind_global("peg-star", F_G_STAR);
    bind_global("peg-alt", F_G_ALT);
    bind_global("peg-seq", F_G_SEQ);
    bind_global("peg-call", FX_G_CALL);
    bind_global("peg-pred", F_G_PRED);
    bind_global("peg-xform", F_G_XFORM);
    bind_global("peg-source", F_S_LIST);
    bind_global("peg-start", F_G_START);
    bind_global("peg-chain", F_S_CHAIN);

    bind_global("peg-end", G_END);
    bind_global("lex-eol", G_EOL);
    bind_global("lex-optwsp", G_WSP_S);
    bind_global("scm-to-eol", G_TO_EOL);
    bind_global("scm-comment", G_COMMENT);
    bind_global("scm-optwsp", G_OPTWSP);
    bind_global("lex-eot", G_EOT);
    bind_global("scm-const", G_CONST);
    bind_global("lex-sign", G_SIGN);
    bind_global("lex-digit", G_DIGIT);
    bind_global("lex-digits", G_DIGITS);
    bind_global("lex-number", G_NUMBER);
    bind_global("scm-symbol", G_SYMBOL);
    bind_global("scm-quoted", G_QUOTED);
    bind_global("scm-dotted", G_DOTTED);
    bind_global("scm-tail", G_TAIL);
    bind_global("scm-list", G_LIST);
    bind_global("scm-expr", G_EXPR);
    bind_global("scm-sexpr", G_SEXPR);
#endif // SCM_PEG_TOOLS

#if SCM_ASM_TOOLS
    bind_global("FALSE", FALSE);
    bind_global("TRUE", TRUE);
    bind_global("NIL", NIL);
    bind_global("UNDEF", UNDEF);
    bind_global("UNIT", UNIT);

    bind_global("Undef_T", Undef_T);
    bind_global("Boolean_T", Boolean_T);
    bind_global("Null_T", Null_T);
    bind_global("Pair_T", Pair_T);
    bind_global("Symbol_T", Symbol_T);
    bind_global("Fexpr_T", Fexpr_T);
    bind_global("Actor_T", Actor_T);
    bind_global("Event_T", Event_T);
    bind_global("Free_T", Free_T);

    bind_global("VM_typeq", VM_typeq);
    bind_global("VM_cell", VM_cell);
    bind_global("VM_get", VM_get);
    bind_global("VM_set", VM_set);
    bind_global("VM_pair", VM_pair);
    bind_global("VM_part", VM_part);
    bind_global("VM_nth", VM_nth);
    bind_global("VM_push", VM_push);
    bind_global("VM_depth", VM_depth);
    bind_global("VM_drop", VM_drop);
    bind_global("VM_pick", VM_pick);
    bind_global("VM_dup", VM_dup);
    bind_global("VM_roll", VM_roll);
    bind_global("VM_alu", VM_alu);
    bind_global("VM_eq", VM_eq);
    bind_global("VM_cmp", VM_cmp);
    bind_global("VM_if", VM_if);
    bind_global("VM_msg", VM_msg);
    bind_global("VM_self", VM_self);
    bind_global("VM_send", VM_send);
    bind_global("VM_new", VM_new);
    bind_global("VM_beh", VM_beh);
    bind_global("VM_end", VM_end);
    bind_global("VM_cvt", VM_cvt);
    bind_global("VM_putc", VM_putc);
    bind_global("VM_getc", VM_getc);
    bind_global("VM_debug", VM_debug);

    bind_global("FLD_T", FLD_T);
    bind_global("FLD_X", FLD_X);
    bind_global("FLD_Y", FLD_Y);
    bind_global("FLD_Z", FLD_Z);

    bind_global("ALU_NOT", ALU_NOT);
    bind_global("ALU_AND", ALU_AND);
    bind_global("ALU_OR", ALU_OR);
    bind_global("ALU_XOR", ALU_XOR);
    bind_global("ALU_ADD", ALU_ADD);
    bind_global("ALU_SUB", ALU_SUB);
    bind_global("ALU_MUL", ALU_MUL);

    bind_global("CMP_EQ", CMP_EQ);
    bind_global("CMP_GE", CMP_GE);
    bind_global("CMP_GT", CMP_GT);
    bind_global("CMP_LT", CMP_LT);
    bind_global("CMP_LE", CMP_LE);
    bind_global("CMP_NE", CMP_NE);
    bind_global("CMP_CLS", CMP_CLS);

    bind_global("END_ABORT", END_ABORT);
    bind_global("END_STOP", END_STOP);
    bind_global("END_COMMIT", END_COMMIT);
    bind_global("END_RELEASE", END_RELEASE);

    bind_global("CVT_INT_FIX", CVT_INT_FIX);
    bind_global("CVT_FIX_INT", CVT_FIX_INT);
    bind_global("CVT_LST_NUM", CVT_LST_NUM);
    bind_global("CVT_LST_SYM", CVT_LST_SYM);

    //bind_global("START", START);
    bind_global("RV_SELF", RV_SELF);
    bind_global("CUST_SEND", CUST_SEND);
    bind_global("SEND_0", SEND_0);
    bind_global("COMMIT", COMMIT);
    bind_global("RESEND", RESEND);
    bind_global("RELEASE_0", RELEASE_0);
    bind_global("RELEASE", RELEASE);

    bind_global("RV_FALSE", RV_FALSE);
    bind_global("RV_TRUE", RV_TRUE);
    bind_global("RV_NIL", RV_NIL);
    bind_global("RV_UNDEF", RV_UNDEF);
    bind_global("RV_UNIT", RV_UNIT);
    bind_global("RV_ZERO", RV_ZERO);
    bind_global("RV_ONE", RV_ONE);

    bind_global("int->fix", F_INT_FIX);
    bind_global("fix->int", F_FIX_INT);
    bind_global("cell", F_CELL);
#endif //SCM_ASM_TOOLS

    bind_global("a-print", A_PRINT);
    bind_global("quit", A_QUIT);
    return UNIT;
}

/*
 * actor event-queue
 */

int_t e_queue_head = START;
int_t e_queue_tail = START;
#if RUNTIME_STATS
static long event_count = 0;
#endif

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
#if RUNTIME_STATS
    ++event_count;
#endif
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
#if RUNTIME_STATS
static long instruction_count = 0;
#endif

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
#if RUNTIME_STATS
    ++instruction_count;
#endif
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
        sp = XFREE(sp);
    }
    XTRACE(debug_print("stack pop", item));
    return item;
}

int_t stack_clear() {
    int_t ep = GET_EP();
    int_t me = get_x(ep);
    int_t stop = get_y(me);  // stop at new stack top
    int_t sp = GET_SP();
    sane = SANITY;
    while ((sp != stop) && IS_PAIR(sp)) {
        int_t rest = cdr(sp);
        XFREE(sp);
        sp = rest;
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
static int_t interrupt() {  // service interrupts (if any)
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
    int_t sec = TO_FIX(now / CLKS_PER_SEC);
    if (IS_ACTOR(clk_handler)) {
        int_t ev = cell_new(Event_T, clk_handler, sec, NIL);
        DEBUG(debug_print("clock event", ev));
        event_q_put(ev);
    }
    return TRUE;
}
static int_t dispatch() {  // dispatch next event (if any)
    XTRACE(event_q_dump());
    if (event_q_empty()) {
        return UNDEF;  // event queue empty
    }
    int_t event = event_q_pop();
    XTRACE(debug_print("dispatch event", event));
    ASSERT(IN_HEAP(event));
    int_t target = get_x(event);
    int_t proc = get_proc(target);
    //ASSERT(IS_PROC(proc));  // FIXME: does not include Fixnum_T and Proc_T
    int_t cont = call_proc(proc, target, event);
    if (cont == FALSE) {  // target busy
#if INCLUDE_DEBUG
        if (runtime_trace) {
            DEBUG(debug_print("dispatch busy", event));
        }
#endif
        event_q_put(event);  // re-queue event
    } else if (cont == TRUE) {  // immediate event -- retry
        cont = dispatch();
        XTRACE(debug_print("dispatch retry", cont));
    } else if (IN_HEAP(cont)) {  // enqueue new continuation
        cont_q_put(cont);
#if INCLUDE_DEBUG
        if (runtime_trace) {
            fprintf(stderr, "thread spawn: %"PdI"{ip=%"PdI",sp=%"PdI",ep=%"PdI"}\n",
                cont, get_t(cont), get_x(cont), get_y(cont));
        }
#endif
    }
    return cont;
}
static int_t execute() {  // execute next VM instruction
    XTRACE(cont_q_dump());
    if (cont_q_empty()) {
        return error("no live threads");  // no more instructions to execute...
    }
    // execute next continuation
    XTRACE(debug_print("execute cont", k_queue_head));
    int_t ip = GET_IP();
    ASSERT(IS_CELL(ip));
    int_t proc = get_t(ip);
    ASSERT(IS_PROC(proc));
#if INCLUDE_DEBUG
    if (!debugger()) return FALSE;  // debugger quit
#endif
    ip = call_proc(proc, ip, GET_EP());
    SET_IP(ip);  // update IP
    int_t cont = cont_q_pop();
    XTRACE(debug_print("execute done", cont));
    if (IN_HEAP(ip)) {
        cont_q_put(cont);  // enqueue continuation
    } else {
        // if "thread" is dead, free cont and event
        int_t event = get_y(cont);
        event = XFREE(event);
        cont = XFREE(cont);
#if MARK_SWEEP_GC
        gc_mark_and_sweep(FALSE);  // no gc output
        //gc_mark_and_sweep(UNDEF);  // one-line gc summary
#endif
    }
    return UNIT;
}
int_t runtime() {
    int_t rv = UNIT;
    while (rv == UNIT) {
        rv = interrupt();
        rv = dispatch();
        rv = execute();
    }
    return rv;
}

/*
 * native procedures
 */

PROC_DECL(Fixnum) {
    return panic("Dispatch to Fixnum!?");
}

PROC_DECL(Proc) {
    return panic("Dispatch to Proc!?");
}

PROC_DECL(Undef) {
    int_t event = arg;
#if INCLUDE_DEBUG
    if (runtime_trace) {
        DEBUG(print_event(event));
        DEBUG(debug_print("Undef", event));
    }
#endif
    ASSERT(self == get_x(event));
    int_t msg = get_y(event);
    event = XFREE(event);  // event is consumed
    int_t cust = msg;
    if (IS_PAIR(msg)) {
        cust = car(msg);
    }
    if (IS_ACTOR(cust)) {
        event = cell_new(Event_T, cust, self, NIL);
        event_q_put(event);
        return TRUE;  // retry event dispatch
    }
    return error("message not understood");
}

PROC_DECL(Boolean) {
    return panic("Dispatch to Boolean!?");
}

PROC_DECL(Null) {
    return panic("Dispatch to Null!?");
}

PROC_DECL(Pair) {
    return panic("Dispatch to Pair!?");
}

PROC_DECL(Symbol) {
    return panic("Dispatch to Symbol!?");
}

PROC_DECL(Fexpr) {
    return panic("Dispatch to Fexpr!?");
}

PROC_DECL(Actor) {
    int_t actor = self;
    int_t event = arg;
    ASSERT(actor == get_x(event));
    if (get_z(actor) != UNDEF) {
        return FALSE;  // actor busy
    }
    int_t beh = get_x(actor);  // actor behavior (initial IP)
    int_t isp = get_y(actor);  // actor state (initial SP)
    ASSERT((isp == NIL) || IS_PAIR(isp));
    // begin actor transaction
    set_z(actor, NIL);  // start with empty set of new events
    // spawn new "thread" to handle event
    int_t cont = cell_new(beh, isp, event, NIL);  // ip=beh, sp=isp, ep=event
    return cont;
}

PROC_DECL(Event) {
    return panic("Dispatch to Event!?");
}

PROC_DECL(Free) {
    return panic("DISPATCH TO FREE CELL!");
}

PROC_DECL(vm_typeq) {
    int_t t = get_x(self);
    int_t v = stack_pop();
    switch (t) {
        case Fixnum_T:  v = (IS_FIX(v) ? TRUE : FALSE);     break;
        case Proc_T:    v = (IS_PROC(v) ? TRUE : FALSE);    break;
        default: {
            if (IS_CELL(v)) {
                v = ((t == get_t(v)) ? TRUE : FALSE);
            } else {
                v = FALSE;  // _v_ out of range
            }
            break;
        }
    }
    stack_push(v);
    return get_y(self);
}

PROC_DECL(vm_cell) {
    int_t n = get_x(self);
    int_t z = UNDEF;
    int_t y = UNDEF;
    int_t x = UNDEF;
    ASSERT(NAT(n) <= 4);
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
    if (IS_CELL(cell)) {
        switch (f) {
            case FLD_T:     v = get_t(cell);    break;
            case FLD_X:     v = get_x(cell);    break;
            case FLD_Y:     v = get_y(cell);    break;
            case FLD_Z:     v = get_z(cell);    break;
            default:        return error("unknown field");
        }
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
    if (IS_CELL(cell)) {
        switch (f) {
            case FLD_T:     set_t(cell, v);     break;
            case FLD_X:     set_x(cell, v);     break;
            case FLD_Y:     set_y(cell, v);     break;
            case FLD_Z:     set_z(cell, v);     break;
            default:        return error("unknown field");
        }
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

static int_t extract_nth(int_t m, int_t n) {
    int_t v = UNDEF;
    if (n == 0) {  // entire list/message
        v = m;
    } else if (n > 0) {  // item at index
        sane = SANITY;
        while (IS_PAIR(m)) {
            if (--n == 0) {
                v = car(m);
                break;
            }
            m = cdr(m);
            if (sane-- == 0) return panic("insane extract_nth");
        }
    } else {  // use -n to select the n-th tail
        sane = SANITY;
        while (IS_PAIR(m)) {
            m = cdr(m);
            if (++n == 0) break;
            if (sane-- == 0) return panic("insane extract_nth");
        }
        v = m;
    }
    return v;
}
PROC_DECL(vm_nth) {
    int_t n = get_x(self);
    int_t m = stack_pop();
    stack_push(extract_nth(m, n));
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
    while (n-- > 0) {  // drop n items from stack
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

PROC_DECL(vm_roll) {
    int_t n = get_x(self);
    int_t v = UNDEF;
    int_t sp = GET_SP();
    int_t pp = sp;
    sane = SANITY;
    if (n < 0) {  // roll top of stack to n-th item
        while (++n < 0) {
            sp = cdr(sp);
            if (sane-- == 0) return panic("insane vm_roll");
        }
        if (sp == NIL) {  // stack underflow
            stack_pop();
        } else if (sp != pp) {
            SET_SP(cdr(pp));
            set_cdr(pp, cdr(sp));
            set_cdr(sp, pp);
        }
    } else {
        while (--n > 0) {  // roll n-th item to top of stack
            pp = sp;
            sp = cdr(sp);
            if (sane-- == 0) return panic("insane vm_roll");
        }
        if (sp == NIL) {  // stack underflow
            stack_push(NIL);
        } else if (sp != pp) {
            set_cdr(pp, cdr(sp));
            set_cdr(sp, GET_SP());
            SET_SP(sp);
        }
    }
    return get_y(self);
}

PROC_DECL(vm_alu) {
    int_t op = get_x(self);
    if (op == ALU_NOT) {  // special case for unary NOT
        int_t n = TO_INT(stack_pop());
        stack_push(TO_FIX(~n));
        return get_y(self);
    }
    int_t m = TO_INT(stack_pop());
    int_t n = TO_INT(stack_pop());
    switch (op) {
        case ALU_AND:   stack_push(TO_FIX(n & m));  break;
        case ALU_OR:    stack_push(TO_FIX(n | m));  break;
        case ALU_XOR:   stack_push(TO_FIX(n ^ m));  break;
        case ALU_ADD:   stack_push(TO_FIX(n + m));  break;
        case ALU_SUB:   stack_push(TO_FIX(n - m));  break;
        case ALU_MUL:   stack_push(TO_FIX(n * m));  break;
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
    int_t m = TO_INT(stack_pop());
    int_t n = TO_INT(stack_pop());
    switch (r) {
        case CMP_EQ:    stack_push((n == m) ? TRUE : FALSE);    break;
        case CMP_GE:    stack_push((n >= m) ? TRUE : FALSE);    break;
        case CMP_GT:    stack_push((n > m)  ? TRUE : FALSE);    break;
        case CMP_LT:    stack_push((n < m)  ? TRUE : FALSE);    break;
        case CMP_LE:    stack_push((n <= m) ? TRUE : FALSE);    break;
        case CMP_NE:    stack_push((n != m) ? TRUE : FALSE);    break;
        case CMP_CLS: {  // character in class
            if (char_in_class(n, m)) {
                stack_push(TRUE);
            } else {
                stack_push(FALSE);
            }
            break;
        }
        default:        return error("unknown relation");
    }
    return get_y(self);
}

PROC_DECL(vm_if) {
    int_t b = stack_pop();
    //if (b == UNDEF) return error("undefined condition");
    return ((b == FALSE) ? get_y(self) : get_x(self));
}

PROC_DECL(vm_msg) {
    int_t n = get_x(self);
    int_t ep = GET_EP();
    int_t m = get_y(ep);
    stack_push(extract_nth(m, n));
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
#if 0
    if (!IS_ACTOR(a)) {
        set_y(me, UNDEF);  // abort actor transaction
        return error("SEND requires an Actor");
    }
#endif
    int_t m = NIL;
    if (n == 0) {
        m = stack_pop();  // message
    } else if (n > 0) {
        m = pop_list(n);  // compose message
    } else {
        return error("vm_send (n < 0) invalid");
    }
    int_t ev = cell_new(Event_T, a, m, get_z(me));
    set_z(me, ev);
    return get_y(self);
}

PROC_DECL(vm_new) {
    int_t n = get_x(self);
    if (n < 0) return error("vm_new (n < 0) invalid");
    int_t ip = stack_pop();  // behavior
#if 0
    while (n--) {
        // compose behavior
        int_t v = stack_pop();  // value
        ip = cell_new(VM_push, v, ip, UNDEF);
    }
    int_t a = cell_new(Actor_T, ip, NIL, UNDEF);
#else
    int_t sp = NIL;
    if (n > 0) {
        sp = GET_SP();
        int_t np = sp;
        while (--n && IS_PAIR(np)) {
            np = cdr(np);
        }
        if (IS_PAIR(np)) {
            SET_SP(cdr(np));
            set_cdr(np, NIL);
        } else {
            SET_SP(NIL);
        }
    }
    int_t a = cell_new(Actor_T, ip, sp, UNDEF);
#endif
    stack_push(a);
    return get_y(self);
}

PROC_DECL(vm_beh) {
    int_t n = get_x(self);
    if (n < 0) return error("vm_beh (n < 0) invalid");
    int_t ep = GET_EP();
    int_t me = get_x(ep);
    int_t ip = stack_pop();  // behavior
    set_x(me, ip);
    if (n > 0) {
        int_t sp = GET_SP();
        set_y(me, sp);
        while (--n && IS_PAIR(sp)) {
            sp = cdr(sp);
        }
        if (IS_PAIR(sp)) {
            SET_SP(cdr(sp));
            set_cdr(sp, NIL);
        } else {
            SET_SP(NIL);
        }
    } else {
        set_y(me, NIL);
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
        set_z(me, UNDEF);  // abort actor transaction
        rv = FALSE;
    } else if (n > 0) {  // COMMIT
        if (n == END_RELEASE) {
            set_y(me, NIL);
        }
        stack_clear();
        int_t e = get_z(me);
        sane = SANITY;
        while (e != NIL) {
            int_t es = get_z(e);
            event_q_put(e);
            e = es;
            if (sane-- == 0) return panic("insane COMMIT");
        }
        if (n == END_RELEASE) {
            me = XFREE(me);
        } else {
            set_z(me, UNDEF);  // commit actor transaction
        }
        rv = TRUE;
    }
    return rv;  // terminate thread
}

PROC_DECL(vm_cvt) {
    int_t c = get_x(self);
    int_t w = stack_pop();
    int_t v = UNDEF;
    switch (c) {
        case CVT_INT_FIX:   v = TO_FIX(w);      break;
        case CVT_FIX_INT:   v = TO_INT(w);      break;
        case CVT_LST_NUM:   v = fixnum(w);      break;
        case CVT_LST_SYM:   v = symbol(w);      break;
        default:            v = error("unknown conversion");
    }
    stack_push(v);
    return get_y(self);
}

PROC_DECL(vm_putc) {
    int_t c = stack_pop();
    console_putc(c);
    return get_y(self);
}

PROC_DECL(vm_getc) {
    int_t c = console_getc();
    stack_push(c);
    return get_y(self);
}

PROC_DECL(vm_debug) {
    int_t x = get_x(self);
    int_t v = stack_pop();
    //fprintf(stderr, "[%"PdI"] ", x);
    print_addr("[", x);
    fprintf(stderr, "] ");
#if 1
    print_sexpr(v);
    fprintf(stderr, "\n");
#else
#if INCLUDE_DEBUG
    //debug_print("", v);
    print_list(v);
#else
    fprintf(stderr, "%"PdI"\n", v);
#endif
#endif
    return get_y(self);
}

/*
 * debugging tools
 */

void print_sexpr(int_t x) {
    if (IS_FIX(x)) {
        fprintf(stderr, "%+"PdI"", TO_INT(x));
    } else if (IS_PROC(x)) {
        fprintf(stderr, "#%s", proc_label(x));
    } else if (x == FALSE) {
        fprintf(stderr, "#f");
    } else if (x == TRUE) {
        fprintf(stderr, "#t");
    } else if (x == NIL) {
        fprintf(stderr, "()");
    } else if (x == UNDEF) {
        fprintf(stderr, "#?");
    } else if (x == UNIT) {
        fprintf(stderr, "#unit");
    } else if (IS_FREE(x)) {
        fprintf(stderr, "#FREE-CELL!");
    } else if (IS_SYM(x)) {
        print_symbol(x);
    } else if (IS_PAIR(x)) {
        char *s = "(";
        while (IS_PAIR(x)) {
            fprintf(stderr, "%s", s);
            print_sexpr(car(x));
            s = " ";
            x = cdr(x);
        }
        if (x != NIL) {
            fprintf(stderr, " . ");
            print_sexpr(x);
        }
        fprintf(stderr, ")");
    } else if (IS_ACTOR(x)) {
        fprintf(stderr, "#actor@%"PdI"", x);
    } else if (IS_FEXPR(x)) {
        fprintf(stderr, "#fexpr@%"PdI"", x);
    } else {
        fprintf(stderr, "^%"PdI"", x);
    }
}

#if INCLUDE_DEBUG

#if USE_INT16_T || (USE_INTPTR_T && (__SIZEOF_POINTER__ == 2))
void hexdump(char *label, int_t *addr, size_t cnt) {
    fprintf(stderr, "%s:", label);
    for (nat_t n = 0; n < cnt; ++n) {
        if ((n & 0x7) == 0x0) {
            fprintf(stderr, "\n%08"PRIxPTR":", (intptr_t)addr);
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

void print_addr(char *prefix, int_t addr) {
    if (IS_FIX(addr)) {
        fprintf(stderr, "%s%+"PdI"", prefix, TO_INT(addr));
    } else {
        fprintf(stderr, "%s^%"PdI"", prefix, addr);
    }
}
void print_labelled(char *prefix, int_t addr) {
    fprintf(stderr, "%s%s(%"PdI")", prefix, cell_label(addr), addr);
}
void debug_print(char *label, int_t addr) {
    fprintf(stderr, "%s: ", label);
    fprintf(stderr, "%s[%"PdI"]", cell_label(addr), addr);
    if (IS_FIX(addr)) {
        print_addr(" = ", addr);
    } else if (addr >= 0) {
        fprintf(stderr, " =");
        print_labelled(" {t:", get_t(addr));
        print_labelled(", x:", get_x(addr));
        print_labelled(", y:", get_y(addr));
        print_labelled(", z:", get_z(addr));
        fprintf(stderr, "}");
    }
    fprintf(stderr, "\n");
}

void print_event(int_t ep) {
    print_addr("(", get_x(ep));  // target actor
    int_t msg = get_y(ep);  // actor message
    sane = SANITY;
    while (IS_PAIR(msg)) {
        print_addr(" ", car(msg));
        msg = cdr(msg);
        if (sane-- == 0) panic("insane print_event");
    }
    if (msg != NIL) {
        print_addr(" . ", msg);
    }
    fprintf(stderr, ") ");
}
static void print_stack(int_t sp) {
    if (IS_PAIR(sp)) {
        print_stack(cdr(sp));
        int_t item = car(sp);
        //fprintf(stderr, " %s[%"PdI"]", cell_label(item), item);
        print_addr(" ", item);
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
        case ALU_NOT:   return "NOT";
        case ALU_AND:   return "AND";
        case ALU_OR:    return "OR";
        case ALU_XOR:   return "XOR";
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
        case CMP_CLS:   return "CLS";
    }
    return "<unknown>";
}
static char *end_label(int_t t) {
    if (t < 0) return "ABORT";
    if (t == END_RELEASE) return "RELEASE";
    if (t > 0) return "COMMIT";
    return "STOP";
}
static char *conversion_label(int_t f) {
    switch (f) {
        case CVT_INT_FIX:   return "INT_FIX";
        case CVT_FIX_INT:   return "FIX_INT";
        case CVT_LST_NUM:   return "LST_NUM";
        case CVT_LST_SYM:   return "LST_SYM";
    }
    return "<unknown>";
}
void print_inst(int_t ip) {
    if (IS_FIX(ip) || (ip < 0)) {
        fprintf(stderr, "<non-inst:%"PdI">", ip);
        return;
    }
    int_t proc = get_t(ip);
    fprintf(stderr, "%s", cell_label(proc));
    switch (proc) {
        case VM_typeq:fprintf(stderr, "{t:%s,k:%"PdI"}", proc_label(get_x(ip)), get_y(ip)); break;
        case VM_cell: fprintf(stderr, "{n:%"PdI",k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_get:  fprintf(stderr, "{f:%s,k:%"PdI"}", field_label(get_x(ip)), get_y(ip)); break;
        case VM_set:  fprintf(stderr, "{f:%s,k:%"PdI"}", field_label(get_x(ip)), get_y(ip)); break;
        case VM_pair: fprintf(stderr, "{n:%"PdI",k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_part: fprintf(stderr, "{n:%"PdI",k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_nth:  fprintf(stderr, "{n:%"PdI",k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_push: fprintf(stderr, "{v:%"PdI",k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_depth:fprintf(stderr, "{k:%"PdI"}", get_y(ip)); break;
        case VM_drop: fprintf(stderr, "{n:%"PdI",k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_pick: fprintf(stderr, "{n:%"PdI",k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_dup:  fprintf(stderr, "{n:%"PdI",k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_roll: fprintf(stderr, "{n:%"PdI",k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_alu:  fprintf(stderr, "{op:%s,k:%"PdI"}", operation_label(get_x(ip)), get_y(ip)); break;
        case VM_eq:   fprintf(stderr, "{n:%"PdI",k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_cmp:  fprintf(stderr, "{r:%s,k:%"PdI"}", relation_label(get_x(ip)), get_y(ip)); break;
        case VM_if:   fprintf(stderr, "{t:%"PdI",f:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_msg:  fprintf(stderr, "{n:%"PdI",k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_self: fprintf(stderr, "{k:%"PdI"}", get_y(ip)); break;
        case VM_send: fprintf(stderr, "{n:%"PdI",k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_new:  fprintf(stderr, "{n:%"PdI",k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_beh:  fprintf(stderr, "{n:%"PdI",k:%"PdI"}", get_x(ip), get_y(ip)); break;
        case VM_end:  fprintf(stderr, "{t:%s}", end_label(get_x(ip))); break;
        case VM_cvt:  fprintf(stderr, "{c:%s}", conversion_label(get_x(ip))); break;
        case VM_putc: fprintf(stderr, "{k:%"PdI"}", get_y(ip)); break;
        case VM_getc: fprintf(stderr, "{k:%"PdI"}", get_y(ip)); break;
        case VM_debug:fprintf(stderr, "{t:%"PdI",k:%"PdI"}", get_x(ip), get_y(ip)); break;
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
void print_value(int_t v) {
    if (IS_FIX(v)) {
        fprintf(stderr, "%+"PdI"", TO_INT(v));
    } else if (v < 0) {
        fprintf(stderr, "%s", cell_label(v));
    } else {
        print_inst(v);
    }
}
void print_list(int_t xs) {
    fprintf(stderr, "%"PdI": ", xs);
    if (!IS_PAIR(xs)) {
        print_value(xs);  // non-list value
        fprintf(stderr, "\n");
        return;
    }
    print_addr("(", car(xs));
    xs = cdr(xs);
    int limit = 8;
    while (IS_PAIR(xs)) {
        print_addr(" ", car(xs));
        xs = cdr(xs);
        if (limit-- == 0) {
            fprintf(stderr, " ...\n");
            return;
        }
    }
    if (xs != NIL) {
        print_addr(" . ", xs);
    }
    fprintf(stderr, ")\n");
}
void continuation_trace() {
    print_event(GET_EP());
    fprintf(stderr, "%"PdI":", GET_IP());
    print_stack(GET_SP());
    fprintf(stderr, " ");
    print_inst(GET_IP());
    fprintf(stderr, "\n");
}
static void print_fixed(int width, int_t value) {
    if (IS_FIX(value)) {
        fprintf(stderr, "%+*"PdI"", width, TO_INT(value));
    } else {
        fprintf(stderr, "%*"PdI"", width, value);
    }
}
void disassemble(int_t ip, int_t n) {
    sane = CELL_MAX;  // a better upper-bound than SANITY...
    while (n-- > 0) {
        char *label = get_symbol_label(ip);
        if (*label) {
            fprintf(stderr, "%s\n", label);
        }
        print_fixed(6, ip);
        fprintf(stderr, ": ");
        print_fixed(6, get_t(ip));
        fprintf(stderr, " ");
        print_fixed(6, get_x(ip));
        fprintf(stderr, " ");
        print_fixed(6, get_y(ip));
        fprintf(stderr, " ");
        print_fixed(6, get_z(ip));
        fprintf(stderr, "  ");
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
    sane = 16;
    while (*expect) {
        if (*expect++ != *actual++) return FALSE;
        if (sane-- == 0) return panic("insane db_cmd_eq");
    }
    return (*actual ? FALSE : TRUE);
}
static int_t db_num_cmd(char *cmd) {
    int_t n = 0;
    nat_t d;
    sane = 16;
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
        fprintf(stderr, "@ ");  // debugger prompt
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
        if (*cmd == 'p') {                  // print
            cmd = db_cmd_token(&p);
            int_t addr = db_num_cmd(cmd);
            print_list(addr);
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
#if RUNTIME_STATS
            if (*cmd == 's') {              // info statistics
                fprintf(stderr, "events=%ld instructions=%ld\n",
                    event_count, instruction_count);
                // reset counters
                event_count = 0;
                instruction_count = 0;
                continue;
            }
            fprintf(stderr, "info: r[egs] t[hreads] e[vents] s[tats]\n");
#else
            fprintf(stderr, "info: r[egs] t[hreads] e[vents]\n");
#endif
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
                case 'p' : fprintf(stderr, "p[rint] <addr> -- print list at <addr>\n"); continue;
                case 't' : fprintf(stderr, "t[race] -- toggle instruction tracing (default: on)\n"); continue;
                case 'i' : fprintf(stderr, "i[nfo] <topic> -- get information on <topic>\n"); continue;
                case 'q' : fprintf(stderr, "q[uit] -- quit runtime\n"); continue;
            }
        }
#if MARK_SWEEP_GC
        if (*cmd == 'g') {  // undocumented command to perform garbage collection
            gc_mark_and_sweep(TRUE);
            continue;
        }
#endif
        fprintf(stderr, "h[elp] b[reak] c[ontinue] s[tep] n[ext] d[isasm] p[rint] t[race] i[nfo] q[uit]\n");
    }
}
#endif // INCLUDE_DEBUG

/*
 * bootstrap
 */

static char repl_lib[] =
#if EVLIS_IS_PAR
" (define par (lambda _))"
#endif
" (define caar (lambda (x) (car (car x))))"
" (define cdar (lambda (x) (cdr (car x))))"
" (define cddr (lambda (x) (nth -2 x))))"
" (define cadar (lambda (x) (cadr (car x))))"
" (define cadddr (lambda (x) (nth 4 x))))"
" (define not (lambda (x) (if x #f #t))))"
" (define length (lambda (x) (if (pair? x) (+ (length (cdr x)) 1) 0)))"
" (define list* (lambda (h . t) (if (pair? t) (cons h (apply list* t)) h)))"
" (define append (lambda x (if (pair? x) (apply (lambda (h . t)"
"   (if (pair? t) (if (pair? h) (cons (car h) (apply append (cons (cdr h) t))) (apply append t)) h)) x) x)))"
" (define filter (lambda (pred? xs) (if (pair? xs) (if (pred? (car xs))"
"   (cons (car xs) (filter pred? (cdr xs))) (filter pred? (cdr xs))) ())))"
" (define reduce (lambda (op z xs) (if (pair? xs) (if (pair? (cdr xs)) (op (car xs) (reduce op z (cdr xs))) (car xs)) z)))"
" (define foldl (lambda (op z xs) (if (pair? xs) (foldl op (op z (car xs)) (cdr xs)) z)))"
" (define foldr (lambda (op z xs) (if (pair? xs) (op (car xs) (foldr op z (cdr xs))) z)))"
" (define reverse (lambda (xs) (foldl (lambda (x y) (cons y x)) () xs)))"
//" (define map (lambda (f xs) (if (pair? xs) (cons (f (car xs)) (map f (cdr xs))) ())))"
" (define map (lambda (f . xs) (if (pair? (car xs))"
"   (cons (apply f (foldr (lambda (x y) (cons (car x) y)) () xs))"
"   (apply map (cons f (foldr (lambda (x y) (cons (cdr x) y)) () xs)))) ())))"
" (define current-env (vau _ e e))"
" (define macro (vau (frml . body) env"
"   (eval (list vau frml '_env_ (list eval (cons seq body) '_env_)) env) ))"
" (define let (macro (bindings . body) (cons (list* lambda (map car bindings) body) (map cadr bindings))))"
" (define and (macro x (if (pair? x) (if (pair? (cdr x))"
"   (list let (list (list '_test_ (car x))) (list if '_test_ (cons 'and (cdr x)) '_test_)) (car x)) #t)))"
" (define or (macro x (if (pair? x) (if (pair? (cdr x))"
"   (list let (list (list '_test_ (car x))) (list if '_test_ '_test_ (cons 'or (cdr x)))) (car x)) #f)))"
" (define quasiquote (vau (x) e (if (pair? x)"
"   (if (eq? (car x) 'unquote) (eval (cadr x) e)"
"   (quasi-list x)) x)))"
" (define quasi-list (lambda (x) (if (pair? x) (if (pair? (car x))"
"   (if (eq? (caar x) 'unquote-splicing) (append (eval (cadar x) e) (quasi-list (cdr x)))"
"   (cons (apply quasiquote (list (car x)) e) (quasi-list (cdr x))))"
"   (cons (car x) (quasi-list (cdr x)))) x)))"
" \0";
static char *repl_inp = repl_lib;

#if BOOTSTRAP_LIB
int_t console_stdio = FALSE;  // start reading from repl_lib
#else
int_t console_stdio = TRUE;  // start reading from stdio
#endif

int_t console_putc(int_t c) {
    ASSERT(IS_FIX(c));
    c = TO_INT(c);
    if (console_stdio) {
        putchar(c);
    }
    return UNIT;
}

int_t console_getc() {
    int_t c = -1;  // EOS
    if (console_stdio) {
        c = getchar();
    } else if (repl_inp && (c = *repl_inp)) {
        if (*++repl_inp == '\0') {
            console_stdio = TRUE;  // switch to stdio
        }
    } else {
        console_stdio = TRUE;  // switch to stdio
    }
    c = TO_FIX(c);
    return c;
}

int main(int argc, char const *argv[])
{
    DEBUG(hexdump("repl_lib", ((int_t *)repl_lib), 16));
#if 0
    // display character class table
    printf("| ch | dec | hex | CTL | DGT | UPR | LWR | DLM | SYM | HEX | WSP |\n");
    printf("|----|-----|-----|-----|-----|-----|-----|-----|-----|-----|-----|\n");
    for (int i = 0; i < 0x80; ++i) {
        if (i == 0x7F) {
            printf("| ^? ");
        } else if (char_class[i] & CTL) {
            printf("| ^%c ", (i + '@'));
        } else {
            printf("| %c  ", i);
        }
        printf("| %3d ", i);
        printf("|  %02x ", i);
        printf("|%s", (char_class[i] & CTL) ? "  x  " : "     ");
        printf("|%s", (char_class[i] & DGT) ? "  x  " : "     ");
        printf("|%s", (char_class[i] & UPR) ? "  x  " : "     ");
        printf("|%s", (char_class[i] & LWR) ? "  x  " : "     ");
        printf("|%s", (char_class[i] & DLM) ? "  x  " : "     ");
        printf("|%s", (char_class[i] & SYM) ? "  x  " : "     ");
        printf("|%s", (char_class[i] & HEX) ? "  x  " : "     ");
        printf("|%s", (char_class[i] & WSP) ? "  x  " : "     ");
        printf("|\n");
    }
    // compare with: echo 'ufork' | cksum
    int_t str = cstr_to_list("ufork\n");
    fprintf(stderr, "%"PRIu32" %"PdI"\n", list_crc(str), list_len(str));
#else
    DEBUG(fprintf(stderr, "PROC_MAX=%"PuI" CELL_MAX=%"PuI"\n", PROC_MAX, CELL_MAX));
    //DEBUG(hexdump("cell memory", ((int_t *)cell_zero), 16*4));
    DEBUG(dump_symbol_table());
    init_global_env();
    gc_add_root(clk_handler);
    clk_timeout = clk_ticks();
    int_t result = runtime();
    DEBUG(debug_print("main result", result));
    DEBUG(test_symbol_intern());
    //DEBUG(hexdump("cell memory", ((int_t *)&cell_table[500]), 16*4));
#if MARK_SWEEP_GC
    gc_mark_and_sweep(TRUE);
#endif // MARK_SWEEP_GC
    DEBUG(fprintf(stderr, "cell_top=%"PuI" gc_free_cnt=%"PRId32"\n", cell_top, gc_free_cnt));
#if RUNTIME_STATS
    fprintf(stderr, "events=%ld instructions=%ld\n", event_count, instruction_count);
#endif
#endif
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
