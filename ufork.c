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
PROC_DECL(Unit);
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
#define Unit_T      (-6)
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
    Unit,
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
        "Unit_T",
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

// VM_cvt conversions
#define CVT_LST_NUM (0)
#define CVT_LST_SYM (1)

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

#define CELL_MAX NAT(1<<12)  // 4K cells
cell_t cell_table[CELL_MAX] = {
    { .t=Boolean_T,     .x=UNDEF,       .y=UNDEF,       .z=UNDEF        },  // FALSE = #f
    { .t=Boolean_T,     .x=UNDEF,       .y=UNDEF,       .z=UNDEF        },  // TRUE = #t
    { .t=Null_T,        .x=UNDEF,       .y=UNDEF,       .z=UNDEF        },  // NIL = ()
    { .t=Undef_T,       .x=UNDEF,       .y=UNDEF,       .z=UNDEF        },  // UNDEF = #?
    { .t=Unit_T,        .x=UNDEF,       .y=UNDEF,       .z=UNDEF        },  // UNIT = #unit
    { .t=Event_T,       .x=77,          .y=NIL,         .z=NIL          },  // <--- START = (A_BOOT)
    //{ .t=Event_T,       .x=494,         .y=NIL,         .z=NIL          },  // <--- START = (G_TEST)
    //{ .t=Event_T,       .x=903,         .y=NIL,         .z=NIL          },  // <--- START = (A_TEST)

#define SELF_EVAL (START+1)
    { .t=VM_self,       .x=UNDEF,       .y=START+2,     .z=UNDEF        },  // value = SELF
#define CUST_SEND (START+2)
    { .t=VM_msg,        .x=1,           .y=START+3,     .z=UNDEF        },  // cust
#define SEND_0 (START+3)
    { .t=VM_send,       .x=0,           .y=START+4,     .z=UNDEF        },  // (cust . value)
#define COMMIT (START+4)
    { .t=VM_end,        .x=END_COMMIT,  .y=UNDEF,       .z=UNDEF        },  // commit actor transaction

#define RESEND (COMMIT+1)
    { .t=VM_msg,        .x=0,           .y=RESEND+1,    .z=UNDEF        },  // msg
    { .t=VM_self,       .x=UNDEF,       .y=RESEND+2,    .z=UNDEF        },  // SELF
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (SELF . msg)

#define A_CLOCK (RESEND+3)
    { .t=Actor_T,       .x=A_CLOCK+1,   .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=TO_FIX(-1),  .y=A_CLOCK+2,   .z=UNDEF        },
#define CLOCK_BEH (A_CLOCK+2)
    { .t=VM_msg,        .x=0,           .y=A_CLOCK+3,   .z=UNDEF        },
    { .t=VM_push,       .x=CLOCK_BEH,   .y=A_CLOCK+4,   .z=UNDEF        },
    { .t=VM_beh,        .x=1,           .y=COMMIT,      .z=UNDEF        },

#define S_VALUE (A_CLOCK+5)
//  { .t=VM_push,       .x=_in_,        .y=S_VALUE+0,   .z=UNDEF        },  // (token . next) -or- NIL
    { .t=VM_msg,        .x=0,           .y=SEND_0,      .z=UNDEF        },  // cust

#define S_GETC (S_VALUE+1)
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
    { .t=Actor_T,       .x=G_LANG+1,    .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=UNDEF,       .y=G_CALL_B,    .z=UNDEF        },  // {x:symbol} patched by A_BOOT

/*
(define empty-env
  (CREATE
    (BEH (cust _index)
      (SEND cust #undefined))))
*/
#define EMPTY_ENV (G_LANG+2)
    { .t=Actor_T,       .x=EMPTY_ENV+1, .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=UNDEF,       .y=CUST_SEND,   .z=UNDEF        },

#define REPL_R (EMPTY_ENV+2)
#define REPL_E (REPL_R+8)
#define REPL_P (REPL_E+7)
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

    { .t=Actor_T,       .x=REPL_E+1,    .y=UNDEF,       .z=UNDEF        },
    { .t=VM_msg,        .x=1,           .y=REPL_E+2,    .z=UNDEF        },  // sexpr
    { .t=VM_debug,      .x=TO_FIX(888), .y=REPL_E+3,    .z=UNDEF        },
    { .t=VM_push,       .x=EMPTY_ENV,   .y=REPL_E+4,    .z=UNDEF        },  // env = EMPTY_ENV
    { .t=VM_push,       .x=REPL_P,      .y=REPL_E+5,    .z=UNDEF        },  // cust = REPL_P
    { .t=VM_msg,        .x=1,           .y=REPL_E+6,    .z=UNDEF        },  // sexpr
    { .t=VM_send,       .x=2,           .y=COMMIT,      .z=UNDEF        },  // (sexpr REPL_P EMPTY_ENV)

    { .t=Actor_T,       .x=REPL_P+1,    .y=UNDEF,       .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=REPL_P+2,    .z=UNDEF        },
    { .t=VM_debug,      .x=TO_FIX(999), .y=REPL_L,      .z=UNDEF        },

    { .t=VM_push,       .x=TO_FIX('>'), .y=REPL_L+1,    .z=UNDEF        },
    { .t=VM_putc,       .x=UNDEF,       .y=REPL_L+2,    .z=UNDEF        },
    { .t=VM_push,       .x=TO_FIX(' '), .y=REPL_L+3,    .z=UNDEF        },
    { .t=VM_putc,       .x=UNDEF,       .y=REPL_R,      .z=UNDEF        },

    { .t=Actor_T,       .x=REPL_F+1,    .y=UNDEF,       .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=REPL_F+2,    .z=UNDEF        },
    { .t=VM_debug,      .x=TO_FIX(666), .y=COMMIT,      .z=UNDEF        },

#define A_BOOT (REPL_F+3)
    { .t=Actor_T,       .x=A_BOOT+1,    .y=UNDEF,       .z=UNDEF        },  // <--- A_BOOT
    { .t=VM_push,       .x=G_LANG+1,    .y=A_BOOT+2,    .z=UNDEF        },  // cell to patch
    { .t=VM_push,       .x=NIL,         .y=A_BOOT+3,    .z=UNDEF        },
    { .t=VM_push,       .x=TO_FIX('g'), .y=A_BOOT+4,    .z=UNDEF        },
    { .t=VM_push,       .x=TO_FIX('n'), .y=A_BOOT+5,    .z=UNDEF        },
    { .t=VM_push,       .x=TO_FIX('a'), .y=A_BOOT+6,    .z=UNDEF        },
    { .t=VM_push,       .x=TO_FIX('l'), .y=A_BOOT+7,    .z=UNDEF        },
    { .t=VM_push,       .x=TO_FIX('-'), .y=A_BOOT+8,    .z=UNDEF        },
    { .t=VM_push,       .x=TO_FIX('g'), .y=A_BOOT+9,    .z=UNDEF        },
    { .t=VM_push,       .x=TO_FIX('e'), .y=A_BOOT+10,   .z=UNDEF        },
    { .t=VM_push,       .x=TO_FIX('p'), .y=A_BOOT+11,   .z=UNDEF        },
    { .t=VM_pair,       .x=8,           .y=A_BOOT+12,   .z=UNDEF        },  // "peg-lang"
    { .t=VM_cvt,        .x=CVT_LST_SYM, .y=A_BOOT+13,   .z=UNDEF        },
    { .t=VM_set,        .x=FLD_X,       .y=REPL_L,      .z=UNDEF        },

/*
(define tag-beh
  (lambda (cust)
    (BEH msg
      (SEND cust (cons SELF msg))
    )))
*/
#define TAG_BEH (A_BOOT+14)
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
    { .t=VM_pick,       .x=3,           .y=K_JOIN_H+3,  .z=UNDEF        },  // k_tail
    { .t=VM_cmp,        .x=CMP_EQ,      .y=K_JOIN_H+4,  .z=UNDEF        },  // (tag == k_tail)
    { .t=VM_if,         .x=K_JOIN_H+5,  .y=COMMIT,      .z=UNDEF        },

    { .t=VM_pick,       .x=3,           .y=K_JOIN_H+6,  .z=UNDEF        },  // head
    { .t=VM_pair,       .x=1,           .y=K_JOIN_H+7,  .z=UNDEF        },  // (head . tail = value)
    { .t=VM_pick,       .x=4,           .y=K_JOIN_H+8,  .z=UNDEF        },  // cust
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (cust (head . tail))
#define K_JOIN_T (K_JOIN_H+9)
//  { .t=VM_push,       .x=_cust_,      .y=K_JOIN_T-2,  .z=UNDEF        },
//  { .t=VM_push,       .x=_k_head_,    .y=K_JOIN_T-1,  .z=UNDEF        },
//  { .t=VM_push,       .x=_tail_,      .y=K_JOIN_T+0,  .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=K_JOIN_T+1,  .z=UNDEF        },  // (tag . value)
    { .t=VM_part,       .x=1,           .y=K_JOIN_T+2,  .z=UNDEF        },  // value tag
    { .t=VM_pick,       .x=4,           .y=K_JOIN_T+3,  .z=UNDEF        },  // k_head
    { .t=VM_cmp,        .x=CMP_EQ,      .y=K_JOIN_T+4,  .z=UNDEF        },  // (tag == k_head)
    { .t=VM_if,         .x=K_JOIN_T+5,  .y=COMMIT,      .z=UNDEF        },

    { .t=VM_pair,       .x=1,           .y=K_JOIN_T+6,  .z=UNDEF        },  // (head = value . tail)
    { .t=VM_pick,       .x=3,           .y=K_JOIN_T+7,  .z=UNDEF        },  // cust
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (cust (head . tail))
/*
(define join-beh
  (lambda (cust k_head k_tail)
    (BEH (tag . value))
      ;
      ))
*/
#define JOIN_BEH (K_JOIN_T+8)
//  { .t=VM_push,       .x=_cust_,      .y=JOIN_BEH-2,  .z=UNDEF        },
//  { .t=VM_push,       .x=_k_head_,    .y=JOIN_BEH-1,  .z=UNDEF        },
//  { .t=VM_push,       .x=_k_tail_,    .y=JOIN_BEH+0,  .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=JOIN_BEH+1,  .z=UNDEF        },  // (tag . value)
    { .t=VM_part,       .x=1,           .y=JOIN_BEH+2,  .z=UNDEF        },  // value tag

    { .t=VM_pick,       .x=4,           .y=JOIN_BEH+3,  .z=UNDEF        },  // k_head
    { .t=VM_pick,       .x=2,           .y=JOIN_BEH+4,  .z=UNDEF        },  // tag
    { .t=VM_cmp,        .x=CMP_EQ,      .y=JOIN_BEH+5,  .z=UNDEF        },  // (tag == k_head)
    { .t=VM_if,         .x=JOIN_BEH+6,  .y=JOIN_BEH+11, .z=UNDEF        },

    { .t=VM_pick,       .x=5,           .y=JOIN_BEH+7,  .z=UNDEF        },  // cust
    { .t=VM_pick,       .x=3,           .y=JOIN_BEH+8,  .z=UNDEF        },  // head = value
    { .t=VM_pick,       .x=5,           .y=JOIN_BEH+9,  .z=UNDEF        },  // k_tail
    { .t=VM_push,       .x=K_JOIN_H,    .y=JOIN_BEH+10, .z=UNDEF        },  // K_JOIN_H
    { .t=VM_beh,        .x=3,           .y=COMMIT,      .z=UNDEF        },  // BECOME (K_JOIN_H cust head k_tail)

    { .t=VM_pick,       .x=3,           .y=JOIN_BEH+12, .z=UNDEF        },  // k_tail
    { .t=VM_pick,       .x=2,           .y=JOIN_BEH+13, .z=UNDEF        },  // tag
    { .t=VM_cmp,        .x=CMP_EQ,      .y=JOIN_BEH+14, .z=UNDEF        },  // (tag == k_tail)
    { .t=VM_if,         .x=JOIN_BEH+15, .y=COMMIT,      .z=UNDEF        },

    { .t=VM_pick,       .x=5,           .y=JOIN_BEH+16, .z=UNDEF        },  // cust
    { .t=VM_pick,       .x=5,           .y=JOIN_BEH+17, .z=UNDEF        },  // k_head
    { .t=VM_pick,       .x=4,           .y=JOIN_BEH+18, .z=UNDEF        },  // tail = value
    { .t=VM_push,       .x=K_JOIN_T,    .y=JOIN_BEH+19, .z=UNDEF        },  // K_JOIN_T
    { .t=VM_beh,        .x=3,           .y=COMMIT,      .z=UNDEF        },  // BECOME (K_JOIN_T cust k_head tail)
/*
(define fork-beh
  (lambda (cust head tail)
    (BEH (h-req t-req))
      ;
      ))
*/
#define FORK_BEH (JOIN_BEH+20)
//  { .t=VM_push,       .x=_cust_,      .y=FORK_BEH-2,  .z=UNDEF        },
//  { .t=VM_push,       .x=_head_,      .y=FORK_BEH-1,  .z=UNDEF        },
//  { .t=VM_push,       .x=_tail_,      .y=FORK_BEH+0,  .z=UNDEF        },
    { .t=VM_pick,       .x=3,           .y=FORK_BEH+1,  .z=UNDEF        },  // cust

    { .t=VM_self,       .x=UNDEF,       .y=FORK_BEH+2,  .z=UNDEF        },  // self
    { .t=VM_push,       .x=TAG_BEH,     .y=FORK_BEH+3,  .z=UNDEF        },  // TAG_BEH
    { .t=VM_new,        .x=1,           .y=FORK_BEH+4,  .z=UNDEF        },  // k_head

    { .t=VM_self,       .x=UNDEF,       .y=FORK_BEH+5,  .z=UNDEF        },  // self
    { .t=VM_push,       .x=TAG_BEH,     .y=FORK_BEH+6,  .z=UNDEF        },  // TAG_BEH
    { .t=VM_new,        .x=1,           .y=FORK_BEH+7,  .z=UNDEF        },  // k_tail

    { .t=VM_msg,        .x=1,           .y=FORK_BEH+8,  .z=UNDEF        },  // h_req
    { .t=VM_pick,       .x=3,           .y=FORK_BEH+9,  .z=UNDEF        },  // k_head
    { .t=VM_pair,       .x=1,           .y=FORK_BEH+10, .z=UNDEF        },  // (k_head . h_req)
    { .t=VM_pick,       .x=6,           .y=FORK_BEH+11, .z=UNDEF        },  // head
    { .t=VM_send,       .x=0,           .y=FORK_BEH+12, .z=UNDEF        },  // (head k_head . h_req)

    { .t=VM_msg,        .x=2,           .y=FORK_BEH+13, .z=UNDEF        },  // t_req
    { .t=VM_pick,       .x=2,           .y=FORK_BEH+14, .z=UNDEF        },  // k_tail
    { .t=VM_pair,       .x=1,           .y=FORK_BEH+15, .z=UNDEF        },  // (k_tail . t_req)
    { .t=VM_pick,       .x=5,           .y=FORK_BEH+16, .z=UNDEF        },  // tail
    { .t=VM_send,       .x=0,           .y=FORK_BEH+17, .z=UNDEF        },  // (tail k_tail . t_req)

    { .t=VM_push,       .x=JOIN_BEH,    .y=FORK_BEH+18, .z=UNDEF        },  // JOIN_BEH
    { .t=VM_beh,        .x=3,           .y=COMMIT,      .z=UNDEF        },  // BECOME (JOIN_BEH cust k_head k_tail)

/*
(define evlis-beh
  (lambda (param)
    (BEH (cust env)           ; eval
      (if (pair? param)
        (SEND
          (CREATE (fork-beh
            cust
            (car param)
            (CREATE (evlis-beh (cdr param))))) ; could BECOME this...
          (list
            (list env)
            (list env)))
        (SEND param (list cust env))))))
*/
#define EVLIS_BEH (FORK_BEH+19)
//  { .t=VM_push,       .x=_param_,     .y=EVLIS_BEH+0, .z=UNDEF        },
    { .t=VM_pick,       .x=1,           .y=EVLIS_BEH+1, .z=UNDEF        },  // param
    { .t=VM_typeq,      .x=Pair_T,      .y=EVLIS_BEH+2, .z=UNDEF        },  // param has type Pair_T
    { .t=VM_if,         .x=EVLIS_BEH+6, .y=EVLIS_BEH+3, .z=UNDEF        },

    { .t=VM_msg,        .x=0,           .y=EVLIS_BEH+4, .z=UNDEF        },  // (cust env)  ; eval
    { .t=VM_pick,       .x=2,           .y=EVLIS_BEH+5, .z=UNDEF        },  // param
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (param cust env)

    { .t=VM_pick,       .x=1,           .y=EVLIS_BEH+7, .z=UNDEF        },  // param
    { .t=VM_part,       .x=1,           .y=EVLIS_BEH+8, .z=UNDEF        },  // rest first

    { .t=VM_pick,       .x=2,           .y=EVLIS_BEH+9, .z=UNDEF        },  // rest
    { .t=VM_push,       .x=EVLIS_BEH,   .y=EVLIS_BEH+10,.z=UNDEF        },  // EVLIS_BEH
    { .t=VM_beh,        .x=1,           .y=EVLIS_BEH+11,.z=UNDEF        },  // BECOME (evlis-beh rest)

    { .t=VM_msg,        .x=1,           .y=EVLIS_BEH+12,.z=UNDEF        },  // cust
    { .t=VM_pick,       .x=2,           .y=EVLIS_BEH+13,.z=UNDEF        },  // first
    { .t=VM_self,       .x=UNDEF,       .y=EVLIS_BEH+14,.z=UNDEF        },  // SELF
    { .t=VM_push,       .x=FORK_BEH,    .y=EVLIS_BEH+15,.z=UNDEF        },  // FORK_BEH
    { .t=VM_new,        .x=3,           .y=EVLIS_BEH+16,.z=UNDEF        },  // ev_fork

    { .t=VM_msg,        .x=-1,          .y=EVLIS_BEH+17,.z=UNDEF        },  // (env)
    { .t=VM_pick,       .x=1,           .y=EVLIS_BEH+18,.z=UNDEF        },  // t_req h_req
    { .t=VM_pick,       .x=3,           .y=EVLIS_BEH+19,.z=UNDEF        },  // ev_fork
    { .t=VM_send,       .x=2,           .y=COMMIT,      .z=UNDEF        },  // (ev_fork h_req t_req)

/*
(define k-call-beh  ; used directly by Pair_T
  (lambda (msg)
    (BEH oper
      (SEND oper msg))))
*/
#define K_CALL (EVLIS_BEH+20)
//  { .t=VM_push,       .x=_msg_,       .y=K_CALL+0,    .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=K_CALL+1,    .z=UNDEF        },  // oper
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (oper . msg)

//
// Parsing Expression Grammar (PEG) behaviors
//
#define G_EMPTY (K_CALL+2)
    { .t=Actor_T,       .x=G_EMPTY+1,   .y=UNDEF,       .z=UNDEF        },
#define G_EMPTY_B (G_EMPTY+1)
    { .t=VM_msg,        .x=-2,          .y=G_EMPTY+2,   .z=UNDEF        },  // in
    { .t=VM_push,       .x=NIL,         .y=G_EMPTY+3,   .z=UNDEF        },  // ()
    { .t=VM_pair,       .x=1,           .y=G_EMPTY+4,   .z=UNDEF        },  // (() . in)
    { .t=VM_msg,        .x=1,           .y=G_EMPTY+5,   .z=UNDEF        },  // custs = (ok . fail)
    { .t=VM_get,        .x=FLD_X,       .y=G_EMPTY+6,   .z=UNDEF        },  // ok
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (ok () . in)

#define G_FAIL (G_EMPTY+7)
    { .t=Actor_T,       .x=G_FAIL+1,    .y=UNDEF,       .z=UNDEF        },
#define G_FAIL_B (G_FAIL+1)
    { .t=VM_msg,        .x=-2,          .y=G_FAIL+2,    .z=UNDEF        },  // in
    { .t=VM_msg,        .x=1,           .y=G_FAIL+3,    .z=UNDEF        },  // custs = (ok . fail)
    { .t=VM_get,        .x=FLD_Y,       .y=G_FAIL+4,    .z=UNDEF        },  // fail
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (fail . in)

#define G_NEXT_K (G_FAIL+5)
//  { .t=VM_push,       .x=_cust_,      .y=G_NEXT_K-1,  .z=UNDEF        },
//  { .t=VM_push,       .x=_value_,     .y=G_NEXT_K+0,  .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=G_NEXT_K+1,  .z=UNDEF        },  // in
    { .t=VM_pick,       .x=2,           .y=G_NEXT_K+2,  .z=UNDEF        },  // value
    { .t=VM_pair,       .x=1,           .y=G_NEXT_K+3,  .z=UNDEF        },  // (value . in)
    { .t=VM_pick,       .x=3,           .y=G_NEXT_K+4,  .z=UNDEF        },  // cust
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (cust value . in)

#define G_ANY (G_NEXT_K+5)
    { .t=Actor_T,       .x=G_ANY+1,     .y=UNDEF,       .z=UNDEF        },
    { .t=VM_msg,        .x=1,           .y=G_ANY+2,     .z=UNDEF        },  // custs = (ok . fail)
    { .t=VM_part,       .x=1,           .y=G_ANY+3,     .z=UNDEF        },  // fail ok
    { .t=VM_msg,        .x=-2,          .y=G_ANY+4,     .z=UNDEF        },  // in
    { .t=VM_eq,         .x=NIL,         .y=G_ANY+5,     .z=UNDEF        },  // in == ()
    { .t=VM_if,         .x=G_ANY+14,    .y=G_ANY+6,     .z=UNDEF        },

    { .t=VM_msg,        .x=-2,          .y=G_ANY+7,     .z=UNDEF        },  // in
    { .t=VM_part,       .x=1,           .y=G_ANY+8,     .z=UNDEF        },  // next token
    { .t=VM_pick,       .x=3,           .y=G_ANY+9,     .z=UNDEF        },  // ok
    { .t=VM_pick,       .x=2,           .y=G_ANY+10,    .z=UNDEF        },  // token
    { .t=VM_push,       .x=G_NEXT_K,    .y=G_ANY+11,    .z=UNDEF        },  // G_NEXT_K
    { .t=VM_new,        .x=2,           .y=G_ANY+12,    .z=UNDEF        },  // k_next
    { .t=VM_pick,       .x=3,           .y=G_ANY+13,    .z=UNDEF        },  // next
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (next . k_next)

    { .t=VM_push,       .x=NIL,         .y=G_ANY+15,    .z=UNDEF        },  // ()
    { .t=VM_pick,       .x=3,           .y=G_ANY+16,    .z=UNDEF        },  // fail
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (fail . ())

#define G_EQ_B (G_ANY+17)
//  { .t=VM_push,       .x=_value_,     .y=G_EQ_B+0,    .z=UNDEF        },
    { .t=VM_msg,        .x=1,           .y=G_EQ_B+1,    .z=UNDEF        },  // custs = (ok . fail)
    { .t=VM_part,       .x=1,           .y=G_EQ_B+2,    .z=UNDEF        },  // fail ok
    { .t=VM_msg,        .x=-2,          .y=G_EQ_B+3,    .z=UNDEF        },  // in
    { .t=VM_eq,         .x=NIL,         .y=G_EQ_B+4,    .z=UNDEF        },  // in == ()
    { .t=VM_if,         .x=G_EQ_B+18,   .y=G_EQ_B+5,    .z=UNDEF        },

    { .t=VM_msg,        .x=-2,          .y=G_EQ_B+6,    .z=UNDEF        },  // in
    { .t=VM_part,       .x=1,           .y=G_EQ_B+7,    .z=UNDEF        },  // next token
    { .t=VM_pick,       .x=1,           .y=G_EQ_B+8,    .z=UNDEF        },  // token token
    { .t=VM_pick,       .x=6,           .y=G_EQ_B+9,    .z=UNDEF        },  // value
    { .t=VM_cmp,        .x=CMP_NE,      .y=G_EQ_B+10,   .z=UNDEF        },  // token != value
    { .t=VM_if,         .x=G_EQ_B+17,   .y=G_EQ_B+11,   .z=UNDEF        },

    { .t=VM_pick,       .x=3,           .y=G_EQ_B+12,   .z=UNDEF        },  // ok
    { .t=VM_pick,       .x=2,           .y=G_EQ_B+13,   .z=UNDEF        },  // token
    { .t=VM_push,       .x=G_NEXT_K,    .y=G_EQ_B+14,   .z=UNDEF        },  // G_NEXT_K
    { .t=VM_new,        .x=2,           .y=G_EQ_B+15,   .z=UNDEF        },  // k_next
    { .t=VM_pick,       .x=3,           .y=G_EQ_B+16,   .z=UNDEF        },  // next
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (next . k_next)

    { .t=VM_drop,       .x=2,           .y=G_EQ_B+18,   .z=UNDEF        },  // fail ok

    { .t=VM_msg,        .x=-2,          .y=G_EQ_B+19,   .z=UNDEF        },  // in
    { .t=VM_pick,       .x=3,           .y=G_EQ_B+20,   .z=UNDEF        },  // fail
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (fail . in)

#define G_OR_F (G_EQ_B+21)
//  { .t=VM_push,       .x=_restart_,   .y=G_OR_F-1,    .z=UNDEF        },
//  { .t=VM_push,       .x=_rest_,      .y=G_OR_F+0,    .z=UNDEF        },
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (rest . restart)
#define G_OR_B (G_OR_F+1)
//  { .t=VM_push,       .x=_first_,     .y=G_OR_B-1,    .z=UNDEF        },
//  { .t=VM_push,       .x=_rest_,      .y=G_OR_B+0,    .z=UNDEF        },
    { .t=VM_msg,        .x=-1,          .y=G_OR_B+1,    .z=UNDEF        },  // resume = (value . in)

    { .t=VM_msg,        .x=0,           .y=G_OR_B+2,    .z=UNDEF        },  // restart = (custs value . in)
    { .t=VM_pick,       .x=3,           .y=G_OR_B+3,    .z=UNDEF        },  // rest
    { .t=VM_push,       .x=G_OR_F,      .y=G_OR_B+4,    .z=UNDEF        },  // G_OR_F
    { .t=VM_new,        .x=2,           .y=G_OR_B+5,    .z=UNDEF        },  // or_fail

    { .t=VM_msg,        .x=1,           .y=G_OR_B+6,    .z=UNDEF        },  // custs
    { .t=VM_get,        .x=FLD_X,       .y=G_OR_B+7,    .z=UNDEF        },  // ok
    { .t=VM_pair,       .x=1,           .y=G_OR_B+8,    .z=UNDEF        },  // (ok . or_fail)
    { .t=VM_pair,       .x=1,           .y=G_OR_B+9,    .z=UNDEF        },  // ((ok . or_fail) . resume)

    { .t=VM_pick,       .x=3,           .y=G_OR_B+10,   .z=UNDEF        },  // first
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (first (ok . or_fail) . resume)

#define G_AND_PR (G_OR_B+11)
//  { .t=VM_push,       .x=_cust_,      .y=G_AND_PR-1,  .z=UNDEF        },
//  { .t=VM_push,       .x=_head_,      .y=G_AND_PR+0,  .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=G_AND_PR+1,  .z=UNDEF        },  // (value . in)
    { .t=VM_part,       .x=1,           .y=G_AND_PR+2,  .z=UNDEF        },  // in tail
    { .t=VM_pick,       .x=3,           .y=G_AND_PR+3,  .z=UNDEF        },  // head
    { .t=VM_pair,       .x=1,           .y=G_AND_PR+4,  .z=UNDEF        },  // (head . tail)
    { .t=VM_pair,       .x=1,           .y=G_AND_PR+5,  .z=UNDEF        },  // ((head . tail) . in)
    { .t=VM_pick,       .x=3,           .y=G_AND_PR+6,  .z=UNDEF        },  // cust
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (cust (head . tail) . in)
#define G_AND_OK (G_AND_PR+7)
//  { .t=VM_push,       .x=_custs_,     .y=G_AND_OK-1,  .z=UNDEF        },  // (ok . and_fail)
//  { .t=VM_push,       .x=_rest_,      .y=G_AND_OK+0,  .z=UNDEF        },
    { .t=VM_pick,       .x=2,           .y=G_AND_OK+1,  .z=UNDEF        },  // custs
    { .t=VM_part,       .x=1,           .y=G_AND_OK+2,  .z=UNDEF        },  // and_fail ok
    { .t=VM_msg,        .x=1,           .y=G_AND_OK+3,  .z=UNDEF        },  // value
    { .t=VM_push,       .x=G_AND_PR,    .y=G_AND_OK+4,  .z=UNDEF        },  // G_AND_PR
    { .t=VM_new,        .x=2,           .y=G_AND_OK+5,  .z=UNDEF        },  // and_pair
    { .t=VM_msg,        .x=0,           .y=G_AND_OK+6,  .z=UNDEF        },  // resume = (value . in)
    { .t=VM_pick,       .x=3,           .y=G_AND_OK+7,  .z=UNDEF        },  // and_fail
    { .t=VM_pick,       .x=3,           .y=G_AND_OK+8,  .z=UNDEF        },  // and_pair
    { .t=VM_pair,       .x=1,           .y=G_AND_OK+9,  .z=UNDEF        },  // (and_pair . and_fail)
    { .t=VM_pair,       .x=1,           .y=G_AND_OK+10, .z=UNDEF        },  // ((and_pair . and_fail) . resume)
    { .t=VM_pick,       .x=4,           .y=G_AND_OK+11, .z=UNDEF        },  // rest
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (rest (and_pair . and_fail) . resume)
#define G_AND_F (G_AND_OK+12)
//  { .t=VM_push,       .x=_restart_,   .y=G_AND_F-1,   .z=UNDEF        },
//  { .t=VM_push,       .x=_rest_,      .y=G_AND_F+0,   .z=UNDEF        },
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (rest . restart)
#define G_AND_B (G_AND_F+1)
//  { .t=VM_push,       .x=_first_,     .y=G_AND_B-1,   .z=UNDEF        },
//  { .t=VM_push,       .x=_rest_,      .y=G_AND_B+0,   .z=UNDEF        },
    { .t=VM_msg,        .x=-1,          .y=G_AND_B+1,   .z=UNDEF        },  // resume = (value . in)
    { .t=VM_msg,        .x=1,           .y=G_AND_B+2,   .z=UNDEF        },  // custs
    { .t=VM_get,        .x=FLD_Y,       .y=G_AND_B+3,   .z=UNDEF        },  // fail

    { .t=VM_msg,        .x=0,           .y=G_AND_B+4,   .z=UNDEF        },  // restart = (custs value . in)
    { .t=VM_pick,       .x=4,           .y=G_AND_B+5,   .z=UNDEF        },  // rest
    { .t=VM_push,       .x=G_AND_F,     .y=G_AND_B+6,   .z=UNDEF        },  // G_AND_F
    { .t=VM_new,        .x=2,           .y=G_AND_B+7,   .z=UNDEF        },  // and_fail

    { .t=VM_msg,        .x=1,           .y=G_AND_B+8,   .z=UNDEF        },  // custs
    { .t=VM_get,        .x=FLD_X,       .y=G_AND_B+9,   .z=UNDEF        },  // ok
    { .t=VM_pair,       .x=1,           .y=G_AND_B+10,  .z=UNDEF        },  // (ok . and_fail)
    { .t=VM_pick,       .x=4,           .y=G_AND_B+11,  .z=UNDEF        },  // rest
    { .t=VM_push,       .x=G_AND_OK,    .y=G_AND_B+12,  .z=UNDEF        },  // G_AND_OK
    { .t=VM_new,        .x=2,           .y=G_AND_B+13,  .z=UNDEF        },  // and_ok

    { .t=VM_pair,       .x=1,           .y=G_AND_B+14,  .z=UNDEF        },  // (and_ok . fail)
    { .t=VM_pair,       .x=1,           .y=G_AND_B+15,  .z=UNDEF        },  // ((and_ok . fail) . resume)
    { .t=VM_pick,       .x=3,           .y=G_AND_B+16,  .z=UNDEF        },  // first
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (first (and_ok . fail) . resume)

/*
Optional(pattern) = Or(And(pattern, Empty), Empty)
Plus(pattern) = And(pattern, Star(pattern))
Star(pattern) = Or(Plus(pattern), Empty)
*/
#define G_OPT_B (G_AND_B+17)
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
    { .t=VM_if,         .x=G_CLS_B+19,  .y=G_CLS_B+5,   .z=UNDEF        },

    { .t=VM_msg,        .x=-2,          .y=G_CLS_B+6,   .z=UNDEF        },  // in
    { .t=VM_part,       .x=1,           .y=G_CLS_B+7,   .z=UNDEF        },  // next token
    { .t=VM_pick,       .x=1,           .y=G_CLS_B+8,   .z=UNDEF        },  // token token
    { .t=VM_pick,       .x=6,           .y=G_CLS_B+9,   .z=UNDEF        },  // class
    { .t=VM_cmp,        .x=CMP_CLS,     .y=G_CLS_B+10,  .z=UNDEF        },  // token in class
    { .t=VM_eq,         .x=FALSE,       .y=G_CLS_B+11,  .z=UNDEF        },  // token ~in class
    { .t=VM_if,         .x=G_CLS_B+18,  .y=G_CLS_B+12,  .z=UNDEF        },

    { .t=VM_pick,       .x=3,           .y=G_CLS_B+13,  .z=UNDEF        },  // ok
    { .t=VM_pick,       .x=2,           .y=G_CLS_B+14,  .z=UNDEF        },  // token
    { .t=VM_push,       .x=G_NEXT_K,    .y=G_CLS_B+15,  .z=UNDEF        },  // G_NEXT_K
    { .t=VM_new,        .x=2,           .y=G_CLS_B+16,  .z=UNDEF        },  // k_next
    { .t=VM_pick,       .x=3,           .y=G_CLS_B+17,  .z=UNDEF        },  // next
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (next . k_next)

    { .t=VM_drop,       .x=2,           .y=G_CLS_B+19,  .z=UNDEF        },  // fail ok

    { .t=VM_msg,        .x=-2,          .y=G_CLS_B+20,  .z=UNDEF        },  // in
    { .t=VM_pick,       .x=3,           .y=G_CLS_B+21,  .z=UNDEF        },  // fail
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (fail . in)

/*
(define op-se-beh             ; self-evaluating operative
  (lambda (oper)              ; (oper cust arg denv)
    (BEH (cust arg . opt-env)
      (if (null? opt-env)
        (SEND cust SELF)      ; eval
        (SEND oper            ; apply
          (list* cust arg opt-env))
      ))))
*/
#define OP_SE_BEH (G_CLS_B+22)
//  { .t=VM_push,       .x=_oper_,      .y=OP_SE_BEH+0, .z=UNDEF        },
    { .t=VM_msg,        .x=-2,          .y=OP_SE_BEH+1, .z=UNDEF        },  // opt-env
    { .t=VM_eq,         .x=NIL,         .y=OP_SE_BEH+2, .z=UNDEF        },  // opt-env == ()
    { .t=VM_if,         .x=SELF_EVAL,   .y=OP_SE_BEH+3, .z=UNDEF        },

    { .t=VM_msg,        .x=0,           .y=OP_SE_BEH+4, .z=UNDEF        },  // (cust arg denv)
    { .t=VM_pick,       .x=2,           .y=OP_SE_BEH+5, .z=UNDEF        },  // oper
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (oper cust arg denv)

#define G_XFM_K (OP_SE_BEH+6)
//  { .t=VM_push,       .x=_ok_,        .y=G_XFM_K-1,   .z=UNDEF        },
//  { .t=VM_push,       .x=_in_,        .y=G_XFM_K+0,   .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=G_XFM_K+1,   .z=UNDEF        },  // value
    { .t=VM_pair,       .x=1,           .y=G_XFM_K+2,   .z=UNDEF        },  // (value . in)
    { .t=VM_pick,       .x=2,           .y=G_XFM_K+3,   .z=UNDEF        },  // ok
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (ok value . in)

#define G_XFM_OK (G_XFM_K+4)
//  { .t=VM_push,       .x=_cust_,      .y=G_XFM_OK-1,  .z=UNDEF        },
//  { .t=VM_push,       .x=_oper_,      .y=G_XFM_OK+0,  .z=UNDEF        },
    { .t=VM_push,       .x=EMPTY_ENV,   .y=G_XFM_OK+1,  .z=UNDEF        },  // denv = EMPTY_ENV
    { .t=VM_msg,        .x=1,           .y=G_XFM_OK+2,  .z=UNDEF        },  // arg = value
    { .t=VM_pick,       .x=4,           .y=G_XFM_OK+3,  .z=UNDEF        },  // cust
    { .t=VM_msg,        .x=-1,          .y=G_XFM_OK+4,  .z=UNDEF        },  // in
    { .t=VM_push,       .x=G_XFM_K,     .y=G_XFM_OK+5,  .z=UNDEF        },  // G_XFM_K
    { .t=VM_new,        .x=2,           .y=G_XFM_OK+6,  .z=UNDEF        },  // k_xfm
    { .t=VM_pick,       .x=4,           .y=G_XFM_OK+7,  .z=UNDEF        },  // oper
    { .t=VM_send,       .x=3,           .y=COMMIT,      .z=UNDEF        },  // (oper k_xfm arg denv)

#define G_XFORM_B (G_XFM_OK+8)
//  { .t=VM_push,       .x=_oper_,      .y=G_XFORM_B-1, .z=UNDEF        },
//  { .t=VM_push,       .x=_ptrn_,      .y=G_XFORM_B+0, .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=G_XFORM_B+1, .z=UNDEF        },  // (custs . resume)
    { .t=VM_part,       .x=1,           .y=G_XFORM_B+2, .z=UNDEF        },  // resume custs
    { .t=VM_part,       .x=1,           .y=G_XFORM_B+3, .z=UNDEF        },  // fail ok

    { .t=VM_pick,       .x=5,           .y=G_XFORM_B+4, .z=UNDEF        },  // oper
    { .t=VM_push,       .x=G_XFM_OK,    .y=G_XFORM_B+5, .z=UNDEF        },  // G_XFM_OK
    { .t=VM_new,        .x=2,           .y=G_XFORM_B+6, .z=UNDEF        },  // ok'

    { .t=VM_pair,       .x=1,           .y=G_XFORM_B+7, .z=UNDEF        },  // custs = (ok' . fail)
    { .t=VM_pair,       .x=1,           .y=G_XFORM_B+8, .z=UNDEF        },  // msg = (custs . resume)
    { .t=VM_pick,       .x=2,           .y=G_XFORM_B+9, .z=UNDEF        },  // ptrn
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (ptrn (ok' . fail) . resume)

//
// Pre-defined PEGs
//
#define G_WSP (G_XFORM_B+10)
    { .t=Actor_T,       .x=G_WSP+1,     .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=WSP,         .y=G_CLS_B,     .z=UNDEF        },  // class = whitespace

#define G_WSP_S (G_WSP+2)
    { .t=Actor_T,       .x=G_WSP_S+1,   .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=G_WSP,       .y=G_STAR_B,    .z=UNDEF        },  // (Star Wsp)

#define G_SGN (G_WSP_S+2)
    { .t=Actor_T,       .x=G_SGN+1,     .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=TO_FIX('-'), .y=G_EQ_B,      .z=UNDEF        },  // value = '-' = 45

#define G_OPEN (G_SGN+2)
    { .t=Actor_T,       .x=G_OPEN+1,    .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=TO_FIX('('), .y=G_EQ_B,      .z=UNDEF        },  // value = '(' = 40

#define G_CLOSE (G_OPEN+2)
    { .t=Actor_T,       .x=G_CLOSE+1,   .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=TO_FIX(')'), .y=G_EQ_B,      .z=UNDEF        },  // value = ')' = 41

#define G_DGT (G_CLOSE+2)
    { .t=Actor_T,       .x=G_DGT+1,     .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=DGT,         .y=G_CLS_B,     .z=UNDEF        },  // class = [0-9]

#define G_UPR (G_DGT+2)
    { .t=Actor_T,       .x=G_UPR+1,     .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=UPR,         .y=G_CLS_B,     .z=UNDEF        },  // class = [A-Z]

#define G_LWR (G_UPR+2)
    { .t=Actor_T,       .x=G_LWR+1,     .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=LWR,         .y=G_CLS_B,     .z=UNDEF        },  // class = [a-z]

#define G_ATOM (G_LWR+2)
    { .t=Actor_T,       .x=G_ATOM+1,    .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,   .x=DGT|LWR|UPR|SYM, .y=G_CLS_B,     .z=UNDEF        },  // class = [0-9A-Za-z...]

#define G_SGN_O (G_ATOM+2)
    { .t=Actor_T,       .x=G_SGN_O+1,   .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=NIL,         .y=G_SGN_O+2,   .z=UNDEF        },  // ()
    { .t=VM_push,       .x=G_EMPTY,     .y=G_SGN_O+3,   .z=UNDEF        },  // Empty
    { .t=VM_push,       .x=TO_FIX('+'), .y=G_SGN_O+4,   .z=UNDEF        },  // value = '+' = 43
    { .t=VM_push,       .x=G_EQ_B,      .y=G_SGN_O+5,   .z=UNDEF        },  // G_EQ_B
    { .t=VM_new,        .x=1,           .y=G_SGN_O+6,   .z=UNDEF        },  // (Eq '+')
    { .t=VM_push,       .x=G_SGN,       .y=G_SGN_O+7,   .z=UNDEF        },  // (Eq '-')
    { .t=VM_pair,       .x=3,           .y=G_ALT_B,     .z=UNDEF        },  // (Alt (Eq '-') (Eq '+') Empty)

#define G_DGT_OK (G_SGN_O+8)
//  { .t=VM_push,       .x=_cust_,      .y=G_DGT_OK+0,  .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=G_DGT_OK+1,  .z=UNDEF        },  // (value . in)
    { .t=VM_part,       .x=1,           .y=G_DGT_OK+2,  .z=UNDEF        },  // in value
    { .t=VM_cvt,        .x=CVT_LST_NUM, .y=G_DGT_OK+3,  .z=UNDEF        },  // fixnum
    { .t=VM_pair,       .x=1,           .y=G_DGT_OK+4,  .z=UNDEF        },  // (fixnum . in)
    { .t=VM_pick,       .x=2,           .y=G_DGT_OK+5,  .z=UNDEF        },  // cust
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (cust fixnum . in)

#define G_DGT_P (G_DGT_OK+6)
    { .t=Actor_T,       .x=G_DGT_P+1,   .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=G_DGT,       .y=G_PLUS_B,    .z=UNDEF        },  // (Plus Dgt)

#define G_FIXNUM (G_DGT_P+2)
    { .t=Actor_T,       .x=G_FIXNUM+1,  .y=UNDEF,       .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=G_FIXNUM+2,  .z=UNDEF        },  // (custs . resume)
    { .t=VM_part,       .x=1,           .y=G_FIXNUM+3,  .z=UNDEF        },  // resume custs
    { .t=VM_part,       .x=1,           .y=G_FIXNUM+4,  .z=UNDEF        },  // fail ok
    { .t=VM_push,       .x=G_DGT_OK,    .y=G_FIXNUM+5,  .z=UNDEF        },  // G_DGT_OK
    { .t=VM_new,        .x=1,           .y=G_FIXNUM+6,  .z=UNDEF        },  // ok'
    { .t=VM_pair,       .x=1,           .y=G_FIXNUM+7,  .z=UNDEF        },  // custs = (ok' . fail)
    { .t=VM_pair,       .x=1,           .y=G_FIXNUM+8,  .z=UNDEF        },  // msg = (custs . resume)
    { .t=VM_push,       .x=G_DGT_P,     .y=G_FIXNUM+9,  .z=UNDEF        },  // G_DGT_P
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (G_DGT_P (ok' . fail) . resume)

#define G_ATOM_OK (G_FIXNUM+10)
//  { .t=VM_push,       .x=_cust_,      .y=G_ATOM_OK+0, .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=G_ATOM_OK+1, .z=UNDEF        },  // (value . in)
    { .t=VM_part,       .x=1,           .y=G_ATOM_OK+2, .z=UNDEF        },  // in value
    { .t=VM_cvt,        .x=CVT_LST_SYM, .y=G_ATOM_OK+3, .z=UNDEF        },  // symbol
    { .t=VM_pair,       .x=1,           .y=G_ATOM_OK+4, .z=UNDEF        },  // (symbol . in)
    { .t=VM_pick,       .x=2,           .y=G_ATOM_OK+5, .z=UNDEF        },  // cust
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (cust symbol . in)

#define G_ATOM_P (G_ATOM_OK+6)
    { .t=Actor_T,       .x=G_ATOM_P+1,  .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=G_ATOM,      .y=G_PLUS_B,    .z=UNDEF        },  // (Plus Atom)

#define G_SYMBOL (G_ATOM_P+2)
    { .t=Actor_T,       .x=G_SYMBOL+1,  .y=UNDEF,       .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=G_SYMBOL+2,  .z=UNDEF        },  // (custs . resume)
    { .t=VM_part,       .x=1,           .y=G_SYMBOL+3,  .z=UNDEF        },  // resume custs
    { .t=VM_part,       .x=1,           .y=G_SYMBOL+4,  .z=UNDEF        },  // fail ok
    { .t=VM_push,       .x=G_ATOM_OK,   .y=G_SYMBOL+5,  .z=UNDEF        },  // G_ATOM_OK
    { .t=VM_new,        .x=1,           .y=G_SYMBOL+6,  .z=UNDEF        },  // ok'
    { .t=VM_pair,       .x=1,           .y=G_SYMBOL+7,  .z=UNDEF        },  // custs = (ok' . fail)
    { .t=VM_pair,       .x=1,           .y=G_SYMBOL+8,  .z=UNDEF        },  // msg = (custs . resume)
    { .t=VM_push,       .x=G_ATOM_P,    .y=G_SYMBOL+9,  .z=UNDEF        },  // G_ATOM_P
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (G_ATOM_P (ok' . fail) . resume)

#define O_CADR (G_SYMBOL+10)
    { .t=Actor_T,       .x=O_CADR+1,    .y=UNDEF,       .z=UNDEF        },  // (cadr cust arg denv)
    { .t=VM_msg,        .x=2,           .y=O_CADR+2,    .z=UNDEF        },  // arg
    { .t=VM_get,        .x=FLD_Y,       .y=O_CADR+3,    .z=UNDEF        },  // cdr(arg)
    { .t=VM_get,        .x=FLD_X,       .y=O_CADR+4,    .z=UNDEF        },  // car(cdr(arg))
    { .t=VM_msg,        .x=1,           .y=O_CADR+5,    .z=UNDEF        },  // cust
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (cust . car(cdr(arg)))
#define OP_CADR (O_CADR+6)
    { .t=Actor_T,       .x=OP_CADR+1,   .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=O_CADR,      .y=OP_SE_BEH,   .z=UNDEF        },

/*
sexpr = Seq(Star(Wsp), alt_ex) -> cadr = (lambda (x) (car (cdr x)))
alt_ex = Alt(list, fixnum, symbol)
list = Seq('(', Star(sexpr), Star(Wsp), ')') -> cadr
fixnum = Plus(Dgt) -> fixnum
symbol = Plus(Atom) -> symbol
*/
#define G_SEXPR (OP_CADR+2)
#define G_SEXPR_X (G_SEXPR+5)
#define G_SEXPR_S (G_SEXPR_X+3)
#define G_ALT_EX (G_SEXPR_S+2)
#define G_LIST (G_ALT_EX+6)
#define G_LIST_X (G_LIST+7)
    { .t=Actor_T,       .x=G_SEXPR+1,   .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=NIL,         .y=G_SEXPR+2,   .z=UNDEF        },  // ()
    { .t=VM_push,       .x=G_ALT_EX,    .y=G_SEXPR+3,   .z=UNDEF        },  // G_ALT_EX
    { .t=VM_push,       .x=G_WSP_S,     .y=G_SEXPR+4,   .z=UNDEF        },  // (Star Wsp)
    { .t=VM_pair,       .x=2,           .y=G_SEQ_B,     .z=UNDEF        },  // (Seq (Star Wsp) (Alt ...))

    { .t=Actor_T,       .x=G_SEXPR_X+1, .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=OP_CADR,     .y=G_SEXPR_X+2, .z=UNDEF        },  // cadr
    { .t=VM_push,       .x=G_SEXPR,     .y=G_XFORM_B,   .z=UNDEF        },  // (xform Sexpr cadr)

    { .t=Actor_T,       .x=G_SEXPR_S+1, .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=G_SEXPR_X,   .y=G_STAR_B,    .z=UNDEF        },  // (Star Sexpr)

    { .t=Actor_T,       .x=G_ALT_EX+1,  .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=NIL,         .y=G_ALT_EX+2,  .z=UNDEF        },  // ()
    { .t=VM_push,       .x=G_SYMBOL,    .y=G_ALT_EX+3,  .z=UNDEF        },  // Symbol
    { .t=VM_push,       .x=G_FIXNUM,    .y=G_ALT_EX+4,  .z=UNDEF        },  // Fixnum
    { .t=VM_push,       .x=G_LIST_X,    .y=G_ALT_EX+5,  .z=UNDEF        },  // List
    { .t=VM_pair,       .x=3,           .y=G_ALT_B,     .z=UNDEF        },  // (Alt List Fixnum Symbol)

    { .t=Actor_T,       .x=G_LIST+1,    .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=NIL,         .y=G_LIST+2,    .z=UNDEF        },  // ()
    { .t=VM_push,       .x=G_CLOSE,     .y=G_LIST+3,    .z=UNDEF        },  // ')'
    { .t=VM_push,       .x=G_WSP_S,     .y=G_LIST+4,    .z=UNDEF        },  // (Star Wsp)
    { .t=VM_push,       .x=G_SEXPR_S,   .y=G_LIST+5,    .z=UNDEF        },  // (Star Sexpr)
    { .t=VM_push,       .x=G_OPEN,      .y=G_LIST+6,    .z=UNDEF        },  // '('
    { .t=VM_pair,       .x=4,           .y=G_SEQ_B,     .z=UNDEF        },  // (Seq '(' (Star Sexpr) (Star Wsp) ')')

    { .t=Actor_T,       .x=G_LIST_X+1,  .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=OP_CADR,     .y=G_LIST_X+2,  .z=UNDEF        },  // cadr
    { .t=VM_push,       .x=G_LIST,      .y=G_XFORM_B,   .z=UNDEF        },  // (xform List cadr)

#define G_PTRN (G_LIST_X+3)
    { .t=Actor_T,       .x=G_PTRN+1,    .y=UNDEF,       .z=UNDEF        },
    //{ .t=VM_push,       .x=G_SGN_O,     .y=G_PTRN+2,    .z=UNDEF        },  // first = (Opt Sgn)
    //{ .t=VM_push,       .x=G_DGT_P,     .y=G_AND_B,     .z=UNDEF        },  // rest = (Plus Dgt)
    { .t=VM_push,       .x=NIL,         .y=G_PTRN+2,    .z=UNDEF        },  // ()
    { .t=VM_push,       .x=G_DGT_P,     .y=G_PTRN+3,    .z=UNDEF        },  // (Plus Dgt)
    { .t=VM_push,       .x=G_LWR,       .y=G_PTRN+4,    .z=UNDEF        },  // Lwr
    { .t=VM_push,       .x=G_UPR,       .y=G_PTRN+5,    .z=UNDEF        },  // Upr
    { .t=VM_push,       .x=G_WSP_S,     .y=G_PTRN+6,    .z=UNDEF        },  // (Star Wsp)
    { .t=VM_pair,       .x=4,           .y=G_SEQ_B,     .z=UNDEF        },  // ((Star Wsp) Upr Lwr (Plus Dgt))

#define S_EMPTY (G_PTRN+7)
    { .t=Actor_T,       .x=S_EMPTY+1,   .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=NIL,         .y=S_VALUE,     .z=UNDEF        },  // ()

#define A_PRINT (S_EMPTY+2)
    { .t=Actor_T,       .x=A_PRINT+1,   .y=UNDEF,       .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=A_PRINT+2,   .z=UNDEF        },
    { .t=VM_debug,      .x=TO_FIX(7331),.y=COMMIT,      .z=UNDEF        },

#define A_OK (A_PRINT+3)
    { .t=Actor_T,       .x=A_OK+1,      .y=UNDEF,       .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=A_OK+2,      .z=UNDEF        },
    { .t=VM_debug,      .x=TO_FIX(777), .y=COMMIT,      .z=UNDEF        },

#define A_FAIL (A_OK+3)
    { .t=Actor_T,       .x=A_FAIL+1,    .y=UNDEF,       .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=A_FAIL+2,    .z=UNDEF        },
    { .t=VM_debug,      .x=TO_FIX(666), .y=COMMIT,      .z=UNDEF        },

#define A_EVAL (A_FAIL+3)
    { .t=Actor_T,       .x=A_EVAL+1,    .y=UNDEF,       .z=UNDEF        },
    { .t=VM_msg,        .x=1,           .y=A_EVAL+2,    .z=UNDEF        },  // sexpr
    { .t=VM_debug,      .x=TO_FIX(888), .y=A_EVAL+3,    .z=UNDEF        },

    { .t=VM_push,       .x=EMPTY_ENV,   .y=A_EVAL+4,    .z=UNDEF        },  // env = EMPTY_ENV
    { .t=VM_push,       .x=A_PRINT,     .y=A_EVAL+5,    .z=UNDEF        },  // cust = A_PRINT
    { .t=VM_msg,        .x=1,           .y=A_EVAL+6,    .z=UNDEF        },  // sexpr
    { .t=VM_send,       .x=2,           .y=COMMIT,      .z=UNDEF        },  // (sexpr A_PRINT EMPTY_ENV)

#define G_TEST (A_EVAL+7)
    { .t=Actor_T,       .x=G_TEST+1,    .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=A_FAIL,      .y=G_TEST+2,    .z=UNDEF        },  // fail = A_FAIL
    //{ .t=VM_push,       .x=A_OK,        .y=G_TEST+3,    .z=UNDEF        },  // ok = A_OK
    { .t=VM_push,       .x=A_EVAL,      .y=G_TEST+3,    .z=UNDEF        },  // ok = A_EVAL
    { .t=VM_pair,       .x=1,           .y=G_TEST+4,    .z=UNDEF        },  // custs = (ok . fail)
    //{ .t=VM_push,       .x=G_EMPTY,     .y=G_TEST+5,    .z=UNDEF        },  // ptrn = G_EMPTY
    //{ .t=VM_push,       .x=G_FAIL,      .y=G_TEST+5,    .z=UNDEF        },  // ptrn = G_FAIL
    //{ .t=VM_push,       .x=G_ANY,       .y=G_TEST+5,    .z=UNDEF        },  // ptrn = G_ANY
    //{ .t=VM_push,       .x=G_SGN_O,     .y=G_TEST+5,    .z=UNDEF        },  // ptrn = G_SGN_O
    //{ .t=VM_push,       .x=G_DGT_P,     .y=G_TEST+5,    .z=UNDEF        },  // ptrn = G_DGT_P
    { .t=VM_push,       .x=G_SEXPR_X,   .y=G_TEST+5,    .z=UNDEF        },  // ptrn = G_SEXPR_X
    //{ .t=VM_push,       .x=G_PTRN,      .y=G_TEST+5,    .z=UNDEF        },  // ptrn = G_PTRN
    { .t=VM_push,       .x=G_START,     .y=G_TEST+6,    .z=UNDEF        },  // G_START
    { .t=VM_new,        .x=2,           .y=G_TEST+7,    .z=UNDEF        },  // start
    //{ .t=VM_push,       .x=S_EMPTY,     .y=G_TEST+8,    .z=UNDEF        },  // src = S_EMPTY
    { .t=VM_push,       .x=S_GETC,      .y=G_TEST+8,    .z=UNDEF        },  // S_GETC
    { .t=VM_new,        .x=0,           .y=G_TEST+9,    .z=UNDEF        },  // src
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (src . start)

//
// Global LISP/Scheme Procedures
//

/*
(define f-oper-beh                      ; operative function adapter
  (lambda (oper)                        ; (oper cust args env)
    (BEH (cust . args)
      (SEND oper (list cust args empty-env)) )))
*/
#define F_OPER_B (G_TEST+10)
//  { .t=VM_push,       .x=_oper_,      .y=F_OPER_B+0,  .z=UNDEF        },
    { .t=VM_push,       .x=EMPTY_ENV,   .y=F_OPER_B+1,  .z=UNDEF        },  // env
    { .t=VM_msg,        .x=-1,          .y=F_OPER_B+2,  .z=UNDEF        },  // args
    { .t=VM_msg,        .x=1,           .y=F_OPER_B+3,  .z=UNDEF        },  // cust
    { .t=VM_roll,       .x=4,           .y=F_OPER_B+4,  .z=UNDEF        },  // oper
    { .t=VM_send,       .x=3,           .y=COMMIT,      .z=UNDEF        },  // (oper cust args env)

/*
(define op-func-beh                     ; self-evaluating operative
  (lambda (func)                        ; (func cust . args)
    (BEH (cust args . opt-env)
      (if (pair? opt-env)
        (SEND func                      ; apply
          (cons cust args))
        (SEND cust SELF)                ; eval
      ))))
*/
#define OP_FUNC_B (F_OPER_B+5)
//  { .t=VM_push,       .x=_func_,      .y=OP_FUNC_B+0, .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=OP_FUNC_B+1, .z=UNDEF        },  // (cust args . opt-env)
    { .t=VM_part,       .x=2,           .y=OP_FUNC_B+2, .z=UNDEF        },  // opt-env args cust
    { .t=VM_roll,       .x=3,           .y=OP_FUNC_B+3, .z=UNDEF        },  // args cust opt-env
    { .t=VM_typeq,      .x=Pair_T,      .y=OP_FUNC_B+4, .z=UNDEF        },  // opt-env has type Pair_T
    { .t=VM_if,         .x=OP_FUNC_B+5, .y=SELF_EVAL,   .z=UNDEF        },

    { .t=VM_pair,       .x=1,           .y=OP_FUNC_B+6, .z=UNDEF        },  // (cust . args)
    { .t=VM_roll,       .x=2,           .y=SEND_0,      .z=UNDEF        },  // func

/*
(define k-invoke-beh
  (lambda (cust func)
    (BEH args
      (SEND func (cons cust args)) )))
*/
#define K_INVOKE (OP_FUNC_B+7)
//  { .t=VM_push,       .x=_cust_,      .y=K_INVOKE-1,  .z=UNDEF        },
//  { .t=VM_push,       .x=_func_,      .y=K_INVOKE+0,  .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=K_INVOKE+1,  .z=UNDEF        },  // args
    { .t=VM_roll,       .x=3,           .y=K_INVOKE+2,  .z=UNDEF        },  // cust
    { .t=VM_pair,       .x=1,           .y=K_INVOKE+3,  .z=UNDEF        },  // (cust . args)
    { .t=VM_roll,       .x=2,           .y=SEND_0,      .z=UNDEF        },  // func
/*
(define ap-func-beh                     ; self-evaluating applicative
  (lambda (func)                        ; (func cust . args)
    (BEH (cust params . opt-env)
      (if (pair? opt-env)
        (SEND                           ; apply
          (CREATE (evlis-beh param))
          (list (CREATE (k-invoke-beh cust func)) (car opt-env)))
        (SEND cust SELF)                ; eval
      ))))
*/
#define AP_FUNC_B (K_INVOKE+4)
//  { .t=VM_push,       .x=_func_,      .y=AP_FUNC_B+0, .z=UNDEF        },
    { .t=VM_msg,        .x=-2,          .y=AP_FUNC_B+1, .z=UNDEF        },  // opt-env
    { .t=VM_typeq,      .x=Pair_T,      .y=AP_FUNC_B+2, .z=UNDEF        },  // opt-env has type Pair_T
    { .t=VM_if,         .x=AP_FUNC_B+3, .y=SELF_EVAL,   .z=UNDEF        },

    { .t=VM_msg,        .x=1,           .y=AP_FUNC_B+4, .z=UNDEF        },  // cust
    { .t=VM_roll,       .x=2,           .y=AP_FUNC_B+5, .z=UNDEF        },  // func
    { .t=VM_push,       .x=K_INVOKE,    .y=AP_FUNC_B+6, .z=UNDEF        },  // K_INVOKE
    { .t=VM_new,        .x=2,           .y=AP_FUNC_B+7, .z=UNDEF        },  // k_invoke

    { .t=VM_msg,        .x=3,           .y=AP_FUNC_B+8, .z=UNDEF        },  // denv
    { .t=VM_roll,       .x=2,           .y=AP_FUNC_B+9, .z=UNDEF        },  // k_invoke
    { .t=VM_msg,        .x=2,           .y=AP_FUNC_B+10,.z=UNDEF        },  // param
    { .t=VM_push,       .x=EVLIS_BEH,   .y=AP_FUNC_B+11,.z=UNDEF        },  // EVLIS_BEH
    { .t=VM_new,        .x=1,           .y=AP_FUNC_B+12,.z=UNDEF        },  // ev_list
    { .t=VM_send,       .x=2,           .y=COMMIT,      .z=UNDEF        },  // (ev_list k_invoke denv)


#define F_QUOTE (AP_FUNC_B+13)
    { .t=Actor_T,       .x=F_QUOTE+1,   .y=UNDEF,       .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=2,           .y=CUST_SEND,   .z=UNDEF        },  // expr = arg1
#define OP_QUOTE (F_QUOTE+2)
    { .t=Actor_T,       .x=OP_QUOTE+1,  .y=UNDEF,       .z=UNDEF        },  // (quote <sexpr>)
    { .t=VM_push,       .x=F_QUOTE,     .y=OP_FUNC_B,   .z=UNDEF        },  // func = F_QUOTE

#define F_LIST (OP_QUOTE+2)
    { .t=Actor_T,       .x=F_LIST+1,    .y=UNDEF,       .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=-1,          .y=CUST_SEND,   .z=UNDEF        },  // args
#define AP_LIST (F_LIST+2)
    { .t=Actor_T,       .x=AP_LIST+1,   .y=UNDEF,       .z=UNDEF        },  // (list . <args>)
    { .t=VM_push,       .x=F_LIST,      .y=AP_FUNC_B,   .z=UNDEF        },  // func = F_LIST

/*
(define k-define-beh
  (lambda (cust symbol)
    (BEH value
      (SEND cust
        (eval `(define ,symbol ',value))))))
*/
#define K_DEFINE (AP_LIST+2)
//  { .t=VM_push,       .x=_cust_,      .y=K_DEFINE-1,  .z=UNDEF        },
//  { .t=VM_push,       .x=_symbol_,    .y=K_DEFINE+0,  .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=K_DEFINE+1,  .z=UNDEF        },  // value
    { .t=VM_set,        .x=FLD_Z,       .y=K_DEFINE+2,  .z=UNDEF        },  // bind(symbol, value)
    { .t=VM_push,       .x=UNIT,        .y=K_DEFINE+3,  .z=UNDEF        },  // #unit
    { .t=VM_pick,       .x=3,           .y=SEND_0,      .z=UNDEF        },  // cust
/*
(define op-define                       ; (define <symbol> <expr>)
  (CREATE
    (BEH (cust params . opt-env)
      (if (pair? opt-env)
        (SEND (cadr params)             ; apply
          (list (CREATE (k_define_beh cust (car params))) (car opt-env)))
        (SEND cust SELF)                ; eval
      ))))
*/
#define OP_DEFINE (K_DEFINE+4)
    { .t=Actor_T,       .x=OP_DEFINE+1, .y=UNDEF,       .z=UNDEF        },
    { .t=VM_msg,        .x=-2,          .y=OP_DEFINE+2, .z=UNDEF        },  // opt-env
    { .t=VM_typeq,      .x=Pair_T,      .y=OP_DEFINE+3, .z=UNDEF        },  // opt-env has type Pair_T
    { .t=VM_if,         .x=OP_DEFINE+4, .y=SELF_EVAL,   .z=UNDEF        },

    { .t=VM_msg,        .x=2,           .y=OP_DEFINE+5, .z=UNDEF        },  // params
    { .t=VM_part,       .x=2,           .y=OP_DEFINE+6, .z=UNDEF        },  // () expr symbol
    { .t=VM_pick,       .x=1,           .y=OP_DEFINE+7, .z=UNDEF        },  // symbol symbol
    { .t=VM_typeq,      .x=Symbol_T,    .y=OP_DEFINE+8, .z=UNDEF        },  // symbol has type Symbol_T
    { .t=VM_if,         .x=OP_DEFINE+10,.y=OP_DEFINE+9, .z=UNDEF        },
    { .t=VM_push,       .x=UNDEF,       .y=CUST_SEND,   .z=UNDEF        },  // #undefined

    { .t=VM_msg,        .x=3,           .y=OP_DEFINE+11,.z=UNDEF        },  // env
    { .t=VM_msg,        .x=1,           .y=OP_DEFINE+12,.z=UNDEF        },  // cust
    { .t=VM_pick,       .x=3,           .y=OP_DEFINE+13,.z=UNDEF        },  // symbol
    { .t=VM_push,       .x=K_DEFINE,    .y=OP_DEFINE+14,.z=UNDEF        },  // K_DEFINE
    { .t=VM_new,        .x=2,           .y=OP_DEFINE+15,.z=UNDEF        },  // k_define
    { .t=VM_pick,       .x=4,           .y=OP_DEFINE+16,.z=UNDEF        },  // expr
    { .t=VM_send,       .x=2,           .y=COMMIT,      .z=UNDEF        },  // (expr k_define env)

#define F_CONS (OP_DEFINE+17)
    { .t=Actor_T,       .x=F_CONS+1,    .y=UNDEF,       .z=UNDEF        },  // (cust . args)
#if 1
    { .t=VM_msg,        .x=3,           .y=F_CONS+2,    .z=UNDEF        },  // tail = arg2
    { .t=VM_msg,        .x=2,           .y=F_CONS+3,    .z=UNDEF        },  // head = arg1
#else
    { .t=VM_msg,        .x=-1,          .y=F_CONS+2,    .z=UNDEF        },  // (head tail)
    { .t=VM_part,       .x=2,           .y=F_CONS+3,    .z=UNDEF        },  // () tail head
#endif
    { .t=VM_pair,       .x=1,           .y=CUST_SEND,   .z=UNDEF        },  // (head . tail)
#define AP_CONS (F_CONS+4)
    { .t=Actor_T,       .x=AP_CONS+1,   .y=UNDEF,       .z=UNDEF        },  // (cons <head> <tail>)
    { .t=VM_push,       .x=F_CONS,      .y=AP_FUNC_B,   .z=UNDEF        },  // func = F_CONS

#define F_CAR (AP_CONS+2)
    { .t=Actor_T,       .x=F_CAR+1,     .y=UNDEF,       .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=2,           .y=F_CAR+2,     .z=UNDEF        },  // pair = arg1
    { .t=VM_get,        .x=FLD_X,       .y=CUST_SEND,   .z=UNDEF        },  // car(pair)
#define AP_CAR (F_CAR+3)
    { .t=Actor_T,       .x=AP_CAR+1,    .y=UNDEF,       .z=UNDEF        },  // (car <pair>)
    { .t=VM_push,       .x=F_CAR,       .y=AP_FUNC_B,   .z=UNDEF        },  // func = F_CAR

#define F_CDR (AP_CAR+2)
    { .t=Actor_T,       .x=F_CDR+1,     .y=UNDEF,       .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=2,           .y=F_CDR+2,     .z=UNDEF        },  // pair = arg1
    { .t=VM_get,        .x=FLD_Y,       .y=CUST_SEND,   .z=UNDEF        },  // cdr(pair)
#define AP_CDR (F_CDR+3)
    { .t=Actor_T,       .x=AP_CDR+1,    .y=UNDEF,       .z=UNDEF        },  // (cdr <pair>)
    { .t=VM_push,       .x=F_CDR,       .y=AP_FUNC_B,   .z=UNDEF        },  // func = F_CDR

#define F_CADR (AP_CDR+2)
    { .t=Actor_T,       .x=F_CADR+1,    .y=UNDEF,       .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=2,           .y=F_CADR+2,    .z=UNDEF        },  // pair = arg1
    { .t=VM_nth,        .x=2,           .y=CUST_SEND,   .z=UNDEF        },  // cadr(pair)
#define AP_CADR (F_CADR+3)
    { .t=Actor_T,       .x=AP_CADR+1,   .y=UNDEF,       .z=UNDEF        },  // (cadr <pair>)
    { .t=VM_push,       .x=F_CADR,      .y=AP_FUNC_B,   .z=UNDEF        },  // func = F_CADR

#define F_CADDR (AP_CADR+2)
    { .t=Actor_T,       .x=F_CADDR+1,   .y=UNDEF,       .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=2,           .y=F_CADDR+2,   .z=UNDEF        },  // pair = arg1
    { .t=VM_nth,        .x=3,           .y=CUST_SEND,   .z=UNDEF        },  // caddr(pair)
#define AP_CADDR (F_CADDR+3)
    { .t=Actor_T,       .x=AP_CADDR+1,  .y=UNDEF,       .z=UNDEF        },  // (caddr <pair>)
    { .t=VM_push,       .x=F_CADDR,     .y=AP_FUNC_B,   .z=UNDEF        },  // func = F_CADDR

#define F_G_EQ (AP_CADDR+2)
    { .t=Actor_T,       .x=F_G_EQ+1,    .y=UNDEF,       .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=2,           .y=F_G_EQ+2,    .z=UNDEF        },  // token = arg1
    { .t=VM_push,       .x=G_EQ_B,      .y=F_G_EQ+3,    .z=UNDEF        },  // G_EQ_B
    { .t=VM_new,        .x=1,           .y=CUST_SEND,   .z=UNDEF        },  // (G_EQ_B token)
#define AP_G_EQ (F_G_EQ+4)
    { .t=Actor_T,       .x=AP_G_EQ+1,   .y=UNDEF,       .z=UNDEF        },  // (peg-eq <token>)
    { .t=VM_push,       .x=F_G_EQ,      .y=AP_FUNC_B,   .z=UNDEF        },  // func = F_G_EQ

#define F_G_OR (AP_G_EQ+2)
    { .t=Actor_T,       .x=F_G_OR+1,    .y=UNDEF,       .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=2,           .y=F_G_OR+2,    .z=UNDEF        },  // first = arg1
    { .t=VM_msg,        .x=3,           .y=F_G_OR+3,    .z=UNDEF        },  // rest = arg2
    { .t=VM_push,       .x=G_OR_B,      .y=F_G_OR+4,    .z=UNDEF        },  // G_OR_B
    { .t=VM_new,        .x=2,           .y=CUST_SEND,   .z=UNDEF        },  // (G_OR_B first rest)
#define AP_G_OR (F_G_OR+5)
    { .t=Actor_T,       .x=AP_G_OR+1,   .y=UNDEF,       .z=UNDEF        },  // (peg-or <first> <rest>)
    { .t=VM_push,       .x=F_G_OR,      .y=AP_FUNC_B,   .z=UNDEF        },  // func = F_G_OR

#define F_G_AND (AP_G_OR+2)
    { .t=Actor_T,       .x=F_G_AND+1,   .y=UNDEF,       .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=2,           .y=F_G_AND+2,   .z=UNDEF        },  // first = arg1
    { .t=VM_msg,        .x=3,           .y=F_G_AND+3,   .z=UNDEF        },  // rest = arg2
    { .t=VM_push,       .x=G_AND_B,     .y=F_G_AND+4,   .z=UNDEF        },  // G_AND_B
    { .t=VM_new,        .x=2,           .y=CUST_SEND,   .z=UNDEF        },  // (G_AND_B first rest)
#define AP_G_AND (F_G_AND+5)
    { .t=Actor_T,       .x=AP_G_AND+1,  .y=UNDEF,       .z=UNDEF        },  // (peg-and <first> <rest>)
    { .t=VM_push,       .x=F_G_AND,     .y=AP_FUNC_B,   .z=UNDEF        },  // func = F_G_AND

#define F_G_CLS (AP_G_AND+2)
    { .t=Actor_T,       .x=F_G_CLS+1,   .y=UNDEF,       .z=UNDEF        },  // (cust . args)
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
#define AP_G_CLS (F_G_CLS+16)
    { .t=Actor_T,       .x=AP_G_CLS+1,  .y=UNDEF,       .z=UNDEF        },  // (peg-class . <classes>)
    { .t=VM_push,       .x=F_G_CLS,     .y=AP_FUNC_B,   .z=UNDEF        },  // func = F_G_CLS

#define F_G_OPT (AP_G_CLS+2)
    { .t=Actor_T,       .x=F_G_OPT+1,   .y=UNDEF,       .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=2,           .y=F_G_OPT+2,   .z=UNDEF        },  // peg = arg1
    { .t=VM_push,       .x=G_OPT_B,     .y=F_G_OPT+3,   .z=UNDEF        },  // G_OPT_B
    { .t=VM_new,        .x=1,           .y=CUST_SEND,   .z=UNDEF        },  // (G_OPT_B peg)
#define AP_G_OPT (F_G_OPT+4)
    { .t=Actor_T,       .x=AP_G_OPT+1,  .y=UNDEF,       .z=UNDEF        },  // (peg-opt <peg>)
    { .t=VM_push,       .x=F_G_OPT,     .y=AP_FUNC_B,   .z=UNDEF        },  // func = F_G_OPT

#define F_G_PLUS (AP_G_OPT+2)
    { .t=Actor_T,       .x=F_G_PLUS+1,  .y=UNDEF,       .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=2,           .y=F_G_PLUS+2,  .z=UNDEF        },  // peg = arg1
    { .t=VM_push,       .x=G_PLUS_B,    .y=F_G_PLUS+3,  .z=UNDEF        },  // G_PLUS_B
    { .t=VM_new,        .x=1,           .y=CUST_SEND,   .z=UNDEF        },  // (G_PLUS_B peg)
#define AP_G_PLUS (F_G_PLUS+4)
    { .t=Actor_T,       .x=AP_G_PLUS+1, .y=UNDEF,       .z=UNDEF        },  // (peg-plus <peg>)
    { .t=VM_push,       .x=F_G_PLUS,    .y=AP_FUNC_B,   .z=UNDEF        },  // func = F_G_PLUS

#define F_G_STAR (AP_G_PLUS+2)
    { .t=Actor_T,       .x=F_G_STAR+1,  .y=UNDEF,       .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=2,           .y=F_G_STAR+2,  .z=UNDEF        },  // peg = arg1
    { .t=VM_push,       .x=G_STAR_B,    .y=F_G_STAR+3,  .z=UNDEF        },  // G_STAR_B
    { .t=VM_new,        .x=1,           .y=CUST_SEND,   .z=UNDEF        },  // (G_STAR_B peg)
#define AP_G_STAR (F_G_STAR+4)
    { .t=Actor_T,       .x=AP_G_STAR+1, .y=UNDEF,       .z=UNDEF        },  // (peg-star <peg>)
    { .t=VM_push,       .x=F_G_STAR,    .y=AP_FUNC_B,   .z=UNDEF        },  // func = F_G_STAR

#define F_G_ALT (AP_G_STAR+2)
    { .t=Actor_T,       .x=F_G_ALT+1,   .y=UNDEF,       .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=-1,          .y=F_G_ALT+2,   .z=UNDEF        },  // pegs = args
    { .t=VM_push,       .x=G_ALT_B,     .y=F_G_ALT+3,   .z=UNDEF        },  // G_ALT_B
    { .t=VM_new,        .x=1,           .y=CUST_SEND,   .z=UNDEF        },  // (G_ALT_B pegs)
#define AP_G_ALT (F_G_ALT+4)
    { .t=Actor_T,       .x=AP_G_ALT+1,  .y=UNDEF,       .z=UNDEF        },  // (peg-alt . <pegs>)
    { .t=VM_push,       .x=F_G_ALT,     .y=AP_FUNC_B,   .z=UNDEF        },  // func = F_G_ALT

#define F_G_SEQ (AP_G_ALT+2)
    { .t=Actor_T,       .x=F_G_SEQ+1,   .y=UNDEF,       .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=-1,          .y=F_G_SEQ+2,   .z=UNDEF        },  // pegs = args
    { .t=VM_push,       .x=G_SEQ_B,     .y=F_G_SEQ+3,   .z=UNDEF        },  // G_SEQ_B
    { .t=VM_new,        .x=1,           .y=CUST_SEND,   .z=UNDEF        },  // (G_SEQ_B pegs)
#define AP_G_SEQ (F_G_SEQ+4)
    { .t=Actor_T,       .x=AP_G_SEQ+1,  .y=UNDEF,       .z=UNDEF        },  // (peg-seq . <pegs>)
    { .t=VM_push,       .x=F_G_SEQ,     .y=AP_FUNC_B,   .z=UNDEF        },  // func = F_G_SEQ

#define F_G_CALL (AP_G_SEQ+2)
    { .t=Actor_T,       .x=F_G_CALL+1,  .y=UNDEF,       .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=2,           .y=F_G_CALL+2,  .z=UNDEF        },  // name = arg1
    { .t=VM_push,       .x=G_CALL_B,    .y=F_G_CALL+3,  .z=UNDEF        },  // G_CALL_B
    { .t=VM_new,        .x=1,           .y=CUST_SEND,   .z=UNDEF        },  // (G_CALL_B name)
#define OP_G_CALL (F_G_CALL+4)
    { .t=Actor_T,       .x=OP_G_CALL+1, .y=UNDEF,       .z=UNDEF        },  // (peg-call <name>)
    { .t=VM_push,       .x=F_G_CALL,    .y=OP_FUNC_B,   .z=UNDEF        },  // func = F_G_CALL

#define F_LST_NUM (OP_G_CALL+2)
    { .t=Actor_T,       .x=F_LST_NUM+1, .y=UNDEF,       .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=2,           .y=F_LST_NUM+2, .z=UNDEF        },  // chars = arg1
    { .t=VM_cvt,        .x=CVT_LST_NUM, .y=CUST_SEND,   .z=UNDEF        },  // lst_num(chars)
#define AP_LST_NUM (F_LST_NUM+3)
    { .t=Actor_T,       .x=AP_LST_NUM+1,.y=UNDEF,       .z=UNDEF        },  // (list->number <chars>)
    { .t=VM_push,       .x=F_LST_NUM,   .y=AP_FUNC_B,   .z=UNDEF        },  // func = F_LST_NUM

#define F_LST_SYM (AP_LST_NUM+2)
    { .t=Actor_T,       .x=F_LST_SYM+1, .y=UNDEF,       .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=2,           .y=F_LST_SYM+2, .z=UNDEF        },  // chars = arg1
    { .t=VM_cvt,        .x=CVT_LST_SYM, .y=CUST_SEND,   .z=UNDEF        },  // lst_sym(chars)
#define AP_LST_SYM (F_LST_SYM+3)
    { .t=Actor_T,       .x=AP_LST_SYM+1,.y=UNDEF,       .z=UNDEF        },  // (list->symbol <chars>)
    { .t=VM_push,       .x=F_LST_SYM,   .y=AP_FUNC_B,   .z=UNDEF        },  // func = F_LST_SYM

#define F_G_SRC (AP_LST_SYM+2)
    { .t=Actor_T,       .x=F_G_SRC+1,   .y=UNDEF,       .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=2,           .y=F_G_SRC+2,   .z=UNDEF        },  // list = arg1
    { .t=VM_push,       .x=S_LIST_B,    .y=F_G_SRC+3,   .z=UNDEF        },  // S_LIST_B
    { .t=VM_new,        .x=1,           .y=CUST_SEND,   .z=UNDEF        },  // src
#define AP_G_SRC (F_G_SRC+4)
    { .t=Actor_T,       .x=AP_G_SRC+1,  .y=UNDEF,       .z=UNDEF        },  // (peg-source <list>)
    { .t=VM_push,       .x=F_G_SRC,     .y=AP_FUNC_B,   .z=UNDEF        },  // func = F_G_SRC

#define F_G_START (AP_G_SRC+2)
    { .t=Actor_T,       .x=F_G_START+1, .y=UNDEF,       .z=UNDEF        },  // (cust . args)
    { .t=VM_msg,        .x=1,           .y=F_G_START+2, .z=UNDEF        },  // fail = cust
    { .t=VM_msg,        .x=1,           .y=F_G_START+3, .z=UNDEF        },  // ok = cust
    { .t=VM_pair,       .x=1,           .y=F_G_START+4, .z=UNDEF        },  // custs = (ok . fail)
    { .t=VM_msg,        .x=2,           .y=F_G_START+5, .z=UNDEF        },  // peg = arg1
    { .t=VM_push,       .x=G_START,     .y=F_G_START+6, .z=UNDEF        },  // G_START
    { .t=VM_new,        .x=2,           .y=F_G_START+7, .z=UNDEF        },  // start
    { .t=VM_msg,        .x=3,           .y=SEND_0,      .z=UNDEF        },  // src = arg2
#define AP_G_START (F_G_START+8)
    { .t=Actor_T,       .x=AP_G_START+1,.y=UNDEF,       .z=UNDEF        },  // (peg-start <peg> <src>)
    { .t=VM_push,       .x=F_G_START,   .y=AP_FUNC_B,   .z=UNDEF        },  // func = F_G_START


#define C_UNDEF_T (AP_G_START+2)
    { .t=VM_push,       .x=VM_push,     .y=C_UNDEF_T+1, .z=UNDEF        },  // VM_push
    { .t=VM_push,       .x=UNDEF,       .y=C_UNDEF_T+2, .z=UNDEF        },  // UNDEF
    { .t=VM_msg,        .x=0,           .y=C_UNDEF_T+3, .z=UNDEF        },  // beh
    { .t=VM_cell,       .x=3,           .y=C_UNDEF_T+4, .z=UNDEF        },  // {t:VM_push, x:UNDEF, y:beh}
    { .t=VM_roll,       .x=5,           .y=SEND_0,      .z=UNDEF        },  // cust
#define C_CONST_T (C_UNDEF_T+5)
    { .t=VM_push,       .x=VM_push,     .y=C_CONST_T+1, .z=UNDEF        },  // VM_push
    { .t=VM_pick,       .x=4,           .y=C_CONST_T+2, .z=UNDEF        },  // value = expr
    { .t=VM_msg,        .x=0,           .y=C_CONST_T+3, .z=UNDEF        },  // beh
    { .t=VM_cell,       .x=3,           .y=C_CONST_T+4, .z=UNDEF        },  // {t:VM_push, x:value, y:beh}
    { .t=VM_roll,       .x=5,           .y=SEND_0,      .z=UNDEF        },  // cust
#define C_VAR_T (C_CONST_T+5)
    { .t=VM_pick,       .x=2,           .y=C_VAR_T+1,   .z=UNDEF        },  // frml
    { .t=VM_pick,       .x=4,           .y=C_VAR_T+2,   .z=UNDEF        },  // name = expr
    { .t=VM_cmp,        .x=CMP_EQ,      .y=C_VAR_T+3,   .z=UNDEF        },  // frml == name
    { .t=VM_if,         .x=C_VAR_T+4,   .y=C_VAR_T+9,   .z=UNDEF        },

    { .t=VM_push,       .x=VM_msg,      .y=C_VAR_T+5,   .z=UNDEF        },  // VM_msg
    { .t=VM_push,       .x=-1,          .y=C_VAR_T+6,   .z=UNDEF        },  // args
    { .t=VM_msg,        .x=0,           .y=C_VAR_T+7,   .z=UNDEF        },  // beh
    { .t=VM_cell,       .x=3,           .y=C_VAR_T+8,   .z=UNDEF        },  // {t:VM_msg, x:-1, y:beh}
    { .t=VM_roll,       .x=5,           .y=SEND_0,      .z=UNDEF        },  // cust

    { .t=VM_pick,       .x=2,           .y=C_VAR_T+10,  .z=UNDEF        },  // frml
    { .t=VM_get,        .x=FLD_X,       .y=C_VAR_T+11,  .z=UNDEF        },  // car(frml)
    { .t=VM_pick,       .x=4,           .y=C_VAR_T+12,  .z=UNDEF        },  // name = expr
    { .t=VM_cmp,        .x=CMP_EQ,      .y=C_VAR_T+13,  .z=UNDEF        },  // car(frml) == name
    { .t=VM_if,         .x=C_VAR_T+14,  .y=C_VAR_T+19,  .z=UNDEF        },

    { .t=VM_push,       .x=VM_msg,      .y=C_VAR_T+15,  .z=UNDEF        },  // VM_msg
    { .t=VM_push,       .x=2,           .y=C_VAR_T+16,  .z=UNDEF        },  // car(args)
    { .t=VM_msg,        .x=0,           .y=C_VAR_T+17,  .z=UNDEF        },  // beh
    { .t=VM_cell,       .x=3,           .y=C_VAR_T+18,  .z=UNDEF        },  // {t:VM_msg, x:2, y:beh}
    { .t=VM_roll,       .x=5,           .y=SEND_0,      .z=UNDEF        },  // cust

    { .t=VM_pick,       .x=2,           .y=C_VAR_T+20,  .z=UNDEF        },  // frml
    { .t=VM_get,        .x=FLD_Y,       .y=C_VAR_T+21,  .z=UNDEF        },  // cdr(frml)
    { .t=VM_pick,       .x=4,           .y=C_VAR_T+22,  .z=UNDEF        },  // name = expr
    { .t=VM_cmp,        .x=CMP_EQ,      .y=C_VAR_T+23,  .z=UNDEF        },  // cdr(frml) == name
    { .t=VM_if,         .x=C_VAR_T+24,  .y=C_VAR_T+29,  .z=UNDEF        },

    { .t=VM_push,       .x=VM_msg,      .y=C_VAR_T+25,  .z=UNDEF        },  // VM_msg
    { .t=VM_push,       .x=-2,          .y=C_VAR_T+26,  .z=UNDEF        },  // cdr(args)
    { .t=VM_msg,        .x=0,           .y=C_VAR_T+27,  .z=UNDEF        },  // beh
    { .t=VM_cell,       .x=3,           .y=C_VAR_T+28,  .z=UNDEF        },  // {t:VM_msg, x:-2, y:beh}
    { .t=VM_roll,       .x=5,           .y=SEND_0,      .z=UNDEF        },  // cust

    { .t=VM_pick,       .x=2,           .y=C_VAR_T+30,  .z=UNDEF        },  // frml
    { .t=VM_get,        .x=FLD_Y,       .y=C_VAR_T+31,  .z=UNDEF        },  // cdr(frml)
    { .t=VM_get,        .x=FLD_X,       .y=C_VAR_T+32,  .z=UNDEF        },  // cadr(frml)
    { .t=VM_pick,       .x=4,           .y=C_VAR_T+33,  .z=UNDEF        },  // name = expr
    { .t=VM_cmp,        .x=CMP_EQ,      .y=C_VAR_T+34,  .z=UNDEF        },  // cadr(frml) == name
    { .t=VM_if,         .x=C_VAR_T+35,  .y=C_VAR_T+40,  .z=UNDEF        },

    { .t=VM_push,       .x=VM_msg,      .y=C_VAR_T+36,  .z=UNDEF        },  // VM_msg
    { .t=VM_push,       .x=3,           .y=C_VAR_T+37,  .z=UNDEF        },  // cadr(args)
    { .t=VM_msg,        .x=0,           .y=C_VAR_T+38,  .z=UNDEF        },  // beh
    { .t=VM_cell,       .x=3,           .y=C_VAR_T+39,  .z=UNDEF        },  // {t:VM_msg, x:3, y:beh}
    { .t=VM_roll,       .x=5,           .y=SEND_0,      .z=UNDEF        },  // cust

    { .t=VM_pick,       .x=2,           .y=C_VAR_T+41,  .z=UNDEF        },  // frml
    { .t=VM_get,        .x=FLD_Y,       .y=C_VAR_T+42,  .z=UNDEF        },  // cdr(frml)
    { .t=VM_get,        .x=FLD_Y,       .y=C_VAR_T+43,  .z=UNDEF        },  // cddr(frml)
    { .t=VM_pick,       .x=4,           .y=C_VAR_T+44,  .z=UNDEF        },  // name = expr
    { .t=VM_cmp,        .x=CMP_EQ,      .y=C_VAR_T+45,  .z=UNDEF        },  // cddr(frml) == name
    { .t=VM_if,         .x=C_VAR_T+46,  .y=C_VAR_T+51,  .z=UNDEF        },

    { .t=VM_push,       .x=VM_msg,      .y=C_VAR_T+47,  .z=UNDEF        },  // VM_msg
    { .t=VM_push,       .x=-3,          .y=C_VAR_T+48,  .z=UNDEF        },  // cddr(args)
    { .t=VM_msg,        .x=0,           .y=C_VAR_T+49,  .z=UNDEF        },  // beh
    { .t=VM_cell,       .x=3,           .y=C_VAR_T+50,  .z=UNDEF        },  // {t:VM_msg, x:-3, y:beh}
    { .t=VM_roll,       .x=5,           .y=SEND_0,      .z=UNDEF        },  // cust

    { .t=VM_pick,       .x=2,           .y=C_VAR_T+52,  .z=UNDEF        },  // frml
    { .t=VM_get,        .x=FLD_Y,       .y=C_VAR_T+53,  .z=UNDEF        },  // cdr(frml)
    { .t=VM_get,        .x=FLD_Y,       .y=C_VAR_T+54,  .z=UNDEF        },  // cddr(frml)
    { .t=VM_get,        .x=FLD_X,       .y=C_VAR_T+55,  .z=UNDEF        },  // caddr(frml)
    { .t=VM_pick,       .x=4,           .y=C_VAR_T+56,  .z=UNDEF        },  // name = expr
    { .t=VM_cmp,        .x=CMP_EQ,      .y=C_VAR_T+57,  .z=UNDEF        },  // cadr(frml) == name
    { .t=VM_if,         .x=C_VAR_T+58,  .y=C_UNDEF_T,   .z=UNDEF        },

    { .t=VM_push,       .x=VM_msg,      .y=C_VAR_T+59,  .z=UNDEF        },  // VM_msg
    { .t=VM_push,       .x=4,           .y=C_VAR_T+60,  .z=UNDEF        },  // caddr(args)
    { .t=VM_msg,        .x=0,           .y=C_VAR_T+61,  .z=UNDEF        },  // beh
    { .t=VM_cell,       .x=3,           .y=C_VAR_T+62,  .z=UNDEF        },  // {t:VM_msg, x:4, y:beh}
    { .t=VM_roll,       .x=5,           .y=SEND_0,      .z=UNDEF        },  // cust
/*
(define k-compile
  (lambda (cust expr frml env)
    (BEH beh
      (if (or (fixnum? expr) (const? expr))
        (SEND cust
          (cell VM_push expr beh))
        (if (symbol? expr)
          (if (eq? frml expr)
            (cell VM_msg -1 beh)
            (if (eq? (car frml) expr)
              (cell VM_msg 2 beh)
              ;...
              ))
          ;...
      )))))
*/
#define K_COMPILE (C_VAR_T+63)
//  { .t=VM_push,       .x=_cust_,      .y=K_COMPILE-3, .z=UNDEF        },
//  { .t=VM_push,       .x=_expr_,      .y=K_COMPILE-2, .z=UNDEF        },
//  { .t=VM_push,       .x=_frml_,      .y=K_COMPILE-1, .z=UNDEF        },
//  { .t=VM_push,       .x=_env_,       .y=K_COMPILE+0, .z=UNDEF        },
    { .t=VM_pick,       .x=3,           .y=K_COMPILE+1, .z=UNDEF        },  // expr
    { .t=VM_typeq,      .x=Fixnum_T,    .y=K_COMPILE+2, .z=UNDEF        },  // expr has type Fixnum_T
    { .t=VM_if,         .x=C_CONST_T,   .y=K_COMPILE+3, .z=UNDEF        },

    { .t=VM_pick,       .x=3,           .y=K_COMPILE+4, .z=UNDEF        },  // expr
    { .t=VM_push,       .x=START,       .y=K_COMPILE+5, .z=UNDEF        },  // START
    { .t=VM_cmp,        .x=CMP_LT,      .y=K_COMPILE+6, .z=UNDEF        },  // expr < START
    { .t=VM_if,         .x=C_CONST_T,   .y=K_COMPILE+7, .z=UNDEF        },

    { .t=VM_pick,       .x=3,           .y=K_COMPILE+8, .z=UNDEF        },  // name = expr
    { .t=VM_typeq,      .x=Symbol_T,    .y=K_COMPILE+9, .z=UNDEF        },  // name has type Symbol_T
    { .t=VM_if,         .x=C_VAR_T,     .y=C_UNDEF_T,   .z=UNDEF        },
/*
(define compile-beh
  (lambda (body)
    (BEH (cust frml env)
      (if (pair? body)
        (SEND
          (CREATE (compile-beh (cdr body)))
          (list (CREATE (k-compile cust (car body) frml env)) frml env))
        (SEND cust CUST_SEND) ; send final result
      ))))
*/
#define COMPILE_B (K_COMPILE+10)
//  { .t=VM_push,       .x=_body_,      .y=COMPILE_B+0, .z=UNDEF        },
    { .t=VM_pick,       .x=1,           .y=COMPILE_B+1, .z=UNDEF        },  // body
    { .t=VM_typeq,      .x=Pair_T,      .y=COMPILE_B+2, .z=UNDEF        },  // body has type Pair_T
    { .t=VM_if,         .x=COMPILE_B+4, .y=COMPILE_B+3, .z=UNDEF        },

    { .t=VM_push,       .x=CUST_SEND,   .y=CUST_SEND,   .z=UNDEF        },  // beh = CUST_SEND

    { .t=VM_msg,        .x=3,           .y=COMPILE_B+5, .z=UNDEF        },  // env
    { .t=VM_msg,        .x=2,           .y=COMPILE_B+6, .z=UNDEF        },  // frml
    { .t=VM_roll,       .x=3,           .y=COMPILE_B+7, .z=UNDEF        },  // body
    { .t=VM_part,       .x=1,           .y=COMPILE_B+8, .z=UNDEF        },  // tail head

    { .t=VM_msg,        .x=1,           .y=COMPILE_B+9, .z=UNDEF        },  // cust
    { .t=VM_roll,       .x=2,           .y=COMPILE_B+10,.z=UNDEF        },  // expr = head
    { .t=VM_msg,        .x=2,           .y=COMPILE_B+11,.z=UNDEF        },  // frml
    { .t=VM_msg,        .x=3,           .y=COMPILE_B+12,.z=UNDEF        },  // env
    { .t=VM_push,       .x=K_COMPILE,   .y=COMPILE_B+13,.z=UNDEF        },  // K_COMPILE
    { .t=VM_new,        .x=4,           .y=COMPILE_B+14,.z=UNDEF        },  // k_compile

    { .t=VM_roll,       .x=2,           .y=COMPILE_B+15,.z=UNDEF        },  // body' = tail
    { .t=VM_push,       .x=COMPILE_B,   .y=COMPILE_B+16,.z=UNDEF        },  // COMPILE_B
    { .t=VM_beh,        .x=1,           .y=COMPILE_B+17,.z=UNDEF        },  // BECOME (COMPILE_B tail)
    { .t=VM_self,       .x=UNDEF,       .y=COMPILE_B+18,.z=UNDEF        },  // SELF
    { .t=VM_send,       .x=3,           .y=COMMIT,      .z=UNDEF        },  // (SELF k_compile frml env)

/*
(define k-lambda-c
  (lambda (cust)
    (BEH beh
      (SEND cust
        (cell Actor_T
          (cell VM_push
            (cell Actor_T
              (cell VM_push #unit beh))
            AP_FUNC_B))
      ))))
*/
#define K_LAMBDAC (COMPILE_B+19)
//  { .t=VM_push,       .x=_cust_,      .y=K_LAMBDAC+0, .z=UNDEF        },
    { .t=VM_push,       .x=VM_push,     .y=K_LAMBDAC+1, .z=UNDEF        },  // VM_push
    { .t=VM_push,       .x=UNIT,        .y=K_LAMBDAC+2, .z=UNDEF        },  // UNIT
    { .t=VM_msg,        .x=0,           .y=K_LAMBDAC+3, .z=UNDEF        },  // beh
    { .t=VM_cell,       .x=3,           .y=K_LAMBDAC+4, .z=UNDEF        },  // {t:VM_push, x:UNIT, y:beh}
    { .t=VM_new,        .x=0,           .y=K_LAMBDAC+5, .z=UNDEF        },  // func
    { .t=VM_push,       .x=VM_push,     .y=K_LAMBDAC+6, .z=UNDEF        },  // VM_push
    { .t=VM_roll,       .x=2,           .y=K_LAMBDAC+7, .z=UNDEF        },  // func
    { .t=VM_push,       .x=AP_FUNC_B,   .y=K_LAMBDAC+8, .z=UNDEF        },  // AP_FUNC_B
    { .t=VM_cell,       .x=3,           .y=K_LAMBDAC+9, .z=UNDEF        },  // {t:VM_push, x:func, y:AP_FUNC_B}
    { .t=VM_new,        .x=0,           .y=K_LAMBDAC+10,.z=UNDEF        },  // appl
    { .t=VM_roll,       .x=2,           .y=SEND_0,      .z=UNDEF        },  // cust
/*
(define lambda-c              ; (lambda-compile <frml> . <body>)
  (CREATE
    (BEH (cust opnd . opt-env)
      (if (pair? opt-env)
        (SEND                 ; apply
          (CREATE (compile-beh (cdr opnd)))
          (list (CREATE (k-lambda-c cust)) (car opnd) (car opt-env)))
        (SEND cust SELF)      ; eval
      ))))
*/
#define LAMBDA_C (K_LAMBDAC+11)
    { .t=Actor_T,       .x=LAMBDA_C+1,  .y=UNDEF,       .z=UNDEF        },  // (lambda <frml> . <body>)
    { .t=VM_msg,        .x=-2,          .y=LAMBDA_C+2,  .z=UNDEF        },  // opt-env
    { .t=VM_typeq,      .x=Pair_T,      .y=LAMBDA_C+3,  .z=UNDEF        },  // opt-env has type Pair_T
    { .t=VM_if,         .x=LAMBDA_C+4,  .y=SELF_EVAL,   .z=UNDEF        },

    { .t=VM_msg,        .x=3,           .y=LAMBDA_C+5,  .z=UNDEF        },  // env
    { .t=VM_msg,        .x=2,           .y=LAMBDA_C+6,  .z=UNDEF        },  // opnd
    { .t=VM_get,        .x=FLD_X,       .y=LAMBDA_C+7,  .z=UNDEF        },  // frml = car(opnd)

    { .t=VM_msg,        .x=1,           .y=LAMBDA_C+8,  .z=UNDEF        },  // cust
    { .t=VM_push,       .x=K_LAMBDAC,   .y=LAMBDA_C+9,  .z=UNDEF        },  // K_LAMBDAC
    { .t=VM_new,        .x=1,           .y=LAMBDA_C+10, .z=UNDEF        },  // k_lambda

    { .t=VM_msg,        .x=2,           .y=LAMBDA_C+11, .z=UNDEF        },  // opnd
    { .t=VM_get,        .x=FLD_Y,       .y=LAMBDA_C+12, .z=UNDEF        },  // body = cdr(opnd)
    { .t=VM_push,       .x=COMPILE_B,   .y=LAMBDA_C+13, .z=UNDEF        },  // COMPILE_B
    { .t=VM_new,        .x=1,           .y=LAMBDA_C+14, .z=UNDEF        },  // compile
    { .t=VM_send,       .x=3,           .y=COMMIT,      .z=UNDEF        },  // (compile k_lambda frml env)

//
// Lambda Calculus behaviors
//

/*
(define bound-beh  ; lookup variable by De Bruijn index
  (lambda (value next)
    (BEH (cust index)
      (define index (- index 1))
      (if (zero? index)
        (SEND cust value)
        (SEND next (list cust index))))))
*/
#define BOUND_BEH (LAMBDA_C+15)
//  { .t=VM_push,       .x=_value_,     .y=BOUND_BEH-1, .z=UNDEF        },
//  { .t=VM_push,       .x=_next_,      .y=BOUND_BEH+0, .z=UNDEF        },
    { .t=VM_msg,        .x=2,           .y=BOUND_BEH+1, .z=UNDEF        },  // index
    { .t=VM_push,       .x=TO_FIX(1),   .y=BOUND_BEH+2, .z=UNDEF        },  // +1
    { .t=VM_alu,        .x=ALU_SUB,     .y=BOUND_BEH+3, .z=UNDEF        },  // index-1
    { .t=VM_pick,       .x=1,           .y=BOUND_BEH+4, .z=UNDEF        },  // index-1 index-1
    { .t=VM_eq,         .x=TO_FIX(0),   .y=BOUND_BEH+5, .z=UNDEF        },  // index-1 == +0
    { .t=VM_if,         .x=BOUND_BEH+9, .y=BOUND_BEH+6, .z=UNDEF        },

    { .t=VM_msg,        .x=1,           .y=BOUND_BEH+7, .z=UNDEF        },  // cust
    { .t=VM_pick,       .x=3,           .y=BOUND_BEH+8, .z=UNDEF        },  // next
    { .t=VM_send,       .x=2,           .y=COMMIT,      .z=UNDEF        },  // (next cust index-1)

    { .t=VM_pick,       .x=3,           .y=BOUND_BEH+10,.z=UNDEF        },  // value
    { .t=VM_msg,        .x=1,           .y=BOUND_BEH+11,.z=UNDEF        },  // cust
    { .t=VM_send,       .x=0,           .y=COMMIT,      .z=UNDEF        },  // (cust . value)

/*
(define const-beh
  (lambda (value)
    (BEH (cust _)             ; eval
      (SEND cust value))))
*/
#define CONST_7 (BOUND_BEH+12)
    { .t=Actor_T,       .x=CONST_7+1,   .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=TO_FIX(7),   .y=CUST_SEND,   .z=UNDEF        },  // value = +7
#define CONST_LST (CONST_7+2)
#if 0
    { .t=Actor_T,       .x=CONST_LST+1, .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=NIL,         .y=CONST_LST+2, .z=UNDEF        },  // ()
    { .t=VM_push,       .x=TO_FIX(3),   .y=CONST_LST+3, .z=UNDEF        },  // +3
    { .t=VM_push,       .x=TO_FIX(2),   .y=CONST_LST+4, .z=UNDEF        },  // +2
    { .t=VM_push,       .x=TO_FIX(1),   .y=CONST_LST+5, .z=UNDEF        },  // +1
    { .t=VM_pair,       .x=3,           .y=CONST_BEH,   .z=UNDEF        },  // value = (+1 +2 +3)
#else
#if 0
    { .t=Pair_T,        .x=TO_FIX(1),   .y=CONST_LST+1, .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX(2),   .y=CONST_LST+2, .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX(3),   .y=CONST_LST+3, .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX(4),   .y=CONST_LST+4, .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX(5),   .y=CONST_LST+5, .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX(6),   .y=NIL,         .z=UNDEF        },
#else
    { .t=Pair_T,        .x=TO_FIX(1),   .y=CONST_LST+1, .z=UNDEF        },
    { .t=Pair_T,        .x=CONST_LST+2, .y=TO_FIX(5),   .z=UNDEF        },
    { .t=Pair_T,        .x=CONST_LST+3, .y=CONST_LST+4, .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX(2),   .y=NIL,         .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX(3),   .y=CONST_LST+5, .z=UNDEF        },
    { .t=Pair_T,        .x=TO_FIX(4),   .y=NIL,         .z=UNDEF        },
#endif
#endif

/*
(define var-beh
  (lambda (index)
    (BEH (cust env)           ; eval
      (SEND env (list cust index)))))
*/
#define VAR_BEH (CONST_LST+6)
//  { .t=VM_push,       .x=_index_,     .y=VAR_BEH+0,   .z=UNDEF        },
    { .t=VM_msg,        .x=1,           .y=VAR_BEH+1,   .z=UNDEF        },  // cust
    { .t=VM_msg,        .x=2,           .y=VAR_BEH+2,   .z=UNDEF        },  // env
    { .t=VM_send,       .x=2,           .y=COMMIT,      .z=UNDEF        },  // (env cust index)
#define VAR_1 (VAR_BEH+3)
    { .t=Actor_T,       .x=VAR_1+1,     .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=TO_FIX(1),   .y=VAR_BEH,     .z=UNDEF        },  // index = +1

/*
(define k-apply-beh
  (lambda (cust oper env)
    (BEH args
      (SEND oper
        (list cust args env)))))
*/
#define K_APPLY (VAR_1+2)
//  { .t=VM_push,       .x=_cust_,      .y=K_APPLY-2,   .z=UNDEF        },
//  { .t=VM_push,       .x=_oper_,      .y=K_APPLY-1,   .z=UNDEF        },
//  { .t=VM_push,       .x=_env_,       .y=K_APPLY+0,   .z=UNDEF        },
    { .t=VM_msg,        .x=0,           .y=K_APPLY+1,   .z=UNDEF        },  // args
    { .t=VM_pick,       .x=4,           .y=K_APPLY+2,   .z=UNDEF        },  // cust
    { .t=VM_pick,       .x=4,           .y=K_APPLY+3,   .z=UNDEF        },  // oper
    { .t=VM_send,       .x=3,           .y=COMMIT,      .z=UNDEF        },  // (oper cust args env)
/*
(define appl-beh
  (lambda (oper senv)
    (BEH (cust params . opt-env)
      (if (pair? opt-env)
        (SEND                 ; apply
          (CREATE (evlis-beh params))
          (list (CREATE (k-apply-beh cust oper senv)) (car opt-env)))
        (SEND cust SELF)      ; eval
      ))))
*/
#define APPL_BEH (K_APPLY+4)
//  { .t=VM_push,       .x=_oper_,      .y=APPL_BEH-1,  .z=UNDEF        },
//  { .t=VM_push,       .x=_senv_,      .y=APPL_BEH+0,  .z=UNDEF        },
    { .t=VM_msg,        .x=-2,          .y=APPL_BEH+1,  .z=UNDEF        },  // opt-env
    { .t=VM_typeq,      .x=Pair_T,      .y=APPL_BEH+2,  .z=UNDEF        },  // opt-env has type Pair_T
    { .t=VM_if,         .x=APPL_BEH+3,  .y=SELF_EVAL,   .z=UNDEF        },

    { .t=VM_msg,        .x=1,           .y=APPL_BEH+4,  .z=UNDEF        },  // cust
    { .t=VM_pick,       .x=3,           .y=APPL_BEH+5,  .z=UNDEF        },  // oper
    { .t=VM_pick,       .x=3,           .y=APPL_BEH+6,  .z=UNDEF        },  // senv
    { .t=VM_push,       .x=K_APPLY,     .y=APPL_BEH+7,  .z=UNDEF        },  // K_APPLY
    { .t=VM_new,        .x=3,           .y=APPL_BEH+8,  .z=UNDEF        },  // k_apply

    { .t=VM_msg,        .x=3,           .y=APPL_BEH+9,  .z=UNDEF        },  // denv
    { .t=VM_pick,       .x=2,           .y=APPL_BEH+10, .z=UNDEF        },  // k_apply
    { .t=VM_msg,        .x=2,           .y=APPL_BEH+11, .z=UNDEF        },  // params
    { .t=VM_push,       .x=EVLIS_BEH,   .y=APPL_BEH+12, .z=UNDEF        },  // EVLIS_BEH
    { .t=VM_new,        .x=1,           .y=APPL_BEH+13, .z=UNDEF        },  // ev_list
    { .t=VM_send,       .x=2,           .y=COMMIT,      .z=UNDEF        },  // (ev_list k_apply denv)

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
#define OPER_BEH (APPL_BEH+14)
//  { .t=VM_push,       .x=_body_,      .y=OPER_BEH+0,  .z=UNDEF        },
    { .t=VM_msg,        .x=-2,          .y=OPER_BEH+1,  .z=UNDEF        },  // opt-env
    { .t=VM_eq,         .x=NIL,         .y=OPER_BEH+2,  .z=UNDEF        },  // opt-env == ()
    { .t=VM_if,         .x=SELF_EVAL,   .y=OPER_BEH+3,  .z=UNDEF        },

    { .t=VM_msg,        .x=2,           .y=OPER_BEH+4,  .z=UNDEF        },  // value = arg
    { .t=VM_msg,        .x=3,           .y=OPER_BEH+5,  .z=UNDEF        },  // next = env
    { .t=VM_push,       .x=BOUND_BEH,   .y=OPER_BEH+6,  .z=UNDEF        },  // BOUND_BEH
    { .t=VM_new,        .x=2,           .y=OPER_BEH+7,  .z=UNDEF        },  // ext-env

    { .t=VM_msg,        .x=1,           .y=OPER_BEH+8,  .z=UNDEF        },  // cust
    { .t=VM_pick,       .x=3,           .y=OPER_BEH+9,  .z=UNDEF        },  // body
    { .t=VM_send,       .x=2,           .y=COMMIT,      .z=UNDEF        },  // (body cust ext-env)

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
#define OP_LAMBDA (OPER_BEH+10)
    { .t=Actor_T,       .x=OP_LAMBDA+1, .y=UNDEF,       .z=UNDEF        },
    { .t=VM_msg,        .x=-2,          .y=OP_LAMBDA+2, .z=UNDEF        },  // opt-env
    { .t=VM_eq,         .x=NIL,         .y=OP_LAMBDA+3, .z=UNDEF        },  // opt-env == ()
    { .t=VM_if,         .x=SELF_EVAL,   .y=OP_LAMBDA+4, .z=UNDEF        },

    { .t=VM_msg,        .x=2,           .y=OP_LAMBDA+5, .z=UNDEF        },  // body
    { .t=VM_push,       .x=OPER_BEH,    .y=OP_LAMBDA+6, .z=UNDEF        },  // OPER_BEH
    { .t=VM_new,        .x=1,           .y=OP_LAMBDA+7, .z=UNDEF        },  // oper

    { .t=VM_msg,        .x=3,           .y=OP_LAMBDA+8, .z=UNDEF        },  // env
    { .t=VM_push,       .x=APPL_BEH,    .y=OP_LAMBDA+9, .z=UNDEF        },  // APPL_BEH
    { .t=VM_new,        .x=2,           .y=CUST_SEND,   .z=UNDEF        },  // appl

/*
(define comb-beh
  (lambda (comb param)
    (BEH (cust env)           ; eval
      (SEND comb
        (list (CREATE (k-call-beh (list cust param env))) env)))))
*/
#define COMB_BEH (OP_LAMBDA+10)
//  { .t=VM_push,       .x=_comb_,      .y=COMB_BEH-1,  .z=UNDEF        },
//  { .t=VM_push,       .x=_param_,     .y=COMB_BEH+0,  .z=UNDEF        },
    { .t=VM_msg,        .x=2,           .y=COMB_BEH+1,  .z=UNDEF        },  // env

    { .t=VM_push,       .x=NIL,         .y=COMB_BEH+2,  .z=UNDEF        },  // ()
    { .t=VM_pick,       .x=2,           .y=COMB_BEH+3,  .z=UNDEF        },  // env
    { .t=VM_pick,       .x=4,           .y=COMB_BEH+4,  .z=UNDEF        },  // param
    { .t=VM_msg,        .x=1,           .y=COMB_BEH+5,  .z=UNDEF        },  // cust
    { .t=VM_pair,       .x=3,           .y=COMB_BEH+6,  .z=UNDEF        },  // msg = (cust param env)
    { .t=VM_push,       .x=K_CALL,      .y=COMB_BEH+7,  .z=UNDEF        },  // K_CALL
    { .t=VM_new,        .x=1,           .y=COMB_BEH+8,  .z=UNDEF        },  // k_call

    { .t=VM_pick,       .x=4,           .y=COMB_BEH+9,  .z=UNDEF        },  // comb
    { .t=VM_send,       .x=2,           .y=COMMIT,      .z=UNDEF        },  // (comb k_call env)

#define OP_I (COMB_BEH+10)
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
    //{ .t=VM_push,       .x=CONST_7,     .y=COMB_BEH,    .z=UNDEF        },  // param = CONST_7
    //{ .t=VM_push,       .x=TO_FIX(-13), .y=COMB_BEH,    .z=UNDEF        },  // param = -13
    { .t=VM_push,       .x=CONST_LST,   .y=COMB_BEH,    .z=UNDEF        },  // param = CONST_LST

#define BOUND_42 (EXPR_I+3)
    { .t=Actor_T,       .x=BOUND_42+1,  .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=TO_FIX(42),  .y=BOUND_42+2,  .z=UNDEF        },  // value = +42
    { .t=VM_push,       .x=EMPTY_ENV,   .y=BOUND_BEH,   .z=UNDEF        },  // next = EMPTY_ENV
#define A_TEST (BOUND_42+3)
    { .t=Actor_T,       .x=A_TEST+1,    .y=UNDEF,       .z=UNDEF        },
    { .t=VM_push,       .x=BOUND_42,    .y=A_TEST+2,    .z=UNDEF        },  // BOUND_42
    { .t=VM_push,       .x=A_PRINT,     .y=A_TEST+3,    .z=UNDEF        },  // A_PRINT
    { .t=VM_push,       .x=EXPR_I,      .y=A_TEST+4,    .z=UNDEF        },  // EXPR_I
    { .t=VM_send,       .x=2,           .y=COMMIT,      .z=UNDEF        },  // (EXPR_I A_PRINT BOUND_42)

#define A_QUIT (A_TEST+5)
    { .t=Actor_T,       .x=A_QUIT+1,    .y=UNDEF,       .z=UNDEF        },
    { .t=VM_end,        .x=END_STOP,    .y=UNDEF,       .z=UNDEF        },  // kill thread

};
cell_t *cell_zero = &cell_table[0];  // base for cell offsets
int_t cell_next = NIL;  // head of cell free-list (or NIL if empty)
int_t cell_top = A_QUIT+2; // limit of allocated cell memory

static struct { int_t addr; char *label; } symbol_table[] = {
    { FALSE, "FALSE" },
    { TRUE, "TRUE" },
    { NIL, "NIL" },
    { UNDEF, "UNDEF" },
    { UNIT, "UNIT" },
    { START, "START" },
    { SELF_EVAL, "SELF_EVAL" },
    { CUST_SEND, "CUST_SEND" },
    { SEND_0, "SEND_0" },
    { COMMIT, "COMMIT" },
    { RESEND, "RESEND" },

    { A_CLOCK, "A_CLOCK" },
    { CLOCK_BEH, "CLOCK_BEH" },

    { S_VALUE, "S_VALUE" },
    { S_GETC, "S_GETC" },
    { S_END_X, "S_END_X" },
    { S_VAL_X, "S_VAL_X" },
    { S_LIST_B, "S_LIST_B" },
    { G_START, "G_START" },
    { G_CALL_B, "G_CALL_B" },
    { G_LANG, "G_LANG" },
    { EMPTY_ENV, "EMPTY_ENV" },

    { REPL_R, "REPL_R" },
    { REPL_E, "REPL_E" },
    { REPL_P, "REPL_P" },
    { REPL_L, "REPL_L" },
    { REPL_F, "REPL_F" },
    { A_BOOT, "A_BOOT" },

    { TAG_BEH, "TAG_BEH" },
    { K_JOIN_H, "K_JOIN_H" },
    { K_JOIN_T, "K_JOIN_T" },
    { JOIN_BEH, "JOIN_BEH" },
    { FORK_BEH, "FORK_BEH" },
    { EVLIS_BEH, "EVLIS_BEH" },
    { K_CALL, "K_CALL" },

    { G_EMPTY, "G_EMPTY" },
    { G_FAIL, "G_FAIL" },
    { G_NEXT_K, "G_NEXT_K" },
    { G_ANY, "G_ANY" },
    { G_EQ_B, "G_EQ_B" },
    { G_OR_F, "G_OR_F" },
    { G_OR_B, "G_OR_B" },
    { G_AND_PR, "G_AND_PR" },
    { G_AND_OK, "G_AND_OK" },
    { G_AND_F, "G_AND_F" },
    { G_AND_B, "G_AND_B" },
    { G_OPT_B, "G_OPT_B" },
    { G_PLUS_B, "G_PLUS_B" },
    { G_STAR_B, "G_STAR_B" },
    { G_ALT_B, "G_ALT_B" },
    { G_SEQ_B, "G_SEQ_B" },
    { G_CLS_B, "G_CLS_B" },
    { OP_SE_BEH, "OP_SE_BEH" },
    { G_XFM_K, "G_XFM_K" },
    { G_XFM_OK, "G_XFM_OK" },
    { G_XFORM_B, "G_XFORM_B" },

    { G_WSP, "G_WSP" },
    { G_WSP_S, "G_WSP_S" },
    { G_SGN, "G_SGN" },
    { G_OPEN, "G_OPEN" },
    { G_CLOSE, "G_CLOSE" },
    { G_DGT, "G_DGT" },
    { G_UPR, "G_UPR" },
    { G_LWR, "G_LWR" },
    { G_ATOM, "G_ATOM" },
    { G_SGN_O, "G_SGN_O" },
    { G_DGT_OK, "G_DGT_OK" },
    { G_DGT_P, "G_DGT_P" },
    { G_FIXNUM, "G_FIXNUM" },
    { G_ATOM_OK, "G_ATOM_OK" },
    { G_ATOM_P, "G_ATOM_P" },
    { G_SYMBOL, "G_SYMBOL" },
    { O_CADR, "O_CADR" },
    { OP_CADR, "OP_CADR" },
    { G_SEXPR, "G_SEXPR" },
    { G_SEXPR_X, "G_SEXPR_X" },
    { G_SEXPR_S, "G_SEXPR_S" },
    { G_ALT_EX, "G_ALT_EX" },
    { G_LIST, "G_LIST" },
    { G_LIST_X, "G_LIST_X" },
    { G_PTRN, "G_PTRN" },

    { S_EMPTY, "S_EMPTY" },
    { A_PRINT, "A_PRINT" },
    { A_OK, "A_OK" },
    { A_FAIL, "A_FAIL" },
    { A_EVAL, "A_EVAL" },
    { G_TEST, "G_TEST" },

    { OP_FUNC_B, "OP_FUNC_B" },
    { K_INVOKE, "K_INVOKE" },
    { AP_FUNC_B, "AP_FUNC_B" },
    { F_QUOTE, "F_QUOTE" },
    { OP_QUOTE, "OP_QUOTE" },
    { F_LIST, "F_LIST" },
    { AP_LIST, "AP_LIST" },
    { K_DEFINE, "K_DEFINE" },
    { OP_DEFINE, "OP_DEFINE" },
    { F_CONS, "F_CONS" },
    { AP_CONS, "AP_CONS" },
    { F_CAR, "F_CAR" },
    { AP_CAR, "AP_CAR" },
    { F_CDR, "F_CDR" },
    { AP_CDR, "AP_CDR" },
    { F_CADR, "F_CADR" },
    { AP_CADR, "AP_CADR" },
    { F_CADDR, "F_CADDR" },
    { AP_CADDR, "AP_CADDR" },
    { F_G_EQ, "F_G_EQ" },
    { AP_G_EQ, "AP_G_EQ" },
    { F_G_OR, "F_G_OR" },
    { AP_G_OR, "AP_G_OR" },
    { F_G_AND, "F_G_AND" },
    { AP_G_AND, "AP_G_AND" },
    { F_G_CLS, "F_G_CLS" },
    { AP_G_CLS, "AP_G_CLS" },
    { F_G_OPT, "F_G_OPT" },
    { AP_G_OPT, "AP_G_OPT" },
    { F_G_PLUS, "F_G_PLUS" },
    { AP_G_PLUS, "AP_G_PLUS" },
    { F_G_STAR, "F_G_STAR" },
    { AP_G_STAR, "AP_G_STAR" },
    { F_G_ALT, "F_G_ALT" },
    { AP_G_ALT, "AP_G_ALT" },
    { F_G_SEQ, "F_G_SEQ" },
    { AP_G_SEQ, "AP_G_SEQ" },
    { F_G_CALL, "F_G_CALL" },
    { OP_G_CALL, "OP_G_CALL" },
    { F_LST_NUM, "F_LST_NUM" },
    { AP_LST_NUM, "AP_LST_NUM" },
    { F_LST_SYM, "F_LST_SYM" },
    { AP_LST_SYM, "AP_LST_SYM" },
    { F_G_SRC, "F_G_SRC" },
    { AP_G_SRC, "AP_G_SRC" },
    { F_G_START, "F_G_START" },
    { AP_G_START, "AP_G_START" },

    { K_COMPILE, "K_COMPILE" },
    { COMPILE_B, "COMPILE_B" },
    { K_LAMBDAC, "K_LAMBDAC" },
    { LAMBDA_C, "LAMBDA_C" },

    { BOUND_BEH, "BOUND_BEH" },
    { CONST_7, "CONST_7" },
    { CONST_LST, "CONST_LST" },
    { VAR_BEH, "VAR_BEH" },
    { VAR_1, "VAR_1" },
    { K_APPLY, "K_APPLY" },
    { APPL_BEH, "APPL_BEH" },
    { OPER_BEH, "OPER_BEH" },
    { OP_LAMBDA, "OP_LAMBDA" },
    { COMB_BEH, "COMB_BEH" },
    { OP_I, "OP_I" },
    { AP_I, "AP_I" },
    { LAMBDA_I, "LAMBDA_I" },
    { EXPR_I, "EXPR_I" },
    { BOUND_42, "BOUND_42" },
    { A_TEST, "A_TEST" },

    { A_QUIT, "A_QUIT" },
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
#define IS_SYM(n)   TYPEQ(Symbol_T,(n))

int_t get_proc(int_t value) {  // get dispatch proc for _value_
    if (IS_FIX(value)) return Fixnum_T;
    if (IS_PROC(value)) return Proc_T;
    if (IS_CELL(value)) return get_t(value);
    return error("no dispatch proc for value");
}

PROC_DECL(Free) {
    return panic("DISPATCH TO FREE CELL!");
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
        //if (a >= cell_top) c = '-';
        //if ((c == 'x') && IS_FREE(a)) c = 'f';  // <-- should not happen
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
    cstr_intern("_");
    cstr_intern("quote");
    cstr_intern("typeq");
    cstr_intern("eval");
    cstr_intern("apply");
    cstr_intern("map");
    cstr_intern("list");
    cstr_intern("cons");
    cstr_intern("car");
    cstr_intern("cdr");
    cstr_intern("if");
    cstr_intern("and");
    cstr_intern("or");
    cstr_intern("eq?");
    cstr_intern("equal?");
    cstr_intern("seq");
    cstr_intern("lambda");
    cstr_intern("macro");
    cstr_intern("vau");
    cstr_intern("define");
    cstr_intern("boolean?");
    cstr_intern("null?");
    cstr_intern("pair?");
    cstr_intern("symbol?");
    cstr_intern("number?");
    cstr_intern("+");
    cstr_intern("-");
    cstr_intern("*");
    cstr_intern("<");
    cstr_intern("<=");
    cstr_intern("=");
    cstr_intern(">=");
    cstr_intern(">");
    cstr_intern("list->number");
    cstr_intern("list->symbol");
    cstr_intern("print");
    cstr_intern("emit");
    cstr_intern("debug-print");
    cstr_intern("fold");
    cstr_intern("foldr");
    cstr_intern("bind");
    cstr_intern("lookup");
    cstr_intern("content");
    cstr_intern("BEH");
    cstr_intern("SELF");
    cstr_intern("CREATE");
    cstr_intern("SEND");
    cstr_intern("BECOME");
    cstr_intern("FAIL");
    cstr_intern("x");
    cstr_intern("xs");
    cstr_intern("y");
    cstr_intern("z");
    cstr_intern("t");
    cstr_intern("i");
    cstr_intern("j");
    cstr_intern("k");
    cstr_intern("cust");
    cstr_intern("msg");
    cstr_intern("req");
    cstr_intern("h");
    cstr_intern("t");
    cstr_intern("head");
    cstr_intern("tail");
    cstr_intern("first");
    cstr_intern("next");
    cstr_intern("rest");
    cstr_intern("in");
    cstr_intern("ok");
    cstr_intern("fail");
    cstr_intern("token");
    cstr_intern("_");
    cstr_intern("_");
    cstr_intern("_");
    ASSERT(cstr_intern("_") == cstr_intern("_"));
    for (int_t slot = 0; slot < SYM_MAX; ++slot) {
        print_intern(slot);
    }
    return UNIT;
}
#endif // INCLUDE_DEBUG

#define bind_global(cstr,val) set_z(cstr_intern(cstr), (val))

int_t init_global_env() {
#if 0
    int_t s;
    s = cstr_intern("lambda");
    set_z(s, OP_LAMBDA);
#endif
    bind_global("peg-lang", G_SEXPR_X);  // language parser start symbol
    bind_global("#f", FALSE);  // FIXME: should be parsed as a constant
    bind_global("#t", TRUE);  // FIXME: should be parsed as a constant
    bind_global("quote", OP_QUOTE);
    bind_global("list", AP_LIST);
    bind_global("lambda", LAMBDA_C);
    bind_global("define", OP_DEFINE);
    bind_global("cons", AP_CONS);
    bind_global("car", AP_CAR);
    bind_global("cdr", AP_CDR);
    bind_global("cadr", AP_CADR);
    bind_global("caddr", AP_CADDR);
#if 1
    bind_global("CTL", TO_FIX(CTL));
    bind_global("DGT", TO_FIX(DGT));
    bind_global("UPR", TO_FIX(UPR));
    bind_global("LWR", TO_FIX(LWR));
    bind_global("DLM", TO_FIX(DLM));
    bind_global("SYM", TO_FIX(SYM));
    bind_global("HEX", TO_FIX(HEX));
    bind_global("WSP", TO_FIX(WSP));
    bind_global("peg-empty", G_EMPTY);
    bind_global("peg-fail", G_FAIL);
    bind_global("peg-any", G_ANY);
    bind_global("peg-eq", AP_G_EQ);
    bind_global("peg-or", AP_G_OR);
    bind_global("peg-and", AP_G_AND);
    bind_global("peg-class", AP_G_CLS);
    bind_global("peg-opt", AP_G_OPT);
    bind_global("peg-plus", AP_G_PLUS);
    bind_global("peg-star", AP_G_STAR);
    bind_global("peg-alt", AP_G_ALT);
    bind_global("peg-seq", AP_G_SEQ);
    bind_global("peg-call", OP_G_CALL);
    bind_global("peg-source", AP_G_SRC);
    bind_global("peg-start", AP_G_START);
#endif
    bind_global("list->number", AP_LST_NUM);
    bind_global("list->symbol", AP_LST_SYM);
    bind_global("a-print", A_PRINT);
    bind_global("quit", A_QUIT);
    return UNIT;
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
        sp = XFREE(sp);
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

static PROC_DECL(Self_Eval) {  // common code for self-evaluating types
    int_t event = arg;
    ASSERT(IS_CELL(event));
    ASSERT(TYPEQ(Event_T, event));
#if INCLUDE_DEBUG
    if (runtime_trace) {
        DEBUG(print_event(event));
        DEBUG(debug_print("Self_Eval", event));
    }
#endif
    ASSERT(self == get_x(event));
    int_t msg = get_y(event);
    event = XFREE(event);  // event is consumed
    if (IS_PAIR(msg)) {
        int_t cust = car(msg);
        msg = cdr(msg);
        if (IS_PAIR(msg)) {
            int_t env = car(msg);
            msg = cdr(msg);
            if ((msg == NIL) && IS_ACTOR(cust)) {
                // eval message
                event = cell_new(Event_T, cust, self, NIL);
                event_q_put(event);
                return TRUE;  // retry event dispatch
            }
        }
    }
    return error("message not understood");
}

PROC_DECL(Fixnum) {
    return Self_Eval(self, arg);
}

PROC_DECL(Proc) {
    return Self_Eval(self, arg);
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
    return Self_Eval(self, arg);
}

PROC_DECL(Null) {
    return Self_Eval(self, arg);
}

PROC_DECL(Pair) {
    int_t event = arg;
#if INCLUDE_DEBUG
    if (runtime_trace) {
        DEBUG(print_event(event));
        DEBUG(debug_print("Pair", self));
    }
#endif
    ASSERT(self == get_x(event));
    int_t msg = get_y(event);
    event = XFREE(event);  // event is consumed
    if (IS_PAIR(msg)) {
        int_t cust = car(msg);
        msg = cdr(msg);
        if (IS_PAIR(msg)) {
            int_t env = car(msg);
            msg = cdr(msg);
            if ((msg == NIL) && IS_ACTOR(cust)) {
                // eval message
                int_t comb = car(self);
                int_t param = cdr(self);
                int_t apply = list_3(cust, param, env);
                int_t beh = K_CALL;
                beh = cell_new(VM_push, apply, beh, UNDEF);
                int_t k_call = cell_new(Actor_T, beh, UNDEF, UNDEF);
                event = cell_new(Event_T, comb, list_2(k_call, env), NIL);
                event_q_put(event);
                return TRUE;  // retry event dispatch
            }
        }
    }
    return error("message not understood");
}

PROC_DECL(Symbol) {
    int_t event = arg;
#if INCLUDE_DEBUG
    if (runtime_trace) {
        DEBUG(print_event(event));
        DEBUG(debug_print("Symbol", self));
    }
#endif
    ASSERT(self == get_x(event));
    int_t msg = get_y(event);
    event = XFREE(event);  // event is consumed
    if (IS_PAIR(msg)) {
        int_t cust = car(msg);
        msg = cdr(msg);
        if (IS_PAIR(msg)) {
            int_t env = car(msg);
            msg = cdr(msg);
            if ((msg == NIL) && IS_ACTOR(cust)) {
                // eval message
                int_t value = get_z(self);  // value of global symbol
                event = cell_new(Event_T, cust, value, NIL);
                event_q_put(event);
                return TRUE;  // retry event dispatch
            }
        }
    }
    return error("message not understood");
}

PROC_DECL(Unit) {
    return Self_Eval(self, arg);
}

PROC_DECL(Actor) {
    int_t actor = self;
    int_t event = arg;
    ASSERT(actor == get_x(event));
    if (get_y(actor) != UNDEF) {
        return FALSE;  // actor busy
    }
    int_t beh = get_x(actor);  // actor behavior (initial IP)
    // begin actor transaction
    set_y(actor, NIL);  // empty set of new events
    set_z(actor, UNDEF);  // no BECOME (yet...)
    // spawn new "thread" to handle event
    int_t cont = cell_new(beh, NIL, event, NIL);  // ip=beh, sp=(), ep=event
    return cont;
}

PROC_DECL(Event) {
    return Self_Eval(self, arg);
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
    ASSERT(NAT(n) < 4);
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
    if (b == UNDEF) return error("undefined condition");
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
    if (n < 0) return error("vm_beh (n < 0) invalid");
    int_t ep = GET_EP();
    int_t me = get_x(ep);
    ASSERT(get_z(me) == UNDEF);  // BECOME only allowed once
    int_t b = stack_pop();  // behavior
    while (n--) {
        // compose behavior
        int_t v = stack_pop();  // value
        b = cell_new(VM_push, v, b, UNDEF);
    }
    set_z(me, b);
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

PROC_DECL(vm_cvt) {
    int_t c = get_x(self);
    int_t w = stack_pop();
    int_t v = UNDEF;
    switch (c) {
        case CVT_LST_SYM:   v = symbol(w);      break;
        case CVT_LST_NUM:   v = fixnum(w);      break;
        default:            v = error("unknown conversion");
    }
    stack_push(v);
    return get_y(self);
}

PROC_DECL(vm_putc) {
    int_t c = stack_pop();
    ASSERT(IS_FIX(c));
    c = TO_INT(c);
    putchar(c);
    return get_y(self);
}

PROC_DECL(vm_getc) {
    int_t c = getchar();
    c = TO_FIX(c);
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
    if (t > 0) return "COMMIT";
    return "STOP";
}
static char *conversion_label(int_t f) {
    switch (f) {
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

int main(int argc, char const *argv[])
{
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
#if 1
    DEBUG(dump_symbol_table());
#else
    DEBUG(test_symbol_intern());
    DEBUG(hexdump("cell memory", ((int_t *)&cell_table[500]), 16*4));
#endif
    init_global_env();
    gc_add_root(K_CALL);  // used in Pair_T
    gc_add_root(clk_handler);
    clk_timeout = clk_ticks();
    int_t result = runtime();
    DEBUG(debug_print("main result", result));
#if MARK_SWEEP_GC
    gc_mark_and_sweep(TRUE);
#endif // MARK_SWEEP_GC
    DEBUG(fprintf(stderr, "cell_top=%"PuI" gc_free_cnt=%"PRId32"\n", cell_top, gc_free_cnt));
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
