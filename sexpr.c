/*
 * sexpr.c -- LISP/Scheme S-expressions (ala John McCarthy)
 */
#include "sexpr.h"
#include "serial.h"

#define USE_SPLAY_ENV   1   // ground environment search (0=linear|1=binary)

#define DEBUG(x) x /* debug logging */
#define TRACE(x)   /* trace logging */

// static actors
extern ACTOR a_nil;
extern ACTOR a_true;
extern ACTOR a_false;
extern ACTOR a_inert;
extern ACTOR a_no_bind;
extern ACTOR a_empty_env;
extern ACTOR a_kernel_err;
extern ACTOR a_exit;

// static combiners
extern ACTOR ap_boolean_p;
extern ACTOR ap_symbol_p;
extern ACTOR ap_env_p;
extern ACTOR ap_ignore_p;
extern ACTOR ap_inert_p;
extern ACTOR ap_pair_p;
extern ACTOR ap_null_p;
extern ACTOR ap_number_p;
extern ACTOR ap_oper_p;
extern ACTOR ap_appl_p;
extern ACTOR ap_combiner_p;

extern ACTOR ap_eq_p;
extern ACTOR ap_equal_p;
extern ACTOR ap_num_eq_p;
extern ACTOR ap_num_lt_p;
extern ACTOR ap_num_le_p;
extern ACTOR ap_num_ge_p;
extern ACTOR ap_num_gt_p;
extern ACTOR ap_num_plus;
extern ACTOR ap_num_minus;
extern ACTOR ap_num_times;
extern ACTOR ap_bit_not;
extern ACTOR ap_bit_and;
extern ACTOR ap_bit_or;
extern ACTOR ap_bit_xor;
extern ACTOR ap_bit_lsl;
extern ACTOR ap_bit_lsr;
extern ACTOR ap_bit_asr;

extern ACTOR ap_cons;
extern ACTOR ap_list;
extern ACTOR op_if;
extern ACTOR op_define;
extern ACTOR op_vau;
extern ACTOR ap_wrap;
extern ACTOR ap_unwrap;
extern ACTOR op_sequence;
extern ACTOR op_timed;
extern ACTOR op_lambda;
extern ACTOR ap_eval;
extern ACTOR ap_make_env;

extern ACTOR ap_dump_env;
extern ACTOR ap_dump_bytes;
extern ACTOR ap_dump_words;
extern ACTOR ap_load_words;
extern ACTOR ap_store_words;
extern ACTOR ap_address_of;
extern ACTOR ap_content_of;
extern ACTOR ap_sponsor_reserve;
extern ACTOR ap_sponsor_release;
extern ACTOR ap_sponsor_enqueue;

// static behaviors
extern ACTOR b_binding;
extern ACTOR b_scope;
extern ACTOR b_symbol;
extern ACTOR b_pair;
extern ACTOR b_number;
extern ACTOR b_appl;
extern ACTOR b_oper;

int
object_p(ACTOR* x)
{
    // [FIXME] make this more robust...
//    return (((u32)x & 0x1f) == 0x0);  // 32-byte alignment
    return (((u32)x & 0x3) == 0x0);  // 32-bit alignment
}

int
boolean_p(ACTOR* x)
{
    return (x == &a_true) || (x == &a_false);
}

int
eq_p(ACTOR* x, ACTOR* y)
{
    struct example_5 *a = (struct example_5 *)x;
    struct example_5 *b = (struct example_5 *)y;

    if (x == y) return 1;  // same object (identical)
    if (a->beh_1c != b->beh_1c) return 0;  // different type/behavior
    ACTOR* beh = a->beh_1c;
    if (beh == &b_number) {
        return a->data_04 == b->data_04;  // same state
    }
    if (beh == &b_appl) {
        return eq_p((ACTOR*)(a->data_04), (ACTOR*)(b->data_04)); // combiner
    }
    return 0;
}

int
equal_p(ACTOR* x, ACTOR* y)
{
    struct example_5 *a = (struct example_5 *)x;
    struct example_5 *b = (struct example_5 *)y;

    if (eq_p(x, y)) return 1;
    ACTOR* beh = a->beh_1c;
    if (beh == &b_pair) {
        return equal_p((ACTOR*)(a->data_04), (ACTOR*)(b->data_04))  // car
            && equal_p((ACTOR*)(a->data_08), (ACTOR*)(b->data_08)); // cdr
    }
    return 0;
}

int
symbol_p(ACTOR* x)
{
    struct example_5 *a = (struct example_5 *)x;
    return (a->beh_1c == &b_symbol);
}

int
inert_p(ACTOR* x)
{
    return (x == &a_inert);
}

int
ignore_p(ACTOR* x)
{
    return (x == &a_no_bind);
}

int
pair_p(ACTOR* x)
{
    struct example_5 *a = (struct example_5 *)x;
    return (a->beh_1c == &b_pair);
}

int
null_p(ACTOR* x)
{
    return (x == &a_nil);
}

int
list_p(ACTOR* x)
{
    // FIXME: "list?" is supposed to fail on an improper list (dotted tail)
    // FIXME: Kernel defines "finite-list?" and "countable-list?"
    return null_p(x) || pair_p(x);
}

int
environment_p(ACTOR* x)
{
    struct example_5 *a = (struct example_5 *)x;
    return (a->beh_1c == &b_scope) || (a->beh_1c == &b_binding);
}

int
number_p(ACTOR* x)
{
    struct example_5 *a = (struct example_5 *)x;
    return (a->beh_1c == &b_number);
}

int
integer_p(ACTOR* x)
{
    return number_p(x);  // [FIXME] currently only int32 is supported
}

int
applicative_p(ACTOR* x)
{
    struct example_5 *a = (struct example_5 *)x;
    return (a->beh_1c == &b_appl);  // FIXME: fails on "exit" (hard-coded)
}

int
operative_p(ACTOR* x)
{
    if (list_p(x) || boolean_p(x) || number_p(x) || inert_p(x) || ignore_p(x)
    ||  symbol_p(x) || environment_p(x) || applicative_p(x)) return 0;
    return object_p(x);  // FIXME: how do we check for operatives? some are hard-coded...
}

int
combiner_p(ACTOR* x)
{
    return applicative_p(x) || operative_p(x);
}

struct sym_24b {  // symbol data is offset 0x04 into a cache-line
    u32         data_04;
    u32         data_08;
    u32         data_0c;
    u32         data_10;
    u32         data_14;
    u32         data_18;
};

static int  // Return 1 if (x == y), otherwise 0.
eq_24b(struct sym_24b* x, struct sym_24b* y)
{
    return (x->data_04 == y->data_04)
        && (x->data_08 == y->data_08)
        && (x->data_0c == y->data_0c)
        && (x->data_10 == y->data_10)
        && (x->data_14 == y->data_14)
        && (x->data_18 == y->data_18);
}

static int  // Return <0 if (x < y), 0> if (x > y), otherwise 0 (x == y).
cmp_24b(struct sym_24b* x, struct sym_24b* y)
{
    int d;

    if ((d = (x->data_04 - y->data_04)) != 0) return d;
    if ((d = (x->data_08 - y->data_08)) != 0) return d;
    if ((d = (x->data_0c - y->data_0c)) != 0) return d;
    if ((d = (x->data_10 - y->data_10)) != 0) return d;
    if ((d = (x->data_14 - y->data_14)) != 0) return d;
    if ((d = (x->data_18 - y->data_18)) != 0) return d;
    return (x->data_18 - y->data_18);
}

static struct example_5* sym_table[1024];
static struct example_5** next_sym = sym_table;

ACTOR*
sym_search(struct sym_24b* name)  // search for interned symbol
{
    struct example_5** sp = sym_table;

    while (sp < next_sym) {
        if (eq_24b(name, (struct sym_24b*)&((*sp)->data_04))) {
            return (ACTOR*)(*sp);
        }
        ++sp;
    }
    return NULL;  // not found
}

ACTOR*
symbol(struct sym_24b* name)
{
    // search for interned symbol
    ACTOR* x = sym_search(name);
    if (x != NULL) {
        TRACE(puts("sym:found\n"));
        return x;  // symbol found, return it
    }
    int mem = ((void *)(next_sym) - (void *)(sym_table));
    TRACE(puts("sym:mem="));
    TRACE(serial_dec32((u32)mem));
    TRACE(puts(",syms=0x"));
    TRACE(serial_hex32((u32)sym_table));
    TRACE(puts(",next=0x"));
    TRACE(serial_hex32((u32)next_sym));
    TRACE(putchar('\n'));
    if (mem >= sizeof(sym_table)) {
        DEBUG(puts("sym:overflow\n"));
        panic();  // FIXME: is there a better way to handle this error?
        return NULL;  // fail -- symbol table overflow!
    }
    // create a new symbol
    struct example_5 *a = create_5(&b_symbol);
    if (a) {
        a->data_04 = name->data_04;
        a->data_08 = name->data_08;
        a->data_0c = name->data_0c;
        a->data_10 = name->data_10;
        a->data_14 = name->data_14;
        a->data_18 = name->data_18;
        *next_sym++ = a;  // intern symbol in table
        TRACE(puts("sym:created="));
        TRACE(print_sexpr((ACTOR*)a));
        TRACE(putchar('\n'));
        return (ACTOR*)a;  // return new symbol
    }
    DEBUG(puts("sym:fail\n"));
    return NULL;  // fail
}

ACTOR*
number(int n)
{
    // FIXME: consider a memo-table for small integers
    TRACE(puts("number(0x"));
    TRACE(serial_hex32((u32)n));
    TRACE(puts(")="));
    struct example_5 *x = create_5(&b_number);
    x->data_04 = (u32)n;
    TRACE(puts("0x"));
    TRACE(serial_hex32((u32)x));
    TRACE(putchar('\n'));
    return (ACTOR*)x;
}

ACTOR*
cons(ACTOR* a, ACTOR* d)
{
    TRACE(puts("cons(0x"));
    TRACE(serial_hex32((u32)a));
    TRACE(puts(",0x"));
    TRACE(serial_hex32((u32)d));
    TRACE(puts(")="));
    struct example_5 *x = create_5(&b_pair);
    x->data_04 = (u32)a;
    x->data_08 = (u32)d;
    TRACE(puts("0x"));
    TRACE(serial_hex32((u32)x));
    TRACE(putchar('\n'));
    return (ACTOR*)x;
}

ACTOR*
car(ACTOR* x)
{
    if (pair_p(x)) {
        struct example_5 *a = (struct example_5 *)x;
        return (ACTOR*)(a->data_04);
    }
    return NULL;  // fail
}

ACTOR*
cdr(ACTOR* x)
{
    if (pair_p(x)) {
        struct example_5 *a = (struct example_5 *)x;
        return (ACTOR*)(a->data_08);
    }
    return NULL;  // fail
}

ACTOR*
set_car(ACTOR* x, ACTOR* a)
{
    if (pair_p(x)) {
        struct example_5 *z = (struct example_5 *)x;
        z->data_04 = (u32)a;
        return x;
    }
    return NULL;  // fail
}

ACTOR*
set_cdr(ACTOR* x, ACTOR* d)
{
    if (pair_p(x)) {
        struct example_5 *z = (struct example_5 *)x;
        z->data_08 = (u32)d;
        return x;
    }
    return NULL;  // fail
}

u32
get_u32(ACTOR* num)
{
    struct example_5 *a = (struct example_5 *)num;
    return a->data_04;
}

ACTOR*
load_words(u32* addr, u32 count)
{
    ACTOR* list = &a_nil;

    while (count > 0) {
        int n = (int)(addr[--count]);
        list = cons(number(n), list);
    }
    return list;
}

void
store_words(u32* addr, ACTOR* list)
{
    while (!null_p(list)) {
        if (!pair_p(list)) return;  // abort
        struct example_5 *x = (struct example_5 *)list;
        *addr++ = get_u32((ACTOR*)(x->data_04));
        list = (ACTOR*)(x->data_08);
    }
}

typedef int (PRED)(ACTOR* x);

ACTOR*
apply_pred(PRED* p, ACTOR* list)  // apply unary predicate to a list
{
    while (!null_p(list)) {
        if (!pair_p(list)) {
            return NULL;  // fail!
        }
        if (!p(car(list))) {
            return &a_false;
        }
        list = cdr(list);
    }
    return &a_true;
}

typedef int (RLTN)(ACTOR* x, ACTOR* y);

ACTOR*
apply_rltn(RLTN* r, ACTOR* list)  // apply binary relation to a list
{
    if (null_p(list)) return &a_true;  // 0-arg base case
    if (!pair_p(list)) return NULL;  // fail: improper arg list
    ACTOR* witness = car(list);
    list = cdr(list);
    while (!null_p(list)) {
        if (!pair_p(list)) {
            return NULL;  // fail: improper arg list
        }
        ACTOR* element = car(list);
        if (!r(witness, element)) {
            return &a_false;
        }
        witness = element;
        list = cdr(list);
    }
    return &a_true;
}

#if 0
int
num_eq_p(ACTOR* x, ACTOR* y)
{
    struct example_5 *a = (struct example_5 *)x;
    struct example_5 *b = (struct example_5 *)y;

    return (a->data_04 == b->data_04);  // same value
}
#endif

ACTOR*
apply_pred_rltn(PRED* p, RLTN* r, ACTOR* list)  // apply relation to elements meeting predicate
{
    if (null_p(list)) return &a_true;  // 0-arg base case
    if (!pair_p(list)) return NULL;  // fail: improper arg list
    ACTOR* witness = car(list);
    if (!p(witness)) return NULL;  // predicate failed
    list = cdr(list);
    while (!null_p(list)) {
        if (!pair_p(list)) {
            return NULL;  // fail: improper arg list
        }
        ACTOR* element = car(list);
        if (!p(element)) return NULL;  // predicate failed
        if (!r(witness, element)) {
            return &a_false;
        }
        witness = element;
        list = cdr(list);
    }
    return &a_true;
}

typedef int (NUM_OP)(int acc, int num);

ACTOR*
num_reduce(int acc, NUM_OP* op, ACTOR* list)  // apply binary operator to numbers
{
    while (!null_p(list)) {
        if (!pair_p(list)) return NULL;  // fail: improper arg list
        ACTOR* item = car(list);
        if (!number_p(item)) return NULL;  // type error
        int num = (int)get_u32(item);
        acc = op(acc, num);
        list = cdr(list);
    }
    return number(acc);
}

/*
 * FIXME! the `no_print` flag has evolved to mean "evaluating preable in ground environment"
 */
static int no_print = 1;  // option to suppress printing of evaluation results
static char* line = NULL;  // sexpr parser input source

void
flush_char()
{
    serial_in_flush();
    line = NULL;
}

void close_env();  // FORWARD DECL

static int
read_char()
{
    if (!line || !line[0]) {  // refill buffer
        if (no_print) {
#if 1
            close_env();
#endif
            no_print = 0;  // enable printing of evaluation results
        }
        line = editline();
        if (!line) return EOF;
    }
    int c = *line++;
    TRACE(serial_hex8((u8)c));
    TRACE(putchar(' '));
    TRACE(putchar(c < ' ' ? '.' : c));
    TRACE(putchar('\n'));
    return c;
}

static void
unread_char(int c)
{
    if (c > 0) {
        TRACE(putchar('<'));
        TRACE(serial_hex8((u8)c));
        TRACE(putchar(' '));
        TRACE(putchar(c < ' ' ? '.' : c));
        TRACE(putchar('\n'));
        *--line = c;
    }
}

void
parse_opt_space()
{
    int c;

    for (;;) {  // skip whitespace
        c = read_char();
        if (c == EOF) return;
        if (c == ';') {
            for (;;) {  // skip comment (to EOL)
                c = read_char();
                if ((c == '\n') || (c == '\r') || (c == EOF)) {
                    break;
                }
            }
        }
        if (c > ' ') {
            break;
        }
    }
    unread_char(c);
}

ACTOR*
parse_list()
{
    int c;
    ACTOR* x = &a_nil;
    ACTOR* y;
    ACTOR* z;

    TRACE(puts("list?\n"));
    c = read_char();
    if (c != '(') {
        unread_char(c);
        return NULL;  // failed
    }
    while ((y = parse_sexpr()) != NULL) {
        x = cons(y, x);  // build list in reverse
    }
    y = &a_nil;
    parse_opt_space();
    c = read_char();
    if ((c == '.') && (x != &a_nil)) {
        TRACE(puts("<tail>\n"));
        y = parse_sexpr();
        if (y == NULL) return NULL;  // failed
        parse_opt_space();
        c = read_char();
    }
    if (c != ')') {
        unread_char(c);
        return NULL;  // failed
    }
    while (x != &a_nil) {  // reverse list in-place
        z = cdr(x);
        y = set_cdr(x, y);
        x = z;
    }
    x = y;
    TRACE(puts("<list>\n"));
    return x;
}

static int
from_digit(int c, int r) {
    if ((c >= '0') && (c <= '9')) return (c - '0');
    if ((r == 16) && (c >= 'a') && (c <= 'f')) return (c - 'a') + 10;
    if ((r == 16) && (c >= 'A') && (c <= 'F')) return (c - 'A') + 10;
    return -1;  // fail
}

ACTOR*
parse_number()
{
    int r = 10;
    int s = '\0';
    int n = 0;
    int c;
    int d;
    ACTOR* x;

    TRACE(puts("number?\n"));
    c = read_char();
    if (c == '#') {
        c = read_char();
        if (c == 'x') {
            r = 16;
        } else {
            unread_char(c);
            unread_char('#');
            return NULL;  // failed
        }
        c = read_char();
    }
    if ((c == '-') || (c == '+')) {  // optional sign
        s = c;
        c = read_char();
    }
    d = from_digit(c, r);
    if (d < 0) {
        unread_char(c);
        if (s) unread_char(s);
        return NULL;  // failed
    }
    n = d;
    for (;;) {
        c = read_char();
        while (c == '_') {  // allow '_' separators
            c = read_char();
        }
        d = from_digit(c, r);
        if (d < 0) {
            unread_char(c);
            break;
        }
        n = (r * n) + d;
    }
    if (s == '-') {
        n = -n;
    }
    x = number(n);
    TRACE(puts("<number>\n"));
    return x;
}

static int
is_ident_char(int c)
{
    return (((c >= 'a') && (c <= 'z'))
        ||  ((c >= 'A') && (c <= 'Z'))
        ||  (c == '!')
        ||  ((c >= '#') && (c <= '&'))
        ||  (c == '*')
        ||  (c == '+')
        ||  (c == '-')
        ||  (c == '.')
        ||  (c == '/')
        ||  ((c >= '0') && (c <= '9'))
        ||  (c == ':')
        ||  ((c >= '<') && (c <= '@'))
        ||  (c == '^')
        ||  (c == '_')
        ||  (c == '~'));
}

static char inert_24b[] =
	"#ine" "rt\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
static char t_24b[] =
	"#t\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
static char f_24b[] =
	"#f\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
static char ignore_24b[] =
	"#ign" "ore\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";

ACTOR*
parse_symbol()
{
    struct sym_24b sym = { 0, 0, 0, 0, 0, 0 };
    char *b = (char *)(&sym);
    int i = 0;
    int c;
    ACTOR* x;

    TRACE(puts("symbol?\n"));
    c = read_char();
    if (!is_ident_char(c)) {
        unread_char(c);
        return NULL;  // failed
    }
    if (c == '.') {  // check for for delimited '.'
        c = read_char();
        if (!is_ident_char(c)) {
            unread_char(c);
            unread_char('.');
            TRACE(puts("<dot>\n"));
            return NULL;  // failed
        }
        b[i++] = '.';
    }
    for (;;) {
        b[i++] = c;
        if (i >= sizeof(sym)) return NULL;  // overflow
        c = read_char();
        if (!is_ident_char(c)) {
            unread_char(c);
            break;
        }
    }
    b[i] = '\0';  // NUL termination
    if (b[0] == '#') {
        if (eq_24b(&sym, (struct sym_24b*)inert_24b)) x = &a_inert;
        else if (eq_24b(&sym, (struct sym_24b*)t_24b)) x = &a_true;
        else if (eq_24b(&sym, (struct sym_24b*)f_24b)) x = &a_false;
        else if (eq_24b(&sym, (struct sym_24b*)ignore_24b)) x = &a_no_bind;
        else return NULL;  // failed
    } else {
        x = symbol(&sym);
    }
    TRACE(puts("<symbol>\n"));
    return x;
}

ACTOR*
parse_atom()
{
    ACTOR* x;

    x = parse_number();
    if (x) return x;  // matched
    x = parse_symbol();
    return x;
}

ACTOR*
parse_sexpr()  /* parse and return s-expression */
{
    ACTOR* x;

    parse_opt_space();
    x = parse_list();
    if (x) return x;  // matched
    x = parse_atom();
    return x;
}

#if 0
#define ONE_OF(c,s) (((c) && strchr((s),(c))) ? 1 : 0)

ACTOR
parse_sexpr()  /* parse and return s-expression */
{
    static char* delim = "\"();'`,[]{}|";

...
    } else if (c == '\'') {
        (src->next)(src);
        c = MK_INT((src->get)(src));
        if (c == '\\') {
            (src->next)(src);
            c = MK_INT((src->get)(src));
            switch(c) {
                case '\\':
                case '\'':
                case '\"':  /* escaped literal */   break;
                case 'r':   c = '\r';               break;
                case 'n':   c = '\n';               break;
                case 't':   c = '\t';               break;
                case 'b':   c = '\b';               break;
                default:    c = EOF;                break;
            }
        } else if (c == '\'') {
            c = EOF;
        }
        x = get_number(NUMBER(c));
        if (c != EOF) {
            (src->next)(src);
            c = MK_INT((src->get)(src));
        }
        if (c == '\'') {
            (src->next)(src);
        } else {
            x = NUMBER(c);  /* malformed character literal */
        }
    } else if (c == '"') {
        x = NUMBER(c);  /* FIXME: implement string literals */
    } else if (ispunct(c) && ONE_OF(c, delim)) {
        x = NUMBER(c);  /* illegal lexeme */
...
#endif

static void
print_number(ACTOR* n)
{
    char dec[12];
    char *p = dec + sizeof(dec);
    int i;
    int s;

    struct example_5 *a = (struct example_5 *)n;
    i = (int)(a->data_04);
    s = (i < 0);
    if (s) i = -i;  // make positive
    *--p = '\0';
    do {
        *--p = (char)((i % 10) + '0');
        i /= 10;
    } while (i && (p > dec));
    if (s) {
        putchar('-');
    }
    puts(p);
}

static void
print_symbol(ACTOR* s)
{
    struct example_5 *a = (struct example_5 *)s;
    puts((char*)(&a->data_04));
}

static void
print_list(ACTOR* x)
{
    putchar('(');
    for (;;) {
        struct example_5 *z = (struct example_5 *)x;
        ACTOR* a = (ACTOR*)(z->data_04);
        ACTOR* d = (ACTOR*)(z->data_08);

        print_sexpr(a);
        if (null_p(d)) {
            putchar(')');
            break;
        } else if (!pair_p(d)) {
            puts(" . ");
            print_sexpr(d);
            putchar(')');
            break;
        }
        putchar(' ');
        x = d;
    }
}

void
print_sexpr(ACTOR* a)  /* print external representation of s-expression */
{
//    if (no_print) return;  // option to suppress printing
    if (!a) {  // null pointer
        puts("#<NULL>");
    } else if ((u32)a & 0x3) {  // misaligned address
        putchar('#');
        serial_hex32((u32)a);
        putchar('?');
    } else if (null_p(a)) {
        puts("()");
    } else if (a == &a_true) {
        puts("#t");
    } else if (a == &a_false) {
        puts("#f");
    } else if (inert_p(a)) {
        puts("#inert");
    } else if (a == &a_no_bind) {
        puts("#ignore");
    } else if ((u32)a & 0x1f) {  // misaligned address
        putchar('#');
        serial_hex32((u32)a);
        putchar('?');
    } else if (number_p(a)) {
        print_number(a);
    } else if (symbol_p(a)) {
        print_symbol(a);
    } else if (pair_p(a)) {
        print_list(a);
    } else if (environment_p(a)) {
        puts("#env@");
        serial_hex32((u32)a);
    } else if (applicative_p(a)) {
        struct example_5 *x = (struct example_5 *)a;
        puts("#wrap@");
        serial_hex32((u32)a);
        putchar('[');
        serial_hex32(x->data_04);
        putchar(']');
    } else {  // unrecognized value
        putchar('#');
        putchar('<');
        serial_hex32((u32)a);
        putchar('>');
    }
}

void
dump_env(ACTOR* x, ACTOR* y)  // dump environment from x to y
{
    while (x != y) {
        struct example_5 *a = (struct example_5 *)x;
        serial_hex32((u32)a);
        puts(": ");
        if (a->beh_1c == &b_scope) {
            puts("--scope--\n");
        } else if (a->beh_1c == &b_binding) {
            print_sexpr((ACTOR*)(a->data_04));
            puts(" = ");
            print_sexpr((ACTOR*)(a->data_08));
            putchar('\n');
        } else {
            print_sexpr(x);
            putchar('\n');
            break;  // terminate loop
        }
        x = (ACTOR*)(a->data_0c);
    }
}

ACTOR*  // returns the augmented environment, or NULL on failure
match_param_tree(ACTOR* def, ACTOR* arg, ACTOR* env)
{
    if (ignore_p(def)) return env;
    if (null_p(def)) {
        if (!null_p(arg)) return NULL;  // FAIL!
        return env;
    }
    if (symbol_p(def)) {
        struct example_5 *x = create_5(&b_binding);
        if (!x) return NULL;  // FAIL!
        x->data_04 = (u32)def;  // set symbol
        x->data_08 = (u32)arg;  // set value
        x->data_0c = (u32)env;  // set next
        x->data_10 = (u32)NULL; // clear left
        x->data_14 = (u32)NULL; // clear right
        return (ACTOR *)x;
    }
    if (pair_p(def)) {
        if (!pair_p(arg)) return NULL;  // FAIL!
        env = match_param_tree(car(def), car(arg), env);
        if (env) {
            env = match_param_tree(cdr(def), cdr(arg), env);
        }
        return env;
    }
    return NULL;  // FAIL!
}

/*
 * FIXME:
 *  `mutate_environment` can be more sophisticated than this implementation.
 *  It should check for errors such as duplicate bindings before mutation.
 *  It should modify existing bindings in-place, rather than prepending a new
 *  binding.
 */
u32
mutate_environment(u32 ext, u32 env)  // mutate env to include ext
{
    u32 x, y, z;
    struct example_5 *p;
    struct example_5 *q;
    struct example_5 *r;
    struct example_5 tmp;

    TRACE(puts("parent_env="));
    TRACE(print_sexpr((ACTOR*)env));
    TRACE(putchar('\n'));
    TRACE(dump_block((u32*)env));
    TRACE(putchar('\n'));
    if (env == ext) return env;  // same environment, no mutation required
    TRACE(dump_env((ACTOR*)ext, (ACTOR*)env));
    x = ext;
    z = (u32)(&a_kernel_err);
    while (x != z) {
        p = (struct example_5 *)x;
        y = p->data_0c;  // next
        if (y == env) {
#if 0
            // FIXME: use stack temporary instead of block allocation...
            q = reserve();  // allocate block
            r = (struct example_5 *)env;
            *q = *r;  // copy original head
            p->data_0c = (u32)q;  // patch tail pointer
            p = (struct example_5 *)ext;
            *r = *p;  // copy extended head
            release(p);  // free extended head
#else
            p->data_0c = ext;  // patch next
            q = (struct example_5 *)ext;
            r = (struct example_5 *)env;
            tmp = *q;  // save first binding
            *q = *r;  // first binding becomes env
            *r = tmp;  // restore first binding
#endif
            TRACE(puts("mutate_env="));
            TRACE(print_sexpr((ACTOR*)env));
            TRACE(putchar('\n'));
            return env;
        }
        x = y;
    }
    return 0;  // FAIL!
}

//static u8 the_margin[128];
static ACTOR* kernel_env = NULL;

static ACTOR*
extend_env(ACTOR* env, struct sym_24b* sym, u32 value)
{
    ACTOR* a = symbol((struct sym_24b*)sym);
    if (!a) return NULL;  // FAIL!
    struct example_5 *x = create_5(&b_binding);
    if (!x) return NULL;  // FAIL!
    x->data_04 = (u32)a;  // set symbol
    x->data_08 = (u32)value;  // set value
    x->data_0c = (u32)env;  // set next
    x->data_10 = (u32)NULL;  // set left
    x->data_14 = (u32)NULL;  // set right
    return (ACTOR *)x;
}

/***
            00_       01_       02_       03_       04_       05_       06_       07_
code  _00:  --code--  --code--  --code--  --code--  --code--  --code--  --code--  --code--
name  _04:            "one"     "two"     "three"   "four"    "five"    "six"     "seven"
value _08:            1         2         3         4         5         6         7
next  _0c:  #0x0100   #0x0200   #0x0300   #0x0400   #0x0500   #0x0600   #0x0700   #env_mt
left  _10:  root      NULL      NULL      NULL      NULL      NULL      NULL      NULL
right _14:            NULL      NULL      NULL      NULL      NULL      NULL      NULL
      _18:
type  _1c:  b_scope   b_bind*g  b_bind*g  b_bind*g  b_bind*g  b_bind*g  b_bind*g  b_bind*g

***/

static int
cmp_symbol(ACTOR* r, ACTOR* s)
{
    struct example_5* x = (struct example_5*)r;
    struct example_5* y = (struct example_5*)s;
    struct sym_24b* a = (struct sym_24b*)(&x->data_04);
    struct sym_24b* b = (struct sym_24b*)(&y->data_04);
    int d = cmp_24b(a, b);
    TRACE(putchar('('));
    TRACE(print_symbol(r));
    TRACE(puts((d < 0) ? " < " : ((d > 0) ? " > " : " = ")));
    TRACE(print_symbol(s));
    TRACE(puts(")\n"));
    return d;
}

ACTOR*
splay_search(ACTOR* env, ACTOR* name)
{
    if (!env) {  // not found, return NULL
        return NULL;
    }
    struct example_5* e = (struct example_5*)env;  // root e
    ACTOR* s = (ACTOR*)(e->data_04);  // e.name
    if (name == s) {  // interned symbols are unique
        // name == e.name
        TRACE(puts("["));
        TRACE(print_sexpr(name));
        TRACE(puts("]="));
        TRACE(print_sexpr(env));
        TRACE(putchar('\n'));
        return env;  // return match
    }
    ACTOR* a = NULL;  // return value
    // zig rotation toward root
    if (cmp_symbol(name, s) < 0) {
        // name < e.name
        a = splay_search((ACTOR*)e->data_10, name);  // recurse left
        if (!a) return NULL;  // failed, return NULL
        struct example_5* x = (struct example_5*)a;
        e->data_10 = x->data_14;  // e.left := a.right
        x->data_14 = (u32)e;  // a.right := e
    } else {
        // name > e.name
        a = splay_search((ACTOR*)e->data_14, name);  // recurse right
        if (!a) return NULL;  // failed, return NULL
        struct example_5* x = (struct example_5*)a;
        e->data_14 = x->data_10;  // e.right := a.left
        x->data_10 = (u32)e;  // a.left := e
    }
    // FIXME: consider adding zig-zig and zig-zag to implement a proper splay tree
    return a;
}

#if 0
static ACTOR*
splay_key(ACTOR* name, ACTOR* value)
{
    struct example_5 *x = create_5(&b_binding);
    if (!x) return NULL;  // FAIL!
    x->data_04 = (u32)name;  // set symbol
    x->data_08 = (u32)value;  // set value
    x->data_0c = (u32)NULL;  // set next
    x->data_10 = (u32)NULL;  // set left
    x->data_14 = (u32)NULL;  // set right
    return (ACTOR *)x;
}
#endif

static ACTOR*
splay_update(ACTOR* env, ACTOR* key)
{
    if (!env) {  // not found, return key
        return key;
    }
    struct example_5* x = (struct example_5*)key;  // search key
    ACTOR* r = (ACTOR*)(x->data_04);  // name
    struct example_5* e = (struct example_5*)env;  // root e
    ACTOR* s = (ACTOR*)(e->data_04);  // e.name
    if (r == s) {  // interned symbols are unique
        // name == e.name
        return env;  // return match
    }
    ACTOR* a = NULL;  // return value
    // zig rotation toward root
    if (cmp_symbol(r, s) < 0) {
        // name < e.name
        a = splay_update((ACTOR*)e->data_10, key);  // recurse left
        x = (struct example_5*)a;
        e->data_10 = x->data_14;  // e.left := a.right
        x->data_14 = (u32)e;  // a.right := e
    } else {
        // name > e.name
        a = splay_update((ACTOR*)e->data_14, key);  // recurse right
        x = (struct example_5*)a;
        e->data_14 = x->data_10;  // e.right := a.left
        x->data_10 = (u32)e;  // a.left := e
    }
    // FIXME: consider adding zig-zig and zig-zag to implement a proper splay tree
    return a;
}

void
close_env()
{
    struct example_5* x;
    /* grow the search tree */
    TRACE(puts("kernel_env=0x"));
    TRACE(serial_hex32((u32)kernel_env));
    TRACE(putchar('\n'));
    ACTOR* root = NULL;
#if USE_SPLAY_ENV
    ACTOR* a = kernel_env;
    while (a && (a != a_empty_env)) {
        x = (struct example_5*)a;
        root = splay_update(root, a);
        if (root != a) {
            puts("DUPLICATE KEYS IN GROUND ENVIRONMENT!\n");
            serial_hex32((u32)root);
            putchar(':');
            putchar('\n');
            dump_block((u32*)root);
            putchar('\n');
            serial_hex32((u32)a);
            putchar(':');
            putchar('\n');
            dump_block((u32*)a);
            putchar('\n');
            //panic();
        }
        a = (ACTOR*)(x->data_0c);  // get next
    }
#endif /* USE_SPLAY_ENV */
    /* create mutable local scope */
    x = create_5(&b_scope);
    if (!x) panic();  // FAIL!
    x->data_0c = (u32)kernel_env;  // set parent
    x->data_10 = (u32)root;  // set root
    kernel_env = (ACTOR *)x;
    /* extend ground environment */
    TRACE(puts("kernel_env'=0x"));
    TRACE(serial_hex32((u32)kernel_env));
    TRACE(putchar('\n'));
}

ACTOR*
ground_env()
{
    char exit_24b[] = "exit" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char otimed_24b[] = "$tim" "ed\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char sponsor_reserve_24b[] = "spon" "sor-" "rese" "rve\0" "\0\0\0\0" "\0\0\0\0";
    char sponsor_release_24b[] = "spon" "sor-" "rele" "ase\0" "\0\0\0\0" "\0\0\0\0";
    char sponsor_enqueue_24b[] = "spon" "sor-" "enqu" "eue\0" "\0\0\0\0" "\0\0\0\0";
    char dump_env_24b[] = "dump" "-env" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char dump_bytes_24b[] = "dump" "-byt" "es\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char dump_words_24b[] = "dump" "-wor" "ds\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char load_words_24b[] = "load" "-wor" "ds\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char store_words_24b[] = "stor" "e-wo" "rds\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char content_of_24b[] = "cont" "ent-" "of\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char address_of_24b[] = "addr" "ess-" "of\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char bit_asr_24b[] = "bit-" "asr\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char bit_lsr_24b[] = "bit-" "lsr\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char bit_lsl_24b[] = "bit-" "lsl\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char bit_not_24b[] = "bit-" "not\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char bit_and_24b[] = "bit-" "and\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char bit_or_24b[] = "bit-" "or\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char bit_xor_24b[] = "bit-" "xor\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char make_env_24b[] = "make" "-env" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char eval_24b[] = "eval" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char wrap_24b[] = "wrap" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char unwrap_24b[] = "unwr" "ap\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char osequence_24b[] = "$seq" "uenc" "e\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char ovau_24b[] = "$vau" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char olambda_24b[] = "$lam" "bda\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char odefinem_24b[] = "$def" "ine!" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char cons_24b[] = "cons" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char p_24b[] = "+\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char m_24b[] = "-\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char t_24b[] = "*\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char oif_24b[] = "$if\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char eq_24b[] = "=?\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char lt_24b[] = "<?\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char leq_24b[] = "<=?\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char geq_24b[] = ">=?\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char gt_24b[] = ">?\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char equalp_24b[] = "equa" "l?\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char eqp_24b[] = "eq?\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char environmentp_24b[] = "envi" "ronm" "ent?" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char combinerp_24b[] = "comb" "iner" "?\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char applicativep_24b[] = "appl" "icat" "ive?" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char operativep_24b[] = "oper" "ativ" "e?\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char numberp_24b[] = "numb" "er?\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char ignorep_24b[] = "igno" "re?\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char symbolp_24b[] = "symb" "ol?\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char inertp_24b[] = "iner" "t?\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char booleanp_24b[] = "bool" "ean?" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char nullp_24b[] = "null" "?\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char pairp_24b[] = "pair" "?\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char list_24b[] = "list" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    ACTOR* a;

    TRACE(puts("dynamic_env="));
    TRACE(print_sexpr(kernel_env));
    TRACE(putchar('\n'));
    if (kernel_env) return kernel_env;  // lazy-initialized singleton

    ACTOR *env = &a_empty_env;  // signal an error on lookup failure

    /* bind "exit" */
    a = extend_env(env, (struct sym_24b*)exit_24b, (u32)&a_exit);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "$timed" */
    a = extend_env(env, (struct sym_24b*)otimed_24b, (u32)&op_timed);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "sponsor-reserve" */
    a = extend_env(env, (struct sym_24b*)sponsor_reserve_24b, (u32)&ap_sponsor_reserve);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "sponsor-release" */
    a = extend_env(env, (struct sym_24b*)sponsor_release_24b, (u32)&ap_sponsor_release);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "sponsor-enqueue" */
    a = extend_env(env, (struct sym_24b*)sponsor_enqueue_24b, (u32)&ap_sponsor_enqueue);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "dump-env" */
    a = extend_env(env, (struct sym_24b*)dump_env_24b, (u32)&ap_dump_env);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "dump-bytes" */
    a = extend_env(env, (struct sym_24b*)dump_bytes_24b, (u32)&ap_dump_bytes);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "dump-words" */
    a = extend_env(env, (struct sym_24b*)dump_words_24b, (u32)&ap_dump_words);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "load-words" */
    a = extend_env(env, (struct sym_24b*)load_words_24b, (u32)&ap_load_words);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "store-words" */
    a = extend_env(env, (struct sym_24b*)store_words_24b, (u32)&ap_store_words);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "content-of" */
    a = extend_env(env, (struct sym_24b*)content_of_24b, (u32)&ap_content_of);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "address-of" */
    a = extend_env(env, (struct sym_24b*)address_of_24b, (u32)&ap_address_of);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "bit-asr" */
    a = extend_env(env, (struct sym_24b*)bit_asr_24b, (u32)&ap_bit_asr);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "bit-lsr" */
    a = extend_env(env, (struct sym_24b*)bit_lsr_24b, (u32)&ap_bit_lsr);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "bit-lsl" */
    a = extend_env(env, (struct sym_24b*)bit_lsl_24b, (u32)&ap_bit_lsl);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "bit-not" */
    a = extend_env(env, (struct sym_24b*)bit_not_24b, (u32)&ap_bit_not);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "bit-xor" */
    a = extend_env(env, (struct sym_24b*)bit_xor_24b, (u32)&ap_bit_xor);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "bit-and" */
    a = extend_env(env, (struct sym_24b*)bit_and_24b, (u32)&ap_bit_and);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "bit-or" */
    a = extend_env(env, (struct sym_24b*)bit_or_24b, (u32)&ap_bit_or);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "make-env" */
    a = extend_env(env, (struct sym_24b*)make_env_24b, (u32)&ap_make_env);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "eval" */
    a = extend_env(env, (struct sym_24b*)eval_24b, (u32)&ap_eval);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "wrap" */
    a = extend_env(env, (struct sym_24b*)wrap_24b, (u32)&ap_wrap);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "unwrap" */
    a = extend_env(env, (struct sym_24b*)unwrap_24b, (u32)&ap_unwrap);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "$sequence" */
    a = extend_env(env, (struct sym_24b*)osequence_24b, (u32)&op_sequence);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "$vau" */
    a = extend_env(env, (struct sym_24b*)ovau_24b, (u32)&op_vau);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "$lambda" */
    a = extend_env(env, (struct sym_24b*)olambda_24b, (u32)&op_lambda);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "$define!" */
    a = extend_env(env, (struct sym_24b*)odefinem_24b, (u32)&op_define);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "cons" */
    a = extend_env(env, (struct sym_24b*)cons_24b, (u32)&ap_cons);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "*" */
    a = extend_env(env, (struct sym_24b*)t_24b, (u32)&ap_num_times);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "-" */
    a = extend_env(env, (struct sym_24b*)m_24b, (u32)&ap_num_minus);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "+" */
    a = extend_env(env, (struct sym_24b*)p_24b, (u32)&ap_num_plus);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "$if" */
    a = extend_env(env, (struct sym_24b*)oif_24b, (u32)&op_if);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind ">?" */
    a = extend_env(env, (struct sym_24b*)gt_24b, (u32)&ap_num_gt_p);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind ">=?" */
    a = extend_env(env, (struct sym_24b*)geq_24b, (u32)&ap_num_ge_p);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "<=?" */
    a = extend_env(env, (struct sym_24b*)leq_24b, (u32)&ap_num_le_p);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "<?" */
    a = extend_env(env, (struct sym_24b*)lt_24b, (u32)&ap_num_lt_p);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "=?" */
    a = extend_env(env, (struct sym_24b*)eq_24b, (u32)&ap_num_eq_p);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "equal?" */
    a = extend_env(env, (struct sym_24b*)equalp_24b, (u32)&ap_equal_p);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "eq?" */
    a = extend_env(env, (struct sym_24b*)eqp_24b, (u32)&ap_eq_p);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "symbol?" */
    a = extend_env(env, (struct sym_24b*)symbolp_24b, (u32)&ap_symbol_p);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "environment?" */
    a = extend_env(env, (struct sym_24b*)environmentp_24b, (u32)&ap_env_p);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "combiner?" */
    a = extend_env(env, (struct sym_24b*)combinerp_24b, (u32)&ap_combiner_p);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "applicative?" */
    a = extend_env(env, (struct sym_24b*)applicativep_24b, (u32)&ap_appl_p);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "operative?" */
    a = extend_env(env, (struct sym_24b*)operativep_24b, (u32)&ap_oper_p);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "number?" */
    a = extend_env(env, (struct sym_24b*)numberp_24b, (u32)&ap_number_p);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "ignore?" */
    a = extend_env(env, (struct sym_24b*)ignorep_24b, (u32)&ap_ignore_p);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "inert?" */
    a = extend_env(env, (struct sym_24b*)inertp_24b, (u32)&ap_inert_p);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "boolean?" */
    a = extend_env(env, (struct sym_24b*)booleanp_24b, (u32)&ap_boolean_p);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "null?" */
    a = extend_env(env, (struct sym_24b*)nullp_24b, (u32)&ap_null_p);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "pair?" */
    a = extend_env(env, (struct sym_24b*)pairp_24b, (u32)&ap_pair_p);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* bind "list" */
    a = extend_env(env, (struct sym_24b*)list_24b, (u32)&ap_list);
    if (!a) return NULL;  // FAIL!
    env = a;

    /* establish ground environment */
    kernel_env = env;
    TRACE(puts("ground_env=0x"));
    TRACE(serial_hex32((u32)kernel_env));
    TRACE(putchar('\n'));
#if 0
    close_env();
#endif

    /* provide additional definitions in source form */
    no_print = 1;  // suppress printing results while evaluating preamble
    line =
//"($define! $quote ($vau (x) #ignore x))\n"
//"($define! car ($lambda ((x . #ignore)) x))\n"
//"($define! cdr ($lambda ((#ignore . x)) x))\n"
//"($define! get-current-environment (wrap ($vau () e e)))\n"
//get-current-environment
//12345678901234567890123 <-- 23 character maximum symbol length
//make-kernel-standard-environment
//make-standard-env
//"($define! get-current-env (wrap ($vau () e e)))\n"
#if 0  // arm assembler
"($timed\n"
"($define! arm-cond-eq\n"
"  ($lambda (inst) (bit-or (bit-and #x0fff_ffff inst) #x0000_0000)))\n"
"($define! arm-cond-ne\n"
"  ($lambda (inst) (bit-or (bit-and #x0fff_ffff inst) #x1000_0000)))\n"
"($define! arm-cond-cs\n"
"  ($lambda (inst) (bit-or (bit-and #x0fff_ffff inst) #x2000_0000)))\n"
"($define! arm-cond-cc\n"
"  ($lambda (inst) (bit-or (bit-and #x0fff_ffff inst) #x3000_0000)))\n"
"($define! arm-cond-mi\n"
"  ($lambda (inst) (bit-or (bit-and #x0fff_ffff inst) #x4000_0000)))\n"
"($define! arm-cond-pl\n"
"  ($lambda (inst) (bit-or (bit-and #x0fff_ffff inst) #x5000_0000)))\n"
//"($define! arm-cond-vs\n"
//"  ($lambda (inst) (bit-or (bit-and #x0fff_ffff inst) #x6000_0000)))\n"
//"($define! arm-cond-vc\n"
//"  ($lambda (inst) (bit-or (bit-and #x0fff_ffff inst) #x7000_0000)))\n"
"($define! arm-cond-hi\n"
"  ($lambda (inst) (bit-or (bit-and #x0fff_ffff inst) #x8000_0000)))\n"
"($define! arm-cond-ls\n"
"  ($lambda (inst) (bit-or (bit-and #x0fff_ffff inst) #x9000_0000)))\n"
"($define! arm-cond-ge\n"
"  ($lambda (inst) (bit-or (bit-and #x0fff_ffff inst) #xa000_0000)))\n"
"($define! arm-cond-lt\n"
"  ($lambda (inst) (bit-or (bit-and #x0fff_ffff inst) #xb000_0000)))\n"
"($define! arm-cond-gt\n"
"  ($lambda (inst) (bit-or (bit-and #x0fff_ffff inst) #xc000_0000)))\n"
"($define! arm-cond-le\n"
"  ($lambda (inst) (bit-or (bit-and #x0fff_ffff inst) #xd000_0000)))\n"
//"($define! arm-cond-al\n"
//"  ($lambda (inst) (bit-or (bit-and #x0fff_ffff inst) #xe000_0000)))\n"
"($define! arm-r0 0)\n"
"($define! arm-r1 1)\n"
"($define! arm-r2 2)\n"
"($define! arm-r3 3)\n"
"($define! arm-r4 4)\n"
"($define! arm-r5 5)\n"
"($define! arm-r6 6)\n"
"($define! arm-r7 7)\n"
"($define! arm-r8 8)\n"
"($define! arm-r9 9)\n"
"($define! arm-sl 10)\n"
"($define! arm-fp 11)\n"
"($define! arm-ip 12)\n"
"($define! arm-sp 13)\n"
"($define! arm-lr 14)\n"
"($define! arm-pc 15)\n"
"($define! arm-bx-Rn ($lambda (n) (bit-and n #xf)))\n"
"($define! arm-bx\n"
"  ($lambda (n) (bit-or #xe12f_ff10 (arm-bx-Rn n)) ))\n"
"($define! arm-blx\n"
"  ($lambda (n) (bit-or #xe12f_ff30 (arm-bx-Rn n)) ))\n"
"($define! arm-pc-prefetch 8)\n"
"($define! arm-b-offset\n"
"  ($lambda (offset)\n"
"    (bit-and (bit-asr (+ offset arm-pc-prefetch) 2) #x00ff_ffff)))\n"
"($define! arm-b\n"
"  ($lambda (offset) (bit-or #xea00_0000 (arm-b-offset offset)) ))\n"
"($define! arm-bl\n"
"  ($lambda (offset) (bit-or #xeb00_0000 (arm-b-offset offset)) ))\n"
//"($define! arm-beq\n"
//"  ($lambda (offset) (arm-cond-eq (arm-b offset)) ))\n"
//"($define! arm-bne\n"
//"  ($lambda (offset) (arm-cond-ne (arm-b offset)) ))\n"
"($define! arm-dp-Rd ($lambda (d) (bit-lsl (bit-and d #xf) 12)))\n"
"($define! arm-dp-Rn ($lambda (n) (bit-lsl (bit-and n #xf) 16)))\n"
"($define! arm-and\n"
"  ($lambda (d n op2) (bit-or #xe000_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))\n"
"($define! arm-ands\n"
"  ($lambda (d n op2) (bit-or #xe010_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))\n"
"($define! arm-eor\n"
"  ($lambda (d n op2) (bit-or #xe020_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))\n"
"($define! arm-eors\n"
"  ($lambda (d n op2) (bit-or #xe030_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))\n"
"($define! arm-sub\n"
"  ($lambda (d n op2) (bit-or #xe040_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))\n"
"($define! arm-subs\n"
"  ($lambda (d n op2) (bit-or #xe050_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))\n"
"($define! arm-rsb\n"
"  ($lambda (d n op2) (bit-or #xe060_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))\n"
"($define! arm-rsbs\n"
"  ($lambda (d n op2) (bit-or #xe070_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))\n"
"($define! arm-add\n"
"  ($lambda (d n op2) (bit-or #xe080_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))\n"
"($define! arm-adds\n"
"  ($lambda (d n op2) (bit-or #xe090_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))\n"
//"($define! arm-adc\n"
//"  ($lambda (d n op2) (bit-or #xe0a0_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))\n"
//"($define! arm-adcs\n"
//"  ($lambda (d n op2) (bit-or #xe0b0_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))\n"
//"($define! arm-sbc\n"
//"  ($lambda (d n op2) (bit-or #xe0c0_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))\n"
//"($define! arm-sbcs\n"
//"  ($lambda (d n op2) (bit-or #xe0d0_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))\n"
//"($define! arm-rsc\n"
//"  ($lambda (d n op2) (bit-or #xe0e0_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))\n"
//"($define! arm-rscs\n"
//"  ($lambda (d n op2) (bit-or #xe0f0_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))\n"
"($define! arm-tst\n"
"  ($lambda (n op2) (bit-or #xe110_0000 (arm-dp-Rn n) op2)))\n"
"($define! arm-teq\n"
"  ($lambda (n op2) (bit-or #xe130_0000 (arm-dp-Rn n) op2)))\n"
"($define! arm-cmp\n"
"  ($lambda (n op2) (bit-or #xe150_0000 (arm-dp-Rn n) op2)))\n"
"($define! arm-cmn\n"
"  ($lambda (n op2) (bit-or #xe170_0000 (arm-dp-Rn n) op2)))\n"
"($define! arm-orr\n"
"  ($lambda (d n op2) (bit-or #xe180_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))\n"
"($define! arm-orrs\n"
"  ($lambda (d n op2) (bit-or #xe190_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))\n"
"($define! arm-mov\n"
"  ($lambda (d op2) (bit-or #xe1a0_0000 (arm-dp-Rd d) op2)))\n"
"($define! arm-movs\n"
"  ($lambda (d op2) (bit-or #xe1b0_0000 (arm-dp-Rd d) op2)))\n"
"($define! arm-bic\n"
"  ($lambda (d n op2) (bit-or #xe1c0_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))\n"
"($define! arm-bics\n"
"  ($lambda (d n op2) (bit-or #xe1d0_0000 (arm-dp-Rd d) (arm-dp-Rn n) op2)))\n"
"($define! arm-mvn\n"
"  ($lambda (d op2) (bit-or #xe1e0_0000 (arm-dp-Rd d) op2)))\n"
"($define! arm-mvns\n"
"  ($lambda (d op2) (bit-or #xe1f0_0000 (arm-dp-Rd d) op2)))\n"
//"; Op2 = immediate\n"
"($define! arm-dp-imm\n"
"  ($lambda (imm) (bit-or (bit-and imm #xff) #x0200_0000)))\n"
"($define! arm-dp-imm-ror\n"
"  ($lambda (imm r)\n"
"    (bit-or (bit-and imm #xff) (bit-lsl (bit-and r #x1e) 7) #x0200_0000)))\n"
//"; Op2 = register\n"
"($define! arm-dp-Rm ($lambda (m) (bit-and m #xf)))\n"
"($define! arm-dp-Rm-lsl-imm\n"
"  ($lambda (m imm) (bit-or (bit-and m #xf) (bit-lsl (bit-and imm #x1f) 7))))\n"
//"($define! arm-dp-Rm-lsr-imm\n"
//"  ($lambda (m imm) (bit-or (bit-and m #xf) (bit-lsl (bit-and imm #x1f) 7) #x0000_0020)))\n"
"($define! arm-dp-Rm-asr-imm\n"
"  ($lambda (m imm) (bit-or (bit-and m #xf) (bit-lsl (bit-and imm #x1f) 7) #x0000_0040)))\n"
//"($define! arm-dp-Rm-ror-imm\n"
//"  ($lambda (m imm) (bit-or (bit-and m #xf) (bit-lsl (bit-and imm #x1f) 7) #x0000_0060)))\n"
"($define! arm-dp-Rm-lsl-Rs\n"
"  ($lambda (m s) (bit-or (bit-and m #xf) (bit-lsl (bit-and s #xf) 8) #x0000_0010)))\n"
//"($define! arm-dp-Rm-lsr-Rs\n"
//"  ($lambda (m s) (bit-or (bit-and m #xf) (bit-lsl (bit-and s #xf) 8) #x0000_0030)))\n"
"($define! arm-dp-Rm-asr-Rs\n"
"  ($lambda (m s) (bit-or (bit-and m #xf) (bit-lsl (bit-and s #xf) 8) #x0000_0050)))\n"
//"($define! arm-dp-Rm-ror-Rs\n"
//"  ($lambda (m s) (bit-or (bit-and m #xf) (bit-lsl (bit-and s #xf) 8) #x0000_0070)))\n"
"($define! arm-mul-Rd\n"
"  ($lambda (d) (bit-lsl (bit-and d #xf) 16)))\n"
"($define! arm-mul-Rm     ; WARNING! MUST NOT BE THE SAME AS Rd\n"
"  ($lambda (m) (bit-and m #xf)))\n"
"($define! arm-mul-Rs\n"
"  ($lambda (s) (bit-lsl (bit-and s #xf) 8)))\n"
"($define! arm-mul-Rn\n"
"  ($lambda (n) (bit-lsl (bit-and s #xf) 12)))\n"
"($define! arm-mul\n"
"  ($lambda (d m s) (bit-or #xe000_0090 (arm-mul-Rd d) (arm-mul-Rm m) (arm-mul-Rs s)) ))\n"
"($define! arm-muls\n"
"  ($lambda (d m s) (bit-or #xe010_0090 (arm-mul-Rd d) (arm-mul-Rm m) (arm-mul-Rs s)) ))\n"
"($define! arm-mla\n"
"  ($lambda (d m s n) (bit-or #xe020_0090 (arm-mul-Rd d) (arm-mul-Rm m) (arm-mul-Rs s) (arm-mul-Rn n)) ))\n"
"($define! arm-mlas\n"
"  ($lambda (d m s n) (bit-or #xe030_0090 (arm-mul-Rd d) (arm-mul-Rm m) (arm-mul-Rs s) (arm-mul-Rn n)) ))\n"
"($define! arm-ls-Rd\n"
"  ($lambda (d) (bit-lsl (bit-and d #xf) 12)))\n"
"($define! arm-ls-Rn\n"
"  ($lambda (n) (bit-lsl (bit-and n #xf) 16)))\n"
"($define! arm-strib       ; [Rn, Ofs] := Rd\n"
"  ($lambda (d n ofs) (bit-or #xe580_0000 (arm-ls-Rd d) (arm-ls-Rn n) ofs)))\n"
"($define! arm-ldrib       ; Rd := [Rn, Ofs]\n"
"  ($lambda (d n ofs) (bit-or #xe590_0000 (arm-ls-Rd d) (arm-ls-Rn n) ofs)))\n"
//"; Ofs = immediate\n"
"($define! arm-ls-imm ($lambda (imm) (bit-and imm #xfff)))\n"
//"; Ofs = register\n"
"($define! arm-ls-Rm ($lambda (m) (bit-or (bit-and m #xf) #x0200_0000)))\n"
"($define! arm-ls-Regs\n"
"  ($lambda r\n"
"    ($if (pair? r)\n"
"      (($lambda ((a . d))\n"
"        (bit-or (bit-lsl 1 a) (apply arm-ls-Regs d))) r)\n"
"      0)))\n"
//"($define! arm-stmda\n"
//"  ($lambda (n . r) (bit-or #xe800_0000 (arm-ls-Rn n) (apply arm-ls-Regs r)) ))\n"
//"($define! arm-ldmda\n"
//"  ($lambda (n . r) (bit-or #xe810_0000 (arm-ls-Rn n) (apply arm-ls-Regs r)) ))\n"
//"($define! arm-stmdb\n"
//"  ($lambda (n . r) (bit-or #xe900_0000 (arm-ls-Rn n) (apply arm-ls-Regs r)) ))\n"
//"($define! arm-ldmdb\n"
//"  ($lambda (n . r) (bit-or #xe910_0000 (arm-ls-Rn n) (apply arm-ls-Regs r)) ))\n"
"($define! arm-stmia\n"
"  ($lambda (n . r) (bit-or #xe880_0000 (arm-ls-Rn n) (apply arm-ls-Regs r)) ))\n"
"($define! arm-ldmia\n"
"  ($lambda (n . r) (bit-or #xe890_0000 (arm-ls-Rn n) (apply arm-ls-Regs r)) ))\n"
"($define! arm-stmib\n"
"  ($lambda (n . r) (bit-or #xe980_0000 (arm-ls-Rn n) (apply arm-ls-Regs r)) ))\n"
"($define! arm-ldmib\n"
"  ($lambda (n . r) (bit-or #xe990_0000 (arm-ls-Rn n) (apply arm-ls-Regs r)) ))\n"
//"($define! arm-stmda-wb\n"
//"  ($lambda (n . r) (bit-or #xe820_0000 (arm-ls-Rn n) (apply arm-ls-Regs r)) ))\n"
//"($define! arm-ldmda-wb\n"
//"  ($lambda (n . r) (bit-or #xe830_0000 (arm-ls-Rn n) (apply arm-ls-Regs r)) ))\n"
"($define! arm-stmdb-wb\n"
"  ($lambda (n . r) (bit-or #xe920_0000 (arm-ls-Rn n) (apply arm-ls-Regs r)) ))\n"
"($define! arm-ldmdb-wb\n"
"  ($lambda (n . r) (bit-or #xe930_0000 (arm-ls-Rn n) (apply arm-ls-Regs r)) ))\n"
"($define! arm-stmia-wb\n"
"  ($lambda (n . r) (bit-or #xe8a0_0000 (arm-ls-Rn n) (apply arm-ls-Regs r)) ))\n"
"($define! arm-ldmia-wb\n"
"  ($lambda (n . r) (bit-or #xe8b0_0000 (arm-ls-Rn n) (apply arm-ls-Regs r)) ))\n"
//"($define! arm-stmib-wb\n"
//"  ($lambda (n . r) (bit-or #xe9a0_0000 (arm-ls-Rn n) (apply arm-ls-Regs r)) ))\n"
//"($define! arm-ldmib-wb\n"
//"  ($lambda (n . r) (bit-or #xe9b0_0000 (arm-ls-Rn n) (apply arm-ls-Regs r)) ))\n"
")\n"
#endif
#if 0  // peg parsing
"($timed\n"
//"; Derived\n"
"($define! peg-peek\n"
"  ($lambda (peg)\n"
"    (peg-not (peg-not peg)) ))\n"
"($define! peg-opt\n"
"  ($lambda (peg)\n"
"    (peg-or peg peg-empty)))\n"
"($define! peg-plus\n"
"  ($lambda (peg)\n"
"    (peg-and peg (peg-star peg)) ))\n"
"($define! peg-star\n"
"  ($lambda (peg)\n"
"    ($lambda (in)\n"
"      ((peg-opt (peg-and peg (peg-star peg))) in))))\n"
"($define! peg-alt\n"
"  ($lambda pegs\n"
"    ($if (pair? pegs)\n"
"      (peg-or (car pegs) (apply peg-alt (cdr pegs)))\n"
"      peg-fail)))\n"
"($define! peg-seq\n"
"  ($lambda pegs\n"
"    ($if (pair? pegs)\n"
"      (peg-and (car pegs) (apply peg-seq (cdr pegs)))\n"
"      peg-empty)))\n"
"($define! peg-equal\n"
"  ($lambda (value)\n"
"    (peg-if ($lambda (token) (equal? value token))) ))\n"
"($define! peg-range\n"
"  ($lambda (lo hi)\n"
"    (peg-if ($lambda (token) (<=? lo token hi))) ))\n"
//"; Primitive\n"
"($define! peg-any\n"
"  ($lambda (in)\n"
"    ($if (null? in)\n"
"      (list #f in)\n"
"      ($let (((token . rest) in))\n"
"        (list #t token rest))) ))\n"
"($define! peg-xform\n"
"  ($lambda (peg xform)\n"
"    ($lambda (in)\n"
"      (xform (peg in)) )))\n"
"($define! peg-not\n"
"  ($lambda (peg)\n"
"    ($lambda (in)\n"
"      ($let (((ok . #ignore) (peg in)))\n"
"        ($if ok\n"
"          (list #f in)\n"
"          (list #t #inert in))) )))\n"
"($define! peg-or\n"
"  ($lambda (left right)\n"
"    ($lambda (in)\n"
"      ($let (((ok . state) (left in)))\n"
"        ($if ok\n"
"          (cons #t state)\n"
"          (right in))) )))\n"
"($define! peg-and\n"
"  ($lambda (left right)\n"
"    ($lambda (in)\n"
"      ($let (((ok . state) (left in)))\n"
"        ($if ok\n"
"          ($let (((lval rest) state))\n"
"            ($let (((ok . state) (right rest)))\n"
"              ($if ok\n"
"                ($let (((rval rest) state))\n"
"                  (list #t (cons lval rval) rest))\n"
"                (list #f in))))\n"
"          (list #f in))) )))\n"
"($define! peg-if\n"
"  ($lambda (test?)\n"
"    ($lambda (in)\n"
"      ($if (null? in)\n"
"        (list #f in)\n"
"        ($let (((token . rest) in))\n"
"          ($if (test? token)\n"
"            (list #t token rest)\n"
"            (list #f in)) )) )))\n"
"($define! peg-fail\n"
"  ($lambda (in)\n"
"    (list #f in)))\n"
"($define! peg-empty\n"
"  ($lambda (in)\n"
"    (list #t () in)))\n"
")\n"
#endif
#if 1  // core language definitions
"($timed\n"
"($define! $let-redirect\n"
"  ($vau (env-exp bindings . body) env\n"
"    (eval (list*\n"
"      (eval (list* $lambda (map car bindings) body) (eval env-exp env))\n"
"      (map cadr bindings))\n"
"    env)))\n"
"($define! $let-safe\n"
"  ($vau (bindings . body) env\n"
"    (eval (list* $let-redirect (make-standard-env) bindings body) env)))\n"
"($define! $letrec*\n"
"  ($vau (bindings . body) env\n"
"    (eval ($if (null? bindings)\n"
"      (list* $letrec bindings body)\n"
"      (list $letrec (list (car bindings)) (list* $letrec* (cdr bindings) body))\n"
"    ) env)))\n"
"($define! $letrec\n"
"  ($vau (bindings . body) env\n"
"    (eval (list* $let ()\n"
"      (list $define! (map car bindings) (list* list (map cadr bindings)))\n"
"      body) env)))\n"
"($define! $let*\n"
"  ($vau (bindings . body) env\n"
"    (eval ($if (null? bindings)\n"
"      (list* $let bindings body)\n"
"      (list $let (list (car bindings)) (list* $let* (cdr bindings) body))\n"
"    ) env)))\n"
"($define! $remote-eval ($vau (o e) d (eval o (eval e d))))\n"
"($define! $bindings->env\n"
"  ($vau bindings denv\n"
"    (eval (list $let-redirect (make-env) bindings (list get-current-env)) denv)))\n"
"($define! $get\n"
"  ($vau (env symbol) dyn\n"
"    (eval symbol\n"
"      (eval env dyn))))\n"
"($define! $set!\n"
"  ($vau (env formal value) dyn\n"
"    (eval\n"
"      (list $define! formal\n"
"        (list (unwrap eval) value dyn))\n"
"      (eval env dyn))))\n"
"($define! $provide!\n"
"  ($vau (symbols . body) env\n"
"    (eval\n"
"      (list $define! symbols\n"
"        (list\n"
"          (list $lambda ()\n"
"            (list* $sequence body)\n"
"            (list* list symbols))))\n"
"      env)))\n"
"($define! $cond\n"
"  ($vau clauses env\n"
"    ($if (null? clauses)\n"
"      #inert\n"
"      (apply\n"
"        ($lambda ((test . body) . rest)\n"
"          ($if (eval test env)\n"
"            (eval (cons $sequence body) env)\n"
"            (eval (cons $cond rest) env)))\n"
"        clauses))))\n"
"($define! $and?\n"
"  ($vau x e\n"
"    ($cond\n"
"      ((null? x) #t)\n"
"      ((null? (cdr x)) (eval (car x) e))  ; tail context\n"
"      ((eval (car x) e) (apply (wrap $and?) (cdr x) e))\n"
"      (#t #f)\n"
"    )))\n"
"($define! $or?\n"
"  ($vau x e\n"
"    ($cond\n"
"      ((null? x) #f)\n"
"      ((null? (cdr x)) (eval (car x) e))  ; tail context\n"
"      ((eval (car x) e) #t)\n"
"      (#t (apply (wrap $or?) (cdr x) e))\n"
"    )))\n"
"($define! and?\n"
"  ($lambda args\n"
"    (eval (cons $and? args) (get-current-env)) ))\n"
"($define! or?\n"
"  ($lambda args\n"
"    (eval (cons $or? args) (get-current-env)) ))\n"
"($define! not? ($lambda (x) ($if x #f #t)))\n"
"($define! length\n"
"  ($lambda (object)\n"
"    ($if (pair? object)\n"
"      (+ 1 (length (cdr object)))\n"
"      0)\n"
"    ))\n"
"($define! append\n"
"  ($lambda x\n"
"    ($if (pair? x)\n"
"      (apply ($lambda (h . t)\n"
"        ($if (pair? t)\n"
"          ($if (null? h)\n"
"            (apply append t)\n"
"            (cons (car h) (apply append (cons (cdr h) t)))\n"
"          )\n"
"        h)\n"
"      ) x)\n"
"      x)\n"
"  ))\n"
"($define! reverse\n"
"  (($lambda ()\n"
"    ($define! push-pop\n"
"      ($lambda (r s)\n"
"        ($if (null? s)\n"
"          r\n"
"          (push-pop\n"
"            (cons (car s) r)\n"
"            (cdr s)))))\n"
"    ($lambda (s)\n"
"      (push-pop () s))\n"
"  )))\n"
"($define! filter\n"
"  ($lambda (accept? xs)\n"
"    ($if (null? xs)\n"
"      ()\n"
"      (($lambda ((first . rest))\n"
"        ($if (eval (list (unwrap accept?) first) (make-env))\n"
"          (cons first (filter accept? rest))\n"
"          (filter accept? rest))\n"
"      ) xs)) ))\n"
"($define! reduce\n"
"  ($lambda (args binop zero)\n"
"    ($if (null? args)\n"
"      zero\n"
"      (($lambda ((first . rest))\n"
"        ($if (null? rest)\n"
"          first\n"
"          (binop first (reduce rest binop zero)))\n"
"      ) args)) ))\n"
"($define! foldl\n"
"  ($lambda (args binop zero)\n"
"    ($if (null? args)\n"
"      zero\n"
"      (($lambda ((first . rest))\n"
"        (foldl rest binop (binop zero first))\n"
"      ) args)) ))\n"
"($define! foldr\n"
"  ($lambda (args binop zero)\n"
"    ($if (null? args)\n"
"      zero\n"
"      (($lambda ((first . rest))\n"
"        (binop first (foldr rest binop zero))\n"
"      ) args)) ))\n"
"($define! make-standard-env ($lambda () (get-current-env)))\n"
"($define! get-current-env (wrap ($vau () e e)))\n"
"($define! list*\n"
"  ($lambda (h . t) ($if (null? t) h (cons h (apply list* t))) ))\n"
"($define! $let\n"
"  ($vau (bindings . body) env\n"
"    (eval (cons\n"
"      (list* $lambda (map car bindings) body)\n"
"      (map cadr bindings)) env)\n"
"  ))\n"
"($define! apply\n"
"  ($lambda (appl arg . opt)\n"
"    (eval (cons (unwrap appl) arg) ($if (null? opt) (make-env) (car opt))) ))\n"
"($provide! (map)\n"
"  ($define! map\n"
"    (wrap ($vau (applicative . lists) env\n"
"      ($if (apply null? lists env)\n"
"        ()\n"
"        (appl applicative (peel lists () ()) env)\n"
"      ))))\n"
"  ($define! peel\n"
"    ($lambda (((head . tail) . more) heads tails)\n"
"      ($if (null? more)\n"
"        (list (cons head heads) (cons tail tails))\n"
"        (($lambda ((heads tails))\n"
"          (list (cons head heads) (cons tail tails)))\n"
"        (peel more heads tails)))\n"
"    ))\n"
"  ($define! appl\n"
"    ($lambda (applicative (heads tails) env)\n"
"      (cons\n"
"        (apply applicative heads env)\n"
"        ($if (apply null? tails env)\n"
"          ()\n"
"          (appl applicative (peel tails () ()) env)))\n"
"    ))\n"
")\n"
"($define! caddr ($lambda ((#ignore . (#ignore . (x . #ignore)))) x))\n"
"($define! caar ($lambda (((x . #ignore) . #ignore)) x))\n"
"($define! cdar ($lambda (((#ignore . x) . #ignore)) x))\n"
"($define! cadr ($lambda ((#ignore x . #ignore)) x))\n"
"($define! cddr ($lambda ((#ignore . (#ignore . x))) x))\n"
"($define! car ($lambda ((x . #ignore)) x))\n"
"($define! cdr ($lambda ((#ignore . x)) x))\n"
")\n"
#endif
"\n";

    return kernel_env;
}
