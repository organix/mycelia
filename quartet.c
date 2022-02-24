/*
 * quartet.c -- Hosted imperative stack-oriented actor machine
 * Copyright 2014-2022 Dale Schumacher, Tristan Slominski
 *
 * Program source is provided as a stream of words
 * (whitespace separated in text format).
 * Each word is looked up in the current dictionary.
 * If the value is a block it is executed,
 * otherwise the value is pushed on the data stack.
 * Literal values are pushed on the data stack,
 * which is used to provide parameters and
 * return values for executing blocks.
 * Some blocks also consume words from the source stream.
 *
 * An actor's behavior is described with a block.
 * The message received by the actor is the contents of the data stack.
 * The SEND primitive sends the current stack contents, clearing the stack.
 * Values may be saved in the dictionary by binding them to a word.
 * All dictionary changes are local to the executing behavior.
 *
 * The data stack contains universal integer values,
 * usually interpreted as signed 2's-complement numbers.
 * Numeric operations do not overflow,
 * but rather wrap around forming a ring,
 * which may be interpreted as either signed or unsigned.
 * The number of bits is not specified,
 * but is often the native machine word size
 * (e.g.: 32 or 64 bits).
 *
 * The quartet program `TRUE 1 LSR DUP NOT . .`
 * prints the minimum and maximum signed values.
 *
 * See further [https://github.com/organix/mycelia/blob/master/quartet.md]
 */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>  // for intptr_t, uintptr_t, uint8_t, uint16_t, etc.
#include <inttypes.h>  // for PRIiPTR, PRIuPTR, PRIXPTR, etc.
#include <ctype.h>

#define DEBUG(x)   // include/exclude debug instrumentation
#define XDEBUG(x) x // include/exclude extra debugging

// universal Integer type (32/64-bit signed 2's-complement)
typedef intptr_t int_t;
#define INT(n) ((int_t)(n))

// universal Natural type (32/64-bit unsigned 2's-complement)
typedef uintptr_t nat_t;
#define NAT(n) ((nat_t)(n))

// universal Data Pointer type (32/64-bit machine address)
typedef void * ptr_t;
#define PTR(n) ((ptr_t)(n))

// universal Code Pointer type (32/64-bit machine address)
typedef int_t (*func_t)(int_t);
#define FUNC(n) ((func_t)(n))

// universal Boolean constants
#define TRUE INT(-1)
#define FALSE INT(0)

// universal Infinity/Undefined
#define INF INT(~(NAT(-1)>>1))

#define NEG(n)   (-(n))
#define ADD(n,m) ((n)+(m))
#define SUB(n,m) ((n)-(m))
#define MUL(n,m) ((n)*(m))
#define CMP(n,m) ((n)-(m))
#define LTZ(n)   (((n)<0)?TRUE:FALSE)
#define EQZ(n)   (((n)==0)?TRUE:FALSE)
#define GTZ(n)   (((n)>0)?TRUE:FALSE)
#define NOT(n)   (~(n))
#define AND(n,m) ((n)&(m))
#define IOR(n,m) ((n)|(m))
#define XOR(n,m) ((n)^(m))
#define LSL(n,m) ((n)<<(m))
#define LSR(n,m) INT(NAT(n)>>(m))
#define ASR(n,m) ((n)>>(m))

typedef struct block_hdr {
    nat_t       len;        // number of int_t in data[]
    int_t       data[];     // addressable memory
} block_hdr_t;

int_t is_word(int_t value);  // FORWARD DECLARATION
int_t is_block(int_t value);  // FORWARD DECLARATION
int_t is_func(int_t value);  // FORWARD DECLARATION

int_t panic(char *reason) {
    fprintf(stderr, "\nPANIC! %s\n", reason);
    exit(-1);
    return FALSE;
}

int_t error(char *reason) {
    fprintf(stderr, "\nERROR! %s\n", reason);
    return FALSE;
}

/*
 * data stack
 */

#define MAX_STACK ((size_t)(128))
int_t data_stack[MAX_STACK];
size_t data_top = 0;

static int_t stack_overflow() { return panic("stack overflow"); }
static int_t stack_underflow() { return error("empty stack"); }

int_t data_push(int_t value) {
    if (data_top >= MAX_STACK) {
        return panic("stack overflow");
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

int_t data_pick(int_t *value_out, int_t n) {
    if ((n < 1) || (n > data_top)) {
        return panic("index out of bounds");
    }
    *value_out = data_stack[data_top - n];
    return TRUE;
}

int_t data_roll(int_t n) {
    if (n == 0) return TRUE;  // no-op
    if (n > 0) {
        if (n > data_top) {
            return panic("index out of bounds");
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
            return panic("index out of bounds");
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

#define CACHE_LINE_SZ (8 * sizeof(int_t))  // bytes per idealized cache line

#define MAX_WORDS ((size_t)(128))
char word_list[MAX_WORDS][CACHE_LINE_SZ] = {
    "CREATE",
    "SEND",
    "BECOME",
    "SELF",
    "=",
    "'",
    "@",
    "[",
    "]",
    "(",
    ")",
    "TRUE",
    "FALSE",
    "IF",
    "ELSE",
    "DROP",
    "DUP",
    "SWAP",
    "PICK",
    "ROLL",
    "DEPTH",
    "INF",
    "NEG",
    "ADD",
    "SUB",
    "MUL",
    "DIVMOD",
    "COMPARE",
    "LT?",
    "EQ?",
    "GT?",
    "NOT",
    "AND",
    "OR",
    "XOR",
    "LSL",
    "LSR",
    "ASR",
    "?",
    "!",
    "??",
    "!!",
    "WORDS",
    "EMIT",
    "...",
    ".?",
    ".",
    ""
};
size_t ro_words = 47;  // limit of read-only words
size_t rw_words = 47;  // limit of read/write words

int_t is_word(int_t value) {
//    if (NAT(value - INT(word_list)) < sizeof(word_list)) {
    if (NAT(value - INT(word_list)) <= (rw_words * CACHE_LINE_SZ)) {
        return TRUE;
    }
    return FALSE;
}

void print_ascii(int_t code) {
    if ((code & 0x7F) == code) {  // code is in ascii range
        putchar(code);
    }
}

void print_block(int_t block);  // FORWARD DECLARATION

void print_value(int_t value) {
    if (value == INF) {
        printf("INF");
    } else if (is_word(value)) {
        printf("%s", PTR(value));
    } else if (is_block(value)) {
        print_block(value);
    } else {
        printf("%"PRIdPTR, value);
    }
    //print_ascii(' ');
    //print_ascii('\n');
    fflush(stdout);
}

void print_stack() {
    for (size_t i = 0; i < data_top; ++i) {
        print_value(data_stack[i]);
        print_ascii(' ');
    }
}

static void print_detail(char *label, int_t value) {
    fprintf(stderr, "%s:", label);
    fprintf(stderr, " d=%"PRIdPTR" u=%"PRIuPTR" x=%"PRIXPTR"",
        value, value, value);
    if (is_word(value)) {
        fprintf(stderr, " s=\"%s\"", PTR(value));
    }
    if (is_block(value)) {
        nat_t *blk = (nat_t *)(value);
        fprintf(stderr, " [%"PRIuPTR"]", *blk);
    }
    if (is_func(value)) {
        fprintf(stderr, " p=%p", PTR(value));
    }
    fprintf(stderr, "\n");
    fflush(stderr);
}

int_t read_word(char *buf, size_t buf_sz) {
    DEBUG(fprintf(stderr, "> read_word\n"));
    char *p = buf;
    char *q = p + buf_sz;
    DEBUG(fprintf(stderr, "  read_word: skip leading whitespace\n"));
    int c = getchar();
    while ((c <= ' ') || (c == '#') || (c >= 0x7F)) {
        if (c == '#') {  // comment extends to end of line
            DEBUG(fprintf(stderr, "  read_word: skip comment to EOL\n"));
            while ((c != EOF) && (c != '\n')) {
                c = getchar();
            }
        }
        if (c == EOF) {
            DEBUG(fprintf(stderr, "< read_word = FALSE (eof)\n"));
            return FALSE;  // end of file
        }
        c = getchar();
    }
    DEBUG(fprintf(stderr, "  read_word: gather word characters\n"));
    while ((c != EOF) && (c > ' ') && (c < 0x7F)) {
        *p++ = c;
        if (p >= q) {
            return panic("word buffer overflow");
        }
        c = getchar();
    }
    *p = '\0';  // NUL-terminate string
    DEBUG(fprintf(stderr, "< read_word = TRUE (word)\n"));
    return TRUE;
}

static char *base36digit = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
int_t word_to_number(int_t *value_out, int_t word) {
    DEBUG(fprintf(stderr, "> word_to_number\n"));
    // attempt to parse word as a number
    int_t neg = FALSE;
    int_t got_base = FALSE;
    int_t got_digit = FALSE;
    uintptr_t base = 10;
    char *s = PTR(word);
    DEBUG(fprintf(stderr, "  word_to_number: s=\"%s\"\n", s));
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
                DEBUG(fprintf(stderr, "< word_to_number = FALSE (base range)\n"));
                return FALSE;  // number base out of range
            }
            got_base = TRUE;
            got_digit = FALSE;
            n = 0;
            c = *s++;
        }
        char *p = strchr(base36digit, toupper(c));
        if (p == NULL) {
            DEBUG(fprintf(stderr, "< word_to_number = FALSE (non-digit)\n"));
            return FALSE;  // non-digit character
        }
        uintptr_t digit = NAT(p - base36digit);
        if (digit >= base) {
            DEBUG(fprintf(stderr, "< word_to_number = FALSE (digit range)\n"));
            return FALSE;  // digit out of range for base
        }
        n *= base;
        n += digit;
        got_digit = TRUE;
        c = *s++;
    }
    if (!got_digit) {
        DEBUG(fprintf(stderr, "< word_to_number = FALSE (need digits)\n"));
        return FALSE;  // need at least one digit
    }
    *value_out = (neg ? -INT(n) : INT(n));
    DEBUG(fprintf(stderr, "< word_to_number = TRUE\n"));
    return TRUE;
}

int_t parse_word(int_t *word_out) {
    ptr_t word_buf = word_list[rw_words];
    if (!read_word(word_buf, CACHE_LINE_SZ)) return FALSE;
    int_t word = INT(word_buf);
    word_to_number(&word, word);  // attempt to parse word as a number
    *word_out = word;
    return TRUE;
}

ptr_t next_word_ptr = PTR(0);
int_t next_word(int_t *word_out) {
    if (next_word_ptr) {
        // read from block data
        return panic("block scope not implemented");
    }
    // read from input stream
    return parse_word(word_out);
}

int_t create_word(int_t *word_out, int_t word) {
    if (rw_words >= MAX_WORDS) return panic("too many words");
    if (PTR(word) != word_list[rw_words]) return panic("can only create last word read");
    ++rw_words;  // create new word
    DEBUG(print_detail("  create_word", word));
    *word_out = word;
    return TRUE;
}

int_t find_ro_word(int_t *word_out, int_t word) {
    for (size_t n = rw_words; (n-- > 0); ) {  // search from _end_ of dictionary
        int_t memo = INT(word_list[n]);
        if (strcmp(PTR(word), PTR(memo)) == 0) {
            *word_out = memo;
            return TRUE;
        }
    }
    return FALSE;
}

int_t get_ro_word(int_t *word_out, int_t word) {
    if (find_ro_word(word_out, word)) return TRUE;  // word already exists
    return create_word(word_out, word);
}

int_t find_rw_word(int_t *word_out, int_t word) {
    for (size_t n = rw_words; (n-- >= ro_words); ) {  // search only read/write words
        int_t memo = INT(word_list[n]);
        if (strcmp(PTR(word), PTR(memo)) == 0) {
            *word_out = memo;
            return TRUE;
        }
    }
    return FALSE;
}

int_t get_rw_word(int_t *word_out, int_t word) {
    if (find_rw_word(word_out, word)) return TRUE;  // word already exists
    return create_word(word_out, word);
}

/*
 * block storage
 */

#define MAX_BLOCK_MEM (4096 / sizeof(int_t))
int_t block_mem[MAX_BLOCK_MEM];
size_t block_next = 0;

int_t is_block(int_t value) {
//    if (NAT(value - INT(block_mem)) < sizeof(block_mem)) {
    if (NAT(value - INT(block_mem)) < (block_next * sizeof(int_t))) {
        return TRUE;
    }
    return FALSE;
}

void print_block(int_t block) {
    block_hdr_t *blk = PTR(block);
    print_ascii('[');
    print_ascii(' ');
    for (size_t i = 0; i < blk->len; ++i) {
        print_value(blk->data[i]);
        print_ascii(' ');
    }
    print_ascii(']');
}

int_t make_block(int_t *block_out, int_t *base, size_t len) {
#if 0
    return panic("block storage not implemented");
#else
    if ((block_next + len) > MAX_BLOCK_MEM) {
        return panic("out of block memory");
    }
    block_hdr_t *blk = PTR(&block_mem[block_next]);
    block_next += len;
    blk->len = NAT(len);
    while (len-- > 0) {
        blk->data[len] = base[len];
    }
    *block_out = INT(blk);
    return TRUE;
#endif
}

/*
 * word definitions
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

int_t lookup_def(int_t *value_out, int_t word);  // FORWARD DECLARATION
int_t get_def(int_t *value_out, int_t word);  // FORWARD DECLARATION
int_t bind_def(int_t word, int_t value);  // FORWARD DECLARATION

static int_t quoted = FALSE;  // word scan mode (TRUE=compile, FALSE=interpret)
int_t interpret();  // FORWARD DECLARATION
int_t compile();  // FORWARD DECLARATION

int_t prim_CREATE() { return panic("unimplemented CREATE"); }
int_t prim_SEND() { return panic("unimplemented SEND"); }
int_t prim_BECOME() { return panic("unimplemented BECOME"); }
int_t prim_SELF() { return panic("unimplemented SELF"); }
int_t prim_Bind() {
    POP1ARG(value);
    int_t word;
    if (!next_word(&word)) return FALSE;
    if (!is_word(word)) return FALSE;
    if (!get_rw_word(&word, word)) return FALSE;
    return bind_def(word, value);
}
int_t prim_Literal() {
    int_t word;
    if (!next_word(&word)) return FALSE;
    //if (!is_word(word)) return FALSE;  // FIXME: numeric literals are ok too...
    if (!get_ro_word(&word, word)) return FALSE;
    return data_push(word);
}
int_t prim_Lookup() {
    int_t word;
    if (!next_word(&word)) return FALSE;
    if (!is_word(word)) return FALSE;
    int_t value;
    if (!get_def(&value, word)) return FALSE;
    return data_push(value);
}
int_t prim_OpenQuote() {
    XDEBUG(fprintf(stderr, "  prim_OpenQuote (data_top=%"PRIdPTR")\n", data_top));
    size_t quote_top = data_top;
    quoted = TRUE;
    int_t ok = compile();
    quoted = FALSE;
    if (data_top < quote_top) return panic("stack underflow");
    if (!ok) {
        data_top = quote_top;  // restore stack top
        return FALSE;
    }
    int_t *base = &data_stack[quote_top];
    size_t len = (data_top - quote_top);
    int_t block;
    if (!make_block(&block, base, len)) return FALSE;
    data_top = quote_top;  // restore stack top
    return data_push(block);
}
int_t prim_CloseQuote() { return panic("unexpected ]"); }
int_t prim_OpenUnquote() { return panic("unexpected ("); }
int_t prim_CloseUnquote() {
    XDEBUG(fprintf(stderr, "  prim_CloseUnquote (data_top=%"PRIdPTR")\n", data_top));
    quoted = TRUE;
    return TRUE;
}
int_t prim_TRUE() { return data_push(TRUE); }
int_t prim_FALSE() { return data_push(FALSE); }
int_t prim_IF() { return panic("unimplemented IF"); }
int_t prim_ELSE() { return panic("unmatched ELSE"); }
int_t prim_DROP() {
    if (data_top < 1) return stack_underflow();
    --data_top;
    return TRUE;
}
int_t prim_DUP() {
    int_t v;
    if (!data_pick(&v, INT(1))) return FALSE;
    return data_push(v);
}
int_t prim_SWAP() {
    POP2ARG(v_2, v_1);
    if (!data_push(v_1)) return FALSE;
    return data_push(v_2);
}
int_t prim_PICK() {
    int_t v_n;
    POP1ARG(n);
    if (!data_pick(&v_n, n)) return FALSE;
    return data_push(v_n);
}
int_t prim_ROLL() { POP1ARG(n); return data_roll(n); }
int_t prim_DEPTH() { return data_push(INT(data_top)); }
int_t prim_INF() { return data_push(INF); }
int_t prim_NEG() { POP1PUSH1(n, NEG); }
int_t prim_ADD() { POP2PUSH1(n, m, ADD); }
int_t prim_SUB() { POP2PUSH1(n, m, SUB); }
int_t prim_MUL() { POP2PUSH1(n, m, MUL); }
int_t prim_DIVMOD() {  // n = (m * q) + r
    POP2ARG(n, m);
    int_t q = INF;
    int_t r = n;
    if ((n == INF) && (m == -1)) {
        q = INF;
        r = 0;
    } else if (m != 0) {
        q = n / m;
        r = n % m;
    }
    if (!data_push(q)) return FALSE;
    return data_push(r);
    // [ 3 ROLL MUL ADD ] = EUCLID  # n m DIVMOD m EUCLID -- n
}
int_t prim_CMP() { POP2PUSH1(n, m, CMP); }
int_t prim_LTZ() { POP1PUSH1(n, LTZ); }
int_t prim_EQZ() { POP1PUSH1(n, EQZ); }
int_t prim_GTZ() { POP1PUSH1(n, GTZ); }
int_t prim_NOT() { POP1PUSH1(n, NOT); }
int_t prim_AND() { POP2PUSH1(n, m, AND); }
int_t prim_IOR() { POP2PUSH1(n, m, IOR); }
int_t prim_XOR() { POP2PUSH1(n, m, XOR); }
int_t prim_LSL() { POP2PUSH1(n, m, LSL); }
int_t prim_LSR() { POP2PUSH1(n, m, LSR); }
int_t prim_ASR() { POP2PUSH1(n, m, ASR); }
// direct memory access
int_t prim_Load() { POP1ARG(addr); return panic("unimplemented ?"); }
int_t prim_Store() { POP2ARG(value, addr); return panic("unimplemented !"); }
int_t prim_LoadAtomic() { POP1ARG(addr); return panic("unimplemented ??"); }
int_t prim_StoreAtomic() { POP2ARG(value, addr); return panic("unimplemented !!"); }
// interactive extentions
int_t prim_WORDS() {
    size_t i;
    printf("ro:");
    for (i = 0; i < ro_words; ++i) {
        print_ascii(' ');
        print_value(INT(word_list[i]));
    }
    print_ascii('\n');
    if (ro_words < rw_words) {
        printf("rw:");
        for (i = ro_words; i < rw_words; ++i) {
            print_ascii(' ');
            print_value(INT(word_list[i]));
        }
        print_ascii('\n');
    }
    fflush(stdout);
    return TRUE;
}
int_t prim_EMIT() { POP1ARG(code); print_ascii(code); return TRUE; }
int_t prim_PrintStack() {
    print_stack();
    fflush(stdout);
    return TRUE;
}
int_t prim_PrintDetail() {
    POP1ARG(value);
    print_value(value);
    fflush(stdout);
    print_detail(" ", value);
    return TRUE;
}
int_t prim_Print() {
    POP1ARG(value);
    print_value(value);
    print_ascii('\n');
    fflush(stdout);
    return TRUE;
}

int_t word_def[MAX_WORDS] = {
    INT(prim_CREATE),
    INT(prim_SEND),
    INT(prim_BECOME),
    INT(prim_SELF),
    INT(prim_Bind),
    INT(prim_Literal),  // [5]
    INT(prim_Lookup),
    INT(prim_OpenQuote),  // [7]
    INT(prim_CloseQuote),  // [8]
    INT(prim_OpenUnquote),  // [9]
    INT(prim_CloseUnquote),  // [10]
    TRUE, // INT(prim_TRUE),  // FIXME: could be just literal TRUE?
    FALSE, // INT(prim_FALSE),  // FIXME: could be just literal FALSE?
    INT(prim_IF),  // [13]
    INT(prim_ELSE),  // [14]
    INT(prim_DROP),
    INT(prim_DUP),
    INT(prim_SWAP),
    INT(prim_PICK),
    INT(prim_ROLL),
    INT(prim_DEPTH),
    INF, // INT(prim_INF),  // FIXME: could be just literal INF?
    INT(prim_NEG),
    INT(prim_ADD),
    INT(prim_SUB),
    INT(prim_MUL),
    INT(prim_DIVMOD),
    INT(prim_CMP),
    INT(prim_LTZ),
    INT(prim_EQZ),
    INT(prim_GTZ),
    INT(prim_NOT),
    INT(prim_AND),
    INT(prim_IOR),
    INT(prim_XOR),
    INT(prim_LSL),
    INT(prim_LSR),
    INT(prim_ASR),
    INT(prim_Load),
    INT(prim_Store),
    INT(prim_LoadAtomic),
    INT(prim_StoreAtomic),
    INT(prim_WORDS),
    INT(prim_EMIT),
    INT(prim_PrintStack),
    INT(prim_PrintDetail),
    INT(prim_Print),
    FALSE
};
// syntactic marker words
int_t word_Literal = INT(&word_list[5]);
int_t word_OpenQuote = INT(&word_list[7]);
int_t word_CloseQuote = INT(&word_list[8]);
int_t word_OpenUnquote = INT(&word_list[9]);
int_t word_CloseUnquote = INT(&word_list[10]);
int_t word_IF = INT(&word_list[13]);
int_t word_ELSE = INT(&word_list[14]);

int_t lookup_def(int_t *value_out, int_t word) {
    size_t index = NAT(word - INT(word_list)) / CACHE_LINE_SZ;
    if (index < rw_words) {
        *value_out = INT(word_def[index]);
        return TRUE;
    }
    return FALSE;
}

int_t get_def(int_t *value_out, int_t word) {
    if (!find_ro_word(&word, word)) {
        // error on undefined word
        print_value(word);
        fflush(stdout);
        return error("undefined word");
    }
    return lookup_def(value_out, word);
}

int_t bind_def(int_t word, int_t value) {
    size_t index = NAT(word - INT(word_list)) / CACHE_LINE_SZ;
    if ((index >= ro_words) && (index < rw_words)) {
        word_def[index] = value;
        return TRUE;
    }
    print_value(word);
    fflush(stdout);
    return error("bind failed");
}

/*
 * word interpreter/compiler
 */

int_t exec_word(int_t word);  // FORWARD DECLARATION

int_t exec_block(int_t word) {
    XDEBUG(fprintf(stderr, "> exec_block\n"));
    XDEBUG(print_detail("  exec_block (word)", word));
    block_hdr_t *blk = PTR(word);
    for (size_t i = 0; i < blk->len; ++i) {
        if (!exec_word(blk->data[i])) return FALSE;
    }
    XDEBUG(fprintf(stderr, "< exec_block\n"));
    return TRUE;
}

int_t exec_word(int_t word) {
    int_t value = word;
    XDEBUG(print_detail("  exec_word (word)", word));

    if (is_word(word)) {
        // find definition in current dictionary
        if (!get_def(&value, word)) return FALSE;
        XDEBUG(print_detail("  exec_word (def)", value));

        // execute value
        if (is_block(value)) {
            return exec_block(value);
        } if (is_func(value)) {
            // execute function
            int_t (*fn)() = FUNC(value);
            return (*fn)();
        }
    }

    // push value on stack
    XDEBUG(print_detail("  exec_word (value)", value));
    return data_push(value);
}

int_t interpret() {
    XDEBUG(fprintf(stderr, "> interpret (quoted=%"PRIdPTR")\n", quoted));
    size_t exec_top = data_top;  // save stack pointer for error recovery
    XDEBUG(fprintf(stderr, "  interpret data_top=%zu\n", exec_top));
    while (!quoted) {

        // read next word from input
        int_t word;
        if (!next_word(&word)) {
            break;  // no more words...
        }
        XDEBUG(print_detail("  interpret (word)", word));

        // execute word
        if (!exec_word(word)) {
            data_top = exec_top;  // restore stack on failure
        }

    } // loop
    XDEBUG(fprintf(stderr, "< interpret (quoted=%"PRIdPTR")\n", quoted));
    return TRUE;
}

int_t compile() {
    XDEBUG(fprintf(stderr, "> compile (quoted=%"PRIdPTR")\n", quoted));
    XDEBUG(print_detail("  compile (word_CloseQuote)", word_CloseQuote));
    XDEBUG(print_detail("  compile (word_OpenUnquote)", word_OpenUnquote));
    while (quoted) {

        // read next word from input
        int_t word;
        if (!next_word(&word)) {
            break;  // no more words...
        }
        XDEBUG(print_detail("  compile (word)", word));

        // compile word reference
        if (is_word(word)) {
            if (!get_ro_word(&word, word)) return FALSE;
            XDEBUG(print_detail("  compile (intern)", word));

            // check for special compile-time behavior
            if (word == word_CloseQuote) {
                XDEBUG(fprintf(stderr, "  word_CloseQuote (data_top=%"PRIdPTR")\n", data_top));
                quoted = FALSE;
                continue;
            }
            if (word == word_OpenUnquote) {
                XDEBUG(fprintf(stderr, "  word_OpenUnquote (data_top=%"PRIdPTR")\n", data_top));
                size_t unquote_top = data_top;
                quoted = FALSE;
                if (!interpret()) return FALSE;
                quoted = TRUE;
                if (data_top < unquote_top) return panic("stack underflow");
                continue;
            }
        }

        // push word on stack
        if (!data_push(word)) return FALSE;

    } // loop
    XDEBUG(fprintf(stderr, "< compile (quoted=%"PRIdPTR")\n", quoted));
    return TRUE;
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

    int_t pos = 1;
    int_t zero = 0;
    int_t neg = -1;
    print_detail("pos", pos);
    print_detail("zero", zero);
    print_detail("neg", neg);
    printf("\"%%d\": pos=%"PRIdPTR" zero=%"PRIdPTR" neg=%"PRIdPTR"\n",
        pos, zero, neg);
    printf("\"%%u\": pos=%"PRIuPTR" zero=%"PRIuPTR" neg=%"PRIuPTR"\n",
        pos, zero, neg);
    printf("\"%%x\": pos=%"PRIXPTR" zero=%"PRIXPTR" neg=%"PRIXPTR"\n",
        pos, zero, neg);
    printf("neg(x) LSL = %"PRIXPTR"\n",
        LSL(neg, 1));
    printf("neg(x) LSR = %"PRIXPTR"\n",
        LSR(neg, 1));
    printf("neg(x) ASR = %"PRIXPTR"\n",
        ASR(neg, 1));
    printf("neg(x) LSR LSL = %"PRIXPTR"\n",
        LSL(LSR(neg, 1), 1));
    printf("neg(x) LSR LSL ASR = %"PRIXPTR"\n",
        ASR(LSL(LSR(neg, 1), 1), 1));
    printf("neg(x) LSR NOT = %"PRIXPTR"\n",
        NOT(LSR(neg, 1)));
    printf("neg(x) LSL NOT = %"PRIXPTR"\n",
        NOT(LSL(neg, 1)));
    printf("pos(x) LTZ = %"PRIXPTR" EQZ = %"PRIXPTR" GTZ = %"PRIXPTR"\n",
        LTZ(pos), EQZ(pos), GTZ(pos));
    printf("zero(x) LTZ = %"PRIXPTR" EQZ = %"PRIXPTR" GTZ = %"PRIXPTR"\n",
        LTZ(zero), EQZ(zero), GTZ(zero));
    printf("neg(x) LTZ = %"PRIXPTR" EQZ = %"PRIXPTR" GTZ = %"PRIXPTR"\n",
        LTZ(neg), EQZ(neg), GTZ(neg));

    printf("word_list[%zu] = \"%s\"\n",
        ro_words-1, word_list[ro_words-1]);
    printf("word_list[%zu] = \"%s\"\n",
        MAX_WORDS-1, word_list[MAX_WORDS-1]);
    int_t cmp;
    if (find_ro_word(&cmp, INT("COMPARE"))) {
        printf("find_ro_word(\"COMPARE\") = %"PRIXPTR" = \"%s\"\n", cmp, PTR(cmp));
    }
    int_t find = INT(find_ro_word);
    printf("find_ro_word = %"PRIXPTR"\n", find);
    printf("lookup_def = %"PRIXPTR"\n", INT(lookup_def));
    printf("is_word(TRUE) = %"PRIdPTR"\n", is_word(TRUE));
    printf("is_word(FALSE) = %"PRIdPTR"\n", is_word(FALSE));
    printf("is_word(word_list[0]) = %"PRIdPTR"\n", is_word(INT(word_list[0])));
    printf("is_word(word_list[%zu]) = %"PRIdPTR"\n", ro_words-1, is_word(INT(word_list[ro_words-1])));
    printf("is_word(word_list[ro_words]) = %"PRIdPTR"\n", is_word(INT(word_list[ro_words])));
    printf("is_word(word_list[%zu]) = %"PRIdPTR"\n", MAX_WORDS-1, is_word(INT(word_list[MAX_WORDS-1])));
    printf("is_word(word_list[MAX_WORDS]) = %"PRIdPTR"\n", is_word(INT(word_list[MAX_WORDS])));

    int_t word;
    int_t num;
    int_t ok;
    word = INT("0");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIXPTR"\n",
        ok, PTR(word), num, num, num);
    word = INT("-1");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIXPTR"\n",
        ok, PTR(word), num, num, num);
    word = INT("0123456789");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIXPTR"\n",
        ok, PTR(word), num, num, num);
    word = INT("16#0123456789ABCdef");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIXPTR"\n",
        ok, PTR(word), num, num, num);
    word = INT("8#0123456789abcDEF");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIXPTR"\n",
        ok, PTR(word), num, num, num);
    word = INT("8#01234567");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIXPTR" num(o)=%lo\n",
        ok, PTR(word), num, num, num, (unsigned long)num);
    word = INT("-10#2");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIXPTR"\n",
        ok, PTR(word), num, num, num);
    word = INT("2#10");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIXPTR"\n",
        ok, PTR(word), num, num, num);
    word = INT("");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIXPTR"\n",
        ok, PTR(word), num, num, num);
    word = INT("#");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIXPTR"\n",
        ok, PTR(word), num, num, num);
    word = INT("#1");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIXPTR"\n",
        ok, PTR(word), num, num, num);
    word = INT("1#");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIXPTR"\n",
        ok, PTR(word), num, num, num);
    word = INT("2#");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIXPTR"\n",
        ok, PTR(word), num, num, num);
    word = INT("-16#F");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIXPTR"\n",
        ok, PTR(word), num, num, num);
    word = INT("2#1000_0000");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIXPTR"\n",
        ok, PTR(word), num, num, num);
    word = INT("36#xyzzy");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIXPTR"\n",
        ok, PTR(word), num, num, num);
}

int main(int argc, char const *argv[])
{
    //print_platform_info();
    //smoke_test();

#if 1
    printf("-- sanity check --\n");
    print_detail("    panic", INT(panic));
    print_detail("   CREATE", INT(prim_CREATE));
    print_detail("        .", INT(prim_Print));
    print_detail("     main", INT(main));
    print_detail("  is_func", INT(is_func));
#endif
    if (!(NAT(panic) < NAT(main))) {
        return panic("expected panic() < main()");
    }
    if (!is_func(INT(prim_CREATE))) {
        return panic("expected is_func(prim_CREATE)");
    }
    if (!is_func(INT(prim_Print))) {
        return panic("expected is_func(prim_Print)");
    }

    //printf("-- interpreter --\n");
    return (interpret() ? 0 : 1);
}

// WARNING! THIS FUNCTION MUST FOLLOW main(), BUT DECLARED BEFORE panic().
int_t is_func(int_t value)
{
    nat_t fn_ptr = NAT(value);
    value = ((NAT(panic) < fn_ptr) && (fn_ptr < NAT(main))) ? TRUE : FALSE;
    return value;
}
