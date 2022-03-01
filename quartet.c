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

#define ALLOW_DMA 0  // define words for direct memory access

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
#define INF INT(~(NAT(-1)>>1))

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
    nat_t       len;                    // number of int_t in data[]
    int_t       data[];                 // addressable memory
} block_t;

#define MAX_NAME_SZ (4 * sizeof(int_t))         // word buffer size
typedef struct thunk {
    //PROC_DECL((*proc));                 // executable code pointer
    int_t       value;                  // bound value
    int_t       var[3];                 // bound variables
    char        name[MAX_NAME_SZ];      // NUL-terminated string
} thunk_t;

#define CACHE_LINE_SZ (sizeof(thunk_t))  // bytes per idealized cache line
#define VMEM_PAGE_SZ ((size_t)1 << 12)  // bytes per idealized memory page

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

void print_ascii(int_t code) {
    if ((code & 0x7F) == code) {  // code is in ascii range
        putchar(code);
    }
}

void print_value(int_t value) {
    if (IS_NUM(value)) {
        value = TO_INT(value);
        if (value == INF) {
            printf("INF");
        } else {
            printf("%"PRIdPTR, value);
        }
    } else if (IS_WORD(value)) {
        thunk_t *w = TO_PTR(value);
        printf("%s", w->name);
    } else if (IS_BLOCK(value)) {
        block_t *blk = TO_PTR(value);
        print_ascii('[');
        print_ascii(' ');
        for (size_t i = 0; i < blk->len; ++i) {
            print_value(blk->data[i]);
            print_ascii(' ');
        }
        print_ascii(']');
    } else {
        printf("%p", TO_PTR(value));
    }
    //print_ascii(' ');
    //print_ascii('\n');
    fflush(stdout);
}

static char *tag_label[] = { "NUM", "WORD", "BLOCK", "PROC" };
static void print_detail(char *label, int_t value) {
    fprintf(stderr, "%s:", label);
    fprintf(stderr, " %"PRIXPTR"", value);
    fprintf(stderr, " t=%s", tag_label[value & TAG_MASK]);
    fprintf(stderr, " i=%"PRIdPTR"", TO_INT(value));
    //fprintf(stderr, " n=%"PRIuPTR"", TO_NAT(value));
    fprintf(stderr, " p=%p", TO_PTR(value));
    if (IS_WORD(value)) {
        thunk_t *w = TO_PTR(value);
        fprintf(stderr, " s=\"%s\"", w->name);
        //fprintf(stderr, " v=%"PRIXPTR"", w->value);
    }
    if (IS_BLOCK(value)) {
        block_t *blk = TO_PTR(value);
        fprintf(stderr, " [%"PRIuPTR"]", blk->len);
    }
    fprintf(stderr, "\n");
    fflush(stderr);
}

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
    if (data_top >= MAX_STACK) {
        return stack_overflow();
    }
    data_stack[data_top++] = value;
    return TRUE;
}

int_t data_pop(int_t *value_out) {
    if (data_top == 0) {
        return stack_underflow();
    }
    *value_out = data_stack[--data_top];
    return TRUE;
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
 * block storage
 */

#define MAX_BLOCK_MEM (VMEM_PAGE_SZ / sizeof(int_t))
int_t block_mem[MAX_BLOCK_MEM];
size_t block_next = 0;

int_t make_block(int_t *block_out, int_t *base, size_t len) {
    size_t next = block_next + len + 1;
    if (next > MAX_BLOCK_MEM) {
        return panic("out of block memory");
    }
    block_t *blk = PTR(&block_mem[block_next]);
    blk->len = NAT(len);
    while (len-- > 0) {
        blk->data[len] = base[len];
    }
    block_next = next;
    *block_out = MK_BLOCK(blk);
    XDEBUG(print_detail("  make_block", *block_out));
    return TRUE;
}

/*
 * word dictionary
 */

PROC_DECL(prim_Undefined);
#define UNDEFINED MK_PROC(prim_Undefined)

PROC_DECL(prim_CREATE);
PROC_DECL(prim_SEND);
PROC_DECL(prim_BECOME);
PROC_DECL(prim_SELF);
PROC_DECL(prim_FAIL);
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
#endif
PROC_DECL(prim_WORDS);
PROC_DECL(prim_EMIT);
PROC_DECL(prim_PrintStack);
PROC_DECL(prim_PrintDetail);
PROC_DECL(prim_Print);

#define MAX_WORDS ((size_t)(128))
thunk_t word_list[MAX_WORDS] = {
    { .value = MK_PROC(prim_CREATE), .name = "CREATE" },
    { .value = MK_PROC(prim_SEND), .name = "SEND" },
    { .value = MK_PROC(prim_BECOME), .name = "BECOME" },
    { .value = MK_PROC(prim_SELF), .name = "SELF" },
    { .value = MK_PROC(prim_FAIL), .name = "FAIL" },
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
    { .value = MK_NUM(INF), .name = "INF" },
    { .value = MK_PROC(prim_NEG), .name = "NEG" },
    { .value = MK_PROC(prim_ADD), .name = "ADD" },
    { .value = MK_PROC(prim_SUB), .name = "SUB" },
    { .value = MK_PROC(prim_MUL), .name = "MUL" },
    { .value = MK_PROC(prim_DIVMOD), .name = "DIVMOD" },
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
#endif
    { .value = MK_PROC(prim_WORDS), .name = "WORDS" },
    { .value = MK_PROC(prim_EMIT), .name = "EMIT" },
    { .value = MK_PROC(prim_PrintStack), .name = "..." },
    { .value = MK_PROC(prim_PrintDetail), .name = ".?" },
    { .value = MK_PROC(prim_Print), .name = "." },
    { .value = UNDEFINED, .name = "" }
};
#if ALLOW_DMA
size_t ro_words = 50;  // limit of read-only words
size_t rw_words = 50;  // limit of read/write words
#else
size_t ro_words = 46;  // limit of read-only words
size_t rw_words = 46;  // limit of read/write words
#endif

static void print_thunk(char *label, thunk_t *w) {
    fprintf(stderr, "%s:", label);
    fprintf(stderr, " %p", w);
    fprintf(stderr, " value=%"PRIXPTR"", w->value);
    fprintf(stderr, " var=[ %"PRIdPTR" %"PRIdPTR" %"PRIdPTR" ]",
        w->var[0], w->var[1], w->var[2]);
    fprintf(stderr, " s=\"%s\"", w->name);
    fprintf(stderr, "\n");
    fflush(stderr);
}

#if 0
/* this code is an example a range check implemented by a single comparison */
int_t is_word(int_t value) {
//    if (NAT(value - INT(word_list)) < sizeof(word_list)) {
    if (NAT(value - INT(word_list)) <= (rw_words * sizeof(thunk_t))) {
        return TRUE;
    }
    return FALSE;
}
#endif

int_t parse_value(int_t *value_out) {
    // read token into next available word buffer
    thunk_t *w = &word_list[rw_words];
    w->value = UNDEFINED;
    char *word_buf = w->name;
    if (!read_token(word_buf, MAX_NAME_SZ)) return FALSE;

    // attempt to parse token as a number
    int_t num = INF;
    if (token_to_number(&num, word_buf)) {
        *value_out = MK_NUM(num);
    } else {
        *value_out = MK_WORD(w);
    }
    DEBUG(print_detail("  parse_value", *value_out));
    return TRUE;
}

int_t *next_value_ptr = PTR(0);
nat_t next_value_cnt = NAT(0);
int_t next_value(int_t *value_out) {
    if (next_value_ptr) {
        DEBUG(fprintf(stderr, "  next_value cnt=%"PRIuPTR" ptr=%p\n", next_value_cnt, next_value_ptr));
        // read from block data
        if (next_value_cnt) {
            --next_value_cnt;
            *value_out = *next_value_ptr++;
            DEBUG(print_detail("  next_value", *value_out));
            return TRUE;
        }
        next_value_ptr = PTR(0);
        return FALSE;  // no more words (in block)
    }
    // read from input stream
    return parse_value(value_out);
}

// convert latest token into new word
int_t create_word(int_t *word_out, int_t word) {
    thunk_t *w = TO_PTR(word);
    if (rw_words >= MAX_WORDS) return panic("too many words");
    if (w != &word_list[rw_words]) return panic("must create from latest token");
    ++rw_words;  // extend r/w dictionary
    word = MK_WORD(w);
    DEBUG(print_thunk("  create_word", TO_PTR(word)));
    *word_out = word;
    return TRUE;
}

// lookup word in entire dictionary, fail if not found.
int_t find_ro_word(int_t *word_out, int_t word) {
    thunk_t *w = TO_PTR(word);
    for (size_t n = rw_words; (n-- > 0); ) {  // search from _end_ of dictionary
        thunk_t *m = &word_list[n];
        if (strcmp(w->name, m->name) == 0) {
            word = MK_WORD(m);
            DEBUG(print_thunk("  ro_word", TO_PTR(word)));
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
    thunk_t *w = TO_PTR(word);
    for (size_t n = rw_words; (n-- > ro_words); ) {  // search from _end_ of dictionary
        thunk_t *m = &word_list[n];
        if (strcmp(w->name, m->name) == 0) {
            word = MK_WORD(m);
            DEBUG(print_thunk("  rw_word", TO_PTR(word)));
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

static int_t undefined_word(int_t word) {
    // error on undefined word
    print_value(word);
    fflush(stdout);
    return error("undefined word");
}

int_t interpret();  // FORWARD DECLARATION
int_t compile();  // FORWARD DECLARATION
int_t get_block(int_t value);  // FORWARD DECLARATION
int_t exec_value(int_t value);  // FORWARD DECLARATION

PROC_DECL(prim_Undefined) { return error("undefined procedure"); }

PROC_DECL(prim_CREATE) { return panic("unimplemented CREATE"); }
PROC_DECL(prim_SEND) { return panic("unimplemented SEND"); }
PROC_DECL(prim_BECOME) { return panic("unimplemented BECOME"); }
PROC_DECL(prim_SELF) { return panic("unimplemented SELF"); }
PROC_DECL(prim_FAIL) { return error("FAIL"); }
PROC_DECL(prim_Bind) {
    POP1ARG(value);
    int_t word;
    if (!next_value(&word)) return FALSE;
    if (!IS_WORD(word)) return error("WORD required");
    if (get_rw_word(&word, word)) {
        thunk_t *w = TO_PTR(word);
        w->value = value;
        return TRUE;
    }
    return undefined_word(word);
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
    if (find_ro_word(&word, word)) {
        thunk_t *w = TO_PTR(word);
        return data_push(w->value);
    }
    return undefined_word(word);
}
PROC_DECL(prim_OpenQuote) { return error("unexpected ["); }
PROC_DECL(prim_CloseQuote) { return error("unexpected ]"); }
PROC_DECL(prim_OpenUnquote) { return error("unexpected ("); }
PROC_DECL(prim_CloseUnquote) { return error("unexpected )"); }
PROC_DECL(prim_TRUE) { return data_push(TRUE); }
PROC_DECL(prim_FALSE) { return data_push(FALSE); }
PROC_DECL(prim_IF) {
    POP1ARG(cond);
    DEBUG(print_detail("  prim_IF (cond)", cond));
    GET_BLOCK(block);
    DEBUG(print_detail("  prim_IF (block)", block));
    if (cond) {
        if (!exec_value(block)) return FALSE;
    }
    return TRUE;
}
// [ DUP EQ? IF-ELSE [ DROP ' = . ] [ DUP LT? IF [ ' < . ] GT? IF [ ' > . ] ] ] = CMP  # n CMP --
PROC_DECL(prim_IF_ELSE) {
    POP1ARG(cond);
    DEBUG(print_detail("  prim_IF_ELSE (cond)", cond));
    GET_BLOCK(cnsq);
    DEBUG(print_detail("  prim_IF_ELSE (cnsq)", cnsq));
    GET_BLOCK(altn);
    DEBUG(print_detail("  prim_IF_ELSE (altn)", altn));
    if (!exec_value(cond ? cnsq : altn)) return FALSE;
    return TRUE;
}
PROC_DECL(prim_WHILE) {
    POP1ARG(cond);
    DEBUG(print_detail("  prim_WHILE (cond)", cond));
    GET_BLOCK(block);
    DEBUG(print_detail("  prim_WHILE (block)", block));
    while (cond) {
        if (!exec_value(block)) return FALSE;
        if (data_top < 1) return stack_underflow();
        cond = data_stack[--data_top];  // pop condition from stack
        DEBUG(print_detail("  prim_WHILE (cond...)", cond));
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
PROC_DECL(prim_INF) { return data_push(MK_NUM(INF)); }
PROC_DECL(prim_NEG) { POP1PUSH1(n, NEG); }
PROC_DECL(prim_ADD) { POP2PUSH1(n, m, ADD); }
PROC_DECL(prim_SUB) { POP2PUSH1(n, m, SUB); }
PROC_DECL(prim_MUL) { POP2PUSH1(n, m, MUL); }
PROC_DECL(prim_DIVMOD) {  // n = (m * q) + r
    POP2ARG(n, m);
    n = TO_INT(n);
    m = TO_INT(m);
    int_t q = INF;
    int_t r = n;
    if ((n == INF) && (m == -1)) {
        q = INF;
        r = 0;
    } else if (m != 0) {
        q = n / m;
        r = n % m;
        // FIXME: map to euclidean division
        // -7 3 DIVMOD -- -3 2  # now: -2 -1
        // -7 -3 DIVMOD -- 3 2  # now: 2 -1
        // [https://en.wikipedia.org/wiki/Modulo_operation]
    }
    q = MK_NUM(q);
    r = MK_NUM(r);
    if (!data_push(q)) return FALSE;
    return data_push(r);
    // [ 3 ROLL MUL ADD ] = EUCLID  # n m DIVMOD m EUCLID -- n
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
PROC_DECL(prim_Load) { POP1ARG(addr); return panic("unimplemented ?"); }
PROC_DECL(prim_Store) { POP2ARG(value, addr); return panic("unimplemented !"); }
PROC_DECL(prim_LoadAtomic) { POP1ARG(addr); return panic("unimplemented ??"); }
PROC_DECL(prim_StoreAtomic) { POP2ARG(value, addr); return panic("unimplemented !!"); }
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
PROC_DECL(prim_EMIT) { POP1ARG(code); print_ascii(code); return TRUE; }
PROC_DECL(prim_PrintStack) {
    print_stack();
    fflush(stdout);
    return TRUE;
}
PROC_DECL(prim_PrintDetail) {
    POP1ARG(value);
    print_value(value);
    fflush(stdout);
    print_detail(" ", value);
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
 * word interpreter/compiler
 */

nat_t quote_depth = 0;  // count nested quoting levels

int_t exec_value(int_t value) {
    DEBUG(print_detail("  exec_value (value)", value));
    if (IS_WORD(value)) {
        // find definition in current dictionary
        int_t word;
        if (!find_ro_word(&word, value)) return undefined_word(value);
        DEBUG(print_detail("  exec_value (word)", word));
        thunk_t *w = TO_PTR(word);
        value = w->value;
        DEBUG(print_detail("  exec_value (def)", value));
    }
    // execute value
    if (IS_BLOCK(value)) {
        // save current value source
        nat_t prev_value_cnt = next_value_cnt;
        int_t *prev_value_ptr = next_value_ptr;
        // use block as value source
        DEBUG(print_detail("  exec_value (block)", value));
        block_t *blk = TO_PTR(value);
        next_value_cnt = blk->len;
        next_value_ptr = blk->data;
        // run nested interpreter, reading from block
        int_t ok = interpret();
        // restore previous value source
        next_value_cnt = prev_value_cnt;
        next_value_ptr = prev_value_ptr;
        return ok;
    }
    if (IS_PROC(value)) {
        PROC_DECL((*proc)) = TO_PTR(value);
        return (*proc)(value);
    }
    // push value on stack
    return data_push(value);
}

int_t get_block(int_t value) {
    if (IS_WORD(value)) {
        thunk_t *w = TO_PTR(value);
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
    DEBUG(fprintf(stderr, "  interpret cnt=%"PRIuPTR" ptr=%p\n", next_value_cnt, next_value_ptr));
    while (next_value(&value)) {
        if (get_block(value)) {
            continue;
        }
        if (IS_WORD(value)) {
            thunk_t *w = TO_PTR(value);
            if ((quote_depth > 0) && (strcmp(w->name, ")") == 0)) {
                break;  // end of unquote...
            }
        }
        if (!exec_value(value)) {
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
    DEBUG(print_detail("  quote_value (value)", value));
    if (IS_WORD(value)) {
        if (!get_ro_word(&value, value)) return FALSE;
        DEBUG(print_detail("  quote_value (word)", value));
    }
    // push value on stack
    return data_push(value);
}

int_t compile() {
    int_t value;
    DEBUG(fprintf(stderr, "> compile data_top=%zu\n", data_top));
    size_t quote_top = data_top;  // save stack pointer for error recovery
    while (next_value(&value)) {
        if (IS_WORD(value)) {
            thunk_t *w = TO_PTR(value);
            if (quote_depth == 1) {
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
            if (strcmp(w->name, "[") == 0) {
                // nested block
                ++quote_depth;
            } else if (strcmp(w->name, "]") == 0) {
                // unnest block
                --quote_depth;
            }
        }
        if (!quote_value(value)) {
            data_top = quote_top;  // restore stack on failure
            DEBUG(fprintf(stderr, "< compile quote FAIL! data_top=%zu\n", data_top));
            return FALSE;
        }
    }
    if (data_top < quote_top) return stack_underflow();
    // construct block value from stack contents
    int_t *base = &data_stack[quote_top];
    size_t len = (data_top - quote_top);
    data_top = quote_top;  // restore stack top
    int_t block;
    if (!make_block(&block, base, len)) {
        DEBUG(fprintf(stderr, "< compile block FAIL! data_top=%zu\n", data_top));
        return FALSE;
    }
    XDEBUG(fprintf(stderr, "  compile: "); print_value(block); print_ascii('\n'); fflush(stdout));
    DEBUG(fprintf(stderr, "< compile ok data_top=%zu\n", data_top));
    return data_push(block);
}

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
    print_detail("TRUE", TRUE);
    print_detail("FALSE", FALSE);

    int_t pos = MK_NUM(1);
    int_t zero = MK_NUM(0);
    int_t neg = MK_NUM(-1);
    print_detail("pos", pos);
    print_detail("zero", zero);
    print_detail("neg", neg);

    print_detail("pos NEG", NEG(pos));
    print_detail("neg NEG", NEG(neg));
    print_detail("neg 1 LSL", LSL(neg, pos));
    print_detail("neg 1 LSR", LSR(neg, pos));
    print_detail("neg 1 ASR", ASR(neg, pos));
    print_detail("neg 1 LSR 1 LSL", LSL(LSR(neg, pos), pos));
    print_detail("neg 1 LSR 1 LSL 1 ASR", ASR(LSL(LSR(neg, pos), pos), pos));
    print_detail("neg 1 LSR NOT", NOT(LSR(neg, pos)));
    print_detail("neg 1 LSL NOT", NOT(LSL(neg, pos)));

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
    printf("-- sanity check --\n");
    print_detail("    panic", INT(panic));
    print_detail("Undefined", INT(prim_Undefined));
    print_detail("   CREATE", INT(prim_CREATE));
    print_detail("      SUB", INT(prim_SUB));
    print_detail("      CMP", INT(prim_CMP));
    print_detail("    Print", INT(prim_Print));
    print_detail("     main", INT(main));
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
