/*
 * sexpr.c -- LISP/Scheme S-expressions (ala John McCarthy)
 */
#include "sexpr.h"
#include "serial.h"

#define DEBUG(x) x /* debug logging */
#define TRACE(x)   /* trace logging */

// static actors
extern ACTOR a_nil;
extern ACTOR a_true;
extern ACTOR a_false;
extern ACTOR a_inert;
extern ACTOR a_no_bind;
extern ACTOR a_kernel_err;
extern ACTOR a_exit;

// static applicatives
extern ACTOR ap_list;
extern ACTOR ap_hexdump;
extern ACTOR ap_load_words;
extern ACTOR ap_store_words;

// static behaviors
extern ACTOR b_binding;
extern ACTOR b_scope;
extern ACTOR b_symbol;
extern ACTOR b_pair;
extern ACTOR b_number;

// asm utilities
extern struct example_4 *create_4(ACTOR* behavior, u32 r4, u32 r5, u32 r6);
extern struct example_5 *create_5(ACTOR* behavior);

struct example_4 {
    u32         code_00;
    u32         r4_04;
    u32         r5_08;
    u32         r6_0c;
    u32         r7_10;
    u32         r8_14;
    u32         r9_18;
    ACTOR*      beh_1c;
};

struct example_5 {
    u32         code_00;
    u32         data_04;
    u32         data_08;
    u32         data_0c;
    u32         data_10;
    u32         data_14;
    u32         data_18;
    ACTOR*      beh_1c;
};

int
boolean_p(ACTOR* x)
{
    return (x == &a_true) || (x == &a_false);
}

int
eq_p(ACTOR* x, ACTOR* y)
{
    // [FIXME] handle more complex cases...
    return (x == y);
}

int
equal_p(ACTOR* x, ACTOR* y)
{
    // [FIXME] handle more complex cases...
    return eq_p(x, y);
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
environment_p(ACTOR* x)
{
    struct example_5 *a = (struct example_5 *)x;
    return (a->beh_1c == &b_scope) || (a->beh_1c == &b_binding);
}

int
ignore_p(ACTOR* x)
{
    return (x == &a_inert);
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

struct sym_24b {  // symbol data is offset 0x04 into a cache-line
    u32         data_04;
    u32         data_08;
    u32         data_0c;
    u32         data_10;
    u32         data_14;
    u32         data_18;
};

static int
eq_24b(struct sym_24b* x, struct sym_24b* y)
{
    return (x->data_04 == y->data_04)
        && (x->data_08 == y->data_08)
        && (x->data_0c == y->data_0c)
        && (x->data_10 == y->data_10)
        && (x->data_14 == y->data_14)
        && (x->data_18 == y->data_18);
}

static struct example_5* sym_table[256];
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
    if (((void *)(*next_sym) - (void *)(sym_table)) < sizeof(sym_table)) {
        TRACE(puts("sym:overflow\n"));
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
        TRACE(puts("sym:created\n"));
        return (ACTOR*)a;  // return new symbol
    }
    TRACE(puts("sym:fail\n"));
    return NULL;  // fail
}

ACTOR*
number(int n)
{
    // FIXME: consider a memo-table for small integers
    DEBUG(puts("number(0x"));
    DEBUG(serial_hex32((u32)n));
    DEBUG(puts(")="));
    struct example_5 *x = create_5(&b_number);
    x->data_04 = (u32)n;
    DEBUG(puts("0x"));
    DEBUG(serial_hex32((u32)x));
    DEBUG(putchar('\n'));
    return (ACTOR*)x;
}

ACTOR*
cons(ACTOR* a, ACTOR* d)
{
    DEBUG(puts("cons(0x"));
    DEBUG(serial_hex32((u32)a));
    DEBUG(puts(",0x"));
    DEBUG(serial_hex32((u32)d));
    DEBUG(puts(")="));
    struct example_5 *x = create_5(&b_pair);
    x->data_04 = (u32)a;
    x->data_08 = (u32)d;
    DEBUG(puts("0x"));
    DEBUG(serial_hex32((u32)x));
    DEBUG(putchar('\n'));
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

    DEBUG(puts("addr=0x"));
    DEBUG(serial_hex32((u32)addr));
    DEBUG(putchar('\n'));
    DEBUG(puts("list=0x"));
    DEBUG(serial_hex32((u32)list));
    DEBUG(putchar('\n'));
    while (count > 0) {
        DEBUG(puts("count="));
        DEBUG(serial_dec32(count));
        DEBUG(putchar('\n'));
        int n = (int)(addr[--count]);
        DEBUG(puts("n=0x"));
        DEBUG(serial_hex32(n));
        DEBUG(putchar('\n'));
        list = cons(number(n), list);
        DEBUG(puts("list=0x"));
        DEBUG(serial_hex32((u32)list));
        DEBUG(putchar('\n'));
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

static char* line = NULL;

void
flush_char()
{
    serial_in_flush();
    line = NULL;
}

static int
read_char()
{
    if (!line || !line[0]) {  // refill buffer
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
    int s = 0;
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
    if (c == '-') {  // optional sign
        s = 1;
        c = read_char();
    } else if (c == '+') {
        c = read_char();
    }
    d = from_digit(c, r);
    if (d < 0) {
        unread_char(c);
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
    if (s) {
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
        if (i >= (sizeof(sym) - 1)) return NULL;  // overflow
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
    } else {  // unrecognized value
        putchar('#');
        putchar('<');
        serial_hex32((u32)a);
        putchar('>');
    }
}

//static u8 the_margin[128];
static ACTOR* kernel_env = NULL;

ACTOR*
ground_env()
{
    char exit_24b[] = "exit" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char hexdump_24b[] = "hexd" "ump\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char load_words_24b[] = "load" "-wor" "ds\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    char list_24b[] = "list" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0" "\0\0\0\0";
    ACTOR* a;
    struct example_5 *x;

    if (kernel_env) return kernel_env;  // lazy-initialized singleton

    ACTOR *env = &a_kernel_err;  // signal an error on lookup failure

    /* bind "exit" */
    a = symbol((struct sym_24b*)exit_24b);
    if (!a) return NULL;  // FAIL!
    x = create_5(&b_binding);
    if (!x) return NULL;  // FAIL!
    x->data_04 = (u32)a;  // set symbol
    x->data_08 = (u32)&a_exit;  // set value
    x->data_0c = (u32)env;  // set next
    env = (ACTOR *)x;

    /* bind "hexdump" */
    a = symbol((struct sym_24b*)hexdump_24b);
    if (!a) return NULL;  // FAIL!
    x = create_5(&b_binding);
    if (!x) return NULL;  // FAIL!
    x->data_04 = (u32)a;  // set symbol
    x->data_08 = (u32)&ap_hexdump;  // set value
    x->data_0c = (u32)env;  // set next
    env = (ACTOR *)x;

    /* bind "load-words" */
    a = symbol((struct sym_24b*)load_words_24b);
    if (!a) return NULL;  // FAIL!
    x = create_5(&b_binding);
    if (!x) return NULL;  // FAIL!
    x->data_04 = (u32)a;  // set symbol
    x->data_08 = (u32)&ap_load_words;  // set value
    x->data_0c = (u32)env;  // set next
    env = (ACTOR *)x;

    /* bind "list" */
    a = symbol((struct sym_24b*)list_24b);
    if (!a) return NULL;  // FAIL!
    x = create_5(&b_binding);
    if (!x) return NULL;  // FAIL!
    x->data_04 = (u32)a;  // set symbol
    x->data_08 = (u32)&ap_list;  // set value
    x->data_0c = (u32)env;  // set next
    env = (ACTOR *)x;

    /* mutable local scope */
    x = create_5(&b_scope);
    if (!x) return NULL;  // FAIL!
    x->data_0c = (u32)env;  // set parent
    env = (ACTOR *)x;

    kernel_env = env;
    return kernel_env;
}

void
kernel_repl()
{
    flush_char();
    for (;;) {
        putchar('\n');
        puts("> ");
        ACTOR* x = parse_sexpr();
        if (x == NULL) break;
        // FIXME: this is just a read-print loop (no eval)
        print_sexpr(x);
        putchar('\n');
    }
}
