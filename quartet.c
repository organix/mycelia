/*
 * quartet.c -- Hosted imperative stack-oriented actor machine
 * Copyright 2014-2022 Dale Schumacher, Tristan Slominski
 */

/**
Program source is provided as a stream of _words_
(whitespace separated in text format).
If the word parses as a _number_
the value is pushed on the data _stack_.
Otherwise the word is looked up in the current _dictionary_.
If the associated value is a _block_ it is executed,
otherwise the value is pushed on the data _stack_.
The data _stack_ holds parameters for executing blocks
and their return values.
Some blocks also consume words from the source stream.

An actor's behavior is described with a _block_.
The message received by the actor is the contents of the data stack.
The `SEND` primitive sends the current stack contents,
clearing the stack.
Values may be saved in the dictionary
by binding them to a word.
All dictionary changes are local to the executing behavior.

See further [https://github.com/organix/mycelia/blob/master/quartet.md]
**/

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>  // for intptr_t, uintptr_t, uint8_t, uint16_t, etc.
#include <inttypes.h>  // for PRIiPTR, PRIuPTR, PRIXPTR, etc.
#include <ctype.h>

#define DEBUG(x)   // include/exclude debug instrumentation
#define XDEBUG(x) x // include/exclude extra debugging

#define INT_T_32B 0  // universal Integer is 32 bits wide
#define INT_T_64B 1  // universal Integer is 64 bits wide
#define ALLOW_DMA 1  // define words for direct memory access

// universal Integer type (32/64-bit signed 2's-complement)
typedef intptr_t int_t;
#define INT(n) ((int_t)(n))

// universal Natural type (32/64-bit unsigned 2's-complement)
typedef uintptr_t nat_t;
#define NAT(n) ((nat_t)(n))

// universal Data Pointer type (32/64-bit machine address)
typedef void * ptr_t;
#define PTR(n) ((ptr_t)(n))

// Procedure declaration
#define PROC_DECL(name)  int_t name(int_t self)

// tagged types
#define TAG_MASK    INT(3)

#define TAG_NUM     INT(0)
#define TAG_WORD    INT(1)
#define TAG_BLOCK   INT(2)
#define TAG_PROC    INT(3)

// tagged type predicates
#define IS_NUM(x)   (((x) & TAG_MASK) == TAG_NUM)
#define IS_WORD(x)  (((x) & TAG_MASK) == TAG_WORD)
#define IS_BLOCK(x) (((x) & TAG_MASK) == TAG_BLOCK)
#define IS_PROC(x)  (((x) & TAG_MASK) == TAG_PROC)

// tagged type constructors
#define MK_NUM(x)   INT(NAT(x) << 2)
#define MK_WORD(x)  INT(NAT(x) | TAG_WORD)
#define MK_BLOCK(x) INT(NAT(x) | TAG_BLOCK)
#define MK_PROC(x)  INT(PTR(x) + TAG_PROC)

// tagged type conversions
#define TO_INT(x)   (INT(x) >> 2)
#define TO_NAT(x)   (NAT(x) >> 2)
#define TO_PTR(x)   PTR(NAT(x) & ~TAG_MASK)

// universal Boolean constants
#define TRUE MK_NUM(-1)
#define FALSE MK_NUM(0)
#define MK_BOOL(x)  ((x) ? TRUE : FALSE)

// universal Infinity
#define INF MK_NUM(TO_INT(~(NAT(-1)>>1)))

#define NEG(n)      MK_NUM(-TO_INT(n))
#define ADD(n,m)    ((n)+(m))
#define SUB(n,m)    ((n)-(m))
#define MUL(n,m)    MK_NUM(TO_INT(n)*TO_INT(m))
#define CMP(n,m)    ((n)-(m))
#define LTZ(n)      (((n)<0)?TRUE:FALSE)
#define EQZ(n)      (((n)==0)?TRUE:FALSE)
#define GTZ(n)      (((n)>0)?TRUE:FALSE)
#define NOT(n)      MK_NUM(~TO_NAT(n))
#define AND(n,m)    ((n)&(m))
#define IOR(n,m)    ((n)|(m))
#define XOR(n,m)    ((n)^(m))
#define LSL(n,m)    ((NAT(n)<<TO_INT(m)) & ~TAG_MASK)
#define LSR(n,m)    ((NAT(n)>>TO_INT(m)) & ~TAG_MASK)
#define ASR(n,m)    ((INT(n)>>TO_INT(m)) & ~TAG_MASK)

typedef struct block {
    int_t       proc;                   // execution behavior
    nat_t       len;                    // number of int_t in data[]
    int_t       data[];                 // addressable memory
} block_t;

typedef struct env {
    int_t       proc;                   // execution behavior
    int_t       value;                  // .data[0] = bound value
    int_t       word;                   // .data[1] = variable name
    struct env *env;                    // .data[2] = parent environment
} env_t;

typedef struct closure {
    int_t       proc;                   // execution behavior
    nat_t       cnt;                    // .data[0] = number of words
    int_t      *ptr;                    // .data[1] = word address in block
    env_t      *env;                    // .data[2] = parent environment
} closure_t;

typedef struct context {
    int_t       proc;                   // execution behavior
    nat_t       cnt;                    // .data[0] = next_value_cnt
    int_t      *ptr;                    // .data[1] = next_value_ptr
    env_t      *env;                    // .data[2] = dynamic environment
} context_t;

#define MAX_NAME_SZ (4 * sizeof(int_t)) // word buffer size
typedef struct word {
    int_t       proc;                   // execution behavior
    int_t       value;                  // .data[0] = bound value
    int_t       word;                   // .data[1] = variable name
    env_t      *env;                    // .data[2] = parent environment
    char        name[MAX_NAME_SZ];      // NUL-terminated string
} word_t;

#define CACHE_LINE_SZ (sizeof(word_t))  // bytes per idealized cache line
#define VMEM_PAGE_SZ ((size_t)1 << 12)  // bytes per idealized memory page

typedef struct actor {
    int_t       proc;                   // execution behavior
    int_t       beh;                    // .data[0] = behavior (block/closure)
    //int_t      *ptr;                    // .data[1] = word address in block
    //env_t      *env;                    // .data[2] = parent environment
} actor_t;

/*
 * error handling
 */

int_t panic(char *reason) {
    fprintf(stderr, "\nPANIC! %s\n", reason);
    exit(-1);
    return FALSE;  // not reached, but typed for easy swap with error()
}

int_t error(char *reason) {
    fprintf(stderr, "\nERROR! %s\n", reason);
    return FALSE;
}

/*
 * printing utilities
 */

PROC_DECL(prim_Block);  // FORWARD DECLARATION
PROC_DECL(prim_Closure);  // FORWARD DECLARATION
PROC_DECL(prim_Actor);  // FORWARD DECLARATION
PROC_DECL(prim_Environment) { return panic("Environment can not be executed"); }
PROC_DECL(prim_Context) { return panic("Context can not be executed"); }

#define IS_ACTOR(x) (IS_BLOCK(x) && (((block_t *)TO_PTR(x))->proc == MK_PROC(prim_Actor)))

PROC_DECL(prim_Undefined) { return error("undefined procedure"); }
#define UNDEFINED MK_PROC(prim_Undefined)

void print_ascii(int_t code) {
    if ((code & 0x7F) == code) {  // code is in ascii range
        putchar(code);
    }
}

void print_block(nat_t len, int_t *data);  // FORWARD DECLARATION
void print_closure(closure_t *scope);  // FORWARD DECLARATION
void print_actor(actor_t *act);  // FORWARD DECLARATION

void print_value(int_t value) {
    if (IS_NUM(value)) {
        if (value == INF) {
            printf("INF");
        } else {
            printf("%"PRIdPTR, TO_INT(value));
        }
    } else if (IS_WORD(value)) {
        word_t *w = TO_PTR(value);
        printf("%s", w->name);
    } else if (IS_BLOCK(value)) {
        block_t *blk = TO_PTR(value);
        if (blk->proc == MK_PROC(prim_Block)) {
            print_block(blk->len, blk->data);
        } else if (blk->proc == MK_PROC(prim_Closure)) {
            print_closure(TO_PTR(value));
        } else if (blk->proc == MK_PROC(prim_Actor)) {
            print_actor(TO_PTR(value));
        } else {
            printf("^[%p]", TO_PTR(blk->proc));
        }
    } else if (value == UNDEFINED) {
        printf("(UNDEFINED)");
    } else {
        printf("%p", TO_PTR(value));
    }
    //print_ascii(' ');
    //print_ascii('\n');
    fflush(stdout);
}

static char *tag_label[] = { "NUM", "WORD", "BLOCK", "PROC" };
static void debug_value(char *label, int_t value) {
    fprintf(stderr, "%s:", label);
    fprintf(stderr, " %"PRIXPTR"", value);
    fprintf(stderr, " t=%s", tag_label[value & TAG_MASK]);
    fprintf(stderr, " i=%"PRIdPTR"", TO_INT(value));
    //fprintf(stderr, " n=%"PRIuPTR"", TO_NAT(value));
    //fprintf(stderr, " p=%p", TO_PTR(value));
    if (IS_WORD(value)) {
        word_t *w = TO_PTR(value);
        fprintf(stderr, " s=\"%s\"", w->name);
        //fprintf(stderr, " v=%"PRIXPTR"", w->value);
    }
    if (IS_BLOCK(value)) {
        block_t *blk = TO_PTR(value);
        if (blk->proc == MK_PROC(prim_Block)) {
            fprintf(stderr, " [%"PRIuPTR"]", blk->len);
        } else if (blk->proc == MK_PROC(prim_Closure)) {
            closure_t *scope = TO_PTR(value);
            fprintf(stderr, " [%"PRIuPTR"] env=%p", scope->cnt, scope->env);
        } else if (blk->proc == MK_PROC(prim_Actor)) {
            actor_t *act = TO_PTR(value);
            fflush(stderr);
            printf(" beh=");
            print_value(act->beh);
            fflush(stdout);
        } else {
            fprintf(stderr, " [...]");
        }
    }
    fprintf(stderr, "\n");
    fflush(stderr);
}

#if INT_T_32B
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
#endif // INT_T_32B

#if INT_T_64B
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
#endif // INT_T_64B

/*
 * parsing utilities
 */

int_t read_token(char *buf, size_t buf_sz) {
    DEBUG(fprintf(stderr, "> read_token\n"));
    char *p = buf;
    char *q = p + buf_sz;
    DEBUG(fprintf(stderr, "  read_token: skip leading whitespace\n"));
    int c = getchar();
    while ((c <= ' ') || (c == '#') || (c >= 0x7F)) {
        if (c == '#') {  // comment extends to end of line
            DEBUG(fprintf(stderr, "  read_token: skip comment to EOL\n"));
            while ((c != EOF) && (c != '\n')) {
                c = getchar();
            }
        }
        if (c == EOF) {
            DEBUG(fprintf(stderr, "< read_token = FALSE (eof)\n"));
            return FALSE;  // end of file
        }
        c = getchar();
    }
    DEBUG(fprintf(stderr, "  read_token: gather token characters\n"));
    while ((c != EOF) && (c > ' ') && (c < 0x7F)) {
        *p++ = c;
        if (p >= q) {
            return panic("token buffer overflow");
        }
        c = getchar();
    }
    *p = '\0';  // NUL-terminate string
    DEBUG(fprintf(stderr, "< read_token = TRUE \"%s\"\n", buf));
    return TRUE;
}

static char *base36digit = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
int_t token_to_number(int_t *value_out, char *s) {
    DEBUG(fprintf(stderr, "> token_to_number\n"));
    // attempt to parse token as a number
    int_t neg = FALSE;
    int_t got_base = FALSE;
    int_t got_digit = FALSE;
    uintptr_t base = 10;
    DEBUG(fprintf(stderr, "  token_to_number: s=\"%s\"\n", s));
    uintptr_t n = 0;
    char c = *s++;
    if (c == '-') {  // remember leading minus
        neg = TRUE;
        c = *s++;
    } else if (c == '+') {  // skip leading plus
        c = *s++;
    }
    while (c) {
        if (c == '_') {  // skip separator
            c = *s++;
        }
        if (!got_base && got_digit && (c == '#')) {
            base = n;
            if ((base < 2) || (base > 36)) {
                DEBUG(fprintf(stderr, "< token_to_number = FALSE (base range)\n"));
                return FALSE;  // number base out of range
            }
            got_base = TRUE;
            got_digit = FALSE;
            n = 0;
            c = *s++;
        }
        char *p = strchr(base36digit, toupper(c));
        if (p == NULL) {
            DEBUG(fprintf(stderr, "< token_to_number = FALSE (non-digit)\n"));
            return FALSE;  // non-digit character
        }
        uintptr_t digit = NAT(p - base36digit);
        if (digit >= base) {
            DEBUG(fprintf(stderr, "< token_to_number = FALSE (digit range)\n"));
            return FALSE;  // digit out of range for base
        }
        n *= base;
        n += digit;
        got_digit = TRUE;
        c = *s++;
    }
    if (!got_digit) {
        DEBUG(fprintf(stderr, "< token_to_number = FALSE (need digits)\n"));
        return FALSE;  // need at least one digit
    }
    *value_out = (neg ? -INT(n) : INT(n));
    DEBUG(fprintf(stderr, "< token_to_number = TRUE\n"));
    return TRUE;
}

/*
 * data stack
 */

#define MAX_STACK ((size_t)(128))
int_t data_stack[MAX_STACK];
size_t data_top = 0;

void print_stack() {
    for (size_t i = 0; i < data_top; ++i) {
        print_value(data_stack[i]);
        print_ascii(' ');
    }
}

static int_t stack_overflow() { return panic("stack overflow"); }
static int_t stack_underflow() { return error("empty stack"); }
static int_t index_out_of_bounds() { return panic("index out of bounds"); }

int_t data_push(int_t value) {
    if (data_top < MAX_STACK) {
        data_stack[data_top++] = value;
        return TRUE;
    }
    return stack_overflow();
}

int_t data_pop(int_t *value_out) {
    if (data_top > 0) {
        *value_out = data_stack[--data_top];
        return TRUE;
    }
    return stack_underflow();
}

int_t data_peek(int_t *value_out) {
    if (data_top > 0) {
        *value_out = data_stack[data_top - 1];
        return TRUE;
    }
    return stack_underflow();
}

int_t data_pick(int_t *value_out, int n) {
    if ((n < 1) || (n > data_top)) {
        return index_out_of_bounds();
    }
    *value_out = data_stack[data_top - n];
    return TRUE;
}

int_t data_roll(int n) {
    if (n == 0) return TRUE;  // no-op
    if (n > 0) {
        if (n > data_top) {
            return index_out_of_bounds();
        }
        int_t *p = &data_stack[data_top - n];
        int_t x = p[0];
        while (--n > 0) {
            p[0] = p[1];
            ++p;
        }
        p[0] = x;
    } else {  // reverse rotate
        n = -n;
        if (n > data_top) {
            return index_out_of_bounds();
        }
        int_t *p = &data_stack[data_top - 1];
        int_t x = p[0];
        while (--n > 0) {
            p[0] = p[-1];
            --p;
        }
        p[0] = x;
    }
    return TRUE;
}

/*
 * word dictionary
 */

static int_t undefined_word(int_t word) {
    // error on undefined word
    print_value(word);
    fflush(stdout);
    return error("undefined word");
}

PROC_DECL(prim_CREATE);
PROC_DECL(prim_SEND);
PROC_DECL(prim_BECOME);
PROC_DECL(prim_SELF);
PROC_DECL(prim_FAIL);
PROC_DECL(prim_STEP);
PROC_DECL(prim_RUN);
PROC_DECL(prim_Bind);
PROC_DECL(prim_Literal);
PROC_DECL(prim_Lookup);
PROC_DECL(prim_OpenQuote);
PROC_DECL(prim_CloseQuote);
PROC_DECL(prim_OpenUnquote);
PROC_DECL(prim_CloseUnquote);
//PROC_DECL(prim_TRUE);
//PROC_DECL(prim_FALSE);
//PROC_DECL(prim_ZEROP);
PROC_DECL(prim_IF);
PROC_DECL(prim_IF_ELSE);
PROC_DECL(prim_WHILE);
PROC_DECL(prim_DROP);
PROC_DECL(prim_DUP);
PROC_DECL(prim_SWAP);
PROC_DECL(prim_PICK);
PROC_DECL(prim_ROLL);
PROC_DECL(prim_DEPTH);
//PROC_DECL(prim_INF);
PROC_DECL(prim_NEG);
PROC_DECL(prim_ADD);
PROC_DECL(prim_SUB);
PROC_DECL(prim_MUL);
PROC_DECL(prim_DIVMOD);
PROC_DECL(prim_FMA);
PROC_DECL(prim_CMP);
PROC_DECL(prim_LTZ);
PROC_DECL(prim_EQZ);
PROC_DECL(prim_GTZ);
PROC_DECL(prim_NOT);
PROC_DECL(prim_AND);
PROC_DECL(prim_IOR);
PROC_DECL(prim_XOR);
PROC_DECL(prim_LSL);
PROC_DECL(prim_LSR);
PROC_DECL(prim_ASR);
#if ALLOW_DMA
PROC_DECL(prim_Load);
PROC_DECL(prim_Store);
PROC_DECL(prim_LoadAtomic);
PROC_DECL(prim_StoreAtomic);
PROC_DECL(prim_DUMP);
#endif
PROC_DECL(prim_WORDS);
PROC_DECL(prim_EMIT);
PROC_DECL(prim_PrintStack);
PROC_DECL(prim_PrintDebug);
PROC_DECL(prim_Print);

#define MAX_WORDS ((size_t)(128))
word_t word_list[MAX_WORDS] = {
    { .value = MK_PROC(prim_CREATE), .name = "CREATE" },
    { .value = MK_PROC(prim_SEND), .name = "SEND" },
    { .value = MK_PROC(prim_BECOME), .name = "BECOME" },
    { .value = MK_PROC(prim_SELF), .name = "SELF" },
    { .value = MK_PROC(prim_FAIL), .name = "FAIL" },
    { .value = MK_PROC(prim_STEP), .name = "STEP" },
    { .value = MK_PROC(prim_RUN), .name = "RUN" },
    { .value = MK_PROC(prim_Bind), .name = "=" },
    { .value = MK_PROC(prim_Literal), .name = "'" },
    { .value = MK_PROC(prim_Lookup), .name = "@" },
    { .value = MK_PROC(prim_OpenQuote), .name = "[" },
    { .value = MK_PROC(prim_CloseQuote), .name = "]" },
    { .value = MK_PROC(prim_OpenUnquote), .name = "(" },
    { .value = MK_PROC(prim_CloseUnquote), .name = ")" },
    { .value = TRUE, .name = "TRUE" },
    { .value = FALSE, .name = "FALSE" },
    { .value = MK_PROC(prim_EQZ), .name = "ZERO?" },
    { .value = MK_PROC(prim_IF), .name = "IF" },
    { .value = MK_PROC(prim_IF_ELSE), .name = "IF-ELSE" },
    { .value = MK_PROC(prim_WHILE), .name = "WHILE" },
    { .value = MK_PROC(prim_DROP), .name = "DROP" },
    { .value = MK_PROC(prim_DUP), .name = "DUP" },
    { .value = MK_PROC(prim_SWAP), .name = "SWAP" },
    { .value = MK_PROC(prim_PICK), .name = "PICK" },
    { .value = MK_PROC(prim_ROLL), .name = "ROLL" },
    { .value = MK_PROC(prim_DEPTH), .name = "DEPTH" },
    { .value = INF, .name = "INF" },
    { .value = MK_PROC(prim_NEG), .name = "NEG" },
    { .value = MK_PROC(prim_ADD), .name = "ADD" },
    { .value = MK_PROC(prim_SUB), .name = "SUB" },
    { .value = MK_PROC(prim_MUL), .name = "MUL" },
    { .value = MK_PROC(prim_DIVMOD), .name = "DIVMOD" },
    { .value = MK_PROC(prim_FMA), .name = "FMA" },
    { .value = MK_PROC(prim_SUB), .name = "COMPARE" },
    { .value = MK_PROC(prim_LTZ), .name = "LT?" },
    { .value = MK_PROC(prim_EQZ), .name = "EQ?" },
    { .value = MK_PROC(prim_GTZ), .name = "GT?" },
    { .value = MK_PROC(prim_NOT), .name = "NOT" },
    { .value = MK_PROC(prim_AND), .name = "AND" },
    { .value = MK_PROC(prim_IOR), .name = "OR" },
    { .value = MK_PROC(prim_XOR), .name = "XOR" },
    { .value = MK_PROC(prim_LSL), .name = "LSL" },
    { .value = MK_PROC(prim_LSR), .name = "LSR" },
    { .value = MK_PROC(prim_ASR), .name = "ASR" },
#if ALLOW_DMA
    { .value = MK_PROC(prim_Load), .name = "?" },
    { .value = MK_PROC(prim_Store), .name = "!" },
    { .value = MK_PROC(prim_LoadAtomic), .name = "??" },
    { .value = MK_PROC(prim_StoreAtomic), .name = "!!" },
    { .value = MK_PROC(prim_DUMP), .name = "DUMP" },
#endif
    { .value = MK_PROC(prim_WORDS), .name = "WORDS" },
    { .value = MK_PROC(prim_EMIT), .name = "EMIT" },
    { .value = MK_PROC(prim_PrintStack), .name = "..." },
    { .value = MK_PROC(prim_PrintDebug), .name = ".?" },
    { .value = MK_PROC(prim_Print), .name = "." },
};
#if ALLOW_DMA
size_t ro_words = 54;  // limit of read-only words
size_t rw_words = 54;  // limit of read/write words
#else
size_t ro_words = 49;  // limit of read-only words
size_t rw_words = 49;  // limit of read/write words
#endif

static void debug_word(char *label, int_t word) {
    word_t *w = TO_PTR(word);
    fprintf(stderr, "%s:", label);
    fprintf(stderr, " %p", PTR(w));
    fprintf(stderr, " value=%"PRIXPTR"", w->value);
    fprintf(stderr, " s=\"%s\"", w->name);
    fprintf(stderr, "\n");
    fflush(stderr);
}

#if 0
/* this code is an example a range check implemented by a single comparison */
int_t is_word(int_t value) {
//    if (NAT(value - INT(word_list)) < sizeof(word_list)) {
    if (NAT(value - INT(word_list)) <= (rw_words * sizeof(word_t))) {
        return TRUE;
    }
    return FALSE;
}
#endif

int_t parse_value(int_t *value_out) {
    // read token into next available word buffer
    word_t *w = &word_list[rw_words];
    w->value = UNDEFINED;
    char *word_buf = w->name;
    if (!read_token(word_buf, MAX_NAME_SZ)) return FALSE;

    // attempt to parse token as a number
    int_t num = TO_INT(INF);
    if (token_to_number(&num, word_buf)) {
        *value_out = MK_NUM(num);
    } else {
        *value_out = MK_WORD(w);
    }
    DEBUG(debug_value("  parse_value", *value_out));
    return TRUE;
}

context_t *next_context = PTR(0);
int_t next_value(int_t *value_out) {
    if (next_context) {
        DEBUG(fprintf(stderr, "  next_value cnt=%"PRIuPTR" ptr=%p env=%p\n",
            next_context->cnt, next_context->ptr, next_context->env));
        // read from block data
        if (next_context->cnt) {
            --next_context->cnt;
            *value_out = *next_context->ptr++;
            DEBUG(debug_value("  next_value", *value_out));
            return TRUE;
        }
        next_context->ptr = PTR(0);
        return FALSE;  // no more words (in block)
    }
    // read from input stream
    return parse_value(value_out);
}

// convert latest token into new word
int_t create_word(int_t *word_out, int_t word) {
    word_t *w = TO_PTR(word);
    if (rw_words >= MAX_WORDS) return panic("too many words");
    if (w != &word_list[rw_words]) return panic("must create from latest token");
    ++rw_words;  // extend r/w dictionary
    word = MK_WORD(w);
    DEBUG(debug_word("  create_word", word));
    *word_out = word;
    return TRUE;
}

// lookup word in entire dictionary, fail if not found.
int_t find_ro_word(int_t *word_out, int_t word) {
    word_t *w = TO_PTR(word);
    for (size_t n = rw_words; (n-- > 0); ) {  // search from _end_ of dictionary
        word_t *m = &word_list[n];
        if (strcmp(w->name, m->name) == 0) {
            word = MK_WORD(m);
            DEBUG(debug_word("  ro_word", word));
            *word_out = word;
            return TRUE;
        }
    }
    return FALSE;
}

// lookup word in entire dictionary, create if not found.
int_t get_ro_word(int_t *word_out, int_t word) {
    if (find_ro_word(word_out, word)) return TRUE;  // word already exists
    return create_word(word_out, word);
}

// lookup word in writable dictionary, fail if not found.
int_t find_rw_word(int_t *word_out, int_t word) {
    word_t *w = TO_PTR(word);
    for (size_t n = rw_words; (n-- > ro_words); ) {  // search from _end_ of dictionary
        word_t *m = &word_list[n];
        if (strcmp(w->name, m->name) == 0) {
            word = MK_WORD(m);
            DEBUG(debug_word("  rw_word", word));
            *word_out = word;
            return TRUE;
        }
    }
    return FALSE;
}

// lookup word in writable dictionary, create if not found.
int_t get_rw_word(int_t *word_out, int_t word) {
    if (find_rw_word(word_out, word)) return TRUE;  // word already exists
    return create_word(word_out, word);
}

// get currently-bound value for word
int_t get_word_value(int_t *value_out, int_t word) {
    // find word in current dictionary
    DEBUG(debug_word("  get_word_value (word)", word));
    int_t norm;
    if (!find_ro_word(&norm, word)) return undefined_word(word);
    if (norm != word) {
        if (next_context) {
            XDEBUG(debug_word("  get_word_value (NORM)", norm));  // FIXME: should not happen!
        } else {
            DEBUG(debug_word("  get_word_value (norm)", norm));
        }
        word = norm;  // use normalized word
    }
    // find value bound to word
    if (next_context) {
        DEBUG(fprintf(stderr, "  get_word_value cnt=%"PRIuPTR" ptr=%p env=%p\n",
            next_context->cnt, next_context->ptr, next_context->env));
        // from local dictionary
        env_t *env = next_context->env;
        while (env) {
            if (env->word == word) {
                *value_out = env->value;
                DEBUG(debug_value("  get_word_value (local)", *value_out));
                return TRUE;
            }
            env = env->env;  // follow environment chain
        }
    }
    // from global dictionary
    word_t *w = TO_PTR(word);
    *value_out = w->value;
    DEBUG(debug_value("  get_word_value (global)", *value_out));
    return TRUE;
}

int_t new_block(int_t *block_out, nat_t len);  // FORWARD

// set currently-bound value for word
int_t set_word_value(int_t word, int_t value) {
    // find word in current dictionary
    if (!get_rw_word(&word, word)) return undefined_word(word);
    DEBUG(debug_word("  set_word_value (word)", word));
    if (next_context) {
        // bind word in local dictionary
        DEBUG(fprintf(stderr, "  set_word_value cnt=%"PRIuPTR" ptr=%p env=%p\n",
            next_context->cnt, next_context->ptr, next_context->env));
        // create local binding
        int_t block;
        if (!new_block(&block, 4)) return FALSE;
        env_t *env = TO_PTR(block);
        env->proc = MK_PROC(prim_Environment);
        env->word = word;
        env->value = value;
        // chain environments
        env->env = next_context->env;
        next_context->env = env;
        DEBUG(debug_value("  set_word_value (local)", value));
    } else {
        // bind word in global dictionary
        word_t *w = TO_PTR(word);
        w->value = value;
        DEBUG(debug_value("  set_word_value (global)", value));
    }
    return TRUE;
}

/*
 * primitive word definitions
 */

#define POP1ARG(arg1) \
    int_t arg1; \
    if (!data_pop(&arg1)) return FALSE;
#define POP2ARG(arg1, arg2) \
    int_t arg1, arg2; \
    if (!data_pop(&arg2)) return FALSE; \
    if (!data_pop(&arg1)) return FALSE;
#define POP1PUSH1(arg, oper) \
    if (data_top < 1) return stack_underflow(); \
    int_t arg = data_stack[data_top-1]; \
    data_stack[data_top-1] = oper(arg); \
    return TRUE;
#define POP2PUSH1(arg1, arg2, oper) \
    if (data_top < 2) return stack_underflow(); \
    int_t arg1 = data_stack[data_top-2]; \
    int_t arg2 = data_stack[data_top-1]; \
    --data_top; \
    data_stack[data_top-1] = oper(arg1, arg2); \
    return TRUE;
#define GET_BLOCK(block) \
    int_t block; \
    if (!next_value(&block)) return FALSE; \
    if (!get_block(block)) return FALSE; \
    block = data_stack[--data_top];

int_t interpret();  // FORWARD DECLARATION
int_t compile();  // FORWARD DECLARATION
int_t get_block(int_t value);  // FORWARD DECLARATION
int_t exec_value(int_t value);  // FORWARD DECLARATION

//PROC_DECL(prim_CREATE) { return error("unimplemented CREATE"); }
//PROC_DECL(prim_SEND) { return error("unimplemented SEND"); }
//PROC_DECL(prim_BECOME) { return error("unimplemented BECOME"); }
//PROC_DECL(prim_SELF) { return error("unimplemented SELF"); }
PROC_DECL(prim_FAIL) { return error("FAIL"); }
//PROC_DECL(prim_STEP) { return error("unimplemented STEP"); }
//PROC_DECL(prim_RUN) { return error("unimplemented RUN"); }
PROC_DECL(prim_Bind) {
    POP1ARG(value);
    int_t word;
    if (!next_value(&word)) return FALSE;
    if (!IS_WORD(word)) return error("WORD required");
    return set_word_value(word, value);
}
PROC_DECL(prim_Literal) {
    int_t word;
    if (!next_value(&word)) return FALSE;
    if (IS_WORD(word)) {
        if (!get_ro_word(&word, word)) return FALSE;
    }
    return data_push(word);
}
PROC_DECL(prim_Lookup) {
    int_t word;
    if (!next_value(&word)) return FALSE;
    if (!IS_WORD(word)) return error("WORD required");
    int_t value;
    if (!get_word_value(&value, word)) return FALSE;
    return data_push(value);
}
PROC_DECL(prim_OpenQuote) { return error("unexpected ["); }
PROC_DECL(prim_CloseQuote) { return error("unexpected ]"); }
PROC_DECL(prim_OpenUnquote) { return error("unexpected ("); }
PROC_DECL(prim_CloseUnquote) { return error("unexpected )"); }
//PROC_DECL(prim_TRUE) { return data_push(TRUE); }
//PROC_DECL(prim_FALSE) { return data_push(FALSE); }
PROC_DECL(prim_IF) {
    POP1ARG(cond);
    DEBUG(debug_value("  prim_IF (cond)", cond));
    GET_BLOCK(block);
    DEBUG(debug_value("  prim_IF (block)", block));
    if (cond) {
        if (!exec_value(block)) return FALSE;
    }
    return TRUE;
}
// [ DUP EQ? IF-ELSE [ DROP ' = . ] [ DUP LT? IF [ ' < . ] GT? IF [ ' > . ] ] ] = CMP  # n CMP --
PROC_DECL(prim_IF_ELSE) {
    POP1ARG(cond);
    DEBUG(debug_value("  prim_IF_ELSE (cond)", cond));
    GET_BLOCK(cnsq);
    DEBUG(debug_value("  prim_IF_ELSE (cnsq)", cnsq));
    GET_BLOCK(altn);
    DEBUG(debug_value("  prim_IF_ELSE (altn)", altn));
    return exec_value(cond ? cnsq : altn);
}
// 5 DUP GT? WHILE [ DUP . 1 SUB DUP GT? ] DROP
PROC_DECL(prim_WHILE) {
    POP1ARG(cond);
    DEBUG(debug_value("  prim_WHILE (cond)", cond));
    GET_BLOCK(block);
    DEBUG(debug_value("  prim_WHILE (block)", block));
    while (cond) {
        if (!exec_value(block)) return FALSE;
        if (data_top < 1) return stack_underflow();
        cond = data_stack[--data_top];  // pop condition from stack
        DEBUG(debug_value("  prim_WHILE (cond...)", cond));
    }
    return TRUE;
}
PROC_DECL(prim_DROP) {
    if (data_top < 1) return stack_underflow();
    --data_top;
    return TRUE;
}
PROC_DECL(prim_DUP) {
    int_t v;
    if (!data_pick(&v, 1)) return FALSE;
    return data_push(v);
}
PROC_DECL(prim_SWAP) {
    POP2ARG(v_2, v_1);
    if (!data_push(v_1)) return FALSE;
    return data_push(v_2);
}
PROC_DECL(prim_PICK) {
    int_t v_n;
    POP1ARG(n);
    if (!data_pick(&v_n, TO_INT(n))) return FALSE;
    return data_push(v_n);
}
PROC_DECL(prim_ROLL) { POP1ARG(n); return data_roll(TO_INT(n)); }
PROC_DECL(prim_DEPTH) { return data_push(MK_NUM(data_top)); }
// [ DEPTH GT? WHILE [ DROP DEPTH GT? ] ] = CLEAR  # clear the stack
//PROC_DECL(prim_INF) { return data_push(INF); }
PROC_DECL(prim_NEG) { POP1PUSH1(n, NEG); }
// [ DUP LT? IF [ NEG ] ] = ABS  # absolute value
PROC_DECL(prim_ADD) { POP2PUSH1(n, m, ADD); }
PROC_DECL(prim_SUB) { POP2PUSH1(n, m, SUB); }
PROC_DECL(prim_MUL) { POP2PUSH1(n, m, MUL); }
PROC_DECL(prim_DIVMOD) {  // n = (m * q) + r
    POP2ARG(n, m);
    n = TO_INT(n);
    m = TO_INT(m);
    int_t i = TO_INT(INF);
    int_t q = i;
    int_t r = n;
    if ((n == i) && (m == -1)) {
        q = i;
        r = 0;
    } else if (m != 0) {
        q = n / m;
        r = n % m;
        // FIXME: map to Euclidean Division
        // -7 3 DIVMOD -- 2 -3  # now: -1 -2
        // -7 -3 DIVMOD -- 2 3  # now: -1 2
        // [https://en.wikipedia.org/wiki/Modulo_operation]
    }
    q = MK_NUM(q);
    r = MK_NUM(r);
    if (!data_push(r)) return FALSE;
    return data_push(q);
}
// n m DIVMOD m FMA -- n  # Check Euclidean Division
PROC_DECL(prim_FMA) {
    POP1ARG(a);
    POP1ARG(b);
    POP1ARG(c);
    int_t x = TO_INT(a) * TO_INT(b) + TO_INT(c);
    return data_push(MK_NUM(x));
}
PROC_DECL(prim_CMP) { POP2PUSH1(n, m, CMP); }
PROC_DECL(prim_LTZ) { POP1PUSH1(n, LTZ); }
PROC_DECL(prim_EQZ) { POP1PUSH1(n, EQZ); }
PROC_DECL(prim_GTZ) { POP1PUSH1(n, GTZ); }
PROC_DECL(prim_NOT) { POP1PUSH1(n, NOT); }
PROC_DECL(prim_AND) { POP2PUSH1(n, m, AND); }
PROC_DECL(prim_IOR) { POP2PUSH1(n, m, IOR); }
PROC_DECL(prim_XOR) { POP2PUSH1(n, m, XOR); }
PROC_DECL(prim_LSL) { POP2PUSH1(n, m, LSL); }
PROC_DECL(prim_LSR) { POP2PUSH1(n, m, LSR); }
PROC_DECL(prim_ASR) { POP2PUSH1(n, m, ASR); }
// direct memory access
#if ALLOW_DMA
PROC_DECL(prim_Load) {
    POP1ARG(addr);
    int_t *ptr = TO_PTR(TO_INT(addr));
    return data_push(MK_NUM(*ptr));
}
PROC_DECL(prim_Store) {
    POP2ARG(value, addr);
    int_t *ptr = TO_PTR(TO_INT(addr));
    *ptr = TO_INT(value);
    return TRUE;
}
PROC_DECL(prim_LoadAtomic) { return prim_Load(self); }
PROC_DECL(prim_StoreAtomic) { return prim_Store(self); }
PROC_DECL(prim_DUMP) {
    POP2ARG(addr, cnt);
    hexdump("hexdump", TO_PTR(TO_INT(addr)), TO_NAT(cnt));
    return TRUE;
}
#endif
// interactive extentions
PROC_DECL(prim_WORDS) {
    size_t i;
    printf("ro:");
    for (i = 0; i < ro_words; ++i) {
        print_ascii(' ');
        print_value(MK_WORD(&word_list[i]));
    }
    print_ascii('\n');
    if (ro_words < rw_words) {
        printf("rw:");
        for (i = ro_words; i < rw_words; ++i) {
            print_ascii(' ');
            print_value(MK_WORD(&word_list[i]));
        }
        print_ascii('\n');
    }
    fflush(stdout);
    return TRUE;
}
PROC_DECL(prim_EMIT) {
    POP1ARG(code);
    print_ascii(TO_INT(code));
    return TRUE;
}
PROC_DECL(prim_PrintStack) {
    print_stack();
    fflush(stdout);
    return TRUE;
}
PROC_DECL(prim_PrintDebug) {
    POP1ARG(value);
    print_value(value);
    fflush(stdout);
    debug_value(" ", value);
    return TRUE;
}
PROC_DECL(prim_Print) {
    POP1ARG(value);
    print_value(value);
    print_ascii('\n');
    fflush(stdout);
    return TRUE;
}

/*
 * block storage
 */

#define MAX_BLOCK_MEM (VMEM_PAGE_SZ / sizeof(int_t))
int_t block_mem[MAX_BLOCK_MEM];
size_t block_next = 0;

void print_block(nat_t len, int_t *data) {
    print_ascii('[');
    print_ascii(' ');
    for (nat_t n = 0; n < len; ++n) {
        print_value(data[n]);
        print_ascii(' ');
    }
    print_ascii(']');
}

void print_env(env_t *env) {
    print_ascii('{');
    while (env) {
        word_t *w = TO_PTR(env->word);
        printf("%s:", w->name);
        print_value(env->value);
        env = env->env;  // follow environment chain
        if (env) print_ascii(',');
    }
    print_ascii('}');
}

void print_closure(closure_t *scope) {
    print_env(scope->env);
    print_block(scope->cnt, scope->ptr);
}

static void debug_env(env_t *env) {
    while (env) {
        word_t *w = TO_PTR(env->word);
        fprintf(stderr, "    ");
        debug_value(w->name, env->value);
        env = env->env;  // follow environment chain
    }
}

static void debug_closure(char *label, int_t block) {
    closure_t *scope = TO_PTR(block);
    fprintf(stderr, "%s:", label);
    fprintf(stderr, " proc=%p", TO_PTR(scope->proc));
    fprintf(stderr, " cnt=%"PRIuPTR"", scope->cnt);
    fprintf(stderr, " ptr=%p", TO_PTR(scope->ptr));
    fprintf(stderr, " env=%p", scope->env);
    fprintf(stderr, "\n");
    debug_env(scope->env);
    fflush(stderr);
    if (scope->cnt && scope->ptr) {
        printf("    ");
        print_block(scope->cnt, scope->ptr);
        print_ascii('\n');
        fflush(stdout);
    }
}

nat_t quote_depth = 0;  // count nested quoting levels

int_t exec_block(nat_t cnt, int_t *ptr, env_t *env) {
    // save current value source
    context_t *prev_context = next_context;

    // use block as value source
    context_t scope_context = {
        .proc = MK_PROC(prim_Context),
        .cnt = cnt,
        .ptr = ptr,
        .env = env
    };
    next_context = &scope_context;

    // run nested interpreter, reading from block
    ++quote_depth;
    int_t ok = interpret();  // FIXME: consider an exit_on_fail parameter?
    --quote_depth;

    // restore previous value source
    next_context = prev_context;
    return ok;
}

PROC_DECL(prim_Block) {
    block_t *blk = TO_PTR(self);
    if (blk->proc != MK_PROC(prim_Block)) return panic("not a Block");
    DEBUG(debug_value("  prim_Block", self));
    DEBUG(printf("    "); print_block(blk->len, blk->data); print_ascii('\n'); fflush(stdout));
    return exec_block(blk->len, blk->data, PTR(0));
}

// allocate _cnt_ consecutive `int_t` slots
int_t new_block(int_t *block_out, nat_t cnt) {
    size_t next = block_next + cnt;
    if (next > MAX_BLOCK_MEM) {
        return panic("out of block memory");
    }
    block_t *blk = PTR(&block_mem[block_next]);
    blk->proc = MK_PROC(prim_Block);
    blk->len = cnt - 2;
    block_next = next;
    *block_out = MK_BLOCK(blk);
    DEBUG(debug_value("  new_block", *block_out));
    return TRUE;
}

int_t make_block(int_t *block_out, int_t *base, nat_t len) {
    if (!new_block(block_out, len + 2)) return FALSE;
    block_t *blk = TO_PTR(*block_out);
    //blk->len = len;  // this is done in new_block()...
    while (len-- > 0) {
        blk->data[len] = base[len];
    }
    DEBUG(debug_value("  make_block", *block_out));
    return TRUE;
}

PROC_DECL(prim_Closure) {
    closure_t *scope = TO_PTR(self);
    if (scope->proc != MK_PROC(prim_Closure)) return panic("not a Closure");
    DEBUG(debug_closure("  prim_Closure", self));
    return exec_block(scope->cnt, scope->ptr, scope->env);
}

// create a new scope for capturing variables
int_t new_scope(int_t *block_out) {
    if (!new_block(block_out, 4)) return FALSE;
    closure_t *scope = TO_PTR(*block_out);
    scope->proc = MK_PROC(prim_Closure);
    scope->cnt = NAT(0);
    scope->ptr = PTR(0);
    scope->env = PTR(0);
    return TRUE;
}

// wrap _block_ in a _closure_ to capture current environment
int_t make_closure(int_t *block_out, int_t block) {
    block_t *blk = TO_PTR(block);
    // FIXME: if !next_context, we _could_ just return the bare block...
    if (blk->proc == MK_PROC(prim_Block)) {
        int_t closure;
        if (!new_scope(&closure)) return panic("scope allocation failed");
        closure_t *scope = TO_PTR(closure);
        scope->cnt = blk->len;
        scope->ptr = blk->data;
        if (next_context) {
            scope->env = next_context->env;
        }
        block = closure;
    }
    *block_out = block;
    DEBUG(debug_value("  make_closure", *block_out));
    return TRUE;
}

/*
 * word interpreter/compiler
 */

int_t exec_value(int_t value) {
    DEBUG(debug_value("  exec_value (value)", value));
    if (IS_WORD(value)) {
        if (!get_word_value(&value, value)) return FALSE;
        DEBUG(debug_value("  exec_value (def)", value));
    }
    // execute value
    if (IS_BLOCK(value)) {
        block_t *blk = TO_PTR(value);
        PROC_DECL((*proc)) = TO_PTR(blk->proc);
        return (*proc)(value);
    }
    if (IS_PROC(value)) {
        PROC_DECL((*proc)) = TO_PTR(value);
        return (*proc)(value);
    }
    // push value on stack
    return data_push(value);
}

int_t get_block(int_t value) {
    if (IS_BLOCK(value)) {
        return data_push(value);
    }
    if (IS_WORD(value)) {
        word_t *w = TO_PTR(value);
        if (strcmp(w->name, "[") == 0) {
            // compile quoted block
            ++quote_depth;
            if (!compile()) return panic("block quoting failed");
            --quote_depth;
            return TRUE;
        }
    }
    return FALSE;
}

int_t interpret() {
    int_t value;
    DEBUG(fprintf(stderr, "> interpret data_top=%zu\n", data_top));
    size_t exec_top = data_top;  // save stack pointer for error recovery
    if (next_context) {
        DEBUG(fprintf(stderr, "  interpret cnt=%"PRIuPTR" ptr=%p env=%p\n",
            next_context->cnt, next_context->ptr, next_context->env));
    }
    while (next_value(&value)) {
        if (get_block(value)) {
            if (!data_pop(&value)) return FALSE;  // pop block from stack
            if (!make_closure(&value, value)) return FALSE;
            if (!data_push(value)) return FALSE;  // push block on stack
            DEBUG(debug_value("  interpret (block)", value));
            continue;
        }
        if (IS_WORD(value)) {
            word_t *w = TO_PTR(value);
            if ((quote_depth > 0) && (strcmp(w->name, ")") == 0)) {
                break;  // end of unquote...
            }
        }
        if (!exec_value(value)) {
            DEBUG(fprintf(stderr, "  interpret FAIL! data_top=%zu\n", data_top));
            data_top = exec_top;  // restore stack on failure
            if (quote_depth > 0) {
                DEBUG(fprintf(stderr, "< interpret exec FAIL! data_top=%zu\n", data_top));
                return FALSE;
            }
        }
    }
    DEBUG(fprintf(stderr, "< interpret ok data_top=%zu\n", data_top));
    return TRUE;
}

int_t quote_value(int_t value) {
    DEBUG(debug_value("  quote_value (value)", value));
    if (IS_WORD(value)) {
        if (!get_ro_word(&value, value)) return FALSE;
        DEBUG(debug_value("  quote_value (word)", value));
    }
    // push value on stack
    return data_push(value);
}

int_t compile() {
    int_t value;
    DEBUG(fprintf(stderr, "> compile data_top=%zu\n", data_top));
    size_t quote_top = data_top;  // save stack pointer for error recovery
    if (next_context) {
        DEBUG(fprintf(stderr, "  compile cnt=%"PRIuPTR" ptr=%p env=%p\n",
            next_context->cnt, next_context->ptr, next_context->env));
    }
    while (next_value(&value)) {
        DEBUG(debug_value("  compile (next)", value));
        if (get_block(value)) {
            continue;  // nested block
        }
        if (IS_WORD(value)) {
            word_t *w = TO_PTR(value);
            if (strcmp(w->name, "]") == 0) {
                break;  // end of quote...
            }
            if (strcmp(w->name, "(") == 0) {
                // interpret unquoted block
                if (!interpret()) return panic("unquoted interpret failed");
                if (data_top < quote_top) return stack_underflow();
                continue;
            }
        }
        if (!quote_value(value)) {
            data_top = quote_top;  // restore stack on failure
            DEBUG(fprintf(stderr, "< compile quote FAIL! data_top=%zu\n", data_top));
            return FALSE;
        }
        DEBUG(debug_value("  compile (value)", value));
    }
    if (data_top < quote_top) return stack_underflow();
    // construct block value from stack contents
    int_t *base = &data_stack[quote_top];
    nat_t len = (data_top - quote_top);
    data_top = quote_top;  // restore stack top
    int_t block;
    if (!make_block(&block, base, len)) {
        DEBUG(fprintf(stderr, "< compile block FAIL! data_top=%zu\n", data_top));
        return FALSE;
    }
    DEBUG(debug_value("  compile", block));
    DEBUG(fprintf(stderr, "< compile ok data_top=%zu\n", data_top));
    return data_push(block);
}

/*
 * actor runtime
 */

#define MAX_MSG_RING (VMEM_PAGE_SZ / sizeof(int_t))
#define MASK_MSG_RING (MAX_MSG_RING - 1)
int_t msg_ring[MAX_MSG_RING];  // ring buffer for messages in transit
int_t msg_head = 0;
int_t msg_tail = 0;

int_t msg_put(int_t value) {
    msg_ring[msg_tail++] = value;
    msg_tail &= MASK_MSG_RING;  // wrap-around
    if (msg_head == msg_tail) return error("message buffer overflow");
    return TRUE;
}

int_t msg_take(int_t *value_out) {
    if (msg_head == msg_tail) return error("message buffer underflow");
    *value_out = msg_ring[msg_head++];
    msg_head &= MASK_MSG_RING;  // wrap-around
    return TRUE;
}

// move stack contents to message queue
int_t msg_enqueue() {
    // store message length (note: natural, not tagged)
    if (!msg_put(INT(data_top))) return FALSE;
    // copy from stack to message
    for (nat_t n = 0; n < data_top; ++n) {
        if (!msg_put(data_stack[n])) return FALSE;
    }
    // clear stack
    data_top = 0;
    // return success
    return TRUE;
}

// move message contents to stack
int_t msg_dequeue() {
    int_t value;
    // load message length (note: natural, not tagged)
    if (!msg_take(&value)) return FALSE;
    nat_t len = NAT(value);
    // transfer message to stack
    while (len--) {
        if (!msg_take(&value)) return FALSE;
        if (!data_push(value)) return FALSE;
    }
    // return success
    return TRUE;
}

void print_actor(actor_t *act) {
    printf("^%p", act);
}

static void debug_actor(char *label, int_t block) {
    actor_t *act = TO_PTR(block);
    fprintf(stderr, "%s:", label);
    fprintf(stderr, " self=%p", act);
    fprintf(stderr, " beh=%"PRIXPTR"", act->beh);
    fflush(stderr);
    print_ascii(' ');
    print_value(act->beh);
    fflush(stdout);
    //fprintf(stderr, " ptr=%p", TO_PTR(scope->ptr));
    //fprintf(stderr, " env=%p", scope->env);
    fprintf(stderr, "\n");
    fflush(stderr);
}

static int_t actor_self = UNDEFINED;
int_t exec_actor(actor_t *act) {
    DEBUG(fprintf(stderr, "> exec_actor self=%p\n", act));
    if (actor_self != UNDEFINED) return error("nested actor invocation");
    actor_self = MK_BLOCK(act);
    DEBUG(debug_actor("  exec_actor (self)", actor_self));
    int_t org_beh = act->beh;  // save actor behavior

    // create recovery snapshot
    DEBUG(printf("  exec_actor (stack): "); print_stack(); print_ascii('\n'); fflush(stdout));
    DEBUG(fprintf(stderr, "  exec_actor (msg_head): %"PRIdPTR"\n", msg_head));
    DEBUG(fprintf(stderr, "  exec_actor (msg_tail): %"PRIdPTR"\n", msg_tail));
    DEBUG(fprintf(stderr, "  exec_actor (block_next): %zu\n", block_next));
    int_t org_tail = msg_tail;  // save message queue tail position
    size_t org_next = block_next;  // save block allocation offset

    // execute actor behavior
    int_t ok = exec_value(org_beh);

    if (!ok) {
        // restore recovery snapshot
        XDEBUG(fprintf(stderr, "  exec_actor restore recovery snapshot...\n"));
        act->beh = org_beh;  // restore actor behavior
        msg_tail = org_tail;  // restore message queue tail position
        block_next = org_next;  // restore block allocation offset
    }
    DEBUG(fprintf(stderr, "  exec_actor (msg_head'): %"PRIdPTR"\n", msg_head));
    DEBUG(fprintf(stderr, "  exec_actor (msg_tail'): %"PRIdPTR"\n", msg_tail));
    DEBUG(fprintf(stderr, "  exec_actor (block_next'): %zu\n", block_next));
    data_top = 0;  // clear the stack
    actor_self = UNDEFINED;
    DEBUG(fprintf(stderr, "< exec_actor ok=%"PRIdPTR"\n", TO_INT(ok)));
    return ok;
}

PROC_DECL(prim_SELF) { return data_push(actor_self); }

PROC_DECL(prim_Actor) {
    DEBUG(debug_value("  prim_Actor", self));
    return data_push(self);
}

// create a new actor with _behavior_
int_t new_actor(int_t *actor_out, int_t behavior) {
    if (!new_block(actor_out, 2)) return FALSE;
    actor_t *act = TO_PTR(*actor_out);
    act->proc = MK_PROC(prim_Actor);
    act->beh = behavior;
    DEBUG(debug_actor("  new_actor", *actor_out));
    return TRUE;
}

PROC_DECL(prim_CREATE) {
    POP1ARG(beh);
    int_t actor;
    if (!new_actor(&actor, beh)) return FALSE;
    return data_push(actor);
}
PROC_DECL(prim_BECOME) {
    POP1ARG(beh);
    if (actor_self == UNDEFINED) return error("unexpected BECOME");
    actor_t *act = TO_PTR(actor_self);
    act->beh = beh;
    return TRUE;
}

int_t msg_send(int_t target) {
    if (!IS_ACTOR(target)) return error("SEND to non-Actor");
    int_t org_tail = msg_tail;  // recovery snapshot
    if (msg_put(target)
    &&  msg_enqueue()) {
        return TRUE;
    }
    msg_tail = org_tail;  // restore snapshot on failure
    return FALSE;
}

PROC_DECL(prim_SEND) {
    POP1ARG(target);
    return msg_send(target);
}

int_t msg_dispatch() {
    data_top = 0;  // clear the stack
    if (msg_head == msg_tail) return error("empty message queue");
    int_t org_head = msg_head;  // recovery snapshot
    int_t target;
    if (msg_take(&target)
    &&  msg_dequeue()) {
        return exec_actor(TO_PTR(target));
    }
    msg_head = org_head;  // restore snapshot on failure
    return FALSE;
}

PROC_DECL(prim_STEP) {
    int_t ok = msg_dispatch();
    return data_push(ok);
}
PROC_DECL(prim_RUN) {
    while (msg_head != msg_tail) {
        int_t ok = msg_dispatch();  // ignore failures...
    }
    return TRUE;
}

/*
 * automated tests
 */

void print_platform_info() {
    printf("-- platform info --\n");
    printf("sizeof(char)=%d\n", (int)sizeof(char));
    printf("sizeof(short)=%d\n", (int)sizeof(short));
    printf("sizeof(int)=%d\n", (int)sizeof(int));
    printf("sizeof(long)=%d\n", (int)sizeof(long));
    printf("sizeof(size_t)=%d\n", (int)sizeof(size_t));
    printf("sizeof(ptrdiff_t)=%d\n", (int)sizeof(ptrdiff_t));
    printf("sizeof(intptr_t)=%d\n", (int)sizeof(intptr_t));
    printf("sizeof(uintptr_t)=%d\n", (int)sizeof(uintptr_t));
    printf("sizeof(uint8_t)=%d\n", (int)sizeof(uint8_t));
    printf("sizeof(uint16_t)=%d\n", (int)sizeof(uint16_t));
    printf("sizeof(uint32_t)=%d\n", (int)sizeof(uint32_t));
    printf("sizeof(uint64_t)=%d\n", (int)sizeof(uint64_t));
    printf("sizeof(int_t)=%d\n", (int)sizeof(int_t));
    printf("sizeof(nat_t)=%d\n", (int)sizeof(nat_t));
}

void smoke_test() {
    printf("-- smoke test --\n");
    debug_value("TRUE", TRUE);
    debug_value("FALSE", FALSE);

    int_t pos = MK_NUM(1);
    int_t zero = MK_NUM(0);
    int_t neg = MK_NUM(-1);
    debug_value("pos", pos);
    debug_value("zero", zero);
    debug_value("neg", neg);

    debug_value("pos NEG", NEG(pos));
    debug_value("neg NEG", NEG(neg));
    debug_value("neg 1 LSL", LSL(neg, pos));
    debug_value("neg 1 LSR", LSR(neg, pos));
    debug_value("neg 1 ASR", ASR(neg, pos));
    debug_value("neg 1 LSR 1 LSL", LSL(LSR(neg, pos), pos));
    debug_value("neg 1 LSR 1 LSL 1 ASR", ASR(LSL(LSR(neg, pos), pos), pos));
    debug_value("neg 1 LSR NOT", NOT(LSR(neg, pos)));
    debug_value("neg 1 LSL NOT", NOT(LSL(neg, pos)));

    printf("pos(x) LTZ = %"PRIdPTR" EQZ = %"PRIdPTR" GTZ = %"PRIdPTR"\n",
        LTZ(pos), EQZ(pos), GTZ(pos));
    printf("zero(x) LTZ = %"PRIdPTR" EQZ = %"PRIdPTR" GTZ = %"PRIdPTR"\n",
        LTZ(zero), EQZ(zero), GTZ(zero));
    printf("neg(x) LTZ = %"PRIdPTR" EQZ = %"PRIdPTR" GTZ = %"PRIdPTR"\n",
        LTZ(neg), EQZ(neg), GTZ(neg));

    printf("word_list[%zu].name = \"%s\"\n",
        ro_words-1, word_list[ro_words-1].name);
    printf("word_list[%zu].name = \"%s\"\n",
        MAX_WORDS-1, word_list[MAX_WORDS-1].name);

    char *token;
    int_t word;
    int_t num;
    int_t ok;
    token = "0";
    ok = token_to_number(&num, token);
    printf("ok=%"PRIdPTR" token=\"%s\" d=%"PRIdPTR" u=%"PRIuPTR" x=%"PRIXPTR"\n",
        ok, token, num, num, num);
    token = "-1";
    ok = token_to_number(&num, token);
    printf("ok=%"PRIdPTR" token=\"%s\" d=%"PRIdPTR" u=%"PRIuPTR" x=%"PRIXPTR"\n",
        ok, token, num, num, num);
    token = "0123456789";
    ok = token_to_number(&num, token);
    printf("ok=%"PRIdPTR" token=\"%s\" d=%"PRIdPTR" u=%"PRIuPTR" x=%"PRIXPTR"\n",
        ok, token, num, num, num);
    token = "16#0123456789ABCdef";
    ok = token_to_number(&num, token);
    printf("ok=%"PRIdPTR" token=\"%s\" d=%"PRIdPTR" u=%"PRIuPTR" x=%"PRIXPTR"\n",
        ok, token, num, num, num);
    token = "8#0123456789abcDEF";
    ok = token_to_number(&num, token);
    printf("ok=%"PRIdPTR" token=\"%s\" d=%"PRIdPTR" u=%"PRIuPTR" x=%"PRIXPTR"\n",
        ok, token, num, num, num);
    token = "8#01234567";
    ok = token_to_number(&num, token);
    printf("ok=%"PRIdPTR" token=\"%s\" d=%"PRIdPTR" u=%"PRIuPTR" x=%"PRIXPTR" o=%lo\n",
        ok, token, num, num, num, (unsigned long)num);
    token = "-10#2";
    ok = token_to_number(&num, token);
    printf("ok=%"PRIdPTR" token=\"%s\" d=%"PRIdPTR" u=%"PRIuPTR" x=%"PRIXPTR"\n",
        ok, token, num, num, num);
    token = "2#10";
    ok = token_to_number(&num, token);
    printf("ok=%"PRIdPTR" token=\"%s\" d=%"PRIdPTR" u=%"PRIuPTR" x=%"PRIXPTR"\n",
        ok, token, num, num, num);
    token = "";
    ok = token_to_number(&num, token);
    printf("ok=%"PRIdPTR" token=\"%s\" d=%"PRIdPTR" u=%"PRIuPTR" x=%"PRIXPTR"\n",
        ok, token, num, num, num);
    token = "#";
    ok = token_to_number(&num, token);
    printf("ok=%"PRIdPTR" token=\"%s\" d=%"PRIdPTR" u=%"PRIuPTR" x=%"PRIXPTR"\n",
        ok, token, num, num, num);
    token = "#1";
    ok = token_to_number(&num, token);
    printf("ok=%"PRIdPTR" token=\"%s\" d=%"PRIdPTR" u=%"PRIuPTR" x=%"PRIXPTR"\n",
        ok, token, num, num, num);
    token = "1#";
    ok = token_to_number(&num, token);
    printf("ok=%"PRIdPTR" token=\"%s\" d=%"PRIdPTR" u=%"PRIuPTR" x=%"PRIXPTR"\n",
        ok, token, num, num, num);
    token = "2#";
    ok = token_to_number(&num, token);
    printf("ok=%"PRIdPTR" token=\"%s\" d=%"PRIdPTR" u=%"PRIuPTR" x=%"PRIXPTR"\n",
        ok, token, num, num, num);
    token = "-16#F";
    ok = token_to_number(&num, token);
    printf("ok=%"PRIdPTR" token=\"%s\" d=%"PRIdPTR" u=%"PRIuPTR" x=%"PRIXPTR"\n",
        ok, token, num, num, num);
    token = "2#1000_0000";
    ok = token_to_number(&num, token);
    printf("ok=%"PRIdPTR" token=\"%s\" d=%"PRIdPTR" u=%"PRIuPTR" x=%"PRIXPTR"\n",
        ok, token, num, num, num);
    token = "36#xyzzy";
    ok = token_to_number(&num, token);
    printf("ok=%"PRIdPTR" token=\"%s\" d=%"PRIdPTR" u=%"PRIuPTR" x=%"PRIXPTR"\n",
        ok, token, num, num, num);
}

int main(int argc, char const *argv[])
{
    //print_platform_info();
    //smoke_test();

#if 1
    printf("-- procedures --\n");
    debug_value("    panic", MK_PROC(panic));
    debug_value("Undefined", MK_PROC(prim_Undefined));
    debug_value("    Block", MK_PROC(prim_Block));
    debug_value("  Closure", MK_PROC(prim_Closure));
    debug_value("    Actor", MK_PROC(prim_Actor));
    debug_value("   CREATE", MK_PROC(prim_CREATE));
    debug_value("     Bind", MK_PROC(prim_Bind));
    debug_value("      SUB", MK_PROC(prim_SUB));
    debug_value("      CMP", MK_PROC(prim_CMP));
    debug_value("    Print", MK_PROC(prim_Print));
    debug_value("     main", MK_PROC(main));
#endif
#if 1
    printf("-- data structures --\n");
    printf(" word_list = %p\n", PTR(word_list));
    printf("data_stack = %p\n", PTR(data_stack));
    printf(" block_mem = %p\n", PTR(block_mem));
    printf("  msg_ring = %p\n", PTR(msg_ring));
#endif
#if 0
    // ensure that function pointer range checks will work...
    if (!(NAT(panic) < NAT(main))) {
        return panic("expected panic() < main()");
    }
    if (!(INT(prim_CREATE) < INT(prim_Print))) {
        return panic("expected panic() < main()");
    }
#endif

    printf("-- interpreter --\n");
    return (interpret() ? 0 : 1);
}
