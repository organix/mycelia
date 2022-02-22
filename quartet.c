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
 * See further [https://github.com/organix/mycelia/blob/master/quartet.md]
 */

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>  // for intptr_t, uintptr_t, uint8_t, uint16_t, etc.
#include <inttypes.h>  // for PRIiPTR, PRIuPTR, PRIxPTR, etc.
#include <ctype.h>

#define DEBUG(x)   // include/exclude debug instrumentation
#define XDEBUG(x) x // include/exclude extra debugging

// universal Integer type (signed 2's-complement 32/64-bit)
typedef intptr_t int_t;
#define INT(n) ((int_t)(n))

// universal Natural type (unsigned 2's-complement 32/64-bit)
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

#define CACHE_LINE_SZ ((size_t)(32))  // number of bytes in a cache line

int_t is_func(int_t value);  // FORWARD DECLARATION
int_t panic(char *reason) {
    fprintf(stderr, "\nPANIC! %s\n", reason);
    exit(-1);
    return FALSE;
}

#define MAX_STACK ((size_t)(128))
int_t data_stack[MAX_STACK];
size_t data_top = 0;

int_t data_push(int_t value) {
    if (data_top >= MAX_STACK) {
        return panic("stack overflow");
    }
    data_stack[data_top++] = value;
    return TRUE;
}

int_t data_pop(int_t *value_out) {
    if (data_top == 0) {
        return panic("empty stack");
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
    "NEG",
    "ADD",
    "SUB",
    "MUL",
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
    "EMIT",
    "...",
    ".?",
    ".",
    ""
};
size_t num_words = 44;

int_t is_word(int_t value) {
//    if (NAT(value - INT(word_list)) < sizeof(word_list)) {
    if (NAT(value - INT(word_list)) <= (num_words * CACHE_LINE_SZ)) {
        return TRUE;
    }
    return FALSE;
}

void print_ascii(int_t code) {
    if ((code & 0x7F) == code) {  // code is in ascii range
        putchar(code);
    }
}

void print_value(int_t value) {
    if (is_word(value)) {
        printf("%s", PTR(value));
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
    fprintf(stderr, " d=%"PRIdPTR" u=%"PRIuPTR" x=%"PRIxPTR"",
        value, value, value);
    if (is_func(value)) {
        fprintf(stderr, " p=%p", PTR(value));
    }
    if (is_word(value)) {
        fprintf(stderr, " s=\"%s\"", PTR(value));
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

int_t next_word(int_t *word_out) {
    ptr_t word_buf = word_list[num_words];
    if (!read_word(word_buf, CACHE_LINE_SZ)) return FALSE;
    *word_out = INT(word_buf);
    return TRUE;
}

int_t lookup_word(int_t *word_out, int_t word) {
    //for (size_t n = 0; (n < num_words); ++n) {  // search from *start* of dictionary
    for (size_t n = num_words; (n-- > 0); ) {  // search from *end* of dictionary
        int_t memo = INT(word_list[n]);
        if (strcmp(PTR(word), PTR(memo)) == 0) {
            *word_out = memo;
            return TRUE;
        }
    }
    return FALSE;
}

int_t intern_word(int_t *word_out, int_t word) {
    if (num_words >= MAX_WORDS) return panic("too many words");
    if (PTR(word) != word_list[num_words]) return panic("can only intern last word read");
    if (!lookup_word(word_out, word)) {
        // new word
        ++num_words;
        *word_out = word;
    }
    DEBUG(print_detail("  intern_word", *word_out));
    return TRUE;
}

static char *base36digit = "0123456789abcdefghijklmnopqrstuvwxyz";
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
        char *p = strchr(base36digit, tolower(c));
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

#define POP1ARG(arg1) \
    int_t arg1; \
    if (!data_pop(&arg1)) return FALSE;
#define POP2ARG(arg1, arg2) \
    int_t arg1, arg2; \
    if (!data_pop(&arg2)) return FALSE; \
    if (!data_pop(&arg1)) return FALSE;
#define POP1PUSH1(arg, oper) \
    if (data_top < 1) return panic("empty stack"); \
    int_t arg = data_stack[data_top-1]; \
    data_stack[data_top-1] = oper(arg); \
    return TRUE;
#define POP2PUSH1(arg1, arg2, oper) \
    if (data_top < 2) return panic("empty stack"); \
    int_t arg1 = data_stack[data_top-2]; \
    int_t arg2 = data_stack[data_top-1]; \
    --data_top; \
    data_stack[data_top-1] = oper(arg1, arg2); \
    return TRUE;

int_t lookup_def(int_t *value_out, int_t word);  // FORWARD DECLARATION
int_t get_def(int_t *value_out, int_t word);  // FORWARD DECLARATION
int_t bind_def(int_t word, int_t value);  // FORWARD DECLARATION

int_t prim_CREATE() { return panic("unimplemented CREATE"); }
int_t prim_SEND() { return panic("unimplemented SEND"); }
int_t prim_BECOME() { return panic("unimplemented BECOME"); }
int_t prim_SELF() { return panic("unimplemented SELF"); }
int_t prim_Bind() {
    POP1ARG(value);
    int_t word;
    if (!next_word(&word)) return FALSE;
    if (!intern_word(&word, word)) return FALSE;
    return bind_def(word, value);
}
int_t prim_Literal() {
    int_t word;
    if (!next_word(&word)) return FALSE;
    if (!intern_word(&word, word)) return FALSE;
    return data_push(word);
}
int_t prim_Lookup() {
    int_t word;
    if (!next_word(&word)) return FALSE;
    int_t value;
    if (!get_def(&value, word)) return FALSE;
    return data_push(value);
}
int_t prim_OpenQuote() { return FALSE; }
int_t prim_CloseQuote() { return panic("unmatched ]"); }
int_t prim_OpenUnquote() { return FALSE; }
int_t prim_CloseUnquote() { return panic("unmatched )"); }
int_t prim_TRUE() { return data_push(TRUE); }
int_t prim_FALSE() { return data_push(FALSE); }
int_t prim_IF() { return panic("unimplemented IF"); }
int_t prim_ELSE() { return panic("unmatched ELSE"); }
int_t prim_DROP() {
    if (data_top < 1) return panic("empty stack");
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
int_t prim_NEG() { POP1PUSH1(n, NEG); }
int_t prim_ADD() { POP2PUSH1(n, m, ADD); }
int_t prim_SUB() { POP2PUSH1(n, m, SUB); }
int_t prim_MUL() { POP2PUSH1(n, m, MUL); }
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
    INT(prim_Literal),
    INT(prim_Lookup),
    INT(prim_OpenQuote),
    INT(prim_CloseQuote),
    INT(prim_OpenUnquote),
    INT(prim_CloseUnquote),
    TRUE, // INT(prim_TRUE),  // FIXME: could be just literal TRUE?
    FALSE, // INT(prim_FALSE),  // FIXME: could be just literal FALSE?
    INT(prim_IF),
    INT(prim_ELSE),
    INT(prim_DROP),
    INT(prim_DUP),
    INT(prim_SWAP),
    INT(prim_PICK),
    INT(prim_ROLL),
    INT(prim_DEPTH),
    INT(prim_NEG),
    INT(prim_ADD),
    INT(prim_SUB),
    INT(prim_MUL),
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
    INT(prim_EMIT),
    INT(prim_PrintStack),
    INT(prim_PrintDetail),
    INT(prim_Print),
    FALSE
};

int_t lookup_def(int_t *value_out, int_t word) {
    size_t index = NAT(word - INT(word_list)) / CACHE_LINE_SZ;
    if (index < num_words) {
        *value_out = INT(word_def[index]);
        return TRUE;
    }
    return FALSE;
}

int_t get_def(int_t *value_out, int_t word) {
    if (!lookup_word(&word, word)) {
        // panic on undefined word
        print_value(word);
        fflush(stdout);
        return panic("undefined word");
    }
    return lookup_def(value_out, word);
}

int_t bind_def(int_t word, int_t value) {
    size_t index = NAT(word - INT(word_list)) / CACHE_LINE_SZ;
    if (index < num_words) {
        word_def[index] = value;
        return TRUE;
    }
    print_value(word);
    fflush(stdout);
    return panic("bind bad word");
}

int_t interpret() {
    while (TRUE) {

        // read next word from input
        int_t word;
        if (!next_word(&word)) {
            return TRUE;  // no more words...
        }

        // attempt to parse word as a number
        int_t number;
        if (word_to_number(&number, word)) {
            // push number on stack
            if (!data_push(number)) {
                return panic("push literal failed");
            }
            continue;
        }

        // find definition in current dictionary
        int_t value;
        if (!get_def(&value, word)) return FALSE;

        // execute value
        if (is_func(value)) {
            // execute function
            int_t (*fn)() = FUNC(value);
            value = (*fn)();
            if (!value) {
                return panic("execution failed");
            }
        } else {
            // push value on stack
            if (!data_push(value)) {
                return panic("push value failed");
            }
        }

    }
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
    printf("\"%%x\": pos=%"PRIxPTR" zero=%"PRIxPTR" neg=%"PRIxPTR"\n",
        pos, zero, neg);
    printf("neg(x) LSL = %"PRIxPTR"\n",
        LSL(neg, 1));
    printf("neg(x) LSR = %"PRIxPTR"\n",
        LSR(neg, 1));
    printf("neg(x) ASR = %"PRIxPTR"\n",
        ASR(neg, 1));
    printf("neg(x) LSR LSL = %"PRIxPTR"\n",
        LSL(LSR(neg, 1), 1));
    printf("neg(x) LSR LSL ASR = %"PRIxPTR"\n",
        ASR(LSL(LSR(neg, 1), 1), 1));
    printf("neg(x) LSR NOT = %"PRIxPTR"\n",
        NOT(LSR(neg, 1)));
    printf("neg(x) LSL NOT = %"PRIxPTR"\n",
        NOT(LSL(neg, 1)));
    printf("pos(x) LTZ = %"PRIxPTR" EQZ = %"PRIxPTR" GTZ = %"PRIxPTR"\n",
        LTZ(pos), EQZ(pos), GTZ(pos));
    printf("zero(x) LTZ = %"PRIxPTR" EQZ = %"PRIxPTR" GTZ = %"PRIxPTR"\n",
        LTZ(zero), EQZ(zero), GTZ(zero));
    printf("neg(x) LTZ = %"PRIxPTR" EQZ = %"PRIxPTR" GTZ = %"PRIxPTR"\n",
        LTZ(neg), EQZ(neg), GTZ(neg));

    printf("word_list[%zu] = \"%s\"\n",
        num_words-1, word_list[num_words-1]);
    printf("word_list[%zu] = \"%s\"\n",
        MAX_WORDS-1, word_list[MAX_WORDS-1]);
    int_t cmp;
    if (lookup_word(&cmp, INT("COMPARE"))) {
        printf("lookup_word(\"COMPARE\") = %"PRIxPTR" = \"%s\"\n", cmp, PTR(cmp));
    }
    int_t find = INT(lookup_word);
    printf("lookup_word = %"PRIxPTR"\n", find);
    printf("lookup_def = %"PRIxPTR"\n", INT(lookup_def));
    printf("is_word(TRUE) = %"PRIdPTR"\n", is_word(TRUE));
    printf("is_word(FALSE) = %"PRIdPTR"\n", is_word(FALSE));
    printf("is_word(word_list[0]) = %"PRIdPTR"\n", is_word(INT(word_list[0])));
    printf("is_word(word_list[%zu]) = %"PRIdPTR"\n", num_words-1, is_word(INT(word_list[num_words-1])));
    printf("is_word(word_list[num_words]) = %"PRIdPTR"\n", is_word(INT(word_list[num_words])));
    printf("is_word(word_list[%zu]) = %"PRIdPTR"\n", MAX_WORDS-1, is_word(INT(word_list[MAX_WORDS-1])));
    printf("is_word(word_list[MAX_WORDS]) = %"PRIdPTR"\n", is_word(INT(word_list[MAX_WORDS])));

    int_t word;
    int_t num;
    int_t ok;
    word = INT("0");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIxPTR"\n",
        ok, PTR(word), num, num, num);
    word = INT("-1");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIxPTR"\n",
        ok, PTR(word), num, num, num);
    word = INT("0123456789");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIxPTR"\n",
        ok, PTR(word), num, num, num);
    word = INT("16#0123456789ABCdef");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIxPTR"\n",
        ok, PTR(word), num, num, num);
    word = INT("8#0123456789abcDEF");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIxPTR"\n",
        ok, PTR(word), num, num, num);
    word = INT("8#01234567");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIxPTR" num(o)=%lo\n",
        ok, PTR(word), num, num, num, (unsigned long)num);
    word = INT("-10#2");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIxPTR"\n",
        ok, PTR(word), num, num, num);
    word = INT("2#10");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIxPTR"\n",
        ok, PTR(word), num, num, num);
    word = INT("");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIxPTR"\n",
        ok, PTR(word), num, num, num);
    word = INT("#");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIxPTR"\n",
        ok, PTR(word), num, num, num);
    word = INT("#1");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIxPTR"\n",
        ok, PTR(word), num, num, num);
    word = INT("1#");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIxPTR"\n",
        ok, PTR(word), num, num, num);
    word = INT("2#");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIxPTR"\n",
        ok, PTR(word), num, num, num);
    word = INT("-16#F");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIxPTR"\n",
        ok, PTR(word), num, num, num);
    word = INT("2#1000_0000");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIxPTR"\n",
        ok, PTR(word), num, num, num);
    word = INT("36#xyzzy");
    ok = word_to_number(&num, word);
    printf("ok=%"PRIdPTR" word=\"%s\" num(d)=%"PRIdPTR" num(u)=%"PRIuPTR" num(x)=%"PRIxPTR"\n",
        ok, PTR(word), num, num, num);
}

int main(int argc, char const *argv[])
{
    //print_platform_info();
    //smoke_test();

#if 0
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
    ptr_t fn_ptr = PTR(value);
    value = ((PTR(panic) < fn_ptr) && (fn_ptr < PTR(main))) ? TRUE : FALSE;
    return value;
}
